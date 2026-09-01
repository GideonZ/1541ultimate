#include "userinterface.h"
#include "machine_monitor.h"
#include "monitor_file_io.h"
#if !defined(RUNS_ON_PC)
#include "c64.h"
#endif

namespace {
static MachineMonitor *active_reset_monitor = 0;
}

// True only when the user interface is holding the machine, which is the freeze
// menu and an overlay that had to freeze because no display asserted hot-plug
// detect. A telnet session and an overlay drawn over a running machine leave it
// running, so a Go there does not have to let go of anything.
static bool ui_holds_machine_frozen(void)
{
#if !defined(RUNS_ON_PC)
    C64 *machine = C64::getMachine();
    return machine && machine->is_accessible();
#else
    return false;
#endif
}

void UserInterface :: run_machine_monitor(MemoryBackend *backend)
{
    bool reopen_after_reset;
    // release_host() below deinits every UI object, deleting the launching
    // browser's window. A reopen-after-reset pass must rebuild them before any
    // key reaches the browser. Survives the pass so the rebuild happens once
    // screen ownership has been re-taken.
    bool torn_down_host = false;
    do {
        MachineMonitor *monitor = new MachineMonitor(this, backend);
        uint16_t go_address = 0;
        DebugContext go_context;
        bool go_has_context = false;
        bool direct_render_target = false;
        reopen_after_reset = false;
#if !defined(RUNS_ON_PC)
        C64 *debug_render_machine = C64::getMachine();
        // After a C=+R debug reset the running C64's BASIC start-up writes
        // clobber the overlay's screen RAM; re-take ownership and redraw the
        // chrome before init() so the new monitor gets a clean canvas. No-op
        // on first entry (already accessible) or telnet (host != machine).
        if (debug_render_machine && host == debug_render_machine &&
                !debug_render_machine->is_accessible()) {
            debug_render_machine->take_ownership(this);
            if (screen) {
                set_screen_title();
            }
        }
        bool c64_render_target = debug_render_machine && host == debug_render_machine &&
            debug_render_machine->is_accessible();
        direct_render_target = debug_render_machine && screen &&
            !screen->prefers_full_refresh();
        monitor->set_debug_run_window_refreeze_enabled(c64_render_target);
        monitor->set_reset_exits_monitor(
            direct_render_target ||
            (debug_render_machine &&
             (host == debug_render_machine || (host && host->is_permanent()))));
#else
        monitor->set_debug_run_window_refreeze_enabled(false);
        monitor->set_reset_exits_monitor(false);
#endif
        if (torn_down_host) {
            appear();   // rebuild what the previous pass tore down
            torn_down_host = false;
        }
        active_reset_monitor = monitor;
        monitor->init(screen, keyboard);
        int ret = 0;
        while(!ret && host->exists()) {
            ret = monitor->poll(0);
            // A Go closes the monitor only where handing the machine back tears
            // the user interface down with it. Where the user interface never
            // held the machine, the jump is made with the monitor still on
            // screen. A Debug session's Go is not this case: it resumes a
            // parked CPU through the staged NMI below.
            if ((ret == 1) && !ui_holds_machine_frozen() &&
                    !monitor->is_debug_session_active() &&
                    !monitor->has_deferred_debug_go() &&
                    monitor->consume_pending_go(&go_address, &go_context,
                                                &go_has_context)) {
                if (go_has_context) {
                    monitor_io::resume_to_context(go_context);
                } else {
                    monitor_io::jump_to(go_address);
                }
                go_has_context = false;
                ret = 0;
            }
            // A menu-button push (hardware or REST) while the monitor owns the
            // loop closes the monitor; pollMenuButtonPush() re-arms the push so
            // the outer run_once() loop also tears the menu down, landing the
            // user back on the live machine instead of a dismissed-menu shell.
            if (!ret && pollMenuButtonPush()) {
                // Must work even mid-debug session: the only escape on a
                // cartridge target with no keyboard injection. Leaves Debug
                // first so the parked CPU is handed back by deinit()'s
                // teardown, or by dispatch_deferred_debug_go() for a parked G.
                if (monitor->is_debug_session_active()) {
                    monitor->leave_debug_for_exit();
                }
                break;
            }
        }
        bool exit_ui = ret == MENU_EXIT;
        // C=+I leaves the monitor with the same value Back returns and
        // records the swap on the monitor, so the request is read from there
        // rather than from `ret`. Unlike a normal exit the user expects the
        // whole UI to close onto the live machine, so it escalates to a full
        // teardown, mirroring the browser's own C=+I handler.
        bool swap_close = monitor->consume_pending_interface_swap();
        bool do_go = monitor->consume_pending_go(&go_address, &go_context,
                                                 &go_has_context);
        bool release_after_exit = monitor->consume_release_host_after_exit();
        // C=+R outside Debug does not reset the machine itself: handle_reset_
        // shortcut() records the request and leaves the monitor, because a
        // machine the user interface is still holding cannot come back to a
        // KERNAL prompt. The reset is performed here, after the monitor has
        // gone. Inside Debug the key takes reset_machine_and_reopen() instead,
        // which is why that path never reaches this flag.
        bool do_reset = monitor->consume_pending_reset();
        bool deferred_debug_go = monitor->has_deferred_debug_go();
        reopen_after_reset = monitor->consume_reopen_after_reset();
        monitor->deinit();
        active_reset_monitor = NULL;
        if (deferred_debug_go && release_after_exit) {
            release_host();
            torn_down_host = true;
#if !defined(RUNS_ON_PC)
            // The C64 is its own host when the monitor renders into the
            // machine, so release ownership through whichever object owns it.
            C64 *machine = C64::getMachine();
            if (machine && machine->is_accessible()) {
                machine->release_ownership();
            } else if (host) {
                host->release_ownership();
            }
#endif
            release_after_exit = false;
        }
        if (deferred_debug_go) {
            monitor->dispatch_deferred_debug_go();
        }
        delete monitor;
        if (exit_ui && !reopen_after_reset) {
            menu_response_to_action = MENU_EXIT;
        }
        if (swap_close) {
            // Do not reopen after an interface swap, and tell the launching
            // menu/browser to hide (close + release host) so the entire UI
            // surface tears down and the file browser is never left visible.
            reopen_after_reset = false;
            menu_response_to_action = MENU_HIDE;
        }
        if (do_go) {
#if !defined(RUNS_ON_PC) && (!defined(U64) || !(U64))
            // A cartridge drawn on the C64 has no second screen: its user
            // interface is on the machine the program is about to take over.
            // Deleting the monitor alone leaves the browser underneath present
            // but never repainted, while machine:menu_screen still answers for
            // it, so a caller cannot tell that user interface from a live one
            // and types into a screen nothing is drawing. Hand the machine
            // back, which also lets jump_to()'s own stop be the one it
            // releases, instead of pulsing NMI at a halted 6510.
            //
            // Only where this user interface is the one drawn on the machine:
            // `host == cart_machine` is the same test c64_render_target uses
            // above. A telnet session's host is its stream, and it never froze
            // the machine, so there is nothing here for it to hand back; a
            // freeze another client owns is not this one's to release.
            C64 *cart_machine = C64::getMachine();
            if (cart_machine && host == cart_machine && cart_machine->is_accessible()) {
                release_host();
                torn_down_host = true;
                cart_machine->release_ownership();
            }
#endif
#if defined(U64) && (U64) && !defined(RUNS_ON_PC)
            C64 *machine = C64::getMachine();
            bool staged_nmi = false;
            if (machine && machine->is_accessible()) {
                staged_nmi = go_has_context ?
                    monitor_io::stage_resume_to_context(go_context) :
                    monitor_io::stage_jump_to(go_address);
                release_host();
                torn_down_host = true;
                machine->release_ownership();
                if (staged_nmi) {
                    monitor_io::pulse_staged_nmi();
                }
            }
            if (!staged_nmi && direct_render_target && host) {
                release_host();
                torn_down_host = true;
                host->release_ownership();
            }
            if (!staged_nmi)
#endif
            {
                if (go_has_context) {
                    monitor_io::resume_to_context(go_context);
                } else {
                    monitor_io::jump_to(go_address);
                }
            }
        }
        if (do_reset) {
#if !defined(RUNS_ON_PC) && !defined(RECOVERYAPP)
            // The same order C64_Subsys uses for MENU_C64_RESET: let go of the
            // machine first, then reset it. Resetting one the user interface is
            // still holding does nothing visible, because the freezer keeps the
            // CPU stopped and the KERNAL never runs.
            C64 *machine = C64::getMachine();
            if (machine) {
                if (machine->is_accessible()) {
                    release_host();
                    torn_down_host = true;
                    machine->release_ownership();
                }
                machine->reset();
            }
#endif
        }
        if (release_after_exit) {
            // Every target, not only the Ultimate 64. release_host() takes down
            // the user interface objects; the freeze is what holds the CPU, and
            // only release_ownership() -> C64::unfreeze() -> C64::resume()
            // writes C64_STOP back to 0. On a cartridge the monitor's user
            // interface is the freezer, so releasing the host alone left the
            // 6510 DMA-held with nothing able to let it go: a host reset cannot
            // reach the cartridge's own stop, and only reopening the cartridge
            // menu cleared it. This is the hand-back the deferred-Go block
            // above already performs.
#if !defined(RUNS_ON_PC)
            C64 *machine = C64::getMachine();
            if (machine && machine->is_accessible()) {
                release_host();
                torn_down_host = true;
                machine->release_ownership();
            } else if (host) {
                release_host();
                torn_down_host = true;
                host->release_ownership();
            }
#else
            release_host();
            torn_down_host = true;
#endif
        }
    } while (reopen_after_reset && host->exists());
}

extern "C" bool machine_monitor_request_global_reset_cancel(void)
{
    // On every C64 hardware reset, reset the saved CPU view to CPU7 so the
    // next fresh monitor open reflects the post-reset banking state rather
    // than any stale CPU5/RAM view from a previous debug session.
    monitor_reset_saved_cpu_view();
    if (!active_reset_monitor) {
        return false;
    }
    // Reset makes any captured live-bank snapshot stale.
    active_reset_monitor->invalidate_live_cpu_port_view();
    // Only cancel the in-flight debug wait. Do NOT call request_reopen_after_reset()
    // here: that would save stale monitor state and schedule a spurious reopen
    // on every REST/menu reset that happens to fire while the monitor is visible.
    active_reset_monitor->request_debug_reset_cancel();
    return true;
}

extern "C" bool machine_monitor_debug_has_captured_cpu_port(void)
{
    if (!active_reset_monitor) {
        return false;
    }
    return active_reset_monitor->debug_observed_cpu_port_held();
}

extern "C" bool machine_monitor_global_reset_sees_debug_session(void)
{
    if (!active_reset_monitor) {
        return false;
    }
    return active_reset_monitor->is_debug_session_active();
}
