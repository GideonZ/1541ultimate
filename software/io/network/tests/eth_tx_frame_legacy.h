/*
 * eth_tx_frame_legacy.h -- the two transmit paths as they were before
 * GideonZ/1541ultimate#843, so the same suite can be run against them.
 *
 * This is the red half of red/green. It is a model of the defect and not of the
 * device: it reproduces what the old code did to memory, which is what the
 * tests measure, and nothing else. Neither function is built into any firmware.
 *
 * Kept beside the tests rather than in the driver directory for that reason.
 */
#ifndef IO_NETWORK_ETH_TX_FRAME_LEGACY_H
#define IO_NETWORK_ETH_TX_FRAME_LEGACY_H

#include <stdint.h>
#include <string.h>

#define ETH_MIN_FRAME_LEN 60
#define AX_HEADER_LEN     4

/* rmii_interface.cc before the fix:
 *
 *     RMII_TX_LENGTH = (pkt_len < 60) ? 60 : pkt_len;
 *
 * The number was raised to the Ethernet minimum, the data was not. The engine
 * sends exactly as many bytes as the register names, reading them from the
 * caller's buffer, so a 42 byte ARP request put the 18 bytes that happened to
 * follow it on the wire.
 */
static inline bool eth_tx_frame_to_send(uint8_t *frame, int pkt_len,
                                        uint8_t *pad, int pad_size,
                                        uint8_t **out_frame, int *out_len)
{
    (void)pad;
    (void)pad_size;
    if (frame == NULL || pkt_len < 0) {
        return false;
    }
    *out_frame = frame;                                             /* no copy */
    *out_len   = (pkt_len < ETH_MIN_FRAME_LEN) ? ETH_MIN_FRAME_LEN : pkt_len;
    return true;
}

/* usb_ax88772.cc before the fix:
 *
 *     buffer[-4] = ...; buffer[-3] = ...; buffer[-2] = ...; buffer[-1] = ...;
 *     bulk_out(&bulk_out_pipe, buffer - 4, pkt_len + 4);
 *
 * The header went immediately in front of the frame the network stack owns,
 * which has no headroom, and the transfer started there. The driver's own
 * buffer was not involved at all -- which is why the checks on it go red.
 */
static inline int ax88772_tx_block(const uint8_t *frame, int pkt_len,
                                   uint8_t *tx, int tx_size)
{
    (void)tx;
    (void)tx_size;
    if (frame == NULL || pkt_len < 0) {
        return -1;
    }
    uint8_t *header = const_cast<uint8_t *>(frame) - AX_HEADER_LEN;
    header[0] = (uint8_t)(pkt_len & 0xFF);
    header[1] = (uint8_t)((pkt_len >> 8) & 0xFF);
    header[2] = (uint8_t)(header[0] ^ 0xFF);
    header[3] = (uint8_t)(header[1] ^ 0xFF);
    return pkt_len + AX_HEADER_LEN;
}

#endif /* IO_NETWORK_ETH_TX_FRAME_LEGACY_H */
