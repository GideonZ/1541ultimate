/*
 * Host test of the printer spool: event and marker order, wrap around (also of the 32 bit
 * counters), a wake up that is never lost, a full ring, a full marker queue.
 */
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <atomic>
#define private public
#include "printer_spool.h"
#undef private

static int failures = 0;
static int checks = 0;
#define CHECK(c) do { checks++; if (!(c)) { failures++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

static uint16_t EV(int type, int v) { return (uint16_t)((type << 8) | (v & 0xFF)); }

/* Drains everything available into 'out': events as 0x1000|ev, markers as 0x2000|value. */
static int drain(PrinterSpool &s, uint16_t *out, int max)
{
    int n = 0;
    uint16_t ev;
    uint8_t cv;
    for (;;) {
        int r = s.next(ev, cv);
        if (r == PRINTER_SPOOL_NONE || n == max)
            return n;
        out[n++] = (r == PRINTER_SPOOL_EVENT) ? (0x1000 | ev) : (0x2000 | cv);
    }
}

static void test_ordering(void)
{
    PrinterSpool s;
    CHECK(s.allocate(16, 16));
    uint16_t out[64];
    s.post_ctrl(7);                     /* before any data: first */
    for (int i = 0; i < 5; i++) CHECK(s.push(EV(1, i)));
    s.post_ctrl(1);
    s.post_ctrl(2);                     /* same position: posted order */
    for (int i = 5; i < 8; i++) CHECK(s.push(EV(0, i)));
    s.post_ctrl(3);
    int n = drain(s, out, 64);
    uint16_t want[] = { 0x2007, 0x1100, 0x1101, 0x1102, 0x1103, 0x1104, 0x2001, 0x2002,
                        0x1005, 0x1006, 0x1007, 0x2003 };
    CHECK(n == 12);
    CHECK(memcmp(out, want, sizeof(want)) == 0);
    CHECK(s.pending() == 0);
}

static void test_wrap(uint32_t start)
{
    PrinterSpool s;
    CHECK(s.allocate(16, 16));
    s.head = s.tail = start;            /* also across the 32 bit counter wrap */
    uint16_t out[64];
    int seq = 0, got = 0;
    for (int round = 0; round < 200; round++) {
        int k = 1 + (round * 7) % 13;   /* 1..13 events, never more than the 16 slots */
        for (int i = 0; i < k; i++) CHECK(s.push(EV(1, seq++)));
        if (round % 3 == 0) s.post_ctrl(round & 7);
        int n = drain(s, out, 64);
        for (int i = 0; i < n; i++) {
            if (out[i] & 0x2000) continue;
            CHECK((out[i] & 0xFF) == (uint16_t)(got & 0xFF) && ((out[i] >> 8) & 3) == 1);
            got++;
        }
    }
    CHECK(got == seq);
    /* marker placed exactly at the slot that wraps */
    s.head = s.tail = start + 15;
    s.push(EV(1, 1)); s.post_ctrl(5); s.push(EV(1, 2)); s.push(EV(1, 3));
    int n = drain(s, out, 64);
    CHECK(n == 4 && out[0] == 0x1101 && out[1] == 0x2005 && out[2] == 0x1102 && out[3] == 0x1103);
}

static void test_coalesce(void)
{
    PrinterSpool s;
    CHECK(s.allocate(16, 16));
    uint16_t out[16];
    s.post_ctrl(9, true);
    s.post_ctrl(9, true);               /* same position, same value: dropped */
    s.push(EV(1, 1));
    s.post_ctrl(9, true);               /* data in between: kept */
    s.post_ctrl(4, true);               /* other value: kept */
    int n = drain(s, out, 16);
    CHECK(n == 4 && out[0] == 0x2009 && out[1] == 0x1101 && out[2] == 0x2009 && out[3] == 0x2004);
}

static void test_no_ring(void)
{
    PrinterSpool s;
    CHECK(!s.allocated());
    CHECK(!s.push(EV(1, 1)));
    CHECK(s.allocate(1000, 64));        /* largest power of two up to 1000 */
    CHECK(s.mask == 511);
    s.release();
    CHECK(!s.allocated());
}

/* ---- threads ---- */
struct Consumer {
    PrinterSpool *s;
    std::atomic<uint32_t> events, markers;
    std::atomic<bool> stop;
    std::atomic<uint32_t> bad;
    uint32_t expect;
};

static void *consumer_main(void *arg)
{
    Consumer *c = (Consumer *) arg;
    c->s->set_consumer(xTaskGetCurrentTaskHandle());
    for (;;) {
        uint16_t ev;
        uint8_t cv;
        int r = c->s->next(ev, cv);
        if (r == PRINTER_SPOOL_EVENT) {
            if ((ev & 0xFF) != (c->expect & 0xFF)) c->bad++;
            c->expect++;
            c->events++;
        } else if (r == PRINTER_SPOOL_CTRL) {
            /* the marker was posted after exactly cv events */
            if (c->events != (uint32_t) cv * 100) c->bad++;
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

/* One event, wait until it has been consumed, many times: a lost wake up hangs this. */
static void test_lost_wake(void)
{
    PrinterSpool s;
    CHECK(s.allocate(64, 16));
    Consumer c;
    c.s = &s; c.events = 0; c.markers = 0; c.stop = false; c.bad = 0; c.expect = 0;
    pthread_t th;
    pthread_create(&th, NULL, consumer_main, &c);
    bool ok = true;
    for (uint32_t i = 1; i <= 3000 && ok; i++) {
        s.push(EV(1, (int)(i - 1)));
        ok = wait_for(c.events, i, 1000);
        if (i % 100 == 0) {             /* a marker after every 100 events */
            s.post_ctrl((uint8_t)(i / 100));
            ok = ok && wait_for(c.markers, i / 100, 1000);
        }
    }
    CHECK(ok);
    CHECK(c.bad == 0);
    c.stop = true;
    if (ok) {
        if (s.consumer) xTaskNotifyGive(s.consumer);   /* wake it so it sees 'stop' */
        pthread_join(th, NULL);
    } else {
        pthread_cancel(th);             /* hung: report, do not hang the test */
        pthread_join(th, NULL);
    }
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
    CHECK(s.allocate(16, 16));
    Producer p;
    p.s = &s; p.n = 100; p.done = 0;
    pthread_t pt;
    pthread_create(&pt, NULL, producer_main, &p);
    usleep(100 * 1000);
    CHECK(p.done == 16);                /* blocked on the full ring, nothing lost or overwritten */
    CHECK(s.pending() == 16);
    Consumer c;
    c.s = &s; c.events = 0; c.markers = 0; c.stop = false; c.bad = 0; c.expect = 0;
    pthread_t ct;
    pthread_create(&ct, NULL, consumer_main, &c);
    CHECK(wait_for(c.events, 100, 5000));
    pthread_join(pt, NULL);
    CHECK(c.bad == 0);
    CHECK(c.events == 100);
    c.stop = true;
    xTaskNotifyGive(s.consumer);
    pthread_join(ct, NULL);
}

static void *post_main(void *arg)
{
    Producer *p = (Producer *) arg;
    for (uint32_t i = 0; i < p->n; i++) {
        p->s->post_ctrl(i & 7);
        p->done = i + 1;
    }
    return NULL;
}

static void test_full_ctrl(void)
{
    PrinterSpool s;
    CHECK(s.allocate(16, 16));
    Producer p;
    p.s = &s; p.n = 10; p.done = 0;
    pthread_t pt;
    pthread_create(&pt, NULL, post_main, &p);
    usleep(100 * 1000);
    CHECK(p.done == PRINTER_SPOOL_CTRL_DEPTH);
    uint16_t ev; uint8_t cv;
    int got = 0;
    for (int i = 0; i < 2000 && got < 10; i++) {
        if (s.next(ev, cv) == PRINTER_SPOOL_CTRL) {
            CHECK(cv == (got & 7));
            got++;
        } else {
            usleep(1000);
        }
    }
    CHECK(got == 10);
    pthread_join(pt, NULL);
}

int main(void)
{
    test_ordering();
    test_wrap(0);
    test_wrap(0xFFFFFFF0u);
    test_coalesce();
    test_no_ring();
    test_full_ring();
    test_full_ctrl();
    test_lost_wake();
    printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
