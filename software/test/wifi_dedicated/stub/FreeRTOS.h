// Host stand-in for FreeRTOS.h: the types and macros the code under test uses.
#ifndef FREERTOS_H_STUB
#define FREERTOS_H_STUB
#include <stdint.h>
#include <stddef.h>
typedef uint32_t TickType_t;
typedef long BaseType_t;
typedef unsigned long UBaseType_t;
typedef void *QueueHandle_t;
typedef void *TaskHandle_t;
#define pdTRUE  1
#define pdFALSE 0
#define configTICK_RATE_HZ 200
#define portTICK_PERIOD_MS (1000 / configTICK_RATE_HZ)
// As in software/FreeRTOS/Source/include/projdefs.h.
#define pdMS_TO_TICKS( xTimeInMs ) ( ( TickType_t ) ( ( ( TickType_t ) ( xTimeInMs ) * ( TickType_t ) configTICK_RATE_HZ ) / ( TickType_t ) 1000 ) )
#endif
