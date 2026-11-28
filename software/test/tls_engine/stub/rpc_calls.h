/* Stand-in for raw_u64/main/rpc_calls.h: only the header type rpc_tls.h builds on. */
#ifndef STUB_RPC_CALLS_H
#define STUB_RPC_CALLS_H
#include <stdint.h>
typedef struct {
    uint8_t command;
    uint8_t thread;
    uint16_t sequence;
} rpc_header_t;
typedef struct {
    rpc_header_t hdr;
    int esp_err;
} rpc_espcmd_resp;
#include "rpc_tls.h"
#endif
