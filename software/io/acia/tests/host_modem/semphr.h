// A mutex on the host is a binary semaphore that starts full.
#ifndef HOST_MODEM_SEMPHR_H
#define HOST_MODEM_SEMPHR_H
#include "FreeRTOS.h"
static inline SemaphoreHandle_t xSemaphoreCreateMutex(void)
{
    SemaphoreHandle_t s = xSemaphoreCreateBinary();
    xSemaphoreGive(s);
    return s;
}
#include "integer.h"
static inline void vTaskDelete(TaskHandle_t) {}
static inline TickType_t xTaskGetTickCount(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return (TickType_t)(t.tv_sec * 1000 + t.tv_nsec / 1000000); }
#endif
