// U2 (cartridge) Debug session: a thin BrkDebugSession subclass supplying
// hardware hooks (DMA peek/poke, C64::reset, cartridge C64_MODE_NMI pulse)
// to drive the connected C64. No visible-ROM patching, so BASIC/KERNAL
// stepping needs code actually in writable RAM (shadow copy or ROM banked out).

#include "monitor_debug_u2.h"

#if !defined(RUNS_ON_PC)

#include "monitor_debug_brk_session.h"
#include "u2_memory_backend.h"
#include "c64.h"
#include "monitor_file_io.h"
#include "itu.h"
#include "FreeRTOS.h"
#include "task.h"
#include <string.h>

namespace {

class U2DebugSession : public BrkDebugSession
{
    // A contextless launch installs $0314-$03FB atomically: the KERNAL vectors,
    // debugger stub area, and a 14-byte launcher at $0340. The launcher runs
    // into the NMI capture scratch above it, which is idle because there is no
    // parked operation or captured register context.
    enum {
        CONTEXTLESS_RELOAD_START = 0x0314,
        CONTEXTLESS_RELOAD_END = 0x03FB,
        CONTEXTLESS_RELOAD_LENGTH =
            CONTEXTLESS_RELOAD_END - CONTEXTLESS_RELOAD_START + 1,
        CONTEXTLESS_LAUNCHER = 0x0340,
        CONTEXTLESS_NMI_VECTOR = 0x0318
    };
    U2MemoryBackend *backend;
    C64 *machine;
    uint8_t contextless_reload[CONTEXTLESS_RELOAD_LENGTH];

protected:
    virtual bool backend_ready(void) const { return machine != 0 && machine->exists(); }
    virtual uint8_t current_cpu_port(void) const
    {
        return backend ? backend->get_live_cpu_port() : (uint8_t)0x07;
    }
    virtual void note_captured_cpu_port(uint8_t cpu_port)
    {
        if (backend) {
            backend->set_observed_cpu_port(cpu_port);
        }
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
    virtual uint8_t peek_run_marker(uint16_t address)
    {
        return machine->peek_while_running(address);
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
        // Intentionally empty on U2: the NMI is delivered in clear_staged_nmi()
        // instead, since the U2 6510 only takes the cartridge NMI edge as it
        // un-stops from a stopped session (a plain assert while frozen or
        // free-running is never observed, verified on hardware).
    }
    virtual void clear_staged_nmi(void)
    {
        // Machine is free-running from its pre-freeze PC; stop/un-stop it with
        // NMI asserted so it observes the edge on resume and vectors through
        // the nmi_redirect_to() trampoline. Same mechanism carries
        // capture_cpu_port_via_nmi() on U2+L/C64U.
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
    virtual bool supports_contextless_breakpoint_launch(void) const
    {
        return true;
    }
    virtual bool prepare_contextless_breakpoint_launch(uint16_t address)
    {
        // Snapshot while the installed handler is still stopped/frozen, so the
        // run window can release the CPU without racing page-three changes
        // into the image handed to the NMI launch.
        bool stopped_it = machine->begin_stopped_session();
        for (int i = 0; i < CONTEXTLESS_RELOAD_LENGTH; i++) {
            contextless_reload[i] = machine->peek(
                (uint16_t)(CONTEXTLESS_RELOAD_START + i));
        }
        machine->end_stopped_session(stopped_it);

        // A trap taken during the short transition into the launcher must not
        // be replayed as the launch result.
        clear_run_result_markers(contextless_reload, CONTEXTLESS_RELOAD_START,
                                 CONTEXTLESS_RELOAD_LENGTH);

        // The launcher installs the hard IRQ/BRK vector so a bootstrap that
        // banks the KERNAL out (CPU0/4/5) reaches the hard BRK stub as reliably
        // as a KERNAL-visible one reaches the soft $0316 vector.
        const uint16_t stub = hard_brk_stub_address();
        const uint8_t launcher[] = {
            0x78,
            0xA9, (uint8_t)(stub & 0xFF),
            0x8D, 0xFE, 0xFF,
            0xA9, (uint8_t)(stub >> 8),
            0x8D, 0xFF, 0xFF,
            0x4C, (uint8_t)(address & 0xFF), (uint8_t)(address >> 8)
        };
        memcpy(contextless_reload +
                   (CONTEXTLESS_LAUNCHER - CONTEXTLESS_RELOAD_START),
               launcher, sizeof(launcher));
        return true;
    }
    virtual bool launch_contextless_with_breakpoints(uint16_t address)
    {
        (void)address;
        // begin_stopped_session()'s return says whether IT put the machine in
        // the stopped state, for end_stopped_session()'s "only the one who
        // stopped it resumes it" pairing elsewhere. It is the wrong signal
        // here: opening the monitor already holds the C64 stopped for the
        // whole Debug session (U2's only UI is the freeze overlay), so this
        // call almost always finds it already stopped and would return
        // false - both as a bogus launch failure (DBG_REFUSED, "UNSAFE
        // TARGET" in the UI) and, worse, silently skipping
        // end_stopped_session_nmi()'s resume/NMI delivery, leaving the CPU
        // parked forever (confirmed on hardware: $FFFE/$FFFF stayed
        // unwritten - the staged launcher never ran - and the C64 went
        // permanently non-ticking). The launch's poke sequence above always
        // runs; a Go's whole purpose is to make the CPU execute now, so
        // delivery must not depend on who is bookkept as the stopper.
        machine->begin_stopped_session();
        for (int i = 0; i < CONTEXTLESS_RELOAD_LENGTH; i++) {
            machine->poke((uint16_t)(CONTEXTLESS_RELOAD_START + i),
                          contextless_reload[i]);
        }
        machine->poke(CONTEXTLESS_NMI_VECTOR,
                      (uint8_t)(CONTEXTLESS_LAUNCHER & 0xFF));
        machine->poke((uint16_t)(CONTEXTLESS_NMI_VECTOR + 1),
                      (uint8_t)(CONTEXTLESS_LAUNCHER >> 8));
        machine->end_stopped_session_nmi(true);
        return true;
    }

    virtual void on_cpu_run_window_open(void)
    {
        if (backend) {
            backend->invalidate_live_cpu_port_cache();
        }
    }

public:
    explicit U2DebugSession(U2MemoryBackend *b)
        : BrkDebugSession(), backend(b), machine(0)
    {
        memset(contextless_reload, 0, sizeof(contextless_reload));
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
