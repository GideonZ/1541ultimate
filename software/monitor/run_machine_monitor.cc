#include "userinterface.h"
#include "machine_monitor.h"
#include "monitor_file_io.h"
#if !defined(RUNS_ON_PC)
#include "c64.h"
#endif

void UserInterface :: run_machine_monitor(MemoryBackend *backend)
{
    bool reopen_after_reset;
    do {
        MachineMonitor *monitor = new MachineMonitor(this, backend);
        uint16_t go_address = 0;
        DebugContext go_context;
        bool go_has_context = false;
        reopen_after_reset = false;
#if !defined(RUNS_ON_PC)
        C64 *debug_render_machine = C64::getMachine();
        bool c64_render_target = debug_render_machine && host == debug_render_machine &&
            debug_render_machine->is_accessible();
        monitor->set_debug_run_window_refreeze_enabled(c64_render_target);
        monitor->set_reset_exits_monitor(
            debug_render_machine &&
            (host == debug_render_machine || (host && host->is_permanent())));
#else
        monitor->set_debug_run_window_refreeze_enabled(false);
        monitor->set_reset_exits_monitor(false);
#endif
        active_machine_monitor = monitor;
        monitor->init(screen, keyboard);
        int ret = 0;
        while(!ret && host->exists()) {
            ret = monitor->poll(0);
        }
        bool exit_ui = ret == MENU_EXIT;
        bool do_go = monitor->consume_pending_go(&go_address, &go_context,
                                                 &go_has_context);
        reopen_after_reset = monitor->consume_reopen_after_reset();
        monitor->deinit();
        active_machine_monitor = NULL;
        delete monitor;
        if (exit_ui && !reopen_after_reset) {
            menu_response_to_action = MENU_EXIT;
        }
        if (do_go) {
#if defined(U64) && (U64) && !defined(RUNS_ON_PC)
            C64 *machine = C64::getMachine();
            if (machine && machine->is_accessible()) {
                release_host();
                machine->release_ownership();
            }
#endif
            if (go_has_context) {
                monitor_io::resume_to_context(go_context);
            } else {
                monitor_io::jump_to(go_address);
            }
        }
    } while (reopen_after_reset && host->exists());
}

extern "C" bool machine_monitor_request_reset_reentry(void *monitor)
{
    if (!monitor) {
        return false;
    }
    ((MachineMonitor *)monitor)->request_reopen_after_reset();
    return true;
}
