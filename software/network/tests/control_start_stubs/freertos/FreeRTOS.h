#ifndef CONTROL_START_FREERTOS_H
#define CONTROL_START_FREERTOS_H
#include <assert.h>
#include <stdint.h>
#include <stddef.h>
typedef int BaseType_t;
typedef unsigned UBaseType_t;
typedef uint32_t TickType_t;
typedef unsigned portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(m) do { assert(!*(m)); ++*(m); } while (0)
#define portEXIT_CRITICAL(m) do { assert(*(m)==1); --*(m); } while (0)
#define pdTRUE 1
#define pdFALSE 0
#define pdPASS 1
#define pdMS_TO_TICKS(n) (n)
#define tskIDLE_PRIORITY 0
#endif
