#ifndef ULTIMATE_TLS_WIRE_H
#define ULTIMATE_TLS_WIRE_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Internal UART payload, not a C64 command. Outer RPC identifiers remain
 * unallocated. Integers are little endian, no C structure is serialized.
 * The mandatory client epoch combines a durable management boot counter with
 * a per-boot exchange counter. Match it even for reservation replies. Neither
 * epoch counter may wrap or be rolled back while old traffic remains.
 */
#define TLS_WIRE_VERSION 2
#define TLS_WIRE_HEADER 32
#define TLS_WIRE_CHUNK 512
#define TLS_WIRE_REQUEST 1
#define TLS_WIRE_REPLY 2
#define TLS_WIRE_TLS 1
#define TLS_WIRE_TCP 2
#define TLS_WIRE_OPEN 1
#define TLS_WIRE_READ 2
#define TLS_WIRE_WRITE 3
#define TLS_WIRE_CLOSE 4
#define TLS_WIRE_TIME 5
#define TLS_WIRE_RESERVE 6

uint16_t tls_wire_get16(const uint8_t *);
uint32_t tls_wire_get32(const uint8_t *);
uint64_t tls_wire_get_epoch(const uint8_t *);
void tls_wire_put_epoch(uint8_t *, uint64_t);
void tls_wire_put16(uint8_t *, uint16_t);
void tls_wire_put32(uint8_t *, uint32_t);
bool tls_wire_valid(const uint8_t *, size_t);
bool tls_wire_matches(const uint8_t *request, size_t request_size,
                      const uint8_t *reply, size_t reply_size);

#ifdef __cplusplus
}
#endif
#endif
