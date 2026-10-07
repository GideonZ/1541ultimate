#ifndef ULTIMATE_TLS_SERVICE_H
#define ULTIMATE_TLS_SERVICE_H
#include "tls_wire.h"

/* Single-worker session owner. Callbacks run only on that worker, never in an
 * ISR or the shared UART dispatcher. open includes any partial-open cleanup.
 * All callbacks must obey the remaining total exchange deadline.
 */
typedef struct {
    void *context;
    int (*open)(void *, uint32_t session, uint64_t epoch, const char *, uint16_t, uint32_t);
    int (*read)(void *, uint8_t *, size_t);
    int (*write)(void *, const uint8_t *, size_t);
    void (*close)(void *);
} tls_service_port;
typedef struct {
    tls_service_port port;
    uint32_t session, retired, sequence, reserved, next_session;
    uint64_t epoch;
    int64_t deadline;
} tls_service;
/* Session tokens advance from seed without wrap; a reservation permits one
 * open. Restart separation relies on the durable client epoch, not this seed. */
void tls_service_init(tls_service *, const tls_service_port *, uint32_t seed);
void tls_service_expire(tls_service *, int64_t now_ms);
/* Input and output may alias; output requires HEADER+CHUNK bytes. Zero means
 * malformed/not a TLS request. queued_ms includes time spent in the queue.
 */
size_t tls_service_handle(tls_service *, const uint8_t *, size_t,
                          uint8_t *, int64_t queued_ms, int64_t now_ms);
#endif
