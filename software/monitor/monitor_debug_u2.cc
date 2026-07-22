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
// U2 does NOT support visible ROM patching, so BASIC/KERNAL stepping is only
// available when the code is actually executing from writable RAM (for
// example after an explicit RAM shadow copy or with the ROM banked out).

#include "monitor_debug_u2.h"

#if !defined(RUNS_ON_PC)

#include "monitor_debug_brk_session.h"
#include "u2_memory_backend.h"
#include "c64.h"
#include "monitor_file_io.h"
#include "itu.h"
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
        return backend ? backend->get_live_cpu_port() : (uint8_t)0x07;
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
        machine->reset();
        return true;
    }
    virtual void pulse_nmi_and_release(bool stopped_it)
    {
        // The redirect NMI must still be asserted when the CPU un-stops.
        // end_stopped_session_nmi() keeps C64_MODE_NMI set through resume()'s
        // un-stop (plain resume() clears C64_MODE first, losing the edge).
        machine->end_stopped_session_nmi(stopped_it);
    }
    virtual void request_staged_nmi(void)
    {
        // Intentionally empty on U2. The redirect NMI is delivered in
        // clear_staged_nmi(), after begin_run_window() has unfrozen the machine:
        // the U2 6510 only takes the cartridge NMI edge as it un-stops from a
        // stopped session, and a plain assert while frozen or free-running is
        // never observed (verified on hardware).
    }
    virtual void clear_staged_nmi(void)
    {
        // begin_run_window() has unfrozen the machine; the 6510 is free-running
        // from its pre-freeze PC. Stop it and un-stop it with the NMI asserted so
        // the CPU observes the edge as it resumes and vectors through the redirect
        // trampoline installed by nmi_redirect_to(). NOTE: on a U2+L plugged into
        // a C64U host this NMI is dropped by the host (the C64U does not forward
        // the cartridge NMI to its internal 6510 in any bus config - see
        // tools/developer/machine-code-monitor/U2_CARTRIDGE_NMI.md), so
        // entry/stepping does not complete there; on a real C64 (native cartridge
        // NMI) this is the correct launch.
        bool stopped_it = machine->begin_stopped_session();
        machine->end_stopped_session_nmi(stopped_it);
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

    // Restore patches/handler while this subclass' hooks are still live. The
    // abstract base destructor must not call cleanup() (its hooks are pure by
    // then), so the leaf owns the final safety-net cleanup.
    virtual ~U2DebugSession() { cleanup(); }
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
