/*
 * cmdbuf_queues.c
 *
 * FreeRTOS queues for cmd_buffer.c (software/io/uart) on the build host: FIFOs of a fixed
 * depth, as xQueueCreate() makes them. Nothing else runs, so a send to a full queue would
 * block for good with portMAX_DELAY and for its whole timeout otherwise: it fails at once
 * here, and the ticks it would have waited are added up in queue_blocked_ticks.
 */

#include <stdlib.h>
#include <string.h>
#include "freertos/queue.h"
#include "cmdbuf_queues.h"

typedef struct {
    UBaseType_t length, item_size, count, head;
    uint8_t *items;
} test_queue_t;

unsigned long queue_blocked_ticks;
int queue_blocked_sends;

QueueHandle_t xQueueCreate(UBaseType_t length, UBaseType_t item_size)
{
    test_queue_t *q = (test_queue_t *)calloc(1, sizeof(test_queue_t));
    q->length = length;
    q->item_size = item_size;
    q->items = (uint8_t *)calloc(length, item_size);
    return q;
}

BaseType_t xQueueGenericReset(QueueHandle_t h, BaseType_t new_queue)
{
    test_queue_t *q = (test_queue_t *)h;
    q->count = q->head = 0;
    return pdTRUE;
}

UBaseType_t uxQueueMessagesWaiting(QueueHandle_t h)
{
    return ((test_queue_t *)h)->count;
}

UBaseType_t uxQueueMessagesWaitingFromISR(QueueHandle_t h)
{
    return ((test_queue_t *)h)->count;
}

// Counting semaphores (my_uart.c's data tokens): a take with none left fails at once, after
// recording the ticks it would have waited
typedef struct { UBaseType_t max, count; } test_sem_t;
unsigned long sem_waited_ticks;
void (*sem_wait_hook)(void); // runs once while a take with none left waits (what happens meanwhile), then the take is tried again
int sem_waits;               // takes that had to wait

SemaphoreHandle_t xSemaphoreCreateCounting(UBaseType_t max, UBaseType_t initial)
{
    test_sem_t *s = (test_sem_t *)calloc(1, sizeof(test_sem_t));
    s->max = max;
    s->count = initial;
    return s;
}

BaseType_t xSemaphoreTake(SemaphoreHandle_t h, TickType_t wait)
{
    test_sem_t *s = (test_sem_t *)h;
    if ((s->count == 0) && (wait > 0)) {
        sem_waits++;
        if (sem_wait_hook) {
            sem_wait_hook();
        }
    }
    if (s->count == 0) {
        sem_waited_ticks += wait;
        return pdFALSE;
    }
    s->count--;
    return pdTRUE;
}

BaseType_t xSemaphoreGive(SemaphoreHandle_t h)
{
    test_sem_t *s = (test_sem_t *)h;
    if (s->count == s->max) {
        return pdFALSE;
    }
    s->count++;
    return pdTRUE;
}

UBaseType_t sem_count(SemaphoreHandle_t h)
{
    return ((test_sem_t *)h)->count;
}

void (*sem_give_isr_hook)(void); // runs at each give from the ISR, before the count changes (what the queues hold at that moment)

BaseType_t xSemaphoreGiveFromISR(SemaphoreHandle_t h, BaseType_t *woken)
{
    if (sem_give_isr_hook) {
        sem_give_isr_hook();
    }
    return xSemaphoreGive(h);
}

UBaseType_t queue_depth(QueueHandle_t h)
{
    return ((test_queue_t *)h)->length;
}

BaseType_t xQueueSend(QueueHandle_t h, const void *item, TickType_t wait)
{
    test_queue_t *q = (test_queue_t *)h;
    if (q->count == q->length) {
        queue_blocked_sends++;
        queue_blocked_ticks += wait;
        return pdFALSE;
    }
    memcpy(q->items + ((q->head + q->count) % q->length) * q->item_size, item, q->item_size);
    q->count++;
    return pdTRUE;
}

BaseType_t xQueueReceive(QueueHandle_t h, void *item, TickType_t wait)
{
    test_queue_t *q = (test_queue_t *)h;
    if (q->count == 0) {
        return pdFALSE;
    }
    memcpy(item, q->items + q->head * q->item_size, q->item_size);
    q->head = (q->head + 1) % q->length;
    q->count--;
    return pdTRUE;
}

BaseType_t xQueueSendFromISR(QueueHandle_t h, const void *item, BaseType_t *woken)
{
    return xQueueSend(h, item, 0);
}

BaseType_t xQueueReceiveFromISR(QueueHandle_t h, void *item, BaseType_t *woken)
{
    return xQueueReceive(h, item, 0);
}
