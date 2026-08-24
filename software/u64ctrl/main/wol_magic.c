/*
 * wol_magic.c
 *
 * See wol_magic.h.
 */

#include "wol_magic.h"
#include <string.h>

static const uint8_t sync_stream[WOL_SYNC_LEN] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };

static bool usable_mac(const uint8_t *mac)
{
    // A station never owns either of these, and both would turn a stretch of
    // padding into a wake request: the broadcast address is what the sync
    // stream is made of, and a frame zeroed by a driver would match all-zero.
    return (memcmp(mac, sync_stream, WOL_MAC_LEN) != 0) &&
           (memcmp(mac, "\x00\x00\x00\x00\x00\x00", WOL_MAC_LEN) != 0);
}

bool wol_is_magic_packet(const uint8_t *frame, size_t len, const uint8_t *mac)
{
    if (!frame || !mac || (len < WOL_PATTERN_LEN) || !usable_mac(mac)) {
        return false;
    }
    if (len > WOL_SCAN_BYTES) {
        len = WOL_SCAN_BYTES;
    }

    for (size_t at = 0; (at + WOL_PATTERN_LEN) <= len; at++) {
        if (memcmp(&frame[at], sync_stream, WOL_SYNC_LEN) != 0) {
            continue;
        }
        // The sync stream is followed by the MAC, sixteen times over. Every
        // offset is tried, rather than only the first 0xFF found, so a frame
        // that happens to start with more than six 0xFF bytes still matches.
        int repeats = 0;
        while (repeats < WOL_REPEATS) {
            const size_t mac_at = at + WOL_SYNC_LEN + ((size_t)repeats * WOL_MAC_LEN);
            if (memcmp(&frame[mac_at], mac, WOL_MAC_LEN) != 0) {
                break;
            }
            repeats++;
        }
        if (repeats == WOL_REPEATS) {
            return true;
        }
    }
    return false;
}
