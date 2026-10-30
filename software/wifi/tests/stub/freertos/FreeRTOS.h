// Stand-in for FreeRTOS on the build host: only what the WiFi modules' connector, cmd_buffer.h and
// the cases use. The queues and semaphores are the scripted ones of module_stubs.c.
#ifndef WIFI_TESTS_FREERTOS_H
#define WIFI_TESTS_FREERTOS_H
#include <stdint.h>
typedef int BaseType_t;
typedef uint32_t TickType_t;
typedef void *QueueHandle_t;
typedef void *SemaphoreHandle_t;
#define pdTRUE  1
#define pdFALSE 0
#define portMAX_DELAY ((TickType_t)0xFFFFFFFFu)
#define pdMS_TO_TICKS(ms) ((TickType_t)(ms))
#ifdef __cplusplus
extern "C" {
#endif
BaseType_t xQueueReceive(QueueHandle_t q, void *item, TickType_t wait);
BaseType_t xQueueSend(QueueHandle_t q, const void *item, TickType_t wait);
BaseType_t xSemaphoreTake(SemaphoreHandle_t s, TickType_t wait);
BaseType_t xSemaphoreGive(SemaphoreHandle_t s);
#ifdef __cplusplus
}
#endif
#endif
