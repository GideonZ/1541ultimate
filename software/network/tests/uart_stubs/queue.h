#ifndef TEST_UART_QUEUE_H
#define TEST_UART_QUEUE_H
#include "FreeRTOS.h"
QueueHandle_t xQueueCreate(unsigned, unsigned);
BaseType_t xQueueSendFromISR(QueueHandle_t, const void*, BaseType_t*);
BaseType_t xQueueReceiveFromISR(QueueHandle_t, void*, BaseType_t*);
BaseType_t xQueueGenericReset(QueueHandle_t, BaseType_t);
BaseType_t xQueueSend(QueueHandle_t, const void*, TickType_t);
unsigned uxQueueMessagesWaiting(QueueHandle_t);
#endif
