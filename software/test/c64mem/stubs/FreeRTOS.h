#ifndef SIM_FREERTOS_H
#define SIM_FREERTOS_H
#include "c64mem_sim.h"
#define portENTER_CRITICAL() sim_enter()
#define portEXIT_CRITICAL()  sim_exit()
#endif
