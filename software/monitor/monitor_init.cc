/*
 * monitor_init.cc
 *
 *  Created on: May 1, 2026
 */

#include "monitor_init.h"

#include "c64.h"
#include "subsys.h"
#include "tasks_collection.h"
#include "userinterface.h"
#if U64
#include "u64_machine.h"
#include "u64_memory_backend.h"
#elif !defined(RECOVERYAPP)
#include "u2_memory_backend.h"
#endif

namespace {
static Action *machine_monitor_actions[256];

#if U64
static SubsysResultCode_e u64_run_machine_monitor(SubsysCommand *cmd)
{
    if (!cmd->user_interface) {
        return SSRET_NO_USER_INTERFACE;
    }

    U64MemoryBackend monitor_backend((U64Machine *)C64::getMachine());
    cmd->user_interface->run_machine_monitor(&monitor_backend);
    return SSRET_OK;
}
#elif !defined(RECOVERYAPP)
static SubsysResultCode_e c64_run_machine_monitor(SubsysCommand *cmd)
{
    if (!cmd->user_interface) {
        return SSRET_NO_USER_INTERFACE;
    }

    U2MemoryBackend monitor_backend(C64::getMachine());
    cmd->user_interface->run_machine_monitor(&monitor_backend);
    return SSRET_OK;
}
#endif
}

Action *register_machine_monitor_task(int subsys_id, actionFunction_t callback, int function_id)
{
    Action *monitor = new Action("Machine Code Monitor", callback, function_id);
    TaskCategory *dev = TasksCollection::getCategory("Developer", SORT_ORDER_DEVELOPER);
    dev->prepend(monitor);
    if ((subsys_id >= 0) && (subsys_id < 256)) {
        machine_monitor_actions[subsys_id] = monitor;
    }
    return monitor;
}

Action *get_machine_monitor_task(int subsys_id)
{
    if ((subsys_id < 0) || (subsys_id >= 256)) {
        return NULL;
    }
    return machine_monitor_actions[subsys_id];
}

#if U64
Action *register_u64_machine_monitor_task(int function_id)
{
    return register_machine_monitor_task(SUBSYSID_U64, u64_run_machine_monitor, function_id);
}
#elif !defined(RECOVERYAPP)
Action *register_c64_machine_monitor_task(int function_id)
{
    return register_machine_monitor_task(SUBSYSID_C64, c64_run_machine_monitor, function_id);
}
#endif
