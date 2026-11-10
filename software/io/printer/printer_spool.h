/*
 * Byte spool between the IEC server (one producer) and the virtual printer task (one
 * consumer): a ring of 16 bit events, plus a short list of markers, each tied to the
 * position in the ring where it was posted. The IEC task never waits for the printer task
 * to finish a page; it waits only when the ring is full (or, for a few microseconds, while
 * the ring moves to another block of memory). No other caller waits on the backlog.
 *
 * Event: bits 8-9 the type, bits 0-7 the value. Positions are free running 32 bit counters;
 * the ring size is a power of two. The big ring (512 KB) is allocated when the printer is
 * switched on; until then, and when the heap refuses it, a small ring inside the object
 * is used, so a byte is never dropped.
 *
 * Every access to the ring, the counters and the markers is inside a critical section,
 * so a marker and the event behind it are never seen out of order, whatever task is
 * posting. Moving the queued events to another ring (up to 4 KB) is done with interrupts
 * on: a 'migrating' flag keeps the producer and the consumer away from the ring meanwhile.
 * The IEC task, in push(), waits out such a move (a copy of at most 4 KB) with whatever locks
 * it holds at that moment, like it waits for a full ring.
 *
 * Markers (never blocking, at most PRINTER_SPOOL_CTRL_DEPTH queued):
 *   settings  a snapshot of the values, applied when the consumer reaches the position
 *   flush     a count and a generation number; the consumer ejects once, then publishes
 *             the generation (flush_done), so a waiter knows its own flush has been done
 * A flush at the same position as the newest marker joins it. Settings never do: every
 * change of the settings gets a marker of its own, in order (a burst of changes with no byte
 * between them is carried out change by change, as an output type toggle that serves as a
 * flush needs); only the same values again, at the same place, are not said twice. When the
 * list is full, a new marker is merged into the newest one AND that one moves to the current
 * position: a merge only ever moves an effect later (the older values apply after more
 * bytes than they should, and the values in between are lost), never earlier. That only
 * happens after PRINTER_SPOOL_CTRL_DEPTH changes within one backlog. A settings merge that
 * would swallow another output type sets 'type_changed' on the marker: the consumer ejects
 * (closes the file) before it applies the last values, so a toggle is never lost. Settings merged into a
 * marker that has a flush are applied after that flush (flush_first), so the page accepted
 * before the change is ejected with the old settings. The marker of a flush that a pending
 * abort waits for is not moved: a merge into it keeps its position.
 *
 * abort(): everything accepted so far is discarded (the consumer jumps to the head as it
 * was when abort() was called) and the consumer is told, ahead of anything else. A Flush
 * that was posted before the abort is still done first: the consumer goes on in stream
 * order up to the newest queued marker that has a flush, then aborts. What the consumer is
 * busy with when the abort comes (a page being encoded) is not interrupted: it is finished
 * and saved, and only what has not been started is discarded.
 */
#ifndef PRINTER_SPOOL_H
#define PRINTER_SPOOL_H

#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"

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
    bool type_changed;      /* the output type was changed more than once in this marker: the consumer ejects first */
    uint8_t flushes;
    bool flush_first;       /* with settings and a flush: the flush is done before the settings are applied */
    uint32_t flush_gen;     /* the newest flush this marker stands for */
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
    uint32_t abort_hold;        /* the newest queued flush, that is done before the abort */
    bool abort_hold_valid;

    volatile bool migrating;    /* the ring is being copied: nobody but its owner touches it */

    uint32_t flush_gen;         /* flushes posted */
    volatile uint32_t done_gen; /* flushes done */
    SemaphoreHandle_t flush_sem;    /* only wakes the waiter; done_gen says whether it is its turn */

    TaskHandle_t consumer;

public:
    PrinterSpool() : ring(fallback), mask(PRINTER_SPOOL_FALLBACK_EVENTS - 1), big(NULL), head(0), tail(0),
                     ctl_first(0), ctl_count(0), abort_kinds(0), abort_to(0), abort_hold(0),
                     abort_hold_valid(false), migrating(false), flush_gen(0), done_gen(0), consumer(NULL)
    {
        flush_sem = xSemaphoreCreateBinary();
    }
    ~PrinterSpool()
    {
        release();
        if (big)
            PRINTER_SPOOL_FREE(big);
        vSemaphoreDelete(flush_sem);
    }

    /* Switch to a big ring (the largest power of two up to 'events' that the heap gives);
     * what is queued moves along. Safe to call from any task, any number of times. */
    bool allocate(uint32_t events = PRINTER_SPOOL_EVENTS, uint32_t min_events = PRINTER_SPOOL_MIN_EVENTS)
    {
        PRINTER_SPOOL_ENTER();
        abort_kinds &= ~PRINTER_SPOOL_ABORT_RELEASE;    /* a Turn Off still on its way must not take it back */
        bool have = (big != NULL && !migrating);        /* a ring on its way back to the small one is not 'have' */
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
        for (;;) {
            PRINTER_SPOOL_ENTER();
            if (!migrating) {
                if (big) {                              /* another task was faster */
                    PRINTER_SPOOL_EXIT();
                    PRINTER_SPOOL_FREE(r);
                    return true;
                }
                migrating = true;
                PRINTER_SPOOL_EXIT();
                break;
            }
            PRINTER_SPOOL_EXIT();               /* the ring is moving (a release, or another allocate): see what it ends up as */
            vTaskDelay(1);
        }
        migrate(r, n - 1, r);
        return true;
    }
    bool allocated(void) { return big != NULL; }

    /* Back to the small ring, if what is queued fits; the big one is freed. */
    void release(void)
    {
        uint16_t *old = NULL;
        PRINTER_SPOOL_ENTER();
        if (big && !migrating && (uint32_t)(head - tail) <= PRINTER_SPOOL_FALLBACK_EVENTS) {
            migrating = true;
            old = big;
        }
        PRINTER_SPOOL_EXIT();
        if (old) {
            migrate(fallback, PRINTER_SPOOL_FALLBACK_EVENTS - 1, NULL);
            PRINTER_SPOOL_FREE(old);
        }
    }

    /* Consumer: the task that gets the wake up. */
    void set_consumer(TaskHandle_t t) { consumer = t; }

    /* Producer (IEC task): false when the ring is full (or on its way to another block). */
    bool try_push(uint16_t ev)
    {
        PRINTER_SPOOL_ENTER();
        if (!migrating && (uint32_t)(head - tail) <= mask) {
            ring[head & mask] = ev;
            head = head + 1;
            PRINTER_SPOOL_EXIT();
            wake();
            return true;
        }
        PRINTER_SPOOL_EXIT();
        return false;
    }

    /* Producer (IEC task). Waits while the ring is full, and only then. */
    bool push(uint16_t ev)
    {
        while (!try_push(ev))
            vTaskDelay(1);
        return true;
    }

    /* Any task, never waiting: the settings, as they are now, from this position on. */
    void post_settings(const PrinterSettings &s)
    {
        PRINTER_SPOOL_ENTER();
        if (ctl_count) {                /* the same values again, at the same place: nothing to say twice */
            PrinterSpoolCtl *n = &ctl[(ctl_first + ctl_count - 1) % PRINTER_SPOOL_CTRL_DEPTH];
            if (n->pos == head && n->has_settings && !n->flushes && same_settings(n->settings, s)) {
                PRINTER_SPOOL_EXIT();
                return;
            }
        }
        PrinterSpoolCtl *c = slot(true);
        if (c->has_settings && c->settings.type != s.type)
            c->type_changed = true;     /* only when the list was full: the output type went through another value */
        c->settings = s;
        c->has_settings = true;
        if (c->flushes)
            c->flush_first = true;      /* only when the list was full: the flush was posted first and stays first */
        PRINTER_SPOOL_EXIT();
        wake();
    }

    /* Any task, never waiting: eject what has been accepted up to now. Returns the number of this flush. */
    uint32_t post_flush(void)
    {
        PRINTER_SPOOL_ENTER();
        PrinterSpoolCtl *c = slot(false);
        if (c->flushes < 255)
            c->flushes++;
        uint32_t gen = ++flush_gen;
        c->flush_gen = gen;
        PRINTER_SPOOL_EXIT();
        wake();
        return gen;
    }

    /* A flush has been done: the generation is published, then the waiter is woken. */
    void flush_done(uint32_t gen)
    {
        if ((int32_t)(gen - done_gen) > 0)
            done_gen = gen;
        xSemaphoreGive(flush_sem);
    }

    /* The Flush/Eject of the menu: post one and wait for it, at most 'timeout_ms'. The semaphore only
     * wakes this task; a wake up that was meant for an earlier flush never ends this wait. */
    bool flush_and_wait(uint32_t timeout_ms)
    {
        while (xSemaphoreTake(flush_sem, 0) == pdTRUE)
            ;
        uint32_t gen = post_flush();
        TickType_t total = timeout_ms / portTICK_PERIOD_MS;
        TickType_t start = xTaskGetTickCount();
        for (;;) {
            if ((int32_t)(done_gen - gen) >= 0)
                return true;
            TickType_t used = xTaskGetTickCount() - start;
            if (used >= total)
                return false;
            xSemaphoreTake(flush_sem, total - used);
        }
    }

    /* Any task, never waiting: discard what has been accepted up to now. */
    void abort(uint8_t kinds)
    {
        PRINTER_SPOOL_ENTER();
        abort_to = head;
        abort_kinds |= (kinds | 0x80);
        abort_hold_valid = false;
        for (uint8_t i = 0; i < ctl_count; i++) {
            PrinterSpoolCtl &c = ctl[(ctl_first + i) % PRINTER_SPOOL_CTRL_DEPTH];
            if (c.flushes) {
                abort_hold = c.pos;
                abort_hold_valid = true;
            }
        }
        PRINTER_SPOOL_EXIT();
        wake();
    }

    /* Consumer: next thing in stream order, without waiting. A marker comes when every
     * event posted before it has been handed out, and not before. */
    int next(uint16_t &ev, PrinterSpoolCtl &c)
    {
        uint16_t *old = NULL;
        int result = PRINTER_SPOOL_NONE;
        PRINTER_SPOOL_ENTER();
        if (migrating) {                    /* woken again when the move is done */
            PRINTER_SPOOL_EXIT();
            return PRINTER_SPOOL_NONE;
        }
        uint32_t t = tail;
        uint32_t h = head;      /* with the markers, in one critical section */
        bool abort_ready = abort_kinds &&
            (!abort_hold_valid || !ctl_count || (int32_t)(ctl[ctl_first].pos - abort_hold) > 0);
        if (abort_ready) {
            c.abort_kinds = abort_kinds & 0x7F;
            if ((int32_t)(abort_to - t) > 0)
                tail = abort_to;
            abort_kinds = 0;
            result = PRINTER_SPOOL_ABORT;
            if ((c.abort_kinds & PRINTER_SPOOL_ABORT_RELEASE) && big &&
                (uint32_t)(head - tail) <= PRINTER_SPOOL_FALLBACK_EVENTS) {
                migrating = true;
                old = big;
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
        if (old) {
            migrate(fallback, PRINTER_SPOOL_FALLBACK_EVENTS - 1, NULL);
            PRINTER_SPOOL_FREE(old);
        }
        return result;
    }

    /* Consumer: sleep until something was posted since the last wait. */
    void wait(void) { ulTaskNotifyTake(pdTRUE, portMAX_DELAY); }

    uint32_t pending(void) { return head - tail; }

private:
    /* The caller has set 'migrating' (under the lock): the producer and the consumer stay away from
     * the ring, so what is queued is copied with interrupts on, then the new ring is published. */
    void migrate(uint16_t *to, uint32_t new_mask, uint16_t *new_big)
    {
        uint32_t t = tail;
        uint32_t n = head - t;
        for (uint32_t i = 0; i < n; i++)
            to[(t + i) & new_mask] = ring[(t + i) & mask];
        PRINTER_SPOOL_ENTER();
        ring = to;
        mask = new_mask;
        big = new_big;
        PRINTER_SPOOL_BARRIER();
        migrating = false;
        PRINTER_SPOOL_EXIT();
        wake();
    }

    /* In the critical section: the marker to fill in. A new one at the head, unless a flush finds
     * the newest at the head already; when the list is full, the newest one. */
    static bool same_settings(const PrinterSettings &a, const PrinterSettings &b)
    {
        return !strncmp(a.filename, b.filename, sizeof(a.filename)) && a.type == b.type && a.density == b.density &&
               a.emulation == b.emulation && a.cbm_charset == b.cbm_charset && a.epson_charset == b.epson_charset &&
               a.ibm_charset == b.ibm_charset && a.page_top == b.page_top && a.page_height == b.page_height;
    }

    PrinterSpoolCtl *slot(bool for_settings)
    {
        if (ctl_count) {
            PrinterSpoolCtl *n = &ctl[(ctl_first + ctl_count - 1) % PRINTER_SPOOL_CTRL_DEPTH];
            if (!for_settings && n->pos == head)
                return n;               /* a flush joins the marker at the same place; settings never do */
            if (ctl_count == PRINTER_SPOOL_CTRL_DEPTH) {
                /* the merged marker moves later, never earlier; but a flush that an abort waits for stays where it is */
                if (!(abort_kinds && abort_hold_valid && n->pos == abort_hold))
                    n->pos = head;
                return n;
            }
        }
        PrinterSpoolCtl *c = &ctl[(ctl_first + ctl_count) % PRINTER_SPOOL_CTRL_DEPTH];
        c->pos = head;
        c->has_settings = false;
        c->type_changed = false;
        c->flushes = 0;
        c->flush_first = false;
        c->flush_gen = 0;
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
