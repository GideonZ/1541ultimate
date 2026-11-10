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
            if (c.has_settings) {
                CHECK(strcmp(c.settings.filename, "file") != 0);
                out[n++] = 0x2000 | c.settings.type;
            }
            if (c.flushes)
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

    /* more changes than markers fit: none of them waits, the last values arrive, no flush is lost */
    int flushes = 0;
    for (int i = 0; i < 40; i++) {
        s.push(EV(1, i));
        s.post_settings(settings(i + 1));
        if (i % 3 == 0) { s.post_flush(); flushes++; }
    }
    int got_flushes = 0, last_type = -1, events = 0;
    for (;;) {
        uint16_t ev;
        PrinterSpoolCtl c;
        int r = s.next(ev, c);
        if (r == PRINTER_SPOOL_NONE) break;
        if (r == PRINTER_SPOOL_EVENT) events++;
        if (r == PRINTER_SPOOL_CTRL) {
            got_flushes += c.flushes;
            if (c.has_settings) last_type = c.settings.type;
        }
    }
    CHECK(events == 40);
    CHECK(got_flushes == flushes);
    CHECK(last_type == 40);
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

static void test_abort(void)
{
    PrinterSpool s;
    uint16_t out[64];
    for (int i = 0; i < 10; i++) s.push(EV(1, i));
    s.post_settings(settings(5));       /* in the discarded range: still applied */
    s.post_flush();                     /* and still answered */
    for (int i = 10; i < 15; i++) s.push(EV(1, i));
    s.abort(PRINTER_SPOOL_ABORT_RESET);
    s.push(EV(1, 15));                  /* accepted after the abort: kept */
    s.push(EV(1, 16));
    int n = drain(s, out, 64);
    CHECK(n == 5);
    CHECK(out[0] == 0x4001);            /* the abort comes first */
    CHECK(out[1] == 0x2005 && out[2] == 0x3001);
    CHECK(out[3] == 0x110F && out[4] == 0x1110);
    CHECK(s.pending() == 0);
    /* an abort on an empty spool is harmless, and a second one too */
    s.abort(0);
    s.abort(0);
    n = drain(s, out, 64);
    CHECK(n == 1 && out[0] == 0x4000);
}

static void test_small_ring(void)
{
    PrinterSpool s;
    uint16_t out[64];
    malloc_fails = true;
    CHECK(!s.allocate());
    CHECK(!s.allocated());
    for (int i = 0; i < PRINTER_SPOOL_FALLBACK_EVENTS; i++) CHECK(s.push(EV(1, i)));   /* never dropped, never waiting */
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
    for (int i = 0; i < PRINTER_SPOOL_FALLBACK_EVENTS + 10; i++) s.push(EV(1, i));
    s.release();
    CHECK(s.allocated());
    s.tail = s.head;
    s.release();
    CHECK(!s.allocated());
    CHECK(live_blocks == 0);
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
            /* the marker was posted after exactly flushes * 100 events */
            if (c->events != (uint32_t) ctl.flushes * 100) c->bad++;
            c->markers++;
        } else {
            if (c->stop) return NULL;
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
            for (uint32_t k = 0; k < i / 100; k++) s.post_flush();
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

#define RUN(t) do { alarm(30); t; } while (0)

int main(void)
{
    signal(SIGALRM, on_alarm);
    RUN(test_ordering());
    RUN(test_wrap(0));
    RUN(test_wrap(0xFFFFFFF0u));
    RUN(test_coalesce_and_exhaustion());
    RUN(test_settings_snapshot());
    RUN(test_abort());
    RUN(test_small_ring());
    RUN(test_allocate_race());
    RUN(test_alloc_hook());
    RUN(test_marker_race());
    RUN(test_posts_never_wait());
    RUN(test_full_ring());
    RUN(test_lost_wake());
    printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
