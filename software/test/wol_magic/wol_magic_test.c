/*
 * Host side tests for the Wake-on-LAN magic packet matcher.
 *
 *   make -C target/pc/linux/wolmagic test
 *
 * The real software/u64ctrl/main/wol_magic.c is compiled here, so what is
 * exercised is the code that ships, not a copy of it. The matcher has no
 * ESP-IDF or lwIP dependency, which is why it is a file of its own.
 */
#include <stdio.h>
#include <string.h>
#include "wol_magic.h"

static int failures;
static int checks;

static void check(int condition, const char *what)
{
    checks++;
    if (!condition) {
        failures++;
        printf("  FAIL  %s\n", what);
    } else {
        printf("  ok    %s\n", what);
    }
}

static const uint8_t mac[WOL_MAC_LEN]   = { 0x24, 0x6F, 0x28, 0x11, 0x22, 0x33 };
static const uint8_t other[WOL_MAC_LEN] = { 0x24, 0x6F, 0x28, 0x11, 0x22, 0x34 };

/* The 102 byte pattern, written at `at` into an otherwise zeroed frame. */
static size_t build(uint8_t *frame, size_t size, size_t at, const uint8_t *target)
{
    memset(frame, 0, size);
    memset(&frame[at], 0xFF, WOL_SYNC_LEN);
    for (int i = 0; i < WOL_REPEATS; i++) {
        memcpy(&frame[at + WOL_SYNC_LEN + (i * WOL_MAC_LEN)], target, WOL_MAC_LEN);
    }
    return at + WOL_PATTERN_LEN;
}

int main(void)
{
    uint8_t frame[512];
    size_t len;

    printf("A magic packet for this station\n");
    // Ethernet + IPv4 + UDP, which is how every wake tool that speaks to port
    // 9 puts it on the wire.
    len = build(frame, sizeof(frame), 42, mac);
    check(wol_is_magic_packet(frame, len, mac), "is recognized behind UDP headers");
    // Ethertype 0x0842, the transport-free form.
    len = build(frame, sizeof(frame), 14, mac);
    check(wol_is_magic_packet(frame, len, mac), "is recognized behind an Ethernet header");
    // Nothing in the standard fixes the offset.
    len = build(frame, sizeof(frame), 0, mac);
    check(wol_is_magic_packet(frame, len, mac), "is recognized at the very start");
    // Tools that carry a SecureOn password append six more bytes.
    len = build(frame, sizeof(frame), 42, mac);
    memset(&frame[len], 0xA5, 6);
    check(wol_is_magic_packet(frame, len + 6, mac), "is recognized with a trailer");

    printf("A frame that is not a wake request for this station\n");
    len = build(frame, sizeof(frame), 42, other);
    check(!wol_is_magic_packet(frame, len, mac), "a packet for another station does not match");
    len = build(frame, sizeof(frame), 42, mac);
    check(!wol_is_magic_packet(frame, len - 1, mac), "a frame one byte short does not match");
    frame[42 + WOL_SYNC_LEN + (8 * WOL_MAC_LEN)] ^= 0xFF;
    check(!wol_is_magic_packet(frame, len, mac), "fifteen of sixteen repetitions do not match");
    memset(frame, 0xFF, sizeof(frame));
    check(!wol_is_magic_packet(frame, sizeof(frame), mac), "an all-ones frame does not match");
    memset(frame, 0, sizeof(frame));
    check(!wol_is_magic_packet(frame, sizeof(frame), mac), "an all-zero frame does not match");

    printf("Extra 0xFF bytes before the pattern\n");
    // A broadcast destination MAC puts six 0xFF at offset 0, so the first run
    // of 0xFF in such a frame is not the one the pattern starts at.
    len = build(frame, sizeof(frame), 42, mac);
    memset(frame, 0xFF, 6);
    check(wol_is_magic_packet(frame, len, mac), "do not hide the pattern");

    printf("A MAC no station owns\n");
    len = build(frame, sizeof(frame), 42, mac);
    check(!wol_is_magic_packet(frame, len, (const uint8_t *)"\xff\xff\xff\xff\xff\xff"),
          "broadcast never matches");
    check(!wol_is_magic_packet(frame, len, (const uint8_t *)"\x00\x00\x00\x00\x00\x00"),
          "all zeroes never matches");

    printf("A pattern beyond what is scanned\n");
    // Sized so that the pattern begins inside the frame but ends past the
    // scan window, which is where a wake tool would have to be malicious
    // rather than merely unusual.
    len = build(frame, sizeof(frame), WOL_SCAN_BYTES - WOL_PATTERN_LEN + 1, mac);
    check(!wol_is_magic_packet(frame, len, mac), "is not searched for");

    printf("Nothing at all\n");
    check(!wol_is_magic_packet(NULL, 0, mac), "a missing frame does not match");
    check(!wol_is_magic_packet(frame, sizeof(frame), NULL), "a missing MAC does not match");

    printf("\n%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
