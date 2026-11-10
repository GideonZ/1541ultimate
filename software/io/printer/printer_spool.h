/*
 * Byte spool between the IEC server (one producer) and the virtual printer task (one
 * consumer): a ring of 16 bit events, plus a short queue of control markers, each tied to
 * the position in the ring where it was posted. The IEC server never waits for the printer
 * task to finish a page; it waits only when the ring is full.
 *
 * Event: bits 8-9 the type, bits 0-7 the value. Positions are free running 32 bit counters;
 * the ring size is a power of two.
 */
#ifndef PRINTER_SPOOL_H
#define PRINTER_SPOOL_H

#include <stdint.h>
#include <stdlib.h>
#include "FreeRTOS.h"
#include "task.h"

#define PRINTER_SPOOL_EVENTS      (256 * 1024)   /* 512 KB */
#define PRINTER_SPOOL_MIN_EVENTS  4096
#define PRINTER_SPOOL_CTRL_DEPTH  4

/* One CPU on the device: the compiler must not reorder. On the host, two threads. */
#if defined(__nios2__) || defined(__riscv)
#define PRINTER_SPOOL_BARRIER() __asm__ __volatile__("" ::: "memory")
#else
#define PRINTER_SPOOL_BARRIER() __atomic_thread_fence(__ATOMIC_SEQ_CST)
#endif

enum {
    PRINTER_SPOOL_NONE = 0,
    PRINTER_SPOOL_EVENT,
    PRINTER_SPOOL_CTRL
};

class PrinterSpool
{
private:
    uint16_t *ring;
    uint32_t mask;
    volatile uint32_t head;     /* written by the producer only */
    volatile uint32_t tail;     /* written by the consumer only */

    struct Ctl {
        uint8_t value;
        uint32_t pos;
    };
    Ctl ctl[PRINTER_SPOOL_CTRL_DEPTH];
    volatile uint8_t ctl_first, ctl_count;

    TaskHandle_t consumer;

public:
    PrinterSpool() : ring(NULL), mask(0), head(0), tail(0), ctl_first(0), ctl_count(0), consumer(NULL) { }
    ~PrinterSpool() { release(); }

    /* Allocate the ring (the largest power of two up to 'events' that the heap gives). */
    bool allocate(uint32_t events = PRINTER_SPOOL_EVENTS, uint32_t min_events = PRINTER_SPOOL_MIN_EVENTS)
    {
        if (ring)
            return true;
        uint32_t n = 1;
        while ((n << 1) <= events && (n << 1) != 0)
            n <<= 1;
        for (; n >= min_events && n >= 2; n >>= 1) {
            uint16_t *r = (uint16_t *) malloc(n * sizeof(uint16_t));
            if (r) {
                mask = n - 1;
                PRINTER_SPOOL_BARRIER();
                ring = r;
                return true;
            }
        }
        return false;
    }
    bool allocated(void) { return ring != NULL; }
    void release(void)
    {
        uint16_t *r = ring;
        ring = NULL;
        free(r);
    }

    /* Consumer: the task that gets the wake up. */
    void set_consumer(TaskHandle_t t) { consumer = t; }

    /* Producer (IEC task): false if there is no ring. Waits while the ring is full. */
    bool push(uint16_t ev)
    {
        if (!ring)
            return false;
        while ((uint32_t)(head - tail) > mask)
            vTaskDelay(1);
        ring[head & mask] = ev;
        PRINTER_SPOOL_BARRIER();
        head = head + 1;
        PRINTER_SPOOL_BARRIER();
        wake();
        return true;
    }

    /* Any task: post a marker at the current position. 'coalesce' drops it when the newest
     * queued marker has the same value and position. Waits while the queue is full. */
    void post_ctrl(uint8_t value, bool coalesce = false)
    {
        for (;;) {
            portENTER_CRITICAL();
            uint32_t pos = head;
            if (coalesce && ctl_count) {
                Ctl &last = ctl[(ctl_first + ctl_count - 1) % PRINTER_SPOOL_CTRL_DEPTH];
                if (last.value == value && last.pos == pos) {
                    portEXIT_CRITICAL();
                    return;
                }
            }
            if (ctl_count < PRINTER_SPOOL_CTRL_DEPTH) {
                Ctl &c = ctl[(ctl_first + ctl_count) % PRINTER_SPOOL_CTRL_DEPTH];
                c.value = value;
                c.pos = pos;
                ctl_count = ctl_count + 1;
                portEXIT_CRITICAL();
                wake();
                return;
            }
            portEXIT_CRITICAL();
            vTaskDelay(1);
        }
    }

    /* Consumer: next thing in stream order, without waiting. A marker comes when every
     * event posted before it has been handed out, and not before. */
    int next(uint16_t &ev, uint8_t &ctl_value)
    {
        uint32_t t = tail;
        portENTER_CRITICAL();
        if (ctl_count && ctl[ctl_first].pos == t) {
            ctl_value = ctl[ctl_first].value;
            ctl_first = (ctl_first + 1) % PRINTER_SPOOL_CTRL_DEPTH;
            ctl_count = ctl_count - 1;
            portEXIT_CRITICAL();
            return PRINTER_SPOOL_CTRL;
        }
        portEXIT_CRITICAL();
        if (t != head) {
            PRINTER_SPOOL_BARRIER();
            ev = ring[t & mask];
            PRINTER_SPOOL_BARRIER();
            tail = t + 1;
            return PRINTER_SPOOL_EVENT;
        }
        return PRINTER_SPOOL_NONE;
    }

    /* Consumer: sleep until something was posted since the last wait. */
    void wait(void) { ulTaskNotifyTake(pdTRUE, portMAX_DELAY); }

    uint32_t pending(void) { return head - tail; }

private:
    void wake(void)
    {
        TaskHandle_t t = consumer;
        if (t)
            xTaskNotifyGive(t);
    }
};

#endif
