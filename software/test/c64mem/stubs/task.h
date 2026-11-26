#ifndef SIM_TASK_H
#define SIM_TASK_H
#include "c64mem_sim.h"
#define vTaskSuspendAll() sim_suspend()
#define xTaskResumeAll()  sim_resume()
#endif
