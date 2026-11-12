/*
 * isr_test.cpp
 *
 * The modules' UART interrupt handler, as cut out of each module's my_uart.c, against the UART and buffer queues of
 * isr_stubs.c. Cases that hold on every tree are here; the cases of a fix are in isr_<fix>_test.cpp.
 */

#include <string.h>
#include "host_test/host_test.h"
#include "isr_test_api.h"

static void send(const uint8_t *b, int n) { isr_interrupt(b, n, 0); }

TEST(Isr, a_frame_is_delivered_whole)
{
    isr_reset();
    uint8_t f[12];
    f[0] = 0xC0;
    memset(f + 1, 0x08, 10);
    f[11] = 0xC0;
    send(f, sizeof(f));
    int size, dropped;
    ASSERT_TRUE(isr_received(&size, &dropped, 0));
    EXPECT_EQ(size, 10);
    EXPECT_EQ(dropped, 0);
    EXPECT_FALSE(isr_received(&size, &dropped, 0));
}

TEST(Isr, escaped_end_and_escape_bytes_are_decoded)
{
    isr_reset();
    const uint8_t f[] = { 0xC0, 0x01, 0xDB, 0xDC, 0x02, 0xDB, 0xDD, 0x03, 0xC0 }; // 01 C0 02 DB 03
    send(f, sizeof(f));
    int size, dropped;
    ASSERT_TRUE(isr_received(&size, &dropped, 0));
    EXPECT_EQ(size, 5);
}

TEST(Isr, a_frame_that_arrives_in_pieces_is_delivered_once)
{
    isr_reset();
    uint8_t f[22];
    f[0] = 0xC0;
    memset(f + 1, 0x08, 20);
    f[21] = 0xC0;
    send(f, 8);
    send(f + 8, 8);
    send(f + 16, 6);
    int size, dropped;
    ASSERT_TRUE(isr_received(&size, &dropped, 0));
    EXPECT_EQ(size, 20);
    EXPECT_FALSE(isr_received(&size, &dropped, 0));
}

TEST(Isr, a_queued_frame_goes_out_at_the_tx_interrupt)
{
    isr_reset();
    ASSERT_TRUE(isr_event());
    isr_tx_interrupt();
    EXPECT_EQ(isr_tx_pending(), 0);
}
