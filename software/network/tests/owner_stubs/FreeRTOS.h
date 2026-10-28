#ifndef TEST_OWNER_FREERTOS_H
#define TEST_OWNER_FREERTOS_H
#include "dma_uart.h"
typedef uint32_t TickType_t;
typedef int BaseType_t;
typedef unsigned UBaseType_t;
typedef void* TaskHandle_t;
typedef void (*TaskFunction_t)(void*);
#define pdPASS pdTRUE
#define configMINIMAL_STACK_SIZE 1600
#define PRIO_DRIVER 1
#define portTICK_PERIOD_MS 5
#define pdMS_TO_TICKS(ms) ((ms) / portTICK_PERIOD_MS)
#define portMAX_DELAY UINT32_MAX
#endif
