#include "userinterface.h"
#include "machine_monitor.h"

void UserInterface :: run_machine_monitor(MemoryBackend *backend)
{
    MachineMonitor *monitor = new MachineMonitor(this, backend);
    monitor->init(screen, keyboard);
    int ret = 0;
    while(!ret && host->exists()) {
        ret = monitor->poll(0);
    }
    monitor->deinit();
    delete monitor;
}
