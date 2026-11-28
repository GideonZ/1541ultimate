#ifndef STUB_TASK_H_
#define STUB_TASK_H_
#include "FreeRTOS.h"
#define eSetValueWithOverwrite 0
#define eSetValueWithoutOverwrite 1
#define pdFAIL 0
// inline, not static: the one task has the same handle in every file
inline TaskHandle_t xTaskGetCurrentTaskHandle(void) { static int me; return (TaskHandle_t)&me; }
BaseType_t xTaskNotifyFromISR(TaskHandle_t t, uint32_t v, int a, BaseType_t *w);
BaseType_t xTaskNotify(TaskHandle_t t, uint32_t v, int a);
TickType_t xTaskGetTickCount(void);
void vTaskSuspendAll(void);
BaseType_t xTaskResumeAll(void);
BaseType_t xTaskNotifyWait(uint32_t a, uint32_t b, uint32_t *v, TickType_t t);
static inline void vTaskDelay(TickType_t t) {}
#endif
