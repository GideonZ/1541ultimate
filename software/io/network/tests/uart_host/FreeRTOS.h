// Stand-in for FreeRTOS on the build host, for the real io/uart/dma_uart.cc and cmd_buffer.c
// (dma_uart_isr_test.cpp): the types, and the queue calls the ISR and the buffer pools use. The
// queues themselves are small FIFOs in the test.
#ifndef UART_HOST_FREERTOS_H
#define UART_HOST_FREERTOS_H
#include <stdint.h>
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef long BaseType_t;
typedef unsigned long UBaseType_t;
typedef uint32_t TickType_t;
typedef void *QueueHandle_t;
#define pdTRUE   1
#define pdFALSE  0
#define pdPASS   pdTRUE

QueueHandle_t xQueueCreate(UBaseType_t len, UBaseType_t size);
BaseType_t xQueueGenericReset(QueueHandle_t q, BaseType_t newQueue);
BaseType_t xQueueSend(QueueHandle_t q, const void *item, TickType_t ticks);
BaseType_t xQueueSendFromISR(QueueHandle_t q, const void *item, BaseType_t *woken);
BaseType_t xQueueReceive(QueueHandle_t q, void *item, TickType_t ticks);
BaseType_t xQueueReceiveFromISR(QueueHandle_t q, void *item, BaseType_t *woken);
UBaseType_t uxQueueMessagesWaiting(QueueHandle_t q);
#ifdef __cplusplus
}
#endif
#endif
