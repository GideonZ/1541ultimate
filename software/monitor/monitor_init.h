/*
 * monitor_init.h
 *
 *  Created on: May 1, 2026
 */

#ifndef MONITOR_INIT_H_
#define MONITOR_INIT_H_

#include "action.h"

Action *register_machine_monitor_task(int subsys_id, actionFunction_t callback, int function_id) __attribute__((weak));
Action *get_machine_monitor_task(int subsys_id) __attribute__((weak));
Action *register_u64_machine_monitor_task(int function_id) __attribute__((weak));
Action *register_c64_machine_monitor_task(int function_id) __attribute__((weak));

#endif /* MONITOR_INIT_H_ */
