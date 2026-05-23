// U64 Debug session.
//
// Thin subclass on top of BrkDebugSession. The shared base owns the BRK
// capture engine, the cassette-buffer trampoline layout, breakpoint patch
// tracking, and the sentinel polling loop. U64 only supplies hardware hooks
// (stopped-session bracketing, peek/poke, reset, NMI pulse) and the
// temporary RAM-shadow patch path for BASIC/KERNAL stepping.

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
    bool basic_shadow_active;
    bool kernal_shadow_active;
    bool port_shadow_active;
    uint8_t saved_ddr;
    uint8_t saved_port;
    uint8_t saved_basic_ram[8192];
    uint8_t saved_kernal_ram[8192];

    enum RomShadow {
        SHADOW_NONE,
        SHADOW_BASIC,
        SHADOW_KERNAL
    };

    enum {
        SHADOW_CPU_PORT = 0x05
    };

    // Identify ROM-visible regions from the monitor CPU-port view. The
    // physical ROM is not writable; U64 Debug patches temporary RAM shadows.
    RomShadow rom_shadow_kind(uint16_t addr, uint8_t cpu_port)
    {
        cpu_port &= 0x07;
        if (addr >= 0xA000 && addr <= 0xBFFF && ((cpu_port & 0x03) == 0x03)) {
            return SHADOW_BASIC;
        }
        if (addr >= 0xE000 && (cpu_port & 0x02)) {
            return SHADOW_KERNAL;
        }
        return SHADOW_NONE;
    }

    uint8_t read_rom_source_byte(uint16_t addr, uint8_t cpu_port)
    {
        uint8_t value = 0;

        if (backend && backend->read_monitor_rom_byte(addr, cpu_port, &value)) {
            return value;
        }
        return machine->peek_cpu(addr, cpu_port);
    }

    void ensure_kernal_shadow(uint8_t cpu_port)
    {
        if (kernal_shadow_active) {
            return;
        }
        for (int i = 0; i < 8192; i++) {
            uint16_t addr = (uint16_t)(0xE000 + i);
            saved_kernal_ram[i] = machine->peek_cpu(addr, SHADOW_CPU_PORT);
            machine->poke_cpu(addr, read_rom_source_byte(addr, cpu_port), SHADOW_CPU_PORT);
        }
        kernal_shadow_active = true;
    }

    void ensure_basic_shadow(uint8_t cpu_port)
    {
        if (!basic_shadow_active) {
            for (int i = 0; i < 8192; i++) {
                uint16_t addr = (uint16_t)(0xA000 + i);
                saved_basic_ram[i] = machine->peek_cpu(addr, SHADOW_CPU_PORT);
                machine->poke_cpu(addr, read_rom_source_byte(addr, cpu_port), SHADOW_CPU_PORT);
            }
            basic_shadow_active = true;
        }
        // The CPU's BRK vector lives in KERNAL space. If BASIC is shadowed,
        // KERNAL must be shadowed too so the BRK path still reaches the
        // monitor's soft-vector handler while HIRAM is off.
        ensure_kernal_shadow(cpu_port);
    }

    void ensure_shadow_for(uint16_t addr, uint8_t cpu_port)
    {
        switch (rom_shadow_kind(addr, cpu_port)) {
            case SHADOW_BASIC:
                ensure_basic_shadow(cpu_port);
                break;
            case SHADOW_KERNAL:
                ensure_kernal_shadow(cpu_port);
                break;
            default:
                break;
        }
    }

    void restore_shadows(void)
    {
        if (basic_shadow_active) {
            for (int i = 0; i < 8192; i++) {
                machine->poke_cpu((uint16_t)(0xA000 + i), saved_basic_ram[i],
                                  SHADOW_CPU_PORT);
            }
            basic_shadow_active = false;
        }
        if (kernal_shadow_active) {
            for (int i = 0; i < 8192; i++) {
                machine->poke_cpu((uint16_t)(0xE000 + i), saved_kernal_ram[i],
                                  SHADOW_CPU_PORT);
            }
            kernal_shadow_active = false;
        }
    }

protected:
    virtual bool backend_ready(void) const { return machine != 0; }
    virtual uint8_t current_cpu_port(void) const
    {
        return backend ? backend->get_monitor_cpu_port() : (uint8_t)0x07;
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
        if (machine->is_accessible()) {
            machine->unfreeze();
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
    virtual bool prepare_run_with_patches(uint8_t cpu_port)
    {
        (void)cpu_port;
        if (!basic_shadow_active && !kernal_shadow_active) {
            return false;
        }
        saved_ddr = machine->peek_visible(0x0000);
        saved_port = machine->peek_visible(0x0001);
        port_shadow_active = true;
        return true;
    }
    virtual void finish_run_with_patches(bool prepared)
    {
        if (!prepared || !port_shadow_active) {
            return;
        }
        machine->poke_cpu(0x0000, saved_ddr, SHADOW_CPU_PORT);
        machine->poke_cpu(0x0001, saved_port, SHADOW_CPU_PORT);
        machine->peek_cpu(0x0001, SHADOW_CPU_PORT);
        machine->peek_cpu(0x0001, SHADOW_CPU_PORT);
        port_shadow_active = false;
    }
    virtual uint8_t debug_run_cpu_ddr(uint8_t cpu_port)
    {
        if (port_shadow_active) {
            return (uint8_t)((saved_ddr & 0xF8) | 0x07);
        }
        return BrkDebugSession::debug_run_cpu_ddr(cpu_port);
    }
    virtual uint8_t debug_run_cpu_port(uint8_t cpu_port)
    {
        if (port_shadow_active) {
            return (uint8_t)((saved_port & 0xF8) | SHADOW_CPU_PORT);
        }
        return BrkDebugSession::debug_run_cpu_port(cpu_port);
    }
    virtual uint8_t debug_restore_cpu_ddr(uint8_t cpu_port)
    {
        if (port_shadow_active) {
            return saved_ddr;
        }
        return BrkDebugSession::debug_restore_cpu_ddr(cpu_port);
    }
    virtual uint8_t debug_restore_cpu_port(uint8_t cpu_port)
    {
        if (port_shadow_active) {
            return saved_port;
        }
        return BrkDebugSession::debug_restore_cpu_port(cpu_port);
    }
    virtual void after_restore_patches(void)
    {
        restore_shadows();
    }

    // U64 internal BASIC/KERNAL ROM is not writable at runtime. For BRK
    // patches, execute a temporary RAM shadow containing the same ROM bytes,
    // then restore the user's hidden RAM and CPU port as soon as the CPU stops.
    virtual uint8_t read_patch_byte(uint16_t addr, uint8_t cpu_port)
    {
        RomShadow shadow = rom_shadow_kind(addr, cpu_port);
        if (shadow == SHADOW_BASIC) {
            return basic_shadow_active ? machine->peek_cpu(addr, SHADOW_CPU_PORT) :
                                         read_rom_source_byte(addr, cpu_port);
        }
        if (shadow == SHADOW_KERNAL) {
            return kernal_shadow_active ? machine->peek_cpu(addr, SHADOW_CPU_PORT) :
                                          read_rom_source_byte(addr, cpu_port);
        }
        return machine->peek_cpu(addr, cpu_port);
    }
    virtual void write_patch_byte(uint16_t addr, uint8_t byte, uint8_t cpu_port)
    {
        if (rom_shadow_kind(addr, cpu_port) != SHADOW_NONE) {
            ensure_shadow_for(addr, cpu_port);
            machine->poke_cpu(addr, byte, SHADOW_CPU_PORT);
            return;
        }
        machine->poke_cpu(addr, byte, cpu_port);
    }

public:
    explicit U64DebugSession(U64MemoryBackend *b)
        : BrkDebugSession(), backend(b), machine(0),
          basic_shadow_active(false), kernal_shadow_active(false),
          port_shadow_active(false), saved_ddr(0), saved_port(0)
    {
        machine = (U64Machine *)C64::getMachine();
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
