// U64 Debug session.
//
// Thin subclass on top of BrkDebugSession. The shared base owns the BRK
// capture engine, the cassette-buffer trampoline layout, breakpoint patch
// tracking, and the sentinel polling loop. U64 only supplies hardware hooks
// (stopped-session bracketing, peek/poke, reset, NMI pulse) and the
// volatile-ROM patch override for BASIC/KERNAL/CHAR stepping.

#include "monitor_debug_u64.h"

#if !defined(RUNS_ON_PC)

#include "monitor_debug_brk_session.h"
#include "u64_memory_backend.h"
#include "u64_machine.h"
#include "u64.h"
#include "monitor_file_io.h"
#include "itu.h"
#include "FreeRTOS.h"
#include "task.h"

// Defined in c64.cc: flags the FPGA ROM image as patched so C64::reset() heals it
// before the booting CPU can execute a stale BRK left in KERNAL/BASIC ROM.
extern "C" void u64_mark_rom_image_dirty(void) __attribute__((weak));

namespace {

class U64DebugSession : public BrkDebugSession
{
    U64MemoryBackend *backend;
    U64Machine *machine;

    // U64 BASIC/KERNAL/CHAR ROMs are served from volatile image buffers in
    // U64_BASIC_BASE/U64_KERNAL_BASE/U64_CHARROM_BASE. Patch those buffers
    // directly instead of copying ROMs into C64 RAM. No flash/config/file
    // storage is touched, so a device reboot or ROM reload always restores
    // the configured images even if a debug session dies before cleanup.
    volatile uint8_t *rom_patch_ptr(uint16_t addr, uint8_t cpu_port)
    {
        cpu_port &= 0x07;
        if (addr >= 0xA000 && addr <= 0xBFFF && ((cpu_port & 0x03) == 0x03)) {
            return (volatile uint8_t *)(U64_BASIC_BASE + (addr - 0xA000));
        }
        if (addr >= 0xD000 && addr <= 0xDFFF &&
                ((cpu_port & 0x03) != 0x00) && ((cpu_port & 0x04) == 0x00)) {
            return (volatile uint8_t *)(U64_CHARROM_BASE + (addr - 0xD000));
        }
        if (addr >= 0xE000 && (cpu_port & 0x02)) {
            return (volatile uint8_t *)(U64_KERNAL_BASE + (addr - 0xE000));
        }
        return 0;
    }

    void wait_for_cpu_visible_rom_byte(uint16_t addr, uint8_t cpu_port, uint8_t byte)
    {
        for (int i = 0; i < 8; i++) {
            if (machine->peek_cpu(addr, cpu_port) == byte) {
                return;
            }
            vTaskDelay(1);
        }
    }

protected:
    virtual bool backend_ready(void) const { return machine != 0; }
    virtual uint8_t current_cpu_port(void) const
    {
        return u64_debug_step_cpu_port(backend);
    }
    virtual void note_captured_cpu_port(uint8_t cpu_port)
    {
        if (backend) {
            backend->set_observed_live_cpu_port(cpu_port);
        }
    }
    virtual bool begin_stopped_session(void) { return machine->begin_stopped_session(); }
    virtual void end_stopped_session(bool stopped_it) { machine->end_stopped_session(stopped_it); }
    virtual uint8_t peek_cpu(uint16_t address, uint8_t cpu_port)
    {
        volatile uint8_t *rom = rom_patch_ptr(address, cpu_port);
        if (rom) {
            // The U64 ROM image buffers are write targets, not authoritative
            // read sources on every core. Read through the CPU-visible aperture
            // so verification sees the byte the 6510 will fetch.
            return machine->peek_cpu(address, cpu_port);
        }
        if (monitor_backing_store_for_cpu_port(address, cpu_port) == MONITOR_BACKING_IO) {
            return machine->peek_visible(address);
        }
        return machine->peek_raw(address);
    }
    virtual void poke_cpu(uint16_t address, uint8_t byte, uint8_t cpu_port)
    {
        if (rom_patch_ptr(address, cpu_port)) {
            // The base never pokes a ROM-mapped address through poke_cpu (only
            // RAM scratch: stack, vectors, trampolines). Route any future ROM
            // write through the single patch path so it uses poke_cpu_rom's
            // aligned store rather than a raw byte poke.
            write_patch_byte(address, byte, cpu_port);
            return;
        }
        if (monitor_backing_store_for_cpu_port(address, cpu_port) == MONITOR_BACKING_IO) {
            machine->poke_visible(address, byte);
            return;
        }
        machine->poke_raw(address, byte);
    }
    virtual uint8_t peek_visible(uint16_t address)
    {
        return machine->peek_visible(address);
    }
    virtual void poke_visible(uint16_t address, uint8_t byte)
    {
        machine->poke_visible(address, byte);
    }
    virtual void poke_visible_preserving_freeze_restore(uint16_t address,
                                                        uint8_t byte)
    {
        machine->poke_visible_preserving_freeze_restore(address, byte);
    }
    virtual void unfreeze_if_accessible(void)
    {
        if (machine && machine->is_accessible()) {
            machine->unfreeze();
        }
    }
    virtual bool machine_is_frozen(void) const
    {
        return machine ? machine->is_accessible() : false;
    }
    virtual void refreeze_machine(void)
    {
        if (machine) {
            machine->refreeze();
        }
    }
    virtual bool reset_machine(void)
    {
        return backend ? backend->reset_machine() : false;
    }
    virtual void pulse_nmi_and_release(bool stopped_it)
    {
        // Raise the NMI request while the CPU is still stopped, release the
        // stop so the CPU resumes and observes the pending edge, then clear
        // the request. This order is required: clearing before the release
        // would let the CPU exit the stopped session without ever seeing the
        // NMI edge.
        C64_MODE = C64_MODE_NMI;
        machine->end_stopped_session(stopped_it);
        C64_MODE = MODE_NORMAL;
    }
    virtual bool begin_clean_stopped_session(void)
    {
        // Patched high-memory release path. A raster-synced stop pairs the launch
        // with the mode-1 resume used by the reliable freeze path, so the live CPU
        // observes freshly armed high-memory BRKs on release.
        return machine->begin_stopped_session(true);
    }
    virtual void settle_visible_rom_for_live_fetch(bool sustained)
    {
        // Overlay/Telnet only. A visible-ROM byte has just been (re)written into
        // the FPGA ROM image while the machine is stopped. The CPU-visible DMA
        // aperture can read the new byte before the live instruction-fetch path
        // observes it, so clock the CPU, then stop again for the controlled
        // launch. The caller clears any stale sentinel afterwards.
        //
        // Two regimes:
        //  - Default (short): a freshly INSTALLED BRK or an ordinary forward
        //    single-step launch byte. A brief clock is enough and keeps stepping
        //    responsive; a long continuous run here destabilises forward stepping.
        //  - Sustained (long): a RESTORED launch opcode that the live 6510 keeps
        //    re-trapping on. The CPU instruction-fetch ROM copy refreshes from
        //    the image buffer only during one sustained continuous run, not from
        //    the image write alone and not from fragmented brief settles (each
        //    stop/start aborts the refresh). Used only on the re-trap relaunch,
        //    so the cost is paid only when actually needed.
        if (!machine || machine->is_accessible()) {
            return;
        }
        machine->end_stopped_session(true);
        wait_10us(sustained ? 5000 : 50);
        machine->begin_stopped_session();
    }
    virtual void request_staged_nmi(void)
    {
        C64_MODE = C64_MODE_NMI;
    }
    virtual void clear_staged_nmi(void)
    {
        wait_10us(1);
        C64_MODE = MODE_NORMAL;
    }
    virtual void delay_ms(int ms)
    {
        vTaskDelay(ms / portTICK_PERIOD_MS);
    }
    virtual bool free_run_no_breakpoint(uint16_t address)
    {
        if (backend) {
            backend->clear_observed_live_cpu_port();
        }
        monitor_io::jump_to(address);
        return true;
    }

    // U64 ROM-image patching: route patch reads/writes through the volatile
    // ROM image buffers for BASIC/KERNAL/CHAR ranges so we can step KERNAL
    // code without copying ROMs into C64 RAM.
    virtual bool supports_visible_rom_patching(void) const { return true; }
    // Visible-ROM fetches can lag monitor writes on the live U64 path.
    virtual bool visible_rom_fetch_lags(void) const { return true; }
    virtual uint8_t read_patch_byte(uint16_t addr, uint8_t cpu_port)
    {
        volatile uint8_t *rom = rom_patch_ptr(addr, cpu_port);
        if (rom) {
            // While the freezer holds the machine, the live aperture serves
            // the freezer cartridge's banking, not BASIC/KERNAL: a raw read
            // returns garbage, and a patch "original" saved from it would be
            // restored INTO the ROM image later (a trashed $FFFE vector was
            // exactly the frozen-continue jiffy-death). The monitor ROM cache
            // is the truthful frozen source.
            uint8_t cached = 0;
            if (machine_is_frozen() && backend &&
                    backend->read_monitor_rom_byte(addr, cpu_port, &cached)) {
                return cached;
            }
            // Keep reads aligned with actual 6510 fetches. The volatile ROM
            // image pointer remains the write target only.
            return machine->peek_cpu(addr, cpu_port);
        }
        if (monitor_backing_store_for_cpu_port(addr, cpu_port) == MONITOR_BACKING_IO) {
            return machine->peek_visible(addr);
        }
        return machine->peek_raw(addr);
    }
    virtual bool read_step_bytes(uint16_t address, uint8_t *dst, uint8_t len)
    {
        if (!dst) {
            return false;
        }
        uint8_t cpu_port = current_cpu_port();
        for (uint8_t i = 0; i < len; i++) {
            uint16_t current = (uint16_t)(address + i);
            if (backend && backend->get_monitor_cpu_port() == cpu_port &&
                    monitor_backing_store_is_visible_rom(
                        monitor_backing_store_for_cpu_port(current, cpu_port))) {
                dst[i] = backend->read(current);
            } else {
                dst[i] = read_patch_byte(current, cpu_port);
            }
        }
        return true;
    }
    virtual void write_patch_byte(uint16_t addr, uint8_t byte, uint8_t cpu_port)
    {
        volatile uint8_t *rom = rom_patch_ptr(addr, cpu_port);
        if (rom) {
            machine->poke_cpu_rom(rom, byte);
            if (u64_mark_rom_image_dirty) u64_mark_rom_image_dirty();
            wait_for_cpu_visible_rom_byte(addr, cpu_port, byte);
            return;
        }
        if (monitor_backing_store_for_cpu_port(addr, cpu_port) == MONITOR_BACKING_IO) {
            machine->poke_visible(addr, byte);
            return;
        }
        machine->poke_raw(addr, byte);
    }

public:
    explicit U64DebugSession(U64MemoryBackend *b)
        : BrkDebugSession(), backend(b), machine(0)
    {
        machine = (U64Machine *)C64::getMachine();
    }

    // Restore patches/handler while this subclass' hooks are still live. The
    // abstract base destructor must not call cleanup() (its hooks are pure by
    // then), so the leaf owns the final safety-net cleanup.
    virtual ~U64DebugSession() { cleanup(); }
};

}

DebugSession *create_u64_debug_session(U64MemoryBackend *backend)
{
    return new U64DebugSession(backend);
}

#else

DebugSession *create_u64_debug_session(class U64MemoryBackend *)
{
    return 0;
}

#endif
