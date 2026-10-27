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
// The dedicated reply channel of wifi_cmd.cc (U64 >= 1) is not under test here
static inline QueueHandle_t xQueueCreate(int length, int item_size) { return NULL; }
static inline BaseType_t xQueueSendFromISR(QueueHandle_t q, const void *item, BaseType_t *w) { return 0; }
#define pdFALSE 0
#define pdTRUE  1
#define portMAX_DELAY 0xFFFFFFFFUL
#define pdMS_TO_TICKS(x) (x)
// The test can let the ISR run just before a critical section
extern "C" void enter_safe_section(void);
#define ENTER_SAFE_SECTION enter_safe_section()
#define LEAVE_SAFE_SECTION
#endif
