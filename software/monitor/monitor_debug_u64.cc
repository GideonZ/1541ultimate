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
#include "FreeRTOS.h"
#include "task.h"

namespace {

class U64DebugSession : public BrkDebugSession
{
    U64MemoryBackend *backend;
    U64Machine *machine;
    struct RomPatchShadow {
        bool used;
        uint16_t address;
        uint8_t cpu_port;
        uint8_t byte;
    };
    RomPatchShadow rom_patch_shadow[16];

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

    int find_rom_patch_shadow(uint16_t addr, uint8_t cpu_port) const
    {
        cpu_port &= 0x07;
        for (int i = 0; i < (int)(sizeof(rom_patch_shadow) / sizeof(rom_patch_shadow[0])); i++) {
            if (rom_patch_shadow[i].used &&
                    rom_patch_shadow[i].address == addr &&
                    rom_patch_shadow[i].cpu_port == cpu_port) {
                return i;
            }
        }
        return -1;
    }

    void remember_rom_patch_shadow(uint16_t addr, uint8_t byte, uint8_t cpu_port)
    {
        cpu_port &= 0x07;
        int slot = find_rom_patch_shadow(addr, cpu_port);
        if (slot < 0) {
            for (int i = 0; i < (int)(sizeof(rom_patch_shadow) / sizeof(rom_patch_shadow[0])); i++) {
                if (!rom_patch_shadow[i].used) {
                    slot = i;
                    break;
                }
            }
        }
        if (slot >= 0) {
            rom_patch_shadow[slot].used = true;
            rom_patch_shadow[slot].address = addr;
            rom_patch_shadow[slot].cpu_port = cpu_port;
            rom_patch_shadow[slot].byte = byte;
        }
    }

protected:
    virtual bool backend_ready(void) const { return machine != 0; }
    virtual uint8_t current_cpu_port(void) const
    {
        return u64_debug_step_cpu_port(backend);
    }
    virtual bool begin_stopped_session(void) { return machine->begin_stopped_session(); }
    virtual void end_stopped_session(bool stopped_it) { machine->end_stopped_session(stopped_it); }
    virtual uint8_t peek_cpu(uint16_t address, uint8_t cpu_port)
    {
        return machine->peek_cpu(address, cpu_port);
    }
    virtual void poke_cpu(uint16_t address, uint8_t byte, uint8_t cpu_port)
    {
        machine->poke_cpu(address, byte, cpu_port);
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
        // Raise the NMI request while the CPU is still stopped, release the
        // stop so the CPU resumes and observes the pending edge, then clear
        // the request. This order is required: clearing before the release
        // would let the CPU exit the stopped session without ever seeing the
        // NMI edge.
        C64_MODE = C64_MODE_NMI;
        machine->end_stopped_session(stopped_it);
        C64_MODE = MODE_NORMAL;
    }
    virtual void delay_ms(int ms)
    {
        vTaskDelay(ms / portTICK_PERIOD_MS);
    }
    virtual bool free_run_no_breakpoint(uint16_t address)
    {
        monitor_io::jump_to(address);
        return true;
    }

    // U64 ROM-image patching: route patch reads/writes through the volatile
    // ROM image buffers for BASIC/KERNAL/CHAR ranges so we can step KERNAL
    // code without copying ROMs into C64 RAM.
    virtual bool supports_visible_rom_patching(void) const { return true; }
    virtual uint8_t read_patch_byte(uint16_t addr, uint8_t cpu_port)
    {
        volatile uint8_t *rom = rom_patch_ptr(addr, cpu_port);
        if (rom) {
            int shadow = find_rom_patch_shadow(addr, cpu_port);
            if (shadow >= 0) {
                return rom_patch_shadow[shadow].byte;
            }
            return backend ? backend->read(addr) : machine->peek_cpu(addr, cpu_port);
        }
        return machine->peek_cpu(addr, cpu_port);
    }
    virtual bool read_step_bytes(uint16_t address, uint8_t *dst, uint8_t len)
    {
        if (!backend || !dst) {
            return false;
        }
        for (uint8_t i = 0; i < len; i++) {
            dst[i] = backend->read((uint16_t)(address + i));
        }
        return true;
    }
    virtual void write_patch_byte(uint16_t addr, uint8_t byte, uint8_t cpu_port)
    {
        volatile uint8_t *rom = rom_patch_ptr(addr, cpu_port);
        if (rom) {
            *rom = byte;
            remember_rom_patch_shadow(addr, byte, cpu_port);
            return;
        }
        machine->poke_cpu(addr, byte, cpu_port);
    }

public:
    explicit U64DebugSession(U64MemoryBackend *b)
        : BrkDebugSession(), backend(b), machine(0)
    {
        machine = (U64Machine *)C64::getMachine();
        for (int i = 0; i < (int)(sizeof(rom_patch_shadow) / sizeof(rom_patch_shadow[0])); i++) {
            rom_patch_shadow[i].used = false;
            rom_patch_shadow[i].address = 0;
            rom_patch_shadow[i].cpu_port = 0;
            rom_patch_shadow[i].byte = 0;
        }
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
