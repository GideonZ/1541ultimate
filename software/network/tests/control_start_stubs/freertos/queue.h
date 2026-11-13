#ifndef CONTROL_START_QUEUE_H
#define CONTROL_START_QUEUE_H
#include "FreeRTOS.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef void *QueueHandle_t;
QueueHandle_t xQueueCreate(UBaseType_t,UBaseType_t);
BaseType_t xQueueSend(QueueHandle_t,const void*,TickType_t);
BaseType_t xQueueReceive(QueueHandle_t,void*,TickType_t);
void vQueueDelete(QueueHandle_t);
#ifdef __cplusplus
}
#endif
#endif
