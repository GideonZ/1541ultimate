// Host stubs for the part of FreeRTOS that wifi_cmd.cc uses
#ifndef STUB_FREERTOS_H_
#define STUB_FREERTOS_H_
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
// wifi_cmd.cc passes a buffer pointer as a 32 bit notify value; on the host it is wider
#define uint32_t uintptr_t
typedef void *SemaphoreHandle_t;
typedef uint16_t u16_t;
typedef long BaseType_t;
typedef unsigned long TickType_t;
typedef void *TaskHandle_t;
typedef void *QueueHandle_t;
#define pdFALSE 0
#define pdTRUE  1
#define portMAX_DELAY 0xFFFFFFFFUL
#define pdMS_TO_TICKS(x) (x)
#define ENTER_SAFE_SECTION
#define LEAVE_SAFE_SECTION
#endif
