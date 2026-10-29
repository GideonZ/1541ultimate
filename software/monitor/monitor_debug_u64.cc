// U64 Debug session: a thin BrkDebugSession subclass supplying hardware
// hooks (stopped-session bracketing, peek/poke, reset, NMI pulse) and the
// volatile-ROM patch override for BASIC/KERNAL/CHAR stepping.

#include "monitor_debug_u64.h"
#include "monitor_init.h"

#if !defined(RUNS_ON_PC)

#include "monitor_debug_brk_session.h"
#include "u64_memory_backend.h"
#include "u64_machine.h"
#include "u64.h"
#include "monitor_file_io.h"
#include "itu.h"
#include "FreeRTOS.h"
#include "task.h"

namespace {

class U64DebugSession : public BrkDebugSession
{
    U64MemoryBackend *backend;
    U64Machine *machine;

    // U64 BASIC/KERNAL/CHAR ROMs are volatile image buffers at U64_BASIC_BASE
    // /U64_KERNAL_BASE/U64_CHARROM_BASE; patch those directly rather than
    // copying into C64 RAM. Untouched flash/config means a reboot always
    // restores the configured images even if a session dies before cleanup.
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

    // Whether the live aperture is known to serve `addr` from the store that
    // `cpu_port` names. The port passed to a CPU-mapped read cannot re-bank the
    // aperture on this hardware: RAM $00/$01 is a DMA-only mirror of the 6510's
    // port, refreshed at reset, so the aperture answers from whatever the
    // running program has mapped. Only a port the 6510 itself reported at the
    // last debug stop counts as knowing; an unknown port is treated as "cannot
    // serve", because the alternative is trusting a read that may be answering
    // from the RAM underneath a ROM. The machine is never stopped to ask: this
    // runs on patch install and restore paths, where doing so would disturb a
    // parked CPU.
    bool live_aperture_serves(uint16_t addr, uint8_t cpu_port)
    {
        uint8_t live = 0;
        if (!backend || !backend->known_live_cpu_port(&live)) {
            return false;
        }
        return monitor_backing_store_for_cpu_port(addr, live) ==
               monitor_backing_store_for_cpu_port(addr, cpu_port);
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
        // The 6510 triggers on the falling edge, so a line another source is
        // already holding low takes no new request: the write below changes
        // nothing and the clear afterwards is what finally raises it. Reading
        // the sense bit costs the C64 nothing, unlike a DMA access here.
        if (C64_CLOCK_DETECT & C64_CD_NMI_SENSE) {
            printf("MCM NMI launch: line already low (CLK=%b MODE=%b)\n",
                   C64_CLOCK_DETECT, C64_MODE);
        }
        // The redirect NMI must still be asserted when the CPU un-stops, which
        // is what end_stopped_session_nmi() does (plain resume() writes
        // C64_MODE = MODE_NORMAL before C64_STOP = 0, so the request is dropped
        // before the un-stop). Same hook as the U2 backend uses.
        if (stopped_it) {
            machine->end_stopped_session_nmi(stopped_it);
            return;
        }
        // An outer stopped session still owns the release, so only latch the
        // request here. Hold it for several C64 cycles: the FPGA samples the
        // line once per phi2, and a bare register write pair is shorter than
        // that.
        C64_MODE = C64_MODE_NMI;
        wait_10us(2);
        C64_MODE = MODE_NORMAL;
    }
    virtual bool begin_clean_stopped_session(void)
    {
        // Patched high-memory release path. A raster-synced stop pairs the launch
        // with the mode-1 resume used by the reliable freeze path, so the live CPU
        // observes freshly armed high-memory BRKs on release.
        return machine->begin_stopped_session(true);
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
    virtual void log_launch_timeout_state(void)
    {
        // The interrupt request is latched the moment it is asserted and holds
        // until it is serviced, so a launch that never traps did not lose it in
        // flight. Either the shared line was already low, in which case there
        // was no edge to latch, or the machine was never released. These three
        // registers separate the two, and reading them changes nothing.
        printf("MCM launch watchdog: STOP=%b MODE=%b CLK=%b\n",
               C64_STOP, C64_MODE, C64_CLOCK_DETECT);
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
            // While frozen, the live aperture serves the freezer cartridge's
            // banking, not BASIC/KERNAL, so a raw read is garbage and would
            // restore a trashed "original" into the ROM image (this caused
            // the frozen-continue jiffy-death via a trashed $FFFE vector).
            uint8_t cached = 0;
            if (machine_is_frozen() && backend &&
                    backend->read_monitor_rom_byte(addr, cpu_port, &cached)) {
                return cached;
            }
            // Keep reads aligned with actual 6510 fetches. The volatile ROM
            // image pointer remains the write target only, and a caller that
            // needs the image's own byte asks read_patch_original_byte.
            return machine->peek_cpu(addr, cpu_port);
        }
        if (monitor_backing_store_for_cpu_port(addr, cpu_port) == MONITOR_BACKING_IO) {
            return machine->peek_visible(addr);
        }
        // peek_cpu/poke_cpu, not the _raw pair: a patch must save and restore
        // the byte the SAME cpu_port selects; the _raw pair ignores the port and
        // follows whatever the live map has banked in.
        return machine->peek_cpu(addr, cpu_port);
    }
    // What a ROM-store patch records so it can put it back. The aperture cannot
    // answer for a ROM the running program has banked out: the cpu_port passed
    // with a read does not re-bank it, because RAM $00/$01 on this hardware is a
    // DMA-only mirror of the 6510's port, refreshed at reset. A KERNAL-store
    // breakpoint armed while the program ran at $01=$35 therefore saved the RAM
    // byte underneath and wrote it into the KERNAL image when it was removed:
    // measured on hardware, $E000 in the image read $EE, the first byte of the
    // scenario's RAM payload, and stayed wrong until the firmware restarted and
    // reloaded the ROMs. The monitor's ROM cache holds the image's own bytes and
    // is used whenever the aperture is not known to be serving this store.
    virtual uint8_t read_patch_original_byte(uint16_t addr, uint8_t cpu_port)
    {
        uint8_t cached = 0;
        if (rom_patch_ptr(addr, cpu_port) && backend &&
                !live_aperture_serves(addr, cpu_port) &&
                backend->read_monitor_rom_byte(addr, cpu_port, &cached)) {
            return cached;
        }
        return read_patch_byte(addr, cpu_port);
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
            wait_for_cpu_visible_rom_byte(addr, cpu_port, byte);
            return;
        }
        if (monitor_backing_store_for_cpu_port(addr, cpu_port) == MONITOR_BACKING_IO) {
            machine->poke_visible(addr, byte);
            return;
        }
        machine->poke_cpu(addr, byte, cpu_port);
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
    virtual void cleanup(void)
    {
        BrkDebugSession::cleanup();
        restore_rom_image_if_patched();
    }
    virtual void cleanup_to_context(const DebugContext *ctx)
    {
        BrkDebugSession::cleanup_to_context(ctx);
        restore_rom_image_if_patched();
    }
    // A leaked ROM patch would otherwise survive until the next machine reset
    // and hand the following suite a corrupted KERNAL. The restore compares the
    // aperture against the loaded image, so a legitimately replaced KERNAL is
    // left alone.
    void restore_rom_image_if_patched(void)
    {
        if (u64_restore_pristine_rom_image) {
            u64_restore_pristine_rom_image();
        }
    }
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
