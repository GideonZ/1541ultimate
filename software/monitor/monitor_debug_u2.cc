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
#include <string.h>

namespace {

class U2DebugSession : public BrkDebugSession
{
    // The boot cart clears page three, so the launch reloads $0314-$03FB: the
    // KERNAL vectors plus the whole debugger stub area. The 14-byte launcher
    // starts at the instruction trampoline ($0340) and runs into the NMI
    // capture scratch above it. Neither is in use during a contextless launch,
    // because there is no parked operation and no freeze in progress.
    enum {
        CONTEXTLESS_RELOAD_START = 0x0314,
        CONTEXTLESS_RELOAD_END = 0x03FB,
        CONTEXTLESS_RELOAD_LENGTH =
            CONTEXTLESS_RELOAD_END - CONTEXTLESS_RELOAD_START + 1,
        CONTEXTLESS_LAUNCHER = 0x0340
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
        // trampoline installed by nmi_redirect_to(). The same mechanism carries
        // C64::capture_cpu_port_via_nmi()'s stub on a U2+L in a C64U host, which
        // reports the port back, so the edge does reach the host's 6510; see
        // tests/e2e/monitor/U2_CARTRIDGE_NMI.md.
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
        // into the image handed to the boot cart.
        bool stopped_it = machine->begin_stopped_session();
        for (int i = 0; i < CONTEXTLESS_RELOAD_LENGTH; i++) {
            contextless_reload[i] = machine->peek(
                (uint16_t)(CONTEXTLESS_RELOAD_START + i));
        }
        machine->end_stopped_session(stopped_it);

        // A trap taken during the short transition into the boot cart must not
        // be replayed as the launch result.
        clear_run_result_markers(contextless_reload, CONTEXTLESS_RELOAD_START,
                                 CONTEXTLESS_RELOAD_LENGTH);

        // start_cartridge() resets the C64 before the boot-cart DMA load. RAM
        // under the KERNAL survives that, but the launcher reinstalls the hard
        // IRQ/BRK vector anyway so a bootstrap that banks the KERNAL out
        // (CPU0/4/5) reaches the hard BRK stub as reliably as a KERNAL-visible
        // one reaches the soft $0316 vector.
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
        // The C64U does not forward the cartridge NMI. The boot cart clears
        // $0300-$03FF during initialization, so reload the debugger's vectors
        // and cassette-buffer handler in the same handoff before it jumps.
        return monitor_io::jump_to_with_payload(
            CONTEXTLESS_LAUNCHER, CONTEXTLESS_RELOAD_START, contextless_reload,
            CONTEXTLESS_RELOAD_LENGTH);
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
