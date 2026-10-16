/*
 * Host test of the reply table of the WiFi command channel: which task gets
 * the reply of the module. Build with -DWIFI_REPLY_LEGACY for the table of
 * before the fix (tag = TX buffer number, every frame registers a waiter),
 * which fails the interleavings below.
 */
#include <stdio.h>
#include "wifi_reply.h"

static wifi_reply_slot_t slots[WIFI_REPLY_SLOTS];
static uint16_t next_sequence;
static int failures;

#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); failures++; } } while (0)

#ifdef WIFI_REPLY_LEGACY
static int tx_buffer;   // the TX buffers are handed out round robin
static int reg(void *task, wifi_reply_tag_t *tag)
{
    tag->thread = tx_buffer;
    tag->sequence = next_sequence++;    // never looked at
    slots[tx_buffer].task = task;
    tx_buffer = (tx_buffer + 1) % WIFI_REPLY_SLOTS;
    return 1;
}
static void *deliver(const wifi_reply_tag_t *tag)
{
    void *task = slots[tag->thread].task;
    slots[tag->thread].task = NULL;
    return task;
}
static void frame(void *task)
{
    wifi_reply_tag_t tag;
    reg(task, &tag);
}
static void abandon(const wifi_reply_tag_t *tag) { (void)tag; }
#else
static int reg(void *task, wifi_reply_tag_t *tag)
{
    return wifi_reply_claim(slots, &next_sequence, task, tag);
}
static void *deliver(const wifi_reply_tag_t *tag)
{
    return wifi_reply_take(slots, tag->thread, tag->sequence);
}
static void frame(void *task)
{
    (void)task;     // frames are not answered and do not register
}
static void abandon(const wifi_reply_tag_t *tag)
{
    wifi_reply_abandon(slots, tag);
}
#endif

static int taskA, taskB, frameTask;

static void reset(void)
{
    for (int i = 0; i < WIFI_REPLY_SLOTS; i++) {
        slots[i].task = NULL;
    }
#ifdef WIFI_REPLY_LEGACY
    tx_buffer = 0;
#endif
}

static void test_plain(void)
{
    reset();
    wifi_reply_tag_t a, b;
    CHECK(reg(&taskA, &a));
    CHECK(reg(&taskB, &b));
    CHECK(deliver(&b) == &taskB);
    CHECK(deliver(&a) == &taskA);
    CHECK(deliver(&a) == NULL);     // a reply is delivered once
}

// Frames go out while a command waits for its reply
static void test_frames_do_not_steal(void)
{
    reset();
    wifi_reply_tag_t a;
    CHECK(reg(&taskA, &a));
    for (int i = 0; i < 3 * WIFI_REPLY_SLOTS; i++) {
        frame(&frameTask);
    }
    CHECK(deliver(&a) == &taskA);
}

// A gave up; B now owns the slot A had; A's late reply must not reach B
static void test_late_reply_after_abandon(void)
{
    reset();
    wifi_reply_tag_t a, b;
    CHECK(reg(&taskA, &a));
    abandon(&a);
    // all slots but one are taken, so B must land on the slot of A
    wifi_reply_tag_t other[WIFI_REPLY_SLOTS];
    for (int i = 0; i < WIFI_REPLY_SLOTS - 1; i++) {
        reg(&frameTask, &other[i]);
    }
    CHECK(reg(&taskB, &b));
    CHECK(deliver(&a) == NULL);
    CHECK(deliver(&b) == &taskB);
}

// A reply that is not for anyone is not delivered
static void test_unknown_reply(void)
{
    reset();
    wifi_reply_tag_t a, bogus;
    CHECK(reg(&taskA, &a));
    bogus.thread = a.thread;
    bogus.sequence = a.sequence + 1;
    CHECK(deliver(&bogus) == NULL);
    bogus.thread = 0xFF;
    CHECK(deliver(&bogus) == NULL);
    CHECK(deliver(&a) == &taskA);
}

#ifndef WIFI_REPLY_LEGACY
static void test_exhaustion_and_wrap(void)
{
    reset();
    wifi_reply_tag_t tag[WIFI_REPLY_SLOTS], extra;
    next_sequence = 0xFFFE;
    for (int i = 0; i < WIFI_REPLY_SLOTS; i++) {
        CHECK(reg(&taskA, &tag[i]));
    }
    CHECK(!reg(&taskB, &extra));
    for (int i = 0; i < WIFI_REPLY_SLOTS; i++) {
        CHECK(deliver(&tag[i]) == &taskA);
    }
    CHECK(reg(&taskB, &extra));
}
#endif

int main(void)
{
    test_plain();
    test_frames_do_not_steal();
    test_late_reply_after_abandon();
    test_unknown_reply();
#ifndef WIFI_REPLY_LEGACY
    test_exhaustion_and_wrap();
#endif
    printf(failures ? "wifi_reply: %d FAILED\n" : "wifi_reply: all passed\n", failures);
    return failures ? 1 : 0;
}
