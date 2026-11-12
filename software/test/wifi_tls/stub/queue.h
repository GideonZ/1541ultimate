#ifndef QUEUE_H_STUB
#define QUEUE_H_STUB
#include "FreeRTOS.h"
BaseType_t xQueueReceive(QueueHandle_t queue, void *item, TickType_t ticks);
QueueHandle_t xQueueCreate(UBaseType_t length, UBaseType_t item_size);
BaseType_t xQueueSend(QueueHandle_t queue, const void *item, TickType_t ticks);
UBaseType_t uxQueueMessagesWaiting(QueueHandle_t queue);
void vQueueDelete(QueueHandle_t queue);
#endif
