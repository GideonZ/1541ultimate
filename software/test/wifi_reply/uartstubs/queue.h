#include "FreeRTOS.h"
extern "C" {
QueueHandle_t xQueueCreate(int length, int item_size);
BaseType_t xQueueGenericReset(QueueHandle_t q, BaseType_t newQueue);
BaseType_t xQueueSendFromISR(QueueHandle_t q, const void *item, BaseType_t *w);
BaseType_t xQueueReceiveFromISR(QueueHandle_t q, void *item, BaseType_t *w);
unsigned uxQueueMessagesWaiting(QueueHandle_t q);
}
