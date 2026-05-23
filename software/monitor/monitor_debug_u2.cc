// U2 (cartridge) Debug session.
//
// Thin subclass on top of BrkDebugSession. The shared base owns the BRK
// capture engine, the cassette-buffer trampoline layout, breakpoint patch
// tracking, and the sentinel polling loop. U2 supplies hardware hooks that
// drive the connected C64 through the cartridge:
//   - begin/end stopped session bracket atomic vector/trampoline installs
//     so the live C64 sees a single consistent transition
//   - peek/poke use C64::peek / C64::poke (DMA into C64 RAM)
//   - reset uses C64::reset
//   - NMI pulse uses the cartridge C64_MODE_NMI register
// U2 does NOT support volatile ROM-image patching, so BASIC/KERNAL stepping
// is only available when the user has loaded code into RAM or has KERNAL
// banked out by the CPU port.

#include "monitor_debug_u2.h"

#if !defined(RUNS_ON_PC)

#include "monitor_debug_brk_session.h"
#include "u2_memory_backend.h"
#include "c64.h"
#include "monitor_file_io.h"
#include "FreeRTOS.h"
#include "task.h"

namespace {

class U2DebugSession : public BrkDebugSession
{
    U2MemoryBackend *backend;
    C64 *machine;

protected:
    virtual bool backend_ready(void) const { return machine != 0 && machine->exists(); }
    virtual uint8_t current_cpu_port(void) const
    {
        // U2 does not expose CPU bank selection from the monitor; the live
        // CPU port read from $0001 is the only authoritative value.
        return 0x07;
    }
    virtual bool begin_stopped_session(void) { return machine->begin_stopped_session(); }
    virtual void end_stopped_session(bool stopped_it) { machine->end_stopped_session(stopped_it); }
    virtual uint8_t peek_cpu(uint16_t address, uint8_t)
    {
        return machine->peek(address);
    }
    virtual void poke_cpu(uint16_t address, uint8_t byte, uint8_t)
    {
        machine->poke(address, byte);
    }
    virtual uint8_t peek_visible(uint16_t address)
    {
        return machine->peek(address);
    }
    virtual void poke_visible(uint16_t address, uint8_t byte)
    {
        machine->poke(address, byte);
    }
    virtual void unfreeze_if_accessible(void)
    {
        if (machine->is_accessible()) {
            machine->unfreeze();
        }
    }
    virtual bool reset_machine(void)
    {
        machine->reset();
        return true;
    }
    virtual void pulse_nmi_and_release(bool stopped_it)
    {
        // Same ordering as U64: NMI request raised while stopped, stopped
        // session released so the CPU resumes and observes the edge, then
        // the request cleared.
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

public:
    explicit U2DebugSession(U2MemoryBackend *b)
        : BrkDebugSession(), backend(b), machine(0)
    {
        machine = C64::getMachine();
    }
};

}

DebugSession *create_u2_debug_session(U2MemoryBackend *backend)
{
    return new U2DebugSession(backend);
}

#else

DebugSession *create_u2_debug_session(class U2MemoryBackend *)
{
    return 0;
}

#endif
