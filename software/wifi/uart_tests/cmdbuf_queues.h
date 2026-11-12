// What cmdbuf_queues.c tells the cases (cmdbuf_test.cpp)
#ifndef CMDBUF_QUEUES_H_
#define CMDBUF_QUEUES_H_
#include "freertos/queue.h"
#ifdef __cplusplus
extern "C" {
#endif
extern unsigned long queue_blocked_ticks; // what the sends to a full queue would have waited, in ticks
extern int queue_blocked_sends;           // sends that found their queue full
UBaseType_t queue_depth(QueueHandle_t q);
#ifdef __cplusplus
}
#endif
#endif
