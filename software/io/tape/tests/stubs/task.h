#ifndef TAPE_TESTS_TASK_H
#define TAPE_TESTS_TASK_H
#include "FreeRTOS.h"
BaseType_t xTaskCreate(TaskFunction_t fn, const char *name, uint16_t stack, void *arg, int prio, TaskHandle_t *handle);
void vTaskDelay(TickType_t ticks);
#endif
