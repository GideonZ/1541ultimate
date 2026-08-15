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
    while(!ret && host->exists()) {
        ret = monitor->poll(0);
        if (!ret && pollMenuButtonPush()) {
            break;
        }
    }
    bool do_go = monitor->consume_pending_go(&go_address);
    bool do_reset = monitor->consume_pending_reset();
    monitor->deinit();
    delete monitor;
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
