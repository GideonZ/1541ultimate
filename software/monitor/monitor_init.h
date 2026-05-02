/*
 * monitor_init.h
 *
 *  Created on: May 1, 2026
 */

#ifndef MONITOR_INIT_H_
#define MONITOR_INIT_H_

#include "action.h"

Action *register_machine_monitor_task(actionFunction_t callback, int function_id);

#endif /* MONITOR_INIT_H_ */