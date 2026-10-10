// Host test of wifi_random.cc, the check on the module's answer to GET_RANDOM:
// answers a broken or confused module could send, none of which may hand out a byte.
//   make -C target/pc/linux/wifirandom test
#include <stdio.h>
#include <stddef.h>
#include <string.h>
#include <limits.h>
#include "rpc_calls.h"
#include "wifi_random.h"

// The layout both sides agree on is asserted in wifi_random.cc itself.

#define SEQUENCE 0x1234
#define HEADER   12

// The length every case below asks for; main runs them for several.
static uint16_t LEN;

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

// Word aligned, as the data of a command_buf_t is.
static union {
    uint32_t align;
    uint8_t bytes[HEADER + 512 + 256];
} reply;

// A correct answer to request SEQUENCE for LEN bytes; returns its size.
static int build(void)
{
    memset(reply.bytes, 0, sizeof(reply.bytes));
    rpc_get_random_resp *r = (rpc_get_random_resp *)reply.bytes;
    r->hdr.command = CMD_GET_RANDOM;
    r->hdr.thread = 12;
    r->hdr.sequence = SEQUENCE;
    r->esp_err = 0;
    r->source = ENTROPY_RADIO;
    r->length = LEN;
    for (int i = 0; i < LEN; i++) {
        r->data[i] = (uint8_t)(0xA0 + (i * 7) + (i >> 8)); // no period of 256
    }
    return HEADER + LEN;
}

static rpc_get_random_resp *answer(void)
{
    return (rpc_get_random_resp *)reply.bytes;
}

// Runs the check on the first size bytes of the reply; true when it returned
// expected and, unless that is success, left buf alone.
static bool outcome(int size, uint16_t len, int expected)
{
    uint8_t buf[600];
    memset(buf, 0xEE, sizeof(buf));
    int result = wifi_random_take_reply(reply.bytes, size, SEQUENCE, buf, len);
    if (result != expected) {
        printf("        returned %d, expected %d\n", result, expected);
        return false;
    }
    if (expected == 0) {
        return (memcmp(buf, answer()->data, len) == 0) && (buf[len] == 0xEE);
    }
    for (size_t i = 0; i < sizeof(buf); i++) {
        if (buf[i] != 0xEE) {
            printf("        byte %d was written\n", (int)i);
            return false;
        }
    }
    return true;
}

static void reply_cases(void)
{
    int size;

    printf("The answer to this request\n");
    size = build();
    check(outcome(size, LEN, 0), "hands out exactly len bytes");
    size = build();
    check(outcome(size + 100, LEN, 0), "ignores bytes the module sent beyond len");

    printf("An answer to something else\n");
    size = build();
    answer()->hdr.sequence = SEQUENCE - 1;
    check(outcome(size, LEN, WIFI_RANDOM_FOREIGN), "wrong sequence is not ours");
    size = build();
    answer()->hdr.sequence = SEQUENCE ^ 0x0100;
    check(outcome(size, LEN, WIFI_RANDOM_FOREIGN), "a sequence that differs only in its high byte is not ours");
    size = build();
    answer()->hdr.command = CMD_WIFI_GETMAC;
    check(outcome(size, LEN, WIFI_RANDOM_FOREIGN), "wrong command is not ours");
    size = build();
    check(outcome(3, LEN, WIFI_RANDOM_FOREIGN), "fewer bytes than a header is not ours");
    bool only_ours = true;
    for (int command = 0; command < 256; command++) {
        size = build();
        answer()->hdr.command = (uint8_t)command;
        uint8_t buf[512];
        int expected = (command == CMD_GET_RANDOM) ? 0 : WIFI_RANDOM_FOREIGN;
        if (wifi_random_take_reply(reply.bytes, size, SEQUENCE, buf, LEN) != expected) {
            printf("        command 0x%02x\n", command);
            only_ours = false;
        }
    }
    check(only_ours, "of all 256 commands only CMD_GET_RANDOM is ours");

    printf("A refusal\n");
    size = build();
    answer()->esp_err = 0x103; // ESP_ERR_INVALID_STATE
    answer()->length = 0;
    check(outcome(HEADER, LEN, WIFI_RANDOM_REFUSED), "esp_err INVALID_STATE with no bytes");
    size = build();
    answer()->esp_err = 0x103;
    check(outcome(size, LEN, WIFI_RANDOM_REFUSED), "esp_err set, even with len bytes behind it");
    const int errors[] = { 0x3000, 0x10000, -1, INT_MIN }; // ESP_ERR_WIFI_BASE, beyond 16 bits, ESP_FAIL
    for (int i = 0; i < 4; i++) {
        char what[64];
        size = build();
        answer()->esp_err = errors[i];
        snprintf(what, sizeof(what), "esp_err %d, with len bytes behind it", errors[i]);
        check(outcome(size, LEN, WIFI_RANDOM_REFUSED), what);
    }
    size = build();
    answer()->esp_err = 0x106; // ESP_ERR_NOT_SUPPORTED, from firmware without the call
    check(outcome(8, LEN, WIFI_RANDOM_REFUSED), "the 8 byte answer of old module firmware");
    size = build();
    answer()->source = ENTROPY_NONE;
    check(outcome(size, LEN, WIFI_RANDOM_REFUSED), "OK without an entropy source");

    printf("A malformed answer\n");
    size = build();
    answer()->esp_err = 0x103; // only partly received, so it must not be read
    check(outcome(7, LEN, WIFI_RANDOM_INVALID), "too short to hold esp_err, whatever lies beyond it");
    size = build();
    check(outcome(HEADER - 1, LEN, WIFI_RANDOM_INVALID), "OK but too short to hold the header");
    size = build();
    answer()->source = ENTROPY_NONE; // a byte past the frame must not be read
    check(outcome(HEADER - 2, LEN, WIFI_RANDOM_INVALID), "too short to hold the header, whatever lies beyond it");
    size = build();
    answer()->length = LEN - 1;
    check(outcome(size, LEN, WIFI_RANDOM_INVALID), "fewer bytes announced than asked");
    size = build();
    answer()->length = LEN + 1;
    check(outcome(size + 1, LEN, WIFI_RANDOM_INVALID), "more bytes announced than asked");
    size = build();
    answer()->length = LEN ^ 0x0100;
    check(outcome(size + 256, LEN, WIFI_RANDOM_INVALID), "a length that differs only in its high byte");
    size = build();
    check(outcome(size - 1, LEN, WIFI_RANDOM_INVALID), "fewer bytes received than announced");
    size = build();
    answer()->length = 0;
    check(outcome(HEADER, LEN, WIFI_RANDOM_INVALID), "OK with no bytes at all");

}

int main(void)
{
    // One byte, a byte count that fills a uint8_t, one past it, a length past
    // 256, and the most the UCI asks for.
    const uint16_t lengths[] = { 1, 255, 256, 300, 512 };
    for (int i = 0; i < 5; i++) {
        LEN = lengths[i];
        printf("\n== len %d\n", LEN);
        reply_cases();
    }

    printf("The UCI status of a result\n");
    check(wifi_random_status(0) == 0, "0 is OK");
    check(wifi_random_status(WIFI_RANDOM_BUSY) == 88, "busy is 88");
    check(wifi_random_status(WIFI_RANDOM_INVALID) == 86, "an invalid reply is 86");
    check(wifi_random_status(WIFI_RANDOM_REFUSED) == 87, "a refusal is 87");
    check(wifi_random_status(WIFI_RANDOM_NO_MODULE) == 87, "no module is 87");
    check(wifi_random_status(WIFI_RANDOM_FOREIGN) == 87, "a foreign reply is 87");
    bool only_zero = true;
    for (int result = -1000; result <= 1000; result++) {
        if ((wifi_random_status(result) == 0) != (result == 0)) {
            only_zero = false;
        }
    }
    check(only_zero, "no result but 0 maps to OK, from -1000 to 1000");
    check((wifi_random_status(INT_MIN) == 87) && (wifi_random_status(INT_MAX) == 87),
          "INT_MIN and INT_MAX are 87, so carry no data");

    printf("\n%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
