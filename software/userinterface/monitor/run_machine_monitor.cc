#include "userinterface.h"
#include "machine_monitor.h"
#include "monitor_file_io.h"
#if defined(U64) && (U64) && !defined(RUNS_ON_PC)
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
    }
    bool do_go = monitor->consume_pending_go(&go_address);
    monitor->deinit();
    delete monitor;
    if (do_go) {
#if defined(U64) && (U64) && !defined(RUNS_ON_PC)
        C64 *machine = C64::getMachine();
        if (machine && machine->is_accessible()) {
            release_host();
            machine->release_ownership();
        }
#endif
        monitor_io::jump_to(go_address);
    }
}
