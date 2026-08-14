#include "userinterface.h"
#include "machine_monitor.h"
#include "monitor_file_io.h"
#if !defined(RUNS_ON_PC) && !defined(RECOVERYAPP)
#include "c64.h"
#endif

void UserInterface :: run_machine_monitor(MemoryBackend *backend)
{
    MachineMonitor *monitor = new MachineMonitor(this, backend);
    uint16_t go_address = 0;
    monitor->init(screen, keyboard);
    int ret = 0;
    // The monitor owns the screen while it runs, so the global C= plus O
    // shortcut is inert until it leaves: the key belongs to the monitor's own
    // keymap here, not to the shortcut that opened it.
    enter_modal();
    while(!ret && host->exists()) {
        ret = monitor->poll(0);
        if (!ret && pollMenuButtonPush()) {
            break;
        }
    }
    leave_modal();
    bool do_go = monitor->consume_pending_go(&go_address);
    monitor->deinit();
    delete monitor;
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
