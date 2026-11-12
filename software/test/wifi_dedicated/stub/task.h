#ifndef TASK_H_STUB
#define TASK_H_STUB
#include "FreeRTOS.h"
TickType_t xTaskGetTickCount(void);
void vTaskSuspendAll(void);
BaseType_t xTaskResumeAll(void);
void vTaskDelay(TickType_t ticks);
#endif
