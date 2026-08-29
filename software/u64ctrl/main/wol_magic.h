/*
 * wol_magic.h
 *
 * Recognizes a Wake-on-LAN magic packet: six 0xFF bytes followed by the target
 * MAC sixteen times, anywhere in the frame. Matching the pattern rather than a
 * port accepts every encapsulation that carries it, UDP port 9 or 7 and raw
 * ethertype 0x0842 alike.
 *
 * Free of ESP-IDF and lwIP, so software/test/wol_magic tests what ships.
 */

#ifndef SOFTWARE_U64CTRL_MAIN_WOL_MAGIC_H_
#define SOFTWARE_U64CTRL_MAIN_WOL_MAGIC_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define WOL_MAC_LEN     6
#define WOL_SYNC_LEN    6   // the 0xFF stream that introduces the pattern
#define WOL_REPEATS     16  // repetitions of the MAC that follow it
#define WOL_PATTERN_LEN (WOL_SYNC_LEN + (WOL_REPEATS * WOL_MAC_LEN))  // 102

/* How much of a frame is worth looking at: 102 bytes of pattern, 42 of
   Ethernet + IPv4 + UDP headers, and room for a tool's own payload. */
#define WOL_SCAN_BYTES  256

/* Whether the first `len` bytes of raw Ethernet frame `frame` carry a magic
   packet for `mac`. False for a broadcast or all-zero `mac`, which no station
   owns and which a run of padding would match. */
bool wol_is_magic_packet(const uint8_t *frame, size_t len, const uint8_t *mac);

#endif /* SOFTWARE_U64CTRL_MAIN_WOL_MAGIC_H_ */
