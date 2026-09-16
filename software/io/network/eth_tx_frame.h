/*
 * eth_tx_frame.h -- what a driver hands the hardware on transmit, checked.
 *
 * Two transmit paths take a buffer from the network stack and have to give the
 * hardware something else: the RMII engine sends exactly as many bytes as its
 * length register names, so a frame shorter than Ethernet's 60 byte minimum has
 * to be padded in memory and not only in the number, and the AX88772 wants a
 * four byte length header immediately in front of the frame, which cannot be
 * written in front of a buffer the stack owns.
 *
 * Both were getting that wrong by reaching outside the caller's buffer. The
 * decisions are free functions over plain integers so they can be pinned down
 * on a build host; see software/io/network/tests/.
 */
#ifndef IO_NETWORK_ETH_TX_FRAME_H
#define IO_NETWORK_ETH_TX_FRAME_H

#include <stdint.h>
#include <string.h>

/* Ethernet's shortest legal frame on the wire, excluding the FCS. */
#define ETH_MIN_FRAME_LEN 60

/* The AX88772 prefixes every frame with a four byte length header. */
#define AX_HEADER_LEN     4

/* Chooses the buffer and length to transmit.
 *
 * A frame of ETH_MIN_FRAME_LEN or more goes out of the caller's buffer
 * untouched, which is every frame carrying real payload. A shorter one is
 * copied into pad and zero filled to the minimum, because padding the length
 * alone makes the engine read past the frame and put whatever follows it in
 * memory on the wire -- an ARP request is 42 bytes, so 18 bytes of the
 * neighbouring heap block left the device on every one of them.
 *
 * Returns false when the frame cannot be transmitted at all, in which case
 * neither output is written.
 */
static inline bool eth_tx_frame_to_send(uint8_t *frame, int pkt_len,
                                        uint8_t *pad, int pad_size,
                                        uint8_t **out_frame, int *out_len)
{
    if (frame == NULL || pkt_len < 0) {
        return false;
    }
    if (pkt_len >= ETH_MIN_FRAME_LEN) {
        *out_frame = frame;
        *out_len   = pkt_len;
        return true;
    }
    if (pad == NULL || pad_size < ETH_MIN_FRAME_LEN) {
        return false;
    }
    memcpy(pad, frame, (size_t)pkt_len);
    memset(pad + pkt_len, 0, (size_t)(ETH_MIN_FRAME_LEN - pkt_len));
    *out_frame = pad;
    *out_len   = ETH_MIN_FRAME_LEN;
    return true;
}

/* Assembles the AX88772 length header and the frame in a buffer the driver
 * owns, and returns how many bytes to send, or -1 when the frame does not fit.
 *
 * The header used to be written at frame - 4. That is only inside an
 * allocation for a buffer from this driver's own receive pool; the buffers the
 * stack supplies on transmit have no headroom, so the write landed in whatever
 * preceded them.
 */
static inline int ax88772_tx_block(const uint8_t *frame, int pkt_len,
                                   uint8_t *tx, int tx_size)
{
    if (frame == NULL || tx == NULL || pkt_len < 0) {
        return -1;
    }
    if (tx_size < AX_HEADER_LEN || pkt_len > (tx_size - AX_HEADER_LEN)) {
        return -1;
    }
    tx[0] = (uint8_t)(pkt_len & 0xFF);
    tx[1] = (uint8_t)((pkt_len >> 8) & 0xFF);
    tx[2] = (uint8_t)(tx[0] ^ 0xFF);
    tx[3] = (uint8_t)(tx[1] ^ 0xFF);
    memcpy(tx + AX_HEADER_LEN, frame, (size_t)pkt_len);
    return pkt_len + AX_HEADER_LEN;
}

#endif /* IO_NETWORK_ETH_TX_FRAME_H */
