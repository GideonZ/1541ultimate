#ifndef STUB_TASK_H_
#define STUB_TASK_H_
#include "FreeRTOS.h"
#define eSetValueWithOverwrite 0
static inline TaskHandle_t xTaskGetCurrentTaskHandle(void) { static int me; return &me; }
BaseType_t xTaskNotifyFromISR(TaskHandle_t t, uint32_t v, int a, BaseType_t *w);
BaseType_t xTaskNotifyWait(uint32_t a, uint32_t b, uint32_t *v, TickType_t t);
static inline void vTaskDelay(TickType_t t) {}
#endif
