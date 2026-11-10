/*
 * Host test of the printer spool: event and marker order, wrap around (also of the 32 bit
 * counters), a marker posted while the consumer is between two calls, a wake up that is never
 * lost, a full ring, markers that never block, abort, the small ring when the heap refuses
 * the big one. Every wait is bounded and ends in a failed check, not in a hang.
 */
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <atomic>
#include <signal.h>
#include <stdlib.h>
#include <unistd.h>

static std::atomic<bool> malloc_fails(false);
static std::atomic<int> live_blocks(0);
static void *test_malloc(size_t n)
{
    if (malloc_fails)
        return NULL;
    live_blocks++;
    return malloc(n);
}
static void test_free(void *p)
{
    live_blocks--;
    free(p);
}
#define PRINTER_SPOOL_MALLOC test_malloc
#define PRINTER_SPOOL_FREE test_free
#define private public
#include "printer_spool.h"
#undef private

static void on_alarm(int)
{
    static const char m[] = "FAIL hang: a test did not finish\n";
    if (write(1, m, sizeof(m) - 1) < 0) { }
    _exit(1);
}

static int failures = 0;
static int checks = 0;
#define CHECK(c) do { checks++; if (!(c)) { failures++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

static uint16_t EV(int type, int v) { return (uint16_t)((type << 8) | (v & 0xFF)); }

static PrinterSettings settings(int v)
{
    PrinterSettings s;
    memset(&s, 0, sizeof(s));
    snprintf(s.filename, sizeof(s.filename), "file%d", v);
    s.type = s.density = s.emulation = (uint8_t) v;
    return s;
}

/* Drains everything available: events as 0x1000|ev, settings as 0x2000|type, flush as 0x3000|count,
 * abort as 0x4000|kinds. */
static int drain(PrinterSpool &s, uint16_t *out, int max)
{
    int n = 0;
    uint16_t ev;
    PrinterSpoolCtl c;
    for (;;) {
        int r = s.next(ev, c);
        if (r == PRINTER_SPOOL_NONE || n == max)
            return n;
        if (r == PRINTER_SPOOL_EVENT) {
            out[n++] = 0x1000 | ev;
        } else if (r == PRINTER_SPOOL_ABORT) {
            out[n++] = 0x4000 | c.abort_kinds;
        } else {
            if (c.flush_first && c.flushes)
                out[n++] = 0x3000 | c.flushes;
            if (c.has_settings) {
                CHECK(strcmp(c.settings.filename, "file") != 0);
                out[n++] = 0x2000 | c.settings.type;
            }
            if (!c.flush_first && c.flushes)
                out[n++] = 0x3000 | c.flushes;
        }
    }
}

static void test_ordering(void)
{
    PrinterSpool s;
    uint16_t out[64];
    s.post_settings(settings(7));       /* before any data: first */
    for (int i = 0; i < 5; i++) CHECK(s.push(EV(1, i)));
    s.post_flush();
    s.post_settings(settings(2));       /* same position, after the flush: its own marker */
    for (int i = 5; i < 8; i++) CHECK(s.push(EV(0, i)));
    s.post_flush();
    int n = drain(s, out, 64);
    uint16_t want[] = { 0x2007, 0x1100, 0x1101, 0x1102, 0x1103, 0x1104, 0x3001, 0x2002,
                        0x1005, 0x1006, 0x1007, 0x3001 };
    CHECK(n == 12);
    CHECK(memcmp(out, want, sizeof(want)) == 0);
    CHECK(s.pending() == 0);
}

static void test_wrap(uint32_t start)
{
    PrinterSpool s;
    s.mask = 15;                        /* a small ring, to wrap often */
    s.head = s.tail = start;            /* also across the 32 bit counter wrap */
    uint16_t out[64];
    int seq = 0, got = 0;
    for (int round = 0; round < 200; round++) {
        int k = 1 + (round * 7) % 13;   /* 1..13 events, never more than the 16 slots */
        for (int i = 0; i < k; i++) CHECK(s.push(EV(1, seq++)));
        if (round % 3 == 0) s.post_flush();
        int n = drain(s, out, 64);
        for (int i = 0; i < n; i++) {
            if (out[i] & 0x2000) continue;
            CHECK((out[i] & 0xFF) == (uint16_t)(got & 0xFF) && ((out[i] >> 8) & 3) == 1);
            got++;
        }
    }
    CHECK(got == seq);
    /* a marker exactly at the slot that wraps */
    s.head = s.tail = start + 15;
    s.push(EV(1, 1)); s.post_flush(); s.push(EV(1, 2)); s.push(EV(1, 3));
    int n = drain(s, out, 64);
    CHECK(n == 4 && out[0] == 0x1101 && out[1] == 0x3001 && out[2] == 0x1102 && out[3] == 0x1103);
}

static void test_wrap_0(void) { test_wrap(0); }
static void test_wrap_ovf(void) { test_wrap(0xFFFFFFF0u); }

static void test_coalesce_and_exhaustion(void)
{
    PrinterSpool s;
    uint16_t out[64];
    s.post_flush();
    s.post_flush();                     /* same position: one marker, count 2 */
    s.post_settings(settings(1));
    s.post_settings(settings(3));       /* same position: the newer values replace the older */
    s.push(EV(1, 1));
    int n = drain(s, out, 64);
    CHECK(n == 3 && out[0] == 0x3002 && out[1] == 0x2003 && out[2] == 0x1101);

    /* more changes than markers fit: none of them waits, the last values arrive, no flush is lost,
     * and nothing takes effect earlier than it was posted */
    uint32_t settings_pos[64], flush_pos[64];
    int flushes = 0;
    for (int i = 0; i < 40; i++) {
        s.push(EV(1, i));
        s.post_settings(settings(i + 1));
        settings_pos[i + 1] = i + 1;
        if (i % 3 == 0) { uint32_t g = s.post_flush(); flush_pos[g] = i + 1; flushes++; }
    }
    int got_flushes = 0, last_type = -1, events = 0, early = 0;
    for (;;) {
        uint16_t ev;
        PrinterSpoolCtl c;
        int r = s.next(ev, c);
        if (r == PRINTER_SPOOL_NONE) break;
        if (r == PRINTER_SPOOL_EVENT) events++;
        if (r == PRINTER_SPOOL_CTRL) {
            got_flushes += c.flushes;
            if (c.flushes && (uint32_t) events < flush_pos[c.flush_gen]) early++;
            if (c.has_settings) {
                last_type = c.settings.type;
                if ((uint32_t) events < settings_pos[c.settings.type]) early++;
            }
        }
    }
    CHECK(events == 40);
    CHECK(got_flushes == flushes);
    CHECK(last_type == 40);
    CHECK(early == 0);
}

/* The values travel with the marker: the settings at the time of posting, not at the time of applying. */
static void test_settings_snapshot(void)
{
    PrinterSpool s;
    PrinterSettings a = settings(1), b = settings(2);
    s.post_settings(a);
    s.push(EV(1, 1));
    s.post_settings(b);
    s.push(EV(1, 2));
    strcpy(a.filename, "changed");      /* the poster's copy changes afterwards */
    a.type = 99;
    uint16_t ev;
    PrinterSpoolCtl c;
    CHECK(s.next(ev, c) == PRINTER_SPOOL_CTRL && c.has_settings && c.settings.type == 1 && !strcmp(c.settings.filename, "file1"));
    CHECK(s.next(ev, c) == PRINTER_SPOOL_EVENT);
    CHECK(s.next(ev, c) == PRINTER_SPOOL_CTRL && c.has_settings && c.settings.type == 2 && !strcmp(c.settings.filename, "file2"));
    CHECK(s.next(ev, c) == PRINTER_SPOOL_EVENT);
    CHECK(s.next(ev, c) == PRINTER_SPOOL_NONE);
}

/* A flush posted before the abort is done first, in stream order; what comes after it is discarded. */
static void test_abort(void)
{
    PrinterSpool s;
    uint16_t out[64];
    for (int i = 0; i < 10; i++) s.push(EV(1, i));
    s.post_settings(settings(5));
    s.post_flush();
    for (int i = 10; i < 15; i++) s.push(EV(1, i));
    s.abort(PRINTER_SPOOL_ABORT_RESET);
    s.push(EV(1, 15));                  /* accepted after the abort: kept */
    s.push(EV(1, 16));
    int n = drain(s, out, 64);
    CHECK(n == 10 + 2 + 1 + 2);
    for (int i = 0; i < 10 && i < n; i++) CHECK(out[i] == (0x1100 | i));    /* up to the flush, in order */
    CHECK(out[10] == 0x2005 && out[11] == 0x3001);
    CHECK(out[12] == 0x4001);           /* then the abort, and what the flush did not cover is gone */
    CHECK(out[13] == 0x110F && out[14] == 0x1110);
    CHECK(s.pending() == 0);
    /* an abort on an empty spool is harmless, and a second one too */
    s.abort(0);
    s.abort(0);
    n = drain(s, out, 64);
    CHECK(n == 1 && out[0] == 0x4000);
}

/* No flush pending: the abort comes first and everything accepted before it is gone. */
static void test_abort_without_flush(void)
{
    PrinterSpool s;
    uint16_t out[64];
    for (int i = 0; i < 10; i++) s.push(EV(1, i));
    s.post_settings(settings(5));       /* still applied, after the abort */
    s.abort(PRINTER_SPOOL_ABORT_RESET | PRINTER_SPOOL_ABORT_RELEASE);
    s.push(EV(1, 15));
    int n = drain(s, out, 64);
    CHECK(n == 3 && out[0] == 0x4003 && out[1] == 0x2005 && out[2] == 0x110F);
}

static void test_small_ring(void)
{
    PrinterSpool s;
    uint16_t out[64];
    malloc_fails = true;
    CHECK(!s.allocate());
    CHECK(!s.allocated());
    for (int i = 0; i < PRINTER_SPOOL_FALLBACK_EVENTS; i++) CHECK(s.try_push(EV(1, i)));   /* never dropped, never waiting */
    CHECK(!s.try_push(EV(1, 0)));       /* and full at exactly that */
    CHECK(s.pending() == PRINTER_SPOOL_FALLBACK_EVENTS);
    malloc_fails = false;
    CHECK(s.allocate());                /* the queued events move to the big ring, in order */
    CHECK(s.allocated());
    CHECK(s.mask == PRINTER_SPOOL_EVENTS - 1);
    for (int i = 0; i < 5; i++) s.push(EV(1, i));
    int bad = 0;
    for (int i = 0; i < PRINTER_SPOOL_FALLBACK_EVENTS + 5; i++) {
        uint16_t ev;
        PrinterSpoolCtl c;
        if (s.next(ev, c) != PRINTER_SPOOL_EVENT || (ev & 0xFF) != (uint16_t)(i < PRINTER_SPOOL_FALLBACK_EVENTS ? i & 0xFF : i - PRINTER_SPOOL_FALLBACK_EVENTS))
            bad++;
    }
    CHECK(bad == 0);
    /* back to the small ring: a Turn Off after the discard */
    for (int i = 0; i < 100; i++) s.push(EV(1, i));
    s.abort(PRINTER_SPOOL_ABORT_RELEASE);
    for (int i = 0; i < 3; i++) s.push(EV(1, i));
    int n = drain(s, out, 64);
    CHECK(n == 4 && out[0] == 0x4002 && out[1] == 0x1100 && out[3] == 0x1102);
    CHECK(!s.allocated());
    CHECK(live_blocks == 0);
    /* a Turn On while the release is on its way keeps the ring */
    CHECK(s.allocate());
    s.abort(PRINTER_SPOOL_ABORT_RELEASE);
    CHECK(s.allocate());
    n = drain(s, out, 64);
    CHECK(n == 1 && out[0] == 0x4000);
    CHECK(s.allocated());
    /* more queued than the small ring holds: the big one stays */
    for (int i = 0; i < PRINTER_SPOOL_FALLBACK_EVENTS + 10; i++) CHECK(s.try_push(EV(1, i)));
    s.release();
    CHECK(s.allocated());
    s.tail = s.head;
    s.release();
    CHECK(!s.allocated());
    CHECK(live_blocks == 0);
}

/* An abort with release, while more than the small ring holds was accepted after it: the big ring stays. */
static void test_abort_release_guard(void)
{
    PrinterSpool s;
    CHECK(s.allocate());
    s.push(EV(1, 1));
    s.abort(PRINTER_SPOOL_ABORT_RELEASE);
    for (int i = 0; i < PRINTER_SPOOL_FALLBACK_EVENTS + 952; i++) CHECK(s.try_push(EV(1, i)));
    uint16_t ev;
    PrinterSpoolCtl c;
    CHECK(s.next(ev, c) == PRINTER_SPOOL_ABORT);
    CHECK(s.allocated());               /* it would not fit the small ring */
    CHECK(s.pending() == PRINTER_SPOOL_FALLBACK_EVENTS + 952);
    int bad = 0;
    for (int i = 0; i < PRINTER_SPOOL_FALLBACK_EVENTS + 952; i++)
        if (s.next(ev, c) != PRINTER_SPOOL_EVENT || (ev & 0xFF) != (uint16_t)(i & 0xFF)) bad++;
    CHECK(bad == 0);
}

/* While the ring moves, the producer and the consumer keep away from it. */
static void test_migrating(void)
{
    PrinterSpool s;
    uint16_t ev;
    PrinterSpoolCtl c;
    CHECK(s.try_push(EV(1, 1)));
    s.migrating = true;
    CHECK(!s.try_push(EV(1, 2)));
    CHECK(s.next(ev, c) == PRINTER_SPOOL_NONE);     /* an event is there, and the consumer stays away */
    s.post_flush();                     /* posting does not care */
    CHECK(s.next(ev, c) == PRINTER_SPOOL_NONE);
    s.migrating = false;
    CHECK(s.try_push(EV(1, 2)));
    CHECK(s.next(ev, c) == PRINTER_SPOOL_EVENT);
    CHECK(s.next(ev, c) == PRINTER_SPOOL_CTRL);
}

/* Turn On while the printer task is giving the big ring back (Turn Off): the printer ends up on a big ring. */
static PrinterSpool *turnon_spool;
static pthread_t turnon_thread;
static void *turnon_main(void *) { turnon_spool->allocate(); return NULL; }
static void turn_on_now(void)
{
    pthread_create(&turnon_thread, NULL, turnon_main, NULL);    /* the task that preempts the printer task here */
    usleep(50000);                      /* it gets as far as it can while the move is not done */
}

static void test_turn_on_during_release(void)
{
    PrinterSpool s;
    turnon_spool = &s;
    CHECK(s.allocate());
    s.abort(PRINTER_SPOOL_ABORT_RESET | PRINTER_SPOOL_ABORT_RELEASE);   /* Turn Off */
    uint16_t ev;
    PrinterSpoolCtl c;
    host_rtos_exit_hook() = turn_on_now;    /* ... and Turn On, between the printer task's check and its copy */
    CHECK(s.next(ev, c) == PRINTER_SPOOL_ABORT);
    host_rtos_exit_hook() = NULL;
    pthread_join(turnon_thread, NULL);
    CHECK(s.allocated());
    CHECK(s.mask == PRINTER_SPOOL_EVENTS - 1);
    CHECK(live_blocks == 1);
    CHECK(s.try_push(EV(1, 1)));
}

/* A settings change never ends up before a flush that was posted first, also when the marker list is full. */
static void test_settings_after_flush_when_full(void)
{
    PrinterSpool s;
    uint16_t out[64];
    for (int i = 0; i < PRINTER_SPOOL_CTRL_DEPTH; i++) { s.push(EV(1, i)); s.post_flush(); }   /* full, a flush in each */
    s.push(EV(1, 8));
    s.post_settings(settings(9));       /* the page before this keeps the old settings */
    int n = drain(s, out, 64);
    int flush_at = -1, settings_at = -1;
    for (int i = 0; i < n; i++) {
        if ((out[i] & 0xF000) == 0x3000) flush_at = i;
        if ((out[i] & 0xF000) == 0x2000) settings_at = i;
    }
    CHECK(settings_at >= 0 && flush_at >= 0);
    CHECK(flush_at < settings_at);      /* the flush is done, then the settings are applied */
    /* and the flushes are all there */
    int flushes = 0;
    for (int i = 0; i < n; i++) if ((out[i] & 0xF000) == 0x3000) flushes += out[i] & 0xFF;
    CHECK(flushes == PRINTER_SPOOL_CTRL_DEPTH);
}

/* A flush that an abort waits for is not pushed past the abort by a merge. */
static void test_merge_keeps_held_flush(void)
{
    PrinterSpool s;
    uint16_t out[64];
    for (int i = 0; i < PRINTER_SPOOL_CTRL_DEPTH; i++) { s.push(EV(1, i)); s.post_flush(); }   /* full */
    s.abort(PRINTER_SPOOL_ABORT_RESET);
    s.push(EV(1, 20));                  /* accepted after the abort */
    s.post_flush();                     /* the list is full: merged into the newest, which the abort waits for */
    int n = drain(s, out, 64);
    int abort_at = -1, flushes_before_abort = 0;
    for (int i = 0; i < n; i++) {
        if ((out[i] & 0xF000) == 0x4000) abort_at = i;
        if (abort_at < 0 && (out[i] & 0xF000) == 0x3000) flushes_before_abort += out[i] & 0xFF;
    }
    CHECK(abort_at >= 0);
    CHECK(flushes_before_abort == PRINTER_SPOOL_CTRL_DEPTH + 1);    /* every flush is done before the abort */
    CHECK(out[n - 1] == 0x1114);        /* what was accepted after the abort is kept */
}

static void *alloc_main(void *arg)
{
    ((PrinterSpool *) arg)->allocate();
    return NULL;
}

static void test_allocate_race(void)
{
    for (int round = 0; round < 50; round++) {
        PrinterSpool *s = new PrinterSpool;
        pthread_t t[4];
        for (int i = 0; i < 4; i++) pthread_create(&t[i], NULL, alloc_main, s);
        for (int i = 0; i < 4; i++) pthread_join(t[i], NULL);
        CHECK(s->allocated());
        CHECK(live_blocks == 1);        /* the losers gave theirs back */
        delete s;
        CHECK(live_blocks == 0);
    }
}

static PrinterSpool *alloc_spool;
static void allocate_again(void)
{
    alloc_spool->allocate();
}

/* Another task allocates while this one is between its check and its switch. */
static void test_alloc_hook(void)
{
    PrinterSpool s;
    alloc_spool = &s;
    host_rtos_exit_hook() = allocate_again;
    CHECK(s.allocate());
    host_rtos_exit_hook() = NULL;
    CHECK(s.allocated());
    CHECK(live_blocks == 1);            /* the slower one gave its block back */
}

/* ---- the consumer between two of its calls (the preemption of the device, on the host) ---- */
static PrinterSpool *race_spool;
static void preempt(void)
{
    race_spool->post_flush();           /* another task: a marker at the position the consumer is at */
    race_spool->push(EV(1, 0x41));      /* the IEC task: a byte behind it */
}

static void test_marker_race(void)
{
    for (int round = 0; round < 100; round++) {
        PrinterSpool s;
        race_spool = &s;
        uint16_t ev;
        PrinterSpoolCtl c;
        host_rtos_exit_hook() = preempt;    /* fires at the end of the critical section in next() */
        int r = s.next(ev, c);
        host_rtos_exit_hook() = NULL;
        int first_event = -1, first_marker = -1, k = 0;
        if (r == PRINTER_SPOOL_EVENT) first_event = k++;
        if (r == PRINTER_SPOOL_CTRL) first_marker = k++;
        for (int i = 0; i < 10; i++) {
            r = s.next(ev, c);
            if (r == PRINTER_SPOOL_EVENT && first_event < 0) first_event = k++;
            if (r == PRINTER_SPOOL_CTRL && first_marker < 0) first_marker = k++;
        }
        CHECK(first_marker >= 0);       /* the marker is delivered, once the byte has gone past or not */
        CHECK(first_event >= 0);
        CHECK(first_marker < first_event);  /* ... before the byte that was accepted after it */
        CHECK(s.ctl_count == 0);
    }
}

/* ---- threads ---- */
struct Consumer {
    PrinterSpool *s;
    std::atomic<uint32_t> events, markers, aborts;
    std::atomic<bool> stop;
    std::atomic<uint32_t> bad;
    uint32_t expect;
    TaskHandle_t handle;
};

static void *consumer_main(void *arg)
{
    Consumer *c = (Consumer *) arg;
    c->handle = xTaskGetCurrentTaskHandle();
    c->s->set_consumer(c->handle);
    for (;;) {
        uint16_t ev;
        PrinterSpoolCtl ctl;
        int r = c->s->next(ev, ctl);
        if (r == PRINTER_SPOOL_EVENT) {
            if ((ev & 0xFF) != (c->expect & 0xFF)) c->bad++;
            c->expect++;
            c->events++;
        } else if (r == PRINTER_SPOOL_ABORT) {
            c->aborts++;
        } else if (r == PRINTER_SPOOL_CTRL) {
            /* a marker was posted after every 100 events, one flush each */
            if (c->events % 100 != 0 || ctl.flushes != 1) c->bad++;
            c->markers++;
            if (ctl.flushes) c->s->flush_done(ctl.flush_gen);
        } else {
            if (c->stop) { c->s->set_consumer(NULL); return NULL; }
            c->s->wait();
        }
    }
}

static bool wait_for(std::atomic<uint32_t> &v, uint32_t target, int ms)
{
    for (int i = 0; i < ms; i++) {
        if (v >= target) return true;
        usleep(1000);
    }
    return v >= target;
}

static bool wait_sleeps(unsigned above, int ms)
{
    for (int i = 0; i < ms; i++) {
        if (host_rtos_sleeps() > above) return true;
        usleep(1000);
    }
    return false;
}

static void stop_consumer(Consumer &c, pthread_t th, bool ok)
{
    c.stop = true;
    if (ok) {
        xTaskNotifyGive(c.handle);      /* wake it so it sees 'stop' */
    } else {
        pthread_cancel(th);             /* hung (a lost wake up): the check has failed, do not hang as well */
    }
    pthread_join(th, NULL);
}

/* One event, wait until it has been consumed, many times: a lost wake up fails this. */
static void test_lost_wake(void)
{
    PrinterSpool s;
    Consumer c;
    c.s = &s; c.events = 0; c.markers = 0; c.aborts = 0; c.stop = false; c.bad = 0; c.expect = 0; c.handle = NULL;
    pthread_t th;
    pthread_create(&th, NULL, consumer_main, &c);
    bool ok = true;
    for (uint32_t i = 1; i <= 3000 && ok; i++) {
        s.push(EV(1, (int)(i - 1)));
        ok = wait_for(c.events, i, 500);
        if (ok && i % 100 == 0) {       /* a marker (one flush) after every 100 events, wait for it */
            s.post_flush();
            ok = wait_for(c.markers, i / 100, 500);
        }
    }
    CHECK(ok);
    if (ok) {
        s.abort(0);                     /* and an abort wakes it too */
        CHECK(wait_for(c.aborts, 1, 500));
    }
    CHECK(c.bad == 0);
    stop_consumer(c, th, ok);
}

struct Producer {
    PrinterSpool *s;
    uint32_t n;
    std::atomic<uint32_t> done;
};

static void *producer_main(void *arg)
{
    Producer *p = (Producer *) arg;
    for (uint32_t i = 0; i < p->n; i++) {
        p->s->push(EV(1, (int) i));
        p->done = i + 1;
    }
    return NULL;
}

static void test_full_ring(void)
{
    PrinterSpool s;
    s.mask = 15;
    Producer p;
    p.s = &s; p.n = 100; p.done = 0;
    unsigned sleeps = host_rtos_sleeps();
    pthread_t pt;
    pthread_create(&pt, NULL, producer_main, &p);
    bool waiting = wait_sleeps(sleeps, 2000);
    CHECK(waiting);                     /* it has run into the full ring ... */
    CHECK(p.done == 16);                /* ... with nothing lost or overwritten */
    CHECK(s.pending() == 16);
    Consumer c;
    c.s = &s; c.events = 0; c.markers = 0; c.aborts = 0; c.stop = false; c.bad = 0; c.expect = 0; c.handle = NULL;
    pthread_t ct;
    pthread_create(&ct, NULL, consumer_main, &c);
    bool ok = wait_for(c.events, 100, 5000);
    CHECK(ok);
    if (ok) pthread_join(pt, NULL); else pthread_cancel(pt);
    CHECK(c.bad == 0);
    CHECK(c.events == 100);
    stop_consumer(c, ct, ok);
}

static void *flusher_main(void *arg)
{
    Producer *p = (Producer *) arg;
    for (uint32_t i = 0; i < p->n; i++) {
        p->s->post_flush();
        p->s->post_settings(settings((int) i));
        p->done = i + 1;
    }
    return NULL;
}

/* Callers other than the IEC task never wait on a backlog, however long. */
static void test_posts_never_wait(void)
{
    PrinterSpool s;
    s.mask = 15;
    s.head = 16;                        /* the ring is full, nobody consumes */
    Producer p;
    p.s = &s; p.n = 1000; p.done = 0;
    unsigned sleeps = host_rtos_sleeps();
    pthread_t pt;
    pthread_create(&pt, NULL, flusher_main, &p);
    bool ok = false;
    for (int i = 0; i < 2000 && !ok; i++) {
        ok = (p.done == 1000);
        if (!ok) usleep(1000);
    }
    CHECK(ok);
    CHECK(host_rtos_sleeps() == sleeps);
    if (ok) pthread_join(pt, NULL); else pthread_cancel(pt);
}

/* Nobody serves the flush: the menu comes back after the bound, and says it was not done. */
static void test_flush_bounded(void)
{
    PrinterSpool s;
    s.push(EV(1, 1));
    TickType_t t0 = xTaskGetTickCount();
    bool ok = s.flush_and_wait(150);
    TickType_t used = xTaskGetTickCount() - t0;
    CHECK(!ok);
    CHECK(used >= 140 && used < 2000);
    /* a wake up that was meant for an earlier flush does not end the wait */
    s.flush_done(0);
    t0 = xTaskGetTickCount();
    ok = s.flush_and_wait(150);
    used = xTaskGetTickCount() - t0;
    CHECK(!ok);
    CHECK(used >= 140 && used < 2000);
    /* the flush is posted, at the right place */
    uint16_t ev;
    PrinterSpoolCtl c;
    int markers = 0, flushes = 0;
    for (int i = 0; i < 10; i++) {
        int r = s.next(ev, c);
        if (r == PRINTER_SPOOL_CTRL) { markers++; flushes += c.flushes; }
    }
    CHECK(markers == 1 && flushes == 2);    /* both, at the same position: merged */
}

static void *serve_main(void *arg)
{
    PrinterSpool *s = (PrinterSpool *) arg;
    s->set_consumer(xTaskGetCurrentTaskHandle());
    for (int i = 0; i < 1500; i++) {
        uint16_t ev;
        PrinterSpoolCtl c;
        int r = s->next(ev, c);
        if (r == PRINTER_SPOOL_CTRL && c.flushes) {
            s->flush_done(c.flush_gen);
            s->flush_done(c.flush_gen);         /* and a stray wake up behind it */
        }
        if (r == PRINTER_SPOOL_NONE) usleep(1000);
    }
    s->set_consumer(NULL);
    return NULL;
}

/* The flush is served: the menu gets its own answer, in time, whatever else wakes it. */
static void test_flush_generation(void)
{
    PrinterSpool s;
    pthread_t t;
    pthread_create(&t, NULL, serve_main, &s);
    xSemaphoreGive(s.flush_sem);        /* stale */
    for (int i = 0; i < 20; i++) {
        s.push(EV(1, i));
        CHECK(s.flush_and_wait(2000));
        CHECK(s.done_gen == s.flush_gen);
    }
    pthread_join(t, NULL);
    /* merged flushes: one eject answers both generations */
    PrinterSpool m;
    uint32_t g1 = m.post_flush(), g2 = m.post_flush();
    uint16_t ev;
    PrinterSpoolCtl c;
    CHECK(m.next(ev, c) == PRINTER_SPOOL_CTRL && c.flushes == 2 && c.flush_gen == g2);
    m.flush_done(c.flush_gen);
    CHECK((int32_t)(m.done_gen - g1) >= 0 && (int32_t)(m.done_gen - g2) >= 0);
}

struct Stress {
    PrinterSpool s;
    std::atomic<uint32_t> consumed;
    std::atomic<bool> stop;
    std::atomic<uint32_t> bad;
    uint32_t produced;
};

static void *stress_consumer(void *arg)
{
    Stress *x = (Stress *) arg;
    x->s.set_consumer(xTaskGetCurrentTaskHandle());
    uint32_t expect = 0;
    for (;;) {
        uint16_t ev;
        PrinterSpoolCtl c;
        int r = x->s.next(ev, c);
        if (r == PRINTER_SPOOL_EVENT) {
            if ((ev & 0xFF) != (expect & 0xFF)) x->bad++;
            expect++;
            x->consumed++;
        } else if (r == PRINTER_SPOOL_NONE) {
            if (x->stop && x->consumed == x->produced) { x->s.set_consumer(NULL); return NULL; }
            ulTaskNotifyTake(pdTRUE, 5);
        }
    }
}

static void *stress_producer(void *arg)
{
    Stress *x = (Stress *) arg;
    for (uint32_t i = 0; i < 200000; i++) {
        x->s.push(EV(1, (int) i));
        x->produced = i + 1;
    }
    x->stop = true;
    return NULL;
}

/* The ring moves between the small and the big one, with a producer and a consumer running: nothing lost, in order. */
static void test_migrate_stress(void)
{
    Stress *x = new Stress;
    x->consumed = 0; x->stop = false; x->bad = 0; x->produced = 0;
    pthread_t pt, ct;
    pthread_create(&ct, NULL, stress_consumer, x);
    pthread_create(&pt, NULL, stress_producer, x);
    int moves = 0;
    while (!x->stop) {
        x->s.allocate(8192, 4096);
        x->s.release();                 /* back when what is queued fits the small ring */
        moves++;
        usleep(50);                     /* (a place where the thread can be cancelled) */
    }
    pthread_join(pt, NULL);
    pthread_join(ct, NULL);
    CHECK(x->bad == 0);
    CHECK(x->consumed == 200000);
    CHECK(moves > 0);
    delete x;
}

/* Runs one test in a thread and gives it a bounded time: a test that blocks fails a check, it does not hang. */
struct Bounded {
    void (*fn)(void);
    std::atomic<bool> done;
};

static void *bounded_main(void *arg)
{
    Bounded *b = (Bounded *) arg;
    b->fn();
    b->done = true;
    return NULL;
}

static void run_bounded(const char *name, void (*fn)(void), int seconds)
{
    Bounded *b = new Bounded;       /* leaked if the test hangs: its thread still points at it */
    b->fn = fn;
    b->done = false;
    pthread_t t;
    pthread_create(&t, NULL, bounded_main, b);
    for (int i = 0; i < seconds * 100 && !b->done; i++)
        usleep(10000);
    checks++;
    if (b->done) {
        pthread_join(t, NULL);
        delete b;
    } else {
        failures++;
        printf("FAIL %s: did not finish in %d s (blocked)\n", name, seconds);
        pthread_cancel(t);
        pthread_join(t, NULL);
    }
}
#define RUN(t) do { alarm(120); if (getenv("SPOOL_TRACE")) printf("run %s\n", #t); run_bounded(#t, t, 10); } while (0)

int main(void)
{
    setvbuf(stdout, NULL, _IOLBF, 0);
    signal(SIGALRM, on_alarm);
    printf("%s\n", "start");
    RUN(test_ordering);
    RUN(test_wrap_0);
    RUN(test_wrap_ovf);
    RUN(test_coalesce_and_exhaustion);
    RUN(test_settings_snapshot);
    RUN(test_abort);
    RUN(test_abort_without_flush);
    RUN(test_small_ring);
    RUN(test_abort_release_guard);
    RUN(test_migrating);
    RUN(test_allocate_race);
    RUN(test_alloc_hook);
    RUN(test_turn_on_during_release);
    RUN(test_settings_after_flush_when_full);
    RUN(test_merge_keeps_held_flush);
    RUN(test_marker_race);
    RUN(test_posts_never_wait);
    RUN(test_full_ring);
    RUN(test_flush_bounded);
    RUN(test_flush_generation);
    RUN(test_lost_wake);
    RUN(test_migrate_stress);
    printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
