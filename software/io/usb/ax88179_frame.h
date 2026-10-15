/*
 * ax88179_frame.h -- bulk transfer framing of the ASIX AX88179 / AX88178A.
 *
 * Free functions, so the host tests in software/io/usb/tests/ can call them.
 *
 * Transmit: every frame is preceded by an eight byte header, two little endian
 * words. The first is the frame length. The second is the TSO segment size,
 * which is zero here, with bits 31 and 15 set when the transfer would
 * otherwise end exactly on a USB packet boundary.
 *
 * Receive: one bulk transfer can carry several frames. The last four bytes
 * hold the number of metadata words (low half) and the offset of the first one
 * (high half). The frames precede the metadata, each padded to a multiple of
 * eight bytes. A frame starts with two bytes of alignment padding. A metadata
 * word holds the length in bits 16..28 and the error flags in bits 29 and 31.
 */
#ifndef IO_USB_AX88179_FRAME_H
#define IO_USB_AX88179_FRAME_H

#include <stdint.h>
#include <string.h>

#define AX179_TX_HEADER_LEN   8
#define AX179_RX_PAD_LEN      2
#define AX179_ETH_HEADER_LEN  14
#define AX179_ETH_MIN_FRAME   60
#define AX179_RXHDR_CRC_ERR   0x20000000u
#define AX179_RXHDR_DROP_ERR  0x80000000u

struct Ax179RxFrame {
    int offset; /* of the Ethernet frame in the transfer */
    int length;
};

static inline uint32_t ax179_le32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/* Builds the header and the frame in tx, zero padding a frame shorter than
 * 60 bytes so that nothing behind it is sent. Returns the number of bytes to
 * transfer, or -1 when the frame does not fit.
 */
static inline int ax179_tx_block(const uint8_t *frame, int pkt_len, uint8_t *tx, int tx_size,
                                 int bulk_max_packet)
{
    if (frame == NULL || tx == NULL || pkt_len < 0 || bulk_max_packet <= 0) {
        return -1;
    }
    int send_len = (pkt_len < AX179_ETH_MIN_FRAME) ? AX179_ETH_MIN_FRAME : pkt_len;
    if (tx_size < AX179_TX_HEADER_LEN || send_len > tx_size - AX179_TX_HEADER_LEN) {
        return -1;
    }
    uint32_t hdr2 = 0;
    if (((send_len + AX179_TX_HEADER_LEN) % bulk_max_packet) == 0) {
        hdr2 = 0x80008000u;
    }
    for (int i = 0; i < 4; i++) {
        tx[i]     = (uint8_t)((uint32_t)send_len >> (8 * i));
        tx[4 + i] = (uint8_t)(hdr2 >> (8 * i));
    }
    memcpy(tx + AX179_TX_HEADER_LEN, frame, (size_t)pkt_len);
    memset(tx + AX179_TX_HEADER_LEN + pkt_len, 0, (size_t)(send_len - pkt_len));
    return send_len + AX179_TX_HEADER_LEN;
}

/* Finds the good frames in one received transfer. Frames with an error flag
 * and runts are skipped. Returns how many entries of out were filled.
 */
static inline int ax179_rx_frames(const uint8_t *buf, int len, struct Ax179RxFrame *out, int max_out)
{
    if (buf == NULL || out == NULL || len < 4) {
        return 0;
    }
    len -= 4;
    uint32_t rx_hdr = ax179_le32(buf + len);
    int pkt_cnt = (int)(rx_hdr & 0xFFFF);
    int hdr_off = (int)(rx_hdr >> 16);

    if (pkt_cnt == 0 || hdr_off + pkt_cnt * 4 > len) {
        return 0;
    }

    int found = 0;
    int pos = 0;
    for (int i = 0; i < pkt_cnt; i++) {
        uint32_t meta = ax179_le32(buf + hdr_off + 4 * i);
        int pkt_len = (int)((meta >> 16) & 0x1FFF);
        int padded = (pkt_len + 7) & ~7;

        if (pkt_len == 0) {
            continue; /* alignment dummy */
        }
        if (pos + pkt_len > hdr_off) {
            break;
        }
        if (!(meta & (AX179_RXHDR_CRC_ERR | AX179_RXHDR_DROP_ERR)) &&
            pkt_len >= AX179_RX_PAD_LEN + AX179_ETH_HEADER_LEN && found < max_out) {
            out[found].offset = pos + AX179_RX_PAD_LEN;
            out[found].length = pkt_len - AX179_RX_PAD_LEN;
            found++;
        }
        pos += padded;
    }
    return found;
}

#endif /* IO_USB_AX88179_FRAME_H */
