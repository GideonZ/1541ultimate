/*
 * monitor_init.cc
 *
 *  Created on: May 1, 2026
 */

#include "monitor_init.h"

#include "subsys.h"
#include "tasks_collection.h"

Action *register_machine_monitor_task(actionFunction_t callback, int function_id)
{
    Action *monitor = new Action("Machine Code Monitor", callback, function_id);
#if U64
    TaskCategory *dev = TasksCollection::getCategory("Developer", SORT_ORDER_DEVELOPER);
    dev->prepend(monitor);
#endif
    return monitor;
}