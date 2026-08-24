/*
 * wol_magic.h
 *
 * Recognizes a Wake-on-LAN magic packet for a given MAC address.
 *
 * The pattern is the one every wake tool sends: six 0xFF bytes followed by the
 * target MAC repeated sixteen times, somewhere inside the frame. It is not
 * tied to a transport -- the same 102 bytes travel in a UDP datagram (port 9
 * or 7) as in a raw frame of ethertype 0x0842 -- so matching on the pattern
 * rather than on a port accepts all of them.
 *
 * Deliberately free of ESP-IDF and lwIP, so the machine that ships is the one
 * the host side tests in software/test/wol_magic.
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

/* How much of a frame is worth looking at. The pattern is 102 bytes and the
   deepest encapsulation that carries it is Ethernet + IPv4 + UDP, so 42 bytes
   of headers; the rest is room for tools that prepend or append their own
   payload, such as a SecureOn password. */
#define WOL_SCAN_BYTES  256

/* Whether `frame` carries a magic packet for `mac`. `frame` is a raw Ethernet
   frame, of which the first `len` bytes are present; a truncated frame simply
   does not match. False for a broadcast or all-zero `mac`, which no station
   owns and which a run of padding would otherwise match. */
bool wol_is_magic_packet(const uint8_t *frame, size_t len, const uint8_t *mac);

#endif /* SOFTWARE_U64CTRL_MAIN_WOL_MAGIC_H_ */
