// Host stubs for the part of FreeRTOS that dma_uart.cc names
#ifndef UARTSTUB_FREERTOS_H_
#define UARTSTUB_FREERTOS_H_
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
typedef long BaseType_t;
typedef unsigned long TickType_t;
typedef void *QueueHandle_t;
typedef void *SemaphoreHandle_t;
#define pdFALSE 0
#define pdTRUE  1
#endif
