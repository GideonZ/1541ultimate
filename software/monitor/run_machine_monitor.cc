#include "userinterface.h"
#include "machine_monitor.h"
#include "monitor_file_io.h"
#if !defined(RUNS_ON_PC) && !defined(RECOVERYAPP)
#include "c64.h"
#endif

// True only when the user interface is holding the machine frozen, which is the
// freeze menu. The overlay and the telnet user interface leave the machine
// running, so a Go there does not have to let go of anything.
static bool ui_holds_machine_frozen(void)
{
#if !defined(RUNS_ON_PC) && !defined(RECOVERYAPP)
    C64 *machine = C64::getMachine();
    return machine && machine->is_accessible();
#else
    return false;
#endif
}

void UserInterface :: run_machine_monitor(MemoryBackend *backend)
{
    MachineMonitor *monitor = new MachineMonitor(this, backend);
    uint16_t go_address = 0;
    monitor->init(screen, keyboard);
    int ret = 0;
    bool do_go = false;
    while(!ret && host->exists()) {
        ret = monitor->poll(0);
        // Go closes the monitor only where letting go of the machine tears the
        // user interface down with it. Everywhere else the jump is made with the
        // monitor still on screen.
        if ((ret == 1) && monitor->consume_pending_go(&go_address)) {
            if (ui_holds_machine_frozen()) {
                do_go = true;
                break;
            }
            monitor_io::jump_to(go_address);
            ret = 0;
        }
        if (!ret && pollMenuButtonPush()) {
            break;
        }
    }
    do_go = do_go || monitor->consume_pending_go(&go_address);
    bool do_reset = monitor->consume_pending_reset();
    bool did_swap_interface = monitor->consume_pending_interface_swap();
    monitor->deinit();
    delete monitor;
    if (did_swap_interface) {
        // The swapped Interface Type only takes effect the next time the menu
        // opens, so the browser underneath has to close too. Answering
        // MENU_HIDE is what the file browser already does for this key: the
        // caller reads menu_response_to_action back out of the command and
        // returns it, and UserInterface's poll loop tears the stack down.
        // Closing only the monitor would leave the user in the interface they
        // just swapped away from.
        menu_response_to_action = MENU_HIDE;
        return;
    }
    if (do_reset) {
#if !defined(RUNS_ON_PC) && !defined(RECOVERYAPP)
        // The same order C64_Subsys uses for MENU_C64_RESET: let go of the
        // machine first, then reset it. Resetting one the menu is still
        // holding does nothing visible, because the freezer keeps the CPU
        // stopped and the KERNAL never runs.
        C64 *machine = C64::getMachine();
        if (machine) {
            if (machine->is_accessible()) {
                release_host();
                machine->release_ownership();
            }
            machine->reset();
        }
#endif
        return;
    }
    if (do_go) {
#if !defined(RUNS_ON_PC) && !defined(RECOVERYAPP)
        C64 *machine = C64::getMachine();
        if (machine && machine->is_accessible()) {
            release_host();
            machine->release_ownership();
        }
#endif
        monitor_io::jump_to(go_address);
    }
}
