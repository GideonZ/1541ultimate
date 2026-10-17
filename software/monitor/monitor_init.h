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
extern "C" bool machine_monitor_request_global_reset_cancel(void) __attribute__((weak));
extern "C" bool machine_monitor_global_reset_sees_debug_session(void) __attribute__((weak));
// True when the debugger already holds a CPU port read off the 6510 itself,
// so a fresh capture would cost a stop/resume round trip for a value we have.
extern "C" bool machine_monitor_debug_has_captured_cpu_port(void) __attribute__((weak));
// Safety net: puts the volatile BASIC/KERNAL images back the way they were
// loaded. A debug patch must never outlive the monitor session that made it.
// Undefined on targets without a ROM aperture, so callers must null-check it.
extern "C" void u64_restore_pristine_rom_image(void) __attribute__((weak));

#endif /* MONITOR_INIT_H_ */
