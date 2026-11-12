/*
 * isr_ovf_test.cpp
 *
 * An RX FIFO overflow loses bytes at an unknown place in the stream. The frame that is open is damaged and is not
 * delivered, and what follows it is skipped up to the next 0xC0; before, the cut frame went to the dispatcher (cut short,
 * or glued to the next one), or the rest of a frame was taken for a frame and ran as a command nobody sent (R10B on the
 * C64U: frames that began in the middle of an echo payload were answered as commands, and the link was dead after).
 */

#include <string.h>
#include "host_test/host_test.h"
#include "isr_test_api.h"

static void send(const uint8_t *b, int n) { isr_interrupt(b, n, 0); }
static void send_good_frame(int n) { uint8_t f[128]; f[0] = 0xC0; memset(f + 1, 0x08, (size_t)n); f[n + 1] = 0xC0; send(f, n + 2); }
static void overflow_with_nothing_read(void) { uint8_t dummy = 0; isr_overflow(); isr_interrupt(&dummy, 0, 0x100 /* RXFIFO_TOUT */); }

TEST(IsrOvf, an_open_frame_hit_by_an_overflow_is_not_delivered)
{
    isr_reset();
    uint8_t head[21];
    head[0] = 0xC0;
    memset(head + 1, 0x08, 20);
    send(head, 21); // the first 20 bytes of a frame: its buffer is open
    overflow_with_nothing_read();
    uint8_t rest[10];
    memset(rest, 0x33, sizeof(rest)); // after the gap: the middle of the same frame
    send(rest, sizeof(rest));
    static const uint8_t end = 0xC0;
    send(&end, 1); // the damaged frame's end
    send_good_frame(30);
    int size, dropped;
    ASSERT_TRUE(isr_received(&size, &dropped, 0));
    EXPECT_EQ(size, 30); // the first frame the dispatcher gets is the good one
    EXPECT_FALSE(isr_received(&size, &dropped, 0));
}

TEST(IsrOvf, a_rest_of_a_frame_after_a_gap_is_not_taken_for_a_frame)
{
    isr_reset();
    uint8_t f[102];
    f[0] = 0xC0;
    memset(f + 1, 0x08, 100);
    f[101] = 0xC0;
    send(f, 102);
    static const uint8_t start = 0xC0;
    send(&start, 1); // the next frame's C0: a buffer is open and empty
    overflow_with_nothing_read(); // ... and its first bytes are lost
    static const uint8_t payload[8] = { 'M', 'N', 'O', 'P', 'Q', 'R', 'S', 'T' };
    send(payload, sizeof(payload)); // the middle of its payload
    send(&start, 1); // its end
    send_good_frame(30);
    int size, dropped;
    ASSERT_TRUE(isr_received(&size, &dropped, 0));
    EXPECT_EQ(size, 100);
    ASSERT_TRUE(isr_received(&size, &dropped, 0));
    EXPECT_EQ(size, 30); // not 8: no frame "MNOPQRST"
    EXPECT_FALSE(isr_received(&size, &dropped, 0));
}

// ... and with no frame open there is nothing to skip: the C0 that follows opens the next one
TEST(IsrOvf, an_overflow_between_frames_loses_no_good_frame)
{
    isr_reset();
    send_good_frame(100);
    overflow_with_nothing_read();
    static const uint8_t tail[5] = { 'M', 'N', 'O', 'P', 'Q' };
    send(tail, sizeof(tail));
    static const uint8_t end = 0xC0;
    send(&end, 1);
    send_good_frame(30);
    int size, dropped;
    ASSERT_TRUE(isr_received(&size, &dropped, 0));
    EXPECT_EQ(size, 100);
    ASSERT_TRUE(isr_received(&size, &dropped, 0));
    EXPECT_EQ(size, 30);
    EXPECT_FALSE(isr_received(&size, &dropped, 0));
}

// The skip is over at the first C0: a flag that stayed would eat the next good frames
TEST(IsrOvf, the_skip_ends_at_the_first_end_byte)
{
    isr_reset();
    uint8_t head[11];
    head[0] = 0xC0;
    memset(head + 1, 0x08, 10);
    send(head, 11);
    overflow_with_nothing_read();
    static const uint8_t end = 0xC0;
    send(&end, 1);
    send_good_frame(30);
    send_good_frame(40);
    int size, dropped;
    ASSERT_TRUE(isr_received(&size, &dropped, 0));
    EXPECT_EQ(size, 30);
    ASSERT_TRUE(isr_received(&size, &dropped, 0));
    EXPECT_EQ(size, 40);
    EXPECT_FALSE(isr_received(&size, &dropped, 0));
}

// The bytes that wait in the FIFO at an overflow are not read: the FIFO is not known to hold an unbroken run
TEST(IsrOvf, the_fifo_is_not_read_after_an_overflow)
{
    isr_reset();
    uint8_t head[21];
    head[0] = 0xC0;
    memset(head + 1, 0x08, 20);
    send(head, 21);
    uint8_t held[128];
    memset(held, 0x22, sizeof(held));
    held[100] = 0xC0; // read, this would deliver the damaged frame
    isr_overflow();
    isr_interrupt(held, sizeof(held), 0);
    send_good_frame(30);
    int size, dropped;
    ASSERT_TRUE(isr_received(&size, &dropped, 0));
    EXPECT_EQ(size, 30);
    EXPECT_FALSE(isr_received(&size, &dropped, 0));
}
