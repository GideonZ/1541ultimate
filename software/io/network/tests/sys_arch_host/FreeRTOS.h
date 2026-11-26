// Stand-in for the FreeRTOS calls of network/sys_arch.c on the build host. The waits record
// the ticks they were asked for and never block (sys_arch_test.cpp).
#ifndef SYS_ARCH_HOST_FREERTOS_H
#define SYS_ARCH_HOST_FREERTOS_H
#include <stdint.h>
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef long BaseType_t;
typedef unsigned long UBaseType_t;
typedef uint32_t TickType_t;
typedef void *TaskHandle_t;
typedef void *QueueHandle_t;
typedef void *SemaphoreHandle_t;
typedef void (*TaskFunction_t)(void *);
#define portBASE_TYPE           long
#define pdTRUE                  1
#define pdFALSE                 0
#define pdPASS                  pdTRUE
#define errQUEUE_FULL           0
#define portMAX_DELAY           ((TickType_t)0xFFFFFFFFu)
#define portTICK_PERIOD_MS      5
#define configMINIMAL_STACK_SIZE 1024
#define configASSERT(x)
#define taskENTER_CRITICAL()
#define taskEXIT_CRITICAL()
#define portEND_SWITCHING_ISR(x)
#define portYIELD_FROM_ISR(x)

TickType_t xTaskGetTickCount(void);
BaseType_t xTaskCreate(TaskFunction_t f, const char *n, uint16_t s, void *p, UBaseType_t pr, TaskHandle_t *h);
QueueHandle_t xQueueCreate(UBaseType_t len, UBaseType_t size);
void vQueueDelete(QueueHandle_t q);
BaseType_t xQueueSend(QueueHandle_t q, const void *item, TickType_t ticks);
BaseType_t xQueueSendToBack(QueueHandle_t q, const void *item, TickType_t ticks);
BaseType_t xQueueSendFromISR(QueueHandle_t q, const void *item, BaseType_t *woken);
BaseType_t xQueueReceive(QueueHandle_t q, void *item, TickType_t ticks);
BaseType_t xQueueReceiveFromISR(QueueHandle_t q, void *item, BaseType_t *woken);
UBaseType_t uxQueueMessagesWaiting(QueueHandle_t q);
SemaphoreHandle_t xSemaphoreCreateMutex(void);
SemaphoreHandle_t xSemaphoreCreateBinary(void);
#define vSemaphoreCreateBinary(s) ((s) = xSemaphoreCreateBinary())
BaseType_t xSemaphoreTake(SemaphoreHandle_t s, TickType_t ticks);
BaseType_t xSemaphoreGive(SemaphoreHandle_t s);
BaseType_t xSemaphoreGiveFromISR(SemaphoreHandle_t s, BaseType_t *woken);
#ifdef __cplusplus
}
#endif
#endif
