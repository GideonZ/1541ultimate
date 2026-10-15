// Bulk framing of the ASIX AX88179 / AX88178A, as the Linux driver
// ax88179_178a.c describes it.

#include "host_test/host_test.h"
#include "../ax88179_frame.h"

#include <vector>

namespace {

const uint8_t kSentinel = 0xA5;

// One received transfer in the layout of the current adapter firmware: each
// frame behind two bytes of alignment padding and padded to eight bytes, then a
// metadata word and a dummy word per frame, four bytes of padding and the
// count/offset word.
struct RxFrameSpec {
    int length;          // Ethernet frame length, without the alignment bytes
    uint32_t flags;      // AX179_RXHDR_* bits
};

void put_le32(std::vector<uint8_t> &v, uint32_t x)
{
    for (int i = 0; i < 4; i++) {
        v.push_back((uint8_t)(x >> (8 * i)));
    }
}

std::vector<uint8_t> build_transfer(const std::vector<RxFrameSpec> &frames, uint8_t first_fill = 0x10)
{
    std::vector<uint8_t> t;
    std::vector<uint32_t> meta;
    uint8_t fill = first_fill;
    for (size_t i = 0; i < frames.size(); i++) {
        int pkt_len = frames[i].length + AX179_RX_PAD_LEN;
        t.push_back(0xEE); // alignment bytes are not part of the frame
        t.push_back(0xEE);
        for (int j = 0; j < frames[i].length; j++) {
            t.push_back(fill);
        }
        fill++;
        while (t.size() % 8) {
            t.push_back(0);
        }
        meta.push_back(((uint32_t)pkt_len << 16) | frames[i].flags);
        meta.push_back(AX179_RXHDR_DROP_ERR); // the dummy word
    }
    uint16_t hdr_off = (uint16_t)t.size();
    for (size_t i = 0; i < meta.size(); i++) {
        put_le32(t, meta[i]);
    }
    put_le32(t, 0); // padding
    put_le32(t, ((uint32_t)hdr_off << 16) | (uint32_t)meta.size());
    return t;
}

} // namespace

TEST(Ax179Rx, ReturnsOneFrameWithoutTheAlignmentBytes)
{
    std::vector<uint8_t> t = build_transfer({ { 60, 0 } });
    Ax179RxFrame out[4];
    ASSERT_EQ(ax179_rx_frames(t.data(), (int)t.size(), out, 4), 1);
    EXPECT_EQ(out[0].offset, 2);
    EXPECT_EQ(out[0].length, 60);
    EXPECT_EQ((int)t[out[0].offset], 0x10);
}

TEST(Ax179Rx, SplitsAggregatedFramesAtTheirPaddedOffsets)
{
    // 61 byte frames pad to 64 with their alignment bytes; 100 to 104.
    std::vector<uint8_t> t = build_transfer({ { 61, 0 }, { 100, 0 }, { 1514, 0 } });
    Ax179RxFrame out[4];
    ASSERT_EQ(ax179_rx_frames(t.data(), (int)t.size(), out, 4), 3);
    EXPECT_EQ(out[0].offset, 2);
    EXPECT_EQ(out[0].length, 61);
    EXPECT_EQ(out[1].offset, 64 + 2);
    EXPECT_EQ(out[1].length, 100);
    EXPECT_EQ(out[2].offset, 64 + 104 + 2);
    EXPECT_EQ(out[2].length, 1514);
    EXPECT_EQ((int)t[out[0].offset], 0x10);
    EXPECT_EQ((int)t[out[1].offset], 0x11);
    EXPECT_EQ((int)t[out[2].offset], 0x12);
}

TEST(Ax179Rx, SkipsFramesWithErrorFlagsButKeepsTheOnesAfterThem)
{
    std::vector<uint8_t> t = build_transfer({ { 80, AX179_RXHDR_CRC_ERR }, { 90, 0 }, { 70, AX179_RXHDR_DROP_ERR } });
    Ax179RxFrame out[4];
    ASSERT_EQ(ax179_rx_frames(t.data(), (int)t.size(), out, 4), 1);
    EXPECT_EQ((int)t[out[0].offset], 0x11);
    EXPECT_EQ(out[0].length, 90);
}

TEST(Ax179Rx, SkipsRunts)
{
    std::vector<uint8_t> t = build_transfer({ { AX179_ETH_HEADER_LEN - 1, 0 }, { AX179_ETH_HEADER_LEN, 0 } });
    Ax179RxFrame out[4];
    ASSERT_EQ(ax179_rx_frames(t.data(), (int)t.size(), out, 4), 1);
    EXPECT_EQ(out[0].length, AX179_ETH_HEADER_LEN);
}

TEST(Ax179Rx, StopsAtTheSizeOfTheOutputArray)
{
    std::vector<uint8_t> t = build_transfer({ { 60, 0 }, { 60, 0 }, { 60, 0 } });
    Ax179RxFrame out[2];
    EXPECT_EQ(ax179_rx_frames(t.data(), (int)t.size(), out, 2), 2);
}

TEST(Ax179Rx, RejectsATransferWhoseMetadataLiesOutsideIt)
{
    std::vector<uint8_t> t = build_transfer({ { 60, 0 } });
    Ax179RxFrame out[4];

    // Count too large for the bytes that remain.
    std::vector<uint8_t> big_count = t;
    big_count[big_count.size() - 4] = 0x40;
    EXPECT_EQ(ax179_rx_frames(big_count.data(), (int)big_count.size(), out, 4), 0);

    // Offset beyond the end.
    std::vector<uint8_t> big_off = t;
    big_off[big_off.size() - 2] = 0xFF;
    big_off[big_off.size() - 1] = 0x7F;
    EXPECT_EQ(ax179_rx_frames(big_off.data(), (int)big_off.size(), out, 4), 0);

    // No frames announced, and shorter than the count word.
    std::vector<uint8_t> none = t;
    none[none.size() - 4] = 0;
    EXPECT_EQ(ax179_rx_frames(none.data(), (int)none.size(), out, 4), 0);
    EXPECT_EQ(ax179_rx_frames(t.data(), 3, out, 4), 0);
    EXPECT_EQ(ax179_rx_frames(NULL, 100, out, 4), 0);
}

TEST(Ax179Rx, RejectsAFrameThatRunsIntoTheMetadata)
{
    std::vector<uint8_t> t = build_transfer({ { 60, 0 } });
    // Claim 200 bytes for a frame that has 64 bytes in front of the metadata.
    uint32_t meta = (200u + AX179_RX_PAD_LEN) << 16;
    size_t hdr_off = t.size() - 4 - 4 - 8; // metadata word, dummy, padding
    for (int i = 0; i < 4; i++) {
        t[hdr_off + i] = (uint8_t)(meta >> (8 * i));
    }
    Ax179RxFrame out[4];
    EXPECT_EQ(ax179_rx_frames(t.data(), (int)t.size(), out, 4), 0);
}

TEST(Ax179Tx, PutsTheLengthAndAZeroSegmentSizeInFrontOfTheFrame)
{
    uint8_t frame[100];
    for (int i = 0; i < 100; i++) frame[i] = (uint8_t)i;
    uint8_t tx[AX179_TX_HEADER_LEN + 1536];
    ASSERT_EQ(ax179_tx_block(frame, 100, tx, sizeof(tx), 512), 108);
    EXPECT_EQ((int)tx[0], 100);
    EXPECT_EQ((int)tx[1], 0);
    EXPECT_EQ((int)tx[2], 0);
    EXPECT_EQ((int)tx[3], 0);
    for (int i = 4; i < 8; i++) EXPECT_EQ((int)tx[i], 0);
    EXPECT_EQ(memcmp(tx + 8, frame, 100), 0);
}

TEST(Ax179Tx, EncodesALengthAbove255InTwoBytes)
{
    uint8_t frame[1514];
    memset(frame, 0x55, sizeof(frame));
    uint8_t tx[AX179_TX_HEADER_LEN + 1536];
    ASSERT_EQ(ax179_tx_block(frame, 1514, tx, sizeof(tx), 512), 1522);
    EXPECT_EQ((int)tx[0], 1514 & 0xFF);
    EXPECT_EQ((int)tx[1], 1514 >> 8);
}

TEST(Ax179Tx, PadsAShortFrameWithZeroNotWithWhatFollowsIt)
{
    uint8_t buffer[256];
    memset(buffer, kSentinel, sizeof(buffer));
    for (int i = 0; i < 42; i++) buffer[i] = (uint8_t)(0x10 + i); // an ARP request
    uint8_t tx[AX179_TX_HEADER_LEN + 1536];
    memset(tx, kSentinel, sizeof(tx));
    ASSERT_EQ(ax179_tx_block(buffer, 42, tx, sizeof(tx), 512), AX179_ETH_MIN_FRAME + 8);
    EXPECT_EQ((int)tx[0], AX179_ETH_MIN_FRAME);
    EXPECT_EQ(memcmp(tx + 8, buffer, 42), 0);
    for (int i = 8 + 42; i < 8 + AX179_ETH_MIN_FRAME; i++) {
        EXPECT_EQ((int)tx[i], 0);
    }
}

TEST(Ax179Tx, AsksForPaddingWhenTheTransferEndsOnAPacketBoundary)
{
    // 504 + 8 is exactly one 512 byte packet.
    uint8_t frame[504];
    memset(frame, 1, sizeof(frame));
    uint8_t tx[AX179_TX_HEADER_LEN + 1536];
    ASSERT_EQ(ax179_tx_block(frame, 504, tx, sizeof(tx), 512), 512);
    EXPECT_EQ((int)tx[4], 0x00);
    EXPECT_EQ((int)tx[5], 0x80);
    EXPECT_EQ((int)tx[6], 0x00);
    EXPECT_EQ((int)tx[7], 0x80);

    // One byte more does not.
    ASSERT_EQ(ax179_tx_block(frame, 503, tx, sizeof(tx), 512), 511);
    EXPECT_EQ((int)tx[5], 0);
    EXPECT_EQ((int)tx[7], 0);
}

TEST(Ax179Tx, RefusesWhatDoesNotFit)
{
    uint8_t frame[1600];
    memset(frame, 1, sizeof(frame));
    uint8_t tx[AX179_TX_HEADER_LEN + 100];
    EXPECT_EQ(ax179_tx_block(frame, 101, tx, sizeof(tx), 512), -1);
    EXPECT_EQ(ax179_tx_block(frame, 100, tx, sizeof(tx), 512), 108);
}
