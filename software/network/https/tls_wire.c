#include "tls_wire.h"
#include "https_client.h"
#include <string.h>

uint16_t tls_wire_get16(const uint8_t *p) { return p[0] | (uint16_t)p[1] << 8; }
uint32_t tls_wire_get32(const uint8_t *p) { return tls_wire_get16(p) | (uint32_t)tls_wire_get16(p + 2) << 16; }
void tls_wire_put16(uint8_t *p, uint16_t n) { p[0] = (uint8_t)n; p[1] = (uint8_t)(n >> 8); }
void tls_wire_put32(uint8_t *p, uint32_t n) { tls_wire_put16(p, (uint16_t)n); tls_wire_put16(p + 2, (uint16_t)(n >> 16)); }
uint64_t tls_wire_get_epoch(const uint8_t *p) { return (uint64_t)tls_wire_get32(p+28)<<32 | tls_wire_get32(p+24); }
void tls_wire_put_epoch(uint8_t *p,uint64_t epoch) { tls_wire_put32(p+24,(uint32_t)epoch); tls_wire_put32(p+28,(uint32_t)(epoch>>32)); }

bool tls_wire_valid(const uint8_t *p, size_t size)
{
    if (!p || size < TLS_WIRE_HEADER || size > TLS_WIRE_HEADER + TLS_WIRE_CHUNK) return false;
    if (p[0] != 'U' || p[1] != 'S' || p[2] != TLS_WIRE_VERSION ||
        (p[3] != TLS_WIRE_REQUEST && p[3] != TLS_WIRE_REPLY) ||
        (p[4] != TLS_WIRE_TLS && p[4] != TLS_WIRE_TCP) ||
        p[5] < TLS_WIRE_OPEN || p[5] > TLS_WIRE_RESERVE || p[6] || p[7] ||
        !tls_wire_get32(p + 8) || !tls_wire_get32(p + 12) ||
        !tls_wire_get32(p+24) || !tls_wire_get32(p+28)) return false;
    unsigned timeout = tls_wire_get16(p + 16), count = tls_wire_get16(p + 18);
    uint32_t result = tls_wire_get32(p + 20);
    size_t payload = size - TLS_WIRE_HEADER;
    if (count > TLS_WIRE_CHUNK || (p[5] == TLS_WIRE_TIME && p[4] != TLS_WIRE_TCP)) return false;
    if(p[5]==TLS_WIRE_RESERVE && (p[4]!=TLS_WIRE_TLS || tls_wire_get32(p+8)!=UINT32_MAX))return false;
    if (p[3] == TLS_WIRE_REPLY) {
        if (timeout) return false;
        /* Negative result codes have no data. Positive results count bytes. */
        if (result & UINT32_C(0x80000000)) return !count && !payload;
        if (p[5] == TLS_WIRE_READ) return result == count && payload == count;
        if (p[5] == TLS_WIRE_WRITE) return result <= TLS_WIRE_CHUNK && !count && !payload;
        if (p[5] == TLS_WIRE_TIME) return !result && count == 8 && payload == 8;
        if (p[5] == TLS_WIRE_RESERVE) return !result && count == 4 && payload == 4 &&
            tls_wire_get32(p+TLS_WIRE_HEADER) && tls_wire_get32(p+TLS_WIRE_HEADER)!=UINT32_MAX;
        return !result && !count && !payload;
    }
    if (!timeout || timeout > HTTPS_MAX_TIMEOUT_MS || result) return false;
    switch (p[5]) {
    case TLS_WIRE_OPEN: {
        if (count < 3 || count > HTTPS_MAX_HOST + 2 || payload != count) return false;
        const uint8_t *data = p + TLS_WIRE_HEADER;
        if (memchr(data + 2, 0, count - 2)) return false;
        char host[HTTPS_MAX_HOST + 1];
        memcpy(host, data + 2, count - 2); host[count - 2] = 0;
        https_request r = {.host=host, .port=tls_wire_get16(data), .path="/", .method="GET", .timeout_ms=timeout};
        return https_request_valid(&r);
    }
    case TLS_WIRE_READ: return count && !payload;
    case TLS_WIRE_WRITE: return count && count == payload;
    default: return !count && !payload;
    }
}

bool tls_wire_matches(const uint8_t *request, size_t request_size,
                      const uint8_t *reply, size_t reply_size)
{
    if (!tls_wire_valid(request, request_size) || !tls_wire_valid(reply, reply_size) ||
        request[3] != TLS_WIRE_REQUEST || reply[3] != TLS_WIRE_REPLY ||
        request[4] != reply[4] || request[5] != reply[5] ||
        memcmp(request + 8, reply + 8, 8) ||
        tls_wire_get_epoch(request)!=tls_wire_get_epoch(reply)) return false;
    uint32_t result = tls_wire_get32(reply + 20);
    if (!(result & UINT32_C(0x80000000)) &&
        (request[5] == TLS_WIRE_READ || request[5] == TLS_WIRE_WRITE))
        return result <= tls_wire_get16(request + 18);
    return true;
}
