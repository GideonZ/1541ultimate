#ifndef TEST_OWNER_TASK_H
#define TEST_OWNER_TASK_H
#include "FreeRTOS.h"
BaseType_t xTaskCreate(TaskFunction_t, const char*, unsigned, void*, UBaseType_t, TaskHandle_t*);
void vTaskDelete(TaskHandle_t);
void xTaskNotifyGive(TaskHandle_t);
uint32_t ulTaskNotifyTake(BaseType_t, TickType_t);
TickType_t xTaskGetTickCount();
void vTaskDelay(TickType_t);
#endif
