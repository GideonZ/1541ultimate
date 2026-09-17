// Regression tests for GideonZ/1541ultimate#822: two transmit paths reached
// outside the buffer the network stack gave them.
//
//   problem 1  the AX88772 length header was written at buffer - 4, which is
//              inside the allocation only for this driver's own receive
//              buffers                                    -> Ax88772TxBlock
//   problem 4  a frame shorter than 60 bytes was padded in the length register
//              but not in memory, so the engine read past it and sent what it
//              found -- 18 bytes of the neighbouring heap block on every ARP
//              request                                    -> ShortFrame
//
// Both are about what leaves the device, so the checks here are written the
// same way: fill the memory around the frame with a sentinel, then require
// that no sentinel byte is inside what the function says to transmit.

#include "../../usb/tests/host_test/host_test.h"
#ifdef ETH_TX_FRAME_LEGACY
#include "eth_tx_frame_legacy.h"   // the pre-fix behaviour: the red half
#else
#include "../eth_tx_frame.h"
#endif

#include <vector>

namespace {

const uint8_t kSentinel = 0xA5;           // what "not ours" looks like
const int     kArpFrameLen = 42;          // an ARP request on the wire
const int     kMaxFrameLen = 1514;        // Ethernet MTU 1500 plus the header

// A caller buffer with the frame at the front and neighbouring memory behind
// it, exactly the shape the leak needs.
struct CallerBuffer {
    uint8_t bytes[256];

    explicit CallerBuffer(int frame_len)
    {
        memset(bytes, kSentinel, sizeof(bytes));
        for (int i = 0; i < frame_len; i++) {
            bytes[i] = (uint8_t)(0x10 + i);   // recognisable, never the sentinel
        }
    }
};

bool contains_sentinel(const uint8_t *p, int len)
{
    for (int i = 0; i < len; i++) {
        if (p[i] == kSentinel) {
            return true;
        }
    }
    return false;
}

} // namespace

// ---------------------------------------------------------------- problem 4 --

TEST(ShortFrame, SendsALongFrameFromTheCallersBufferUntouched)
{
    CallerBuffer caller(ETH_MIN_FRAME_LEN);
    uint8_t pad[ETH_MIN_FRAME_LEN];
    memset(pad, 0, sizeof(pad));

    uint8_t *out = NULL;
    int out_len = 0;
    EXPECT_TRUE(eth_tx_frame_to_send(caller.bytes, ETH_MIN_FRAME_LEN, pad, sizeof(pad),
                                     &out, &out_len));
    EXPECT_EQ(out, caller.bytes);           // no copy on the path that carries payload
    EXPECT_EQ(out_len, ETH_MIN_FRAME_LEN);
}

TEST(ShortFrame, PadsAnArpRequestWithZeroAndNotWithTheNeighbour)
{
    CallerBuffer caller(kArpFrameLen);
    uint8_t pad[ETH_MIN_FRAME_LEN];
    memset(pad, kSentinel, sizeof(pad));    // the pad buffer is dirty too

    uint8_t *out = NULL;
    int out_len = 0;
    EXPECT_TRUE(eth_tx_frame_to_send(caller.bytes, kArpFrameLen, pad, sizeof(pad),
                                     &out, &out_len));
    EXPECT_EQ(out_len, ETH_MIN_FRAME_LEN);
    EXPECT_EQ(memcmp(out, caller.bytes, kArpFrameLen), 0);

    // The 18 bytes that used to be the neighbouring heap block.
    for (int i = kArpFrameLen; i < ETH_MIN_FRAME_LEN; i++) {
        EXPECT_EQ((int)out[i], 0);
    }
}

TEST(ShortFrame, NothingBehindTheFrameEverLeavesTheDevice)
{
    // The property, stated once for every short length: what is transmitted
    // may not contain a byte the caller did not put in the frame.
    for (int len = 0; len < ETH_MIN_FRAME_LEN; len++) {
        CallerBuffer caller(len);
        uint8_t pad[ETH_MIN_FRAME_LEN];
        memset(pad, kSentinel, sizeof(pad));

        uint8_t *out = NULL;
        int out_len = 0;
        EXPECT_TRUE(eth_tx_frame_to_send(caller.bytes, len, pad, sizeof(pad),
                                         &out, &out_len));
        EXPECT_EQ(out_len, ETH_MIN_FRAME_LEN);
        EXPECT_FALSE(contains_sentinel(out, out_len));
    }
}

TEST(ShortFrame, KeepsTheBoundaryWhereEthernetPutsIt)
{
    CallerBuffer caller(ETH_MIN_FRAME_LEN);
    uint8_t pad[ETH_MIN_FRAME_LEN];
    uint8_t *out = NULL;
    int out_len = 0;

    // 59 is copied, 60 is not.
    EXPECT_TRUE(eth_tx_frame_to_send(caller.bytes, 59, pad, sizeof(pad), &out, &out_len));
    EXPECT_EQ(out, pad);
    EXPECT_TRUE(eth_tx_frame_to_send(caller.bytes, 60, pad, sizeof(pad), &out, &out_len));
    EXPECT_EQ(out, caller.bytes);
}

TEST(ShortFrame, RefusesWhatItCannotPad)
{
    CallerBuffer caller(kArpFrameLen);
    uint8_t pad[ETH_MIN_FRAME_LEN];
    uint8_t *out = NULL;
    int out_len = 0;

    EXPECT_FALSE(eth_tx_frame_to_send(caller.bytes, -1, pad, sizeof(pad), &out, &out_len));
    EXPECT_FALSE(eth_tx_frame_to_send(NULL, kArpFrameLen, pad, sizeof(pad), &out, &out_len));
    EXPECT_FALSE(eth_tx_frame_to_send(caller.bytes, kArpFrameLen, pad, ETH_MIN_FRAME_LEN - 1,
                                      &out, &out_len));
    EXPECT_FALSE(eth_tx_frame_to_send(caller.bytes, kArpFrameLen, NULL, ETH_MIN_FRAME_LEN,
                                      &out, &out_len));
}

// ---------------------------------------------------------------- problem 1 --

TEST(Ax88772TxBlock, WritesTheHeaderInFrontOfTheFrameInTheDriversOwnBuffer)
{
    CallerBuffer caller(kArpFrameLen);
    uint8_t tx[AX_HEADER_LEN + 1536];

    const int sent = ax88772_tx_block(caller.bytes, kArpFrameLen, tx, sizeof(tx));
    EXPECT_EQ(sent, kArpFrameLen + AX_HEADER_LEN);
    EXPECT_EQ((int)tx[0], kArpFrameLen);
    EXPECT_EQ((int)tx[1], 0);
    EXPECT_EQ((int)tx[2], kArpFrameLen ^ 0xFF);
    EXPECT_EQ((int)tx[3], 0xFF);
    EXPECT_EQ(memcmp(tx + AX_HEADER_LEN, caller.bytes, kArpFrameLen), 0);
}

TEST(Ax88772TxBlock, WritesNothingInFrontOfEitherBuffer)
{
    // The defect itself: the header used to go to buffer - 4, where buffer is
    // the frame the network stack owns. So both are guarded here -- the eight
    // bytes in front of the frame, which is where the header used to land, and
    // the eight in front of the destination.
    uint8_t src_block[8 + kMaxFrameLen];
    memset(src_block, kSentinel, sizeof(src_block));
    uint8_t *frame = src_block + 8;
    memset(frame, 0x5A, kMaxFrameLen);

    uint8_t dst_block[8 + AX_HEADER_LEN + 1536];
    memset(dst_block, kSentinel, sizeof(dst_block));
    uint8_t *tx = dst_block + 8;

    EXPECT_EQ(ax88772_tx_block(frame, kMaxFrameLen, tx, AX_HEADER_LEN + 1536),
              kMaxFrameLen + AX_HEADER_LEN);
    for (int i = 0; i < 8; i++) {
        EXPECT_EQ((int)src_block[i], (int)kSentinel);
        EXPECT_EQ((int)dst_block[i], (int)kSentinel);
    }
}

TEST(Ax88772TxBlock, CarriesBothLengthBytesAndTheirComplements)
{
    uint8_t frame[1500];
    memset(frame, 0x33, sizeof(frame));
    uint8_t tx[AX_HEADER_LEN + 1536];

    EXPECT_EQ(ax88772_tx_block(frame, 1500, tx, sizeof(tx)), 1504);
    EXPECT_EQ((int)tx[0], 1500 & 0xFF);
    EXPECT_EQ((int)tx[1], 1500 >> 8);
    EXPECT_EQ((int)tx[2], (1500 & 0xFF) ^ 0xFF);
    EXPECT_EQ((int)tx[3], (1500 >> 8) ^ 0xFF);
}

TEST(Ax88772TxBlock, TakesTheLargestFrameThatFitsAndRefusesOneMore)
{
    uint8_t frame[1537];
    memset(frame, 0x77, sizeof(frame));
    uint8_t tx[AX_HEADER_LEN + 1536];

    EXPECT_EQ(ax88772_tx_block(frame, 1536, tx, sizeof(tx)), 1540);
    EXPECT_EQ(ax88772_tx_block(frame, 1537, tx, sizeof(tx)), -1);
}

TEST(Ax88772TxBlock, RefusesWhatItCannotAssemble)
{
    uint8_t frame[64];
    memset(frame, 0x11, sizeof(frame));
    uint8_t tx[AX_HEADER_LEN + 64];

    EXPECT_EQ(ax88772_tx_block(frame, -1, tx, sizeof(tx)), -1);
    EXPECT_EQ(ax88772_tx_block(NULL, 42, tx, sizeof(tx)), -1);
    EXPECT_EQ(ax88772_tx_block(frame, 42, NULL, sizeof(tx)), -1);
    EXPECT_EQ(ax88772_tx_block(frame, 0, tx, AX_HEADER_LEN - 1), -1);
}

// --------------------------------------------------- the review's cases --
//
// GideonZ/1541ultimate#843 asked for the two contracts to be pinned at the
// lengths where they break, with a canary on both sides of every buffer rather
// than only in front of it. A leak past the end is what problem 4 actually was,
// so the tail guard is the one that matters most here.

namespace {

// A frame with eight sentinel bytes in front of it and eight behind, so a write
// or a read on either side of the frame is visible. The frame itself is filled
// with a pattern that is never the sentinel.
struct GuardedFrame {
    static const int kGuard = 8;

    std::vector<uint8_t> block;
    int len;

    explicit GuardedFrame(int frame_len)
        : block((size_t)(2 * kGuard + (frame_len > 0 ? frame_len : 1)), kSentinel),
          len(frame_len)
    {
        for (int i = 0; i < frame_len; i++) {
            bytes()[i] = (uint8_t)(0x10 + (i & 0x7F));   // 0x10..0x8F, never 0xA5
        }
    }

    uint8_t *bytes() { return &block[kGuard]; }

    bool guards_intact() const
    {
        for (int i = 0; i < kGuard; i++) {
            if (block[i] != kSentinel) {
                return false;
            }
            if (block[block.size() - 1 - i] != kSentinel) {
                return false;
            }
        }
        return true;
    }

    bool unchanged() const
    {
        for (int i = 0; i < len; i++) {
            if (block[kGuard + i] != (uint8_t)(0x10 + (i & 0x7F))) {
                return false;
            }
        }
        return guards_intact();
    }
};

} // namespace

TEST(Ax88772TxBlock, AssemblesEveryLengthTheReviewNamedWithBothGuardsIntact)
{
    // 1537 is one byte more than the block can carry and has to be refused;
    // every other length is assembled whole.
    const int lengths[] = { 0, 1, 1500, 1536, 1537 };

    for (int i = 0; i < 5; i++) {
        const int len = lengths[i];
        GuardedFrame frame(len);

        std::vector<uint8_t> dst((size_t)(2 * GuardedFrame::kGuard + AX_HEADER_LEN + 1536),
                                 kSentinel);
        uint8_t *tx = &dst[GuardedFrame::kGuard];

        const int sent = ax88772_tx_block(frame.bytes(), len, tx, AX_HEADER_LEN + 1536);

        if (len > 1536) {
            EXPECT_EQ(sent, -1);
        } else {
            EXPECT_EQ(sent, len + AX_HEADER_LEN);
            // The four byte header: length little endian, then each byte
            // complemented.
            EXPECT_EQ((int)tx[0], len & 0xFF);
            EXPECT_EQ((int)tx[1], (len >> 8) & 0xFF);
            EXPECT_EQ((int)tx[2], (len & 0xFF) ^ 0xFF);
            EXPECT_EQ((int)tx[3], ((len >> 8) & 0xFF) ^ 0xFF);
            // The payload, byte for byte, and nothing else.
            EXPECT_EQ(memcmp(tx + AX_HEADER_LEN, frame.bytes(), (size_t)len), 0);
        }

        // Nothing on either side of either buffer moved. The header used to be
        // written at frame - 4, which is the front guard here.
        EXPECT_TRUE(frame.unchanged());
        for (int g = 0; g < GuardedFrame::kGuard; g++) {
            EXPECT_EQ((int)dst[g], (int)kSentinel);
            EXPECT_EQ((int)dst[dst.size() - 1 - g], (int)kSentinel);
        }
    }
}

TEST(ShortFrame, LeavesTheCallersBufferAloneAtEveryLengthTheReviewNamed)
{
    // 42 is an ARP request, 59 the last length that needs padding, 60 the first
    // that does not. None of the three may change the buffer the stack owns.
    const int lengths[] = { 42, 59, 60 };

    for (int i = 0; i < 3; i++) {
        const int len = lengths[i];
        GuardedFrame frame(len);

        std::vector<uint8_t> pad_block((size_t)(2 * GuardedFrame::kGuard + ETH_MIN_FRAME_LEN),
                                       kSentinel);
        uint8_t *pad = &pad_block[GuardedFrame::kGuard];

        uint8_t *out = NULL;
        int out_len = 0;
        EXPECT_TRUE(eth_tx_frame_to_send(frame.bytes(), len, pad, ETH_MIN_FRAME_LEN,
                                         &out, &out_len));
        EXPECT_EQ(out_len, len < ETH_MIN_FRAME_LEN ? ETH_MIN_FRAME_LEN : len);

        // What goes on the wire starts with the frame and continues with zeros,
        // never with whatever was next to it.
        EXPECT_EQ(memcmp(out, frame.bytes(), (size_t)len), 0);
        for (int j = len; j < out_len; j++) {
            EXPECT_EQ((int)out[j], 0);
        }

        // The caller's buffer is untouched, guards included.
        EXPECT_TRUE(frame.unchanged());

        // And the pad buffer is written only as far as the minimum frame.
        for (int g = 0; g < GuardedFrame::kGuard; g++) {
            EXPECT_EQ((int)pad_block[g], (int)kSentinel);
            EXPECT_EQ((int)pad_block[pad_block.size() - 1 - g], (int)kSentinel);
        }
    }
}
