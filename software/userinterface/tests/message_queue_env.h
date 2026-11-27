// Stand-ins for the part of FreeRTOS and userinterface.h that postMessage / getMessage
// use. The queue is real: a bounded FIFO whose xQueueSend fails when it is full, as the
// firmware's does with a zero timeout.
#ifndef MESSAGE_QUEUE_ENV_H
#define MESSAGE_QUEUE_ENV_H
#include <stdint.h>
#include <string.h>
#include "mystring.h"

typedef int BaseType_t;
typedef uint32_t TickType_t;
#define pdTRUE 1
#define pdFALSE 0

struct HostQueue {
    unsigned depth, item, count, head;
    unsigned char *data;
};
typedef HostQueue *QueueHandle_t;

static inline QueueHandle_t xQueueCreate(unsigned depth, unsigned item)
{
    HostQueue *q = new HostQueue;
    q->depth = depth; q->item = item; q->count = 0; q->head = 0;
    q->data = new unsigned char[depth * item];
    return q;
}
static inline BaseType_t xQueueSend(QueueHandle_t q, const void *p, TickType_t)
{
    if (q->count == q->depth) return pdFALSE;
    memcpy(q->data + ((q->head + q->count) % q->depth) * q->item, p, q->item);
    q->count++;
    return pdTRUE;
}
static inline BaseType_t xQueueReceive(QueueHandle_t q, void *p, TickType_t)
{
    if (!q->count) return pdFALSE;
    memcpy(p, q->data + q->head * q->item, q->item);
    q->head = (q->head + 1) % q->depth;
    q->count--;
    return pdTRUE;
}

class UserInterface {
public:
    static void postMessage(const char *msg);
    static mstring *getMessage(void);
};

extern QueueHandle_t userMessageQueue;
#endif
