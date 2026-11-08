// FreeRTOS queues for the host tests of the WiFi TX path (wifi_tx_wake_test.cpp, wifi_tx_sim_test.cpp):
// FIFOs of items, as dma_uart_isr_test.cpp has them, with two hooks. A receive that would have to wait
// (empty queue, ticks > 0) tells host_queue_wait what it asked for and the test decides what time does
// (the simulation moves the clock and drains the UART; the unit test only records the ticks), and
// host_count_hook runs once right after the next uxQueueMessagesWaiting() read, so that a test can
// put an interrupt between the read and what follows. Include in exactly one translation unit.
#ifndef UART_HOST_FIFO_QUEUES_H
#define UART_HOST_FIFO_QUEUES_H
#include <deque>
#include <vector>
#include <string.h>
#include "FreeRTOS.h"

namespace {
struct Fifo {
    size_t item;
    size_t cap;
    std::deque<std::vector<uint8_t> > q;
};
} // namespace

// The longest wait any xQueueReceive asked for since the test cleared it
static TickType_t host_longest_wait_asked;
static void (*host_queue_wait)(QueueHandle_t q, TickType_t ticks);
static void (*host_count_hook)(void);

extern "C" {
QueueHandle_t xQueueCreate(UBaseType_t len, UBaseType_t size)
{
    Fifo *f = new Fifo;
    f->item = size;
    f->cap = len;
    return f;
}
BaseType_t xQueueGenericReset(QueueHandle_t q, BaseType_t) { ((Fifo *)q)->q.clear(); return pdPASS; }
BaseType_t xQueueSend(QueueHandle_t q, const void *item, TickType_t)
{
    Fifo *f = (Fifo *)q;
    if (f->q.size() >= f->cap) {
        return pdFALSE;
    }
    f->q.push_back(std::vector<uint8_t>((const uint8_t *)item, (const uint8_t *)item + f->item));
    return pdTRUE;
}
BaseType_t xQueueSendFromISR(QueueHandle_t q, const void *item, BaseType_t *) { return xQueueSend(q, item, 0); }
BaseType_t xQueueReceive(QueueHandle_t q, void *item, TickType_t ticks)
{
    Fifo *f = (Fifo *)q;
    if (f->q.empty() && ticks) {
        if (ticks > host_longest_wait_asked) {
            host_longest_wait_asked = ticks;
        }
        if (host_queue_wait) {
            host_queue_wait(q, ticks);
        }
    }
    if (f->q.empty()) {
        return pdFALSE;
    }
    memcpy(item, f->q.front().data(), f->item);
    f->q.pop_front();
    return pdTRUE;
}
BaseType_t xQueueReceiveFromISR(QueueHandle_t q, void *item, BaseType_t *) { return xQueueReceive(q, item, 0); }
UBaseType_t uxQueueMessagesWaiting(QueueHandle_t q)
{
    UBaseType_t n = ((Fifo *)q)->q.size();
    if (host_count_hook) {
        void (*h)(void) = host_count_hook;
        host_count_hook = NULL;
        h();
    }
    return n;
}
UBaseType_t uxQueueMessagesWaitingFromISR(QueueHandle_t q) { return ((Fifo *)q)->q.size(); }
}
#endif
