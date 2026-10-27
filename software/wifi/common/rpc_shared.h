/*
 * rpc_shared.h
 *
 * What the Ultimate and the WiFi modules of the Ultimate 64 (software/wifi/raw_u64)
 * and of the Ultimate 64 Elite II and C64 Ultimate (software/u64ctrl) must agree
 * on, in one place. Included by each module's
 * rpc_calls.h, after rpc_header_t, and by the Ultimate through it.
 */

#ifndef SOFTWARE_WIFI_COMMON_RPC_SHARED_H_
#define SOFTWARE_WIFI_COMMON_RPC_SHARED_H_

#include <stddef.h>
#include <stdint.h>

// The Nios updater is still built as C++98
#if defined(__cplusplus) && (__cplusplus < 201103L)
#define RPC_CONCAT_(a, b) a ## b
#define RPC_CONCAT(a, b) RPC_CONCAT_(a, b)
#define RPC_STATIC_ASSERT(cond, msg) typedef char RPC_CONCAT(rpc_assert_, __LINE__)[(cond) ? 1 : -1]
#elif defined(__cplusplus)
#define RPC_STATIC_ASSERT static_assert
#else
#define RPC_STATIC_ASSERT _Static_assert
#endif

//----------------------------------
// hardware random bytes
#define CMD_GET_RANDOM        0x20 // u64ctrl's own commands run from 0x13 and may reach 0x1F; kept apart so the maps agree
#define RANDOM_MAX_BYTES 512

typedef enum {
    ENTROPY_NONE  = 0, // RNG not fed by a live entropy source; no bytes are returned
    ENTROPY_RADIO = 1, // Wi-Fi is running, so the RNG gives true random numbers
} entropy_source_t;

typedef struct {
    rpc_header_t hdr;
    uint16_t length;
} rpc_get_random_req;

typedef struct {
    rpc_header_t hdr;
    int esp_err; // to be casted to esp_err_t
    uint8_t source; // to be casted to entropy_source_t
    uint16_t length;
    uint8_t data[]; // sizeof() excludes it, so it is exactly the header size
} rpc_get_random_resp;

// The Ultimate decodes these by offset: a layout change must break the build,
// on every compiler that includes this, not the wire
RPC_STATIC_ASSERT(sizeof(rpc_get_random_req) == 6, "rpc_get_random_req changed");
RPC_STATIC_ASSERT(offsetof(rpc_get_random_req, length) == 4, "rpc_get_random_req changed");
RPC_STATIC_ASSERT(sizeof(rpc_get_random_resp) == 12, "rpc_get_random_resp changed");
RPC_STATIC_ASSERT(offsetof(rpc_get_random_resp, esp_err) == 4, "rpc_get_random_resp changed");
RPC_STATIC_ASSERT(offsetof(rpc_get_random_resp, source) == 8, "rpc_get_random_resp changed");
RPC_STATIC_ASSERT(offsetof(rpc_get_random_resp, length) == 10, "rpc_get_random_resp changed");
RPC_STATIC_ASSERT(offsetof(rpc_get_random_resp, data) == 12, "rpc_get_random_resp changed");
RPC_STATIC_ASSERT(ENTROPY_NONE == 0, "ENTROPY_NONE changed");

#endif /* SOFTWARE_WIFI_COMMON_RPC_SHARED_H_ */
