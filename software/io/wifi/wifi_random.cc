// wifi_random.cc: checks the WiFi module's answer to GET_RANDOM. Free of
// FreeRTOS and of the UART driver, so that the host test tests what ships.

#include <stddef.h>
#include <string.h>
#include "rpc_calls.h"
#include "wifi_random.h"

// The layout the module sends and this code reads. Checked here so that the
// firmware build itself, not only the host test, fails if it ever changes.
static_assert(sizeof(rpc_get_random_req) == 6, "rpc_get_random_req changed");
static_assert(offsetof(rpc_get_random_req, length) == 4, "rpc_get_random_req changed");
static_assert(sizeof(rpc_get_random_resp) == 12, "rpc_get_random_resp changed");
static_assert(offsetof(rpc_get_random_resp, esp_err) == 4, "rpc_get_random_resp changed");
static_assert(offsetof(rpc_get_random_resp, source) == 8, "rpc_get_random_resp changed");
static_assert(offsetof(rpc_get_random_resp, length) == 10, "rpc_get_random_resp changed");
static_assert(offsetof(rpc_get_random_resp, data) == 12, "rpc_get_random_resp changed");

int wifi_random_take_reply(const uint8_t *reply, int size, uint16_t sequence,
                           uint8_t *buf, uint16_t len)
{
    const rpc_get_random_resp *result = (const rpc_get_random_resp *)reply;
    const int with_error = offsetof(rpc_get_random_resp, source);
    const int header = offsetof(rpc_get_random_resp, data);

    if ((size < (int)sizeof(rpc_header_t)) || (result->hdr.command != CMD_GET_RANDOM) ||
            (result->hdr.sequence != sequence)) {
        return WIFI_RANDOM_FOREIGN;
    }
    if (size < with_error) {
        return WIFI_RANDOM_INVALID;
    }
    if (result->esp_err != 0) {
        return WIFI_RANDOM_REFUSED;
    }
    if (size < header) {
        return WIFI_RANDOM_INVALID;
    }
    // Bytes drawn without an entropy source are refused even when esp_err
    // says OK.
    if (result->source == ENTROPY_NONE) {
        return WIFI_RANDOM_REFUSED;
    }
    if ((result->length != len) || (size < header + len)) {
        return WIFI_RANDOM_INVALID;
    }
    memcpy(buf, result->data, len);
    return 0;
}

int wifi_random_status(int result)
{
    switch (result) {
        case 0:
            return 0;
        case WIFI_RANDOM_BUSY:
            return 88;
        case WIFI_RANDOM_INVALID:
            return 86;
        default:
            return 87;
    }
}
