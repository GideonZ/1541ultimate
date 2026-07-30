#include "userinterface.h"
#include "machine_monitor.h"
#include "monitor_file_io.h"
#if !defined(RUNS_ON_PC)
#include "c64.h"
#endif

namespace {
static MachineMonitor *active_reset_monitor = 0;
}

void UserInterface :: run_machine_monitor(MemoryBackend *backend)
{
    bool reopen_after_reset;
    do {
        MachineMonitor *monitor = new MachineMonitor(this, backend);
        uint16_t go_address = 0;
        DebugContext go_context;
        bool go_has_context = false;
        bool direct_render_target = false;
        reopen_after_reset = false;
#if !defined(RUNS_ON_PC)
        C64 *debug_render_machine = C64::getMachine();
        // After a C=+X reset the C64 is running again, so the firmware
        // overlay it was rendering into has lost ownership of screen RAM:
        // the live BASIC start-up writes are clobbering everything the new
        // monitor draws. Re-take ownership before init() so the firmware
        // gets the screen back, then redraw the chrome (title bar + border
        // separators) so the new monitor sees a clean canvas. The first-ever
        // monitor entry from the freezer menu already has is_accessible()
        // true, so this is a no-op there; non-C64-hosted UIs (telnet) skip
        // it because host != machine.
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
        active_reset_monitor = monitor;
        monitor->init(screen, keyboard);
        int ret = 0;
        while(!ret && host->exists()) {
            ret = monitor->poll(0);
            // A menu-button push (hardware or REST) while the monitor owns the
            // loop closes the monitor; pollMenuButtonPush() re-arms the push so
            // the outer run_once() loop also tears the menu down, landing the
            // user back on the live machine instead of a dismissed-menu shell.
            if (!ret && !monitor->is_debug_session_active() && pollMenuButtonPush()) {
                break;
            }
        }
        bool exit_ui = ret == MENU_EXIT;
        // C=+I inside the monitor routes to swap_interface_type(), which toggles
        // the persisted UI mode and returns MENU_HIDE. Unlike a normal monitor
        // exit (which returns to the launching menu) the user expects the whole
        // monitor + menu/browser UI to close so they land back on the live
        // machine and the next monitor open uses the new mode. Escalate MENU_HIDE
        // to a full UI teardown (mirrors the browser's own C=+I handler).
        bool swap_close = ret == MENU_HIDE;
        bool do_go = monitor->consume_pending_go(&go_address, &go_context,
                                                 &go_has_context);
        bool release_after_exit = monitor->consume_release_host_after_exit();
        bool deferred_debug_go = monitor->has_deferred_debug_go();
        reopen_after_reset = monitor->consume_reopen_after_reset();
        monitor->deinit();
        active_reset_monitor = NULL;
        if (deferred_debug_go && release_after_exit) {
#if defined(U64) && (U64) && !defined(RUNS_ON_PC)
            C64 *machine = C64::getMachine();
            if (machine && machine->is_accessible()) {
                release_host();
                machine->release_ownership();
            } else if (host) {
                release_host();
                host->release_ownership();
            }
#else
            release_host();
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
#if defined(U64) && (U64) && !defined(RUNS_ON_PC)
            C64 *machine = C64::getMachine();
            bool staged_nmi = false;
            if (machine && machine->is_accessible()) {
                staged_nmi = go_has_context ?
                    monitor_io::stage_resume_to_context(go_context) :
                    monitor_io::stage_jump_to(go_address);
                release_host();
                machine->release_ownership();
                if (staged_nmi) {
                    monitor_io::pulse_staged_nmi();
                }
            }
            if (!staged_nmi && direct_render_target && host) {
                release_host();
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
        if (release_after_exit) {
#if defined(U64) && (U64) && !defined(RUNS_ON_PC)
            C64 *machine = C64::getMachine();
            if (machine && machine->is_accessible()) {
                release_host();
                machine->release_ownership();
            } else if (host) {
                release_host();
                host->release_ownership();
            }
#else
            release_host();
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

extern "C" bool machine_monitor_global_reset_sees_debug_session(void)
{
    if (!active_reset_monitor) {
        return false;
    }
    return active_reset_monitor->is_debug_session_active();
}
