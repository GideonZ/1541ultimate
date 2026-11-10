/*
 * Byte spool between the IEC server (one producer) and the virtual printer task (one
 * consumer): a ring of 16 bit events, plus a short list of markers, each tied to the
 * position in the ring where it was posted. The IEC task never waits for the printer task
 * to finish a page; it waits only when the ring is full. No other caller waits at all.
 *
 * Event: bits 8-9 the type, bits 0-7 the value. Positions are free running 32 bit counters;
 * the ring size is a power of two. The big ring (512 KB) is allocated when the printer is
 * switched on; until then, and when the heap refuses it, a small ring inside the object
 * is used, so a byte is never dropped.
 *
 * Every access to the ring, the counters and the markers is inside one critical section:
 * a marker and the event behind it are never seen out of order, whatever task is posting.
 *
 * Markers (never blocking, at most PRINTER_SPOOL_CTRL_DEPTH queued):
 *   settings  a snapshot of the values, applied when the consumer reaches the position
 *   flush     a count; the consumer ejects once and answers every waiter
 * When the list is full a new marker is merged into the newest one (settings: the newer
 * values replace the older ones; flush: the count grows) and takes effect at that earlier
 * position. That only happens after PRINTER_SPOOL_CTRL_DEPTH changes within one backlog.
 *
 * abort(): everything accepted so far is discarded (the consumer jumps to the head as it
 * was when abort() was called) and the consumer is told, ahead of anything else.
 */
#ifndef PRINTER_SPOOL_H
#define PRINTER_SPOOL_H

#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "FreeRTOS.h"
#include "task.h"

#ifndef PRINTER_SPOOL_MALLOC
#define PRINTER_SPOOL_MALLOC malloc
#define PRINTER_SPOOL_FREE   free
#endif

#define PRINTER_SPOOL_EVENTS          (256 * 1024)   /* 512 KB */
#define PRINTER_SPOOL_MIN_EVENTS      4096
#define PRINTER_SPOOL_FALLBACK_EVENTS 2048           /* 4 KB, always there */
#define PRINTER_SPOOL_CTRL_DEPTH      8

/* One CPU on the device: the compiler must not reorder (and the critical section of the port
 * has no memory clobber of its own). On the host, two threads. */
#ifdef PRINTER_SPOOL_HOST_TEST
#define PRINTER_SPOOL_BARRIER() __atomic_thread_fence(__ATOMIC_SEQ_CST)
#else
#define PRINTER_SPOOL_BARRIER() __asm__ __volatile__("" ::: "memory")
#endif
#define PRINTER_SPOOL_ENTER() do { portENTER_CRITICAL(); PRINTER_SPOOL_BARRIER(); } while (0)
#define PRINTER_SPOOL_EXIT()  do { PRINTER_SPOOL_BARRIER(); portEXIT_CRITICAL(); } while (0)

enum {
    PRINTER_SPOOL_NONE = 0,
    PRINTER_SPOOL_EVENT,
    PRINTER_SPOOL_CTRL,
    PRINTER_SPOOL_ABORT
};

/* Kinds of abort */
#define PRINTER_SPOOL_ABORT_RESET    1   /* the consumer resets the emulation */
#define PRINTER_SPOOL_ABORT_RELEASE  2   /* the big ring is given back, if what is left fits the small one */

/* The settings the printer task applies, by value */
struct PrinterSettings {
    char filename[64];
    uint8_t type, density, emulation, cbm_charset, epson_charset, ibm_charset, page_top, page_height;
};

struct PrinterSpoolCtl {
    uint32_t pos;
    bool has_settings;
    PrinterSettings settings;
    uint8_t flushes;
    uint8_t abort_kinds;    /* for PRINTER_SPOOL_ABORT */
};

class PrinterSpool
{
private:
    uint16_t *ring;
    uint32_t mask;
    uint16_t *big;              /* the heap ring, if any */
    uint16_t fallback[PRINTER_SPOOL_FALLBACK_EVENTS];
    volatile uint32_t head;     /* written by the producer only */
    volatile uint32_t tail;     /* written by the consumer only */

    PrinterSpoolCtl ctl[PRINTER_SPOOL_CTRL_DEPTH];
    uint8_t ctl_first, ctl_count;

    uint8_t abort_kinds;
    uint32_t abort_to;

    TaskHandle_t consumer;

public:
    PrinterSpool() : ring(fallback), mask(PRINTER_SPOOL_FALLBACK_EVENTS - 1), big(NULL), head(0), tail(0),
                     ctl_first(0), ctl_count(0), abort_kinds(0), abort_to(0), consumer(NULL) { }
    ~PrinterSpool()
    {
        release();
        if (big)
            PRINTER_SPOOL_FREE(big);
    }

    /* Switch to a big ring (the largest power of two up to 'events' that the heap gives);
     * what is queued moves along. Safe to call from any task, any number of times. */
    bool allocate(uint32_t events = PRINTER_SPOOL_EVENTS, uint32_t min_events = PRINTER_SPOOL_MIN_EVENTS)
    {
        PRINTER_SPOOL_ENTER();
        abort_kinds &= ~PRINTER_SPOOL_ABORT_RELEASE;    /* a Turn Off still on its way must not take it back */
        bool have = (big != NULL);
        PRINTER_SPOOL_EXIT();
        if (have)
            return true;
        uint32_t n = 1;
        while ((n << 1) <= events && (n << 1) != 0)
            n <<= 1;
        uint16_t *r = NULL;
        for (; n >= min_events && n >= PRINTER_SPOOL_FALLBACK_EVENTS; n >>= 1) {
            r = (uint16_t *) PRINTER_SPOOL_MALLOC(n * sizeof(uint16_t));
            if (r)
                break;
        }
        if (!r) {
            printf("Printer spool: no memory for the big ring, using %d events\n", PRINTER_SPOOL_FALLBACK_EVENTS);
            return false;
        }
        PRINTER_SPOOL_ENTER();
        if (big) {                                      /* another task was faster */
            PRINTER_SPOOL_EXIT();
            PRINTER_SPOOL_FREE(r);
            return true;
        }
        move_to(r, n - 1);
        big = r;
        PRINTER_SPOOL_EXIT();
        return true;
    }
    bool allocated(void) { return big != NULL; }

    /* Back to the small ring, if what is queued fits; the big one is freed. */
    void release(void)
    {
        PRINTER_SPOOL_ENTER();
        uint16_t *r = big;
        if (r && (uint32_t)(head - tail) <= PRINTER_SPOOL_FALLBACK_EVENTS) {
            move_to(fallback, PRINTER_SPOOL_FALLBACK_EVENTS - 1);
            big = NULL;
        } else {
            r = NULL;
        }
        PRINTER_SPOOL_EXIT();
        if (r)
            PRINTER_SPOOL_FREE(r);
    }

    /* Consumer: the task that gets the wake up. */
    void set_consumer(TaskHandle_t t) { consumer = t; }

    /* Producer (IEC task). Waits while the ring is full, and only then. */
    bool push(uint16_t ev)
    {
        for (;;) {
            PRINTER_SPOOL_ENTER();
            if ((uint32_t)(head - tail) <= mask) {
                ring[head & mask] = ev;
                head = head + 1;
                PRINTER_SPOOL_EXIT();
                wake();
                return true;
            }
            PRINTER_SPOOL_EXIT();
            vTaskDelay(1);
        }
    }

    /* Any task, never waiting: the settings, as they are now, from this position on. */
    void post_settings(const PrinterSettings &s)
    {
        PRINTER_SPOOL_ENTER();
        PrinterSpoolCtl *c = slot(true);
        c->settings = s;
        c->has_settings = true;
        PRINTER_SPOOL_EXIT();
        wake();
    }

    /* Any task, never waiting: eject what has been accepted up to now. */
    void post_flush(void)
    {
        PRINTER_SPOOL_ENTER();
        PrinterSpoolCtl *c = slot(false);
        if (c->flushes < 255)
            c->flushes++;
        PRINTER_SPOOL_EXIT();
        wake();
    }

    /* Any task, never waiting: discard what has been accepted up to now. */
    void abort(uint8_t kinds)
    {
        PRINTER_SPOOL_ENTER();
        abort_to = head;
        abort_kinds |= (kinds | 0x80);
        PRINTER_SPOOL_EXIT();
        wake();
    }

    /* Consumer: next thing in stream order, without waiting. A marker comes when every
     * event posted before it has been handed out, and not before. */
    int next(uint16_t &ev, PrinterSpoolCtl &c)
    {
        uint16_t *to_free = NULL;
        int result = PRINTER_SPOOL_NONE;
        PRINTER_SPOOL_ENTER();
        uint32_t t = tail;
        uint32_t h = head;      /* with the markers, in one critical section */
        if (abort_kinds) {
            c.abort_kinds = abort_kinds & 0x7F;
            if ((int32_t)(abort_to - t) > 0)
                tail = abort_to;
            abort_kinds = 0;
            result = PRINTER_SPOOL_ABORT;
            if ((c.abort_kinds & PRINTER_SPOOL_ABORT_RELEASE) && big &&
                (uint32_t)(head - tail) <= PRINTER_SPOOL_FALLBACK_EVENTS) {
                to_free = big;
                move_to(fallback, PRINTER_SPOOL_FALLBACK_EVENTS - 1);
                big = NULL;
            }
        } else if (ctl_count && (int32_t)(ctl[ctl_first].pos - t) <= 0) {
            c = ctl[ctl_first];
            ctl_first = (ctl_first + 1) % PRINTER_SPOOL_CTRL_DEPTH;
            ctl_count = ctl_count - 1;
            result = PRINTER_SPOOL_CTRL;
        } else if (t != h) {
            ev = ring[t & mask];
            tail = t + 1;
            result = PRINTER_SPOOL_EVENT;
        }
        PRINTER_SPOOL_EXIT();
        if (to_free)
            PRINTER_SPOOL_FREE(to_free);
        return result;
    }

    /* Consumer: sleep until something was posted since the last wait. */
    void wait(void) { ulTaskNotifyTake(pdTRUE, portMAX_DELAY); }

    uint32_t pending(void) { return head - tail; }

private:
    /* In the critical section: the ring changes to 'to', what is queued comes along. */
    void move_to(uint16_t *to, uint32_t new_mask)
    {
        uint32_t n = head - tail;
        for (uint32_t i = 0; i < n; i++)
            to[(tail + i) & new_mask] = ring[(tail + i) & mask];
        ring = to;
        mask = new_mask;
    }

    /* In the critical section: the marker to fill in. A new one at the head, unless the newest
     * is at the head already (and, for settings, has no flush yet: they stay in order); when
     * the list is full, the newest one. */
    PrinterSpoolCtl *slot(bool for_settings)
    {
        if (ctl_count) {
            PrinterSpoolCtl *n = &ctl[(ctl_first + ctl_count - 1) % PRINTER_SPOOL_CTRL_DEPTH];
            if (n->pos == head && !(for_settings && n->flushes))
                return n;
            if (ctl_count == PRINTER_SPOOL_CTRL_DEPTH)
                return n;
        }
        PrinterSpoolCtl *c = &ctl[(ctl_first + ctl_count) % PRINTER_SPOOL_CTRL_DEPTH];
        c->pos = head;
        c->has_settings = false;
        c->flushes = 0;
        c->abort_kinds = 0;
        ctl_count = ctl_count + 1;
        return c;
    }

    void wake(void)
    {
        TaskHandle_t t = consumer;
        if (t)
            xTaskNotifyGive(t);
    }
};

#endif
