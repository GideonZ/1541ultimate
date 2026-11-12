// Stand-in for FreeRTOS's queue.h on the build host: FreeRTOS.h's calls, and the rest of what
// cmd_buffer.c uses (cmdbuf_queues.c has them; the module cases do not call them)
#ifndef WIFI_TESTS_QUEUE_H
#define WIFI_TESTS_QUEUE_H
#include "FreeRTOS.h"
#ifdef __cplusplus
extern "C" {
#endif
QueueHandle_t xQueueCreate(UBaseType_t length, UBaseType_t item_size);
BaseType_t xQueueGenericReset(QueueHandle_t q, BaseType_t new_queue);
BaseType_t xQueueSendFromISR(QueueHandle_t q, const void *item, BaseType_t *woken);
BaseType_t xQueueReceiveFromISR(QueueHandle_t q, void *item, BaseType_t *woken);
#ifdef __cplusplus
}
#endif
#endif
