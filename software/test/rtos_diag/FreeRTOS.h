/*
 * Stand-in for FreeRTOS.h in the host test of software/system/assert.c. It carries
 * the real FreeRTOSConfig.h, so the test sees the configuration the firmware is built
 * with, and only the few kernel types and calls assert.c uses.
 */
#ifndef RTOS_DIAG_FREERTOS_H
#define RTOS_DIAG_FREERTOS_H

#include <stddef.h>
#include <stdint.h>
#include "FreeRTOSConfig.h"

typedef uint32_t StackType_t;
typedef long BaseType_t;
typedef unsigned long UBaseType_t;
typedef void *TaskHandle_t;

void *pvPortMalloc(size_t size);
void vPortFree(void *pv);
size_t xPortGetFreeHeapSize(void);
size_t xPortGetMinimumEverFreeHeapSize(void);

#endif
