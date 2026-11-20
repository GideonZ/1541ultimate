/*
 * isr_tx_test.cpp
 *
 * Replies queued faster than the TX interrupt is serviced (seen on a C64U, which kept 11 replies queued and released one per
 * frame it received). The interrupt is edge-like (it is asserted by a task enabling it with the FIFO below the threshold of
 * 96, or by the FIFO draining across the threshold; the handler's own enable raises nothing; kicks that come before the
 * service merge). The old handler sent ONE frame per service and relied on the interrupt to send the next: frames shorter
 * than the threshold never cross it, so N-1 stayed queued, and each new reply (a new kick) released one: a constant lag.
 * The model is fitted to that evidence (isr_stubs.c); it is not a measurement of the chip.
 */

#include <string.h>
#include "host_test/host_test.h"
#include "isr_test_api.h"

TEST(IsrTx, a_burst_of_replies_is_all_sent)
{
    isr_reset();
    isr_txm(1);
    for (int i = 0; i < 11; i++) {
        ASSERT_TRUE(isr_reply(45));
    }
    isr_service_tx();                       // the one interrupt the 11 kicks made
    EXPECT_TRUE(isr_tx_settle(20) < 20);
    EXPECT_EQ(isr_tx_pending(), 0);         // old handler: 10 left, whatever the UART does
    ASSERT_TRUE(isr_reply(45));             // and one more reply later does not leave a lag behind
    isr_service_tx();
    isr_tx_settle(20);
    EXPECT_EQ(isr_tx_pending(), 0);
}

// A reply goes out when it is queued, with no interrupt serviced (the FIFO has room)
TEST(IsrTx, a_reply_needs_no_interrupt_to_start)
{
    isr_reset();
    isr_txm(1);
    ASSERT_TRUE(isr_reply(45));
    EXPECT_EQ(isr_tx_pending(), 0);
    EXPECT_EQ(isr_tx_fifo_used(), 45 + 2);  // C0, the frame, C0
}

// A frame longer than the FIFO is sent in pieces: the interrupt is asked for the rest, and the drain brings it
TEST(IsrTx, a_frame_longer_than_the_fifo_is_resumed)
{
    isr_reset();
    isr_txm(1);
    ASSERT_TRUE(isr_reply(300));
    EXPECT_EQ(isr_tx_pending(), 1);
    EXPECT_TRUE(isr_tx_settle(20) < 20);
    EXPECT_EQ(isr_tx_pending(), 0);
}

// Frames go out in the order they were queued, each whole: the byte count is the sum, and escapes are doubled
TEST(IsrTx, nothing_is_lost_or_added_on_the_way)
{
    isr_reset();
    isr_txm(1);
    ASSERT_TRUE(isr_reply(50));
    ASSERT_TRUE(isr_reply(70));
    ASSERT_TRUE(isr_reply(30));
    isr_tx_settle(20);
    EXPECT_EQ(isr_tx_pending(), 0);
    EXPECT_EQ(isr_tx_total(), (50 + 2) + (70 + 2) + (30 + 2)); // each frame: C0, its bytes, C0; none twice, none cut
}
