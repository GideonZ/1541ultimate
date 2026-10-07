#ifndef ULTIMATE_TLS_STREAM_H
#define ULTIMATE_TLS_STREAM_H

#include "https_client.h"
#include "mbedtls/ssl.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Platform callbacks are trusted firmware code. TCP callbacks must honor the
 * supplied remaining deadline, accept short transfers, and never overrun the
 * buffer. close must be bounded and safe after a partially failed open.
 * time_ready verifies that the system UTC clock has a fresh synchronization;
 * Mbed TLS uses that same system clock to check certificate validity dates.
 * trust attaches the platform CA store (for example ESP's certificate bundle).
 * It must not install a callback that suppresses certificate verification.
 */
typedef struct {
    void *context;
    int64_t (*now_ms)(void *);
    bool (*time_ready)(void *);
    bool (*cancelled)(void *);
    int (*trust)(void *, mbedtls_ssl_config *);
    int (*open)(void *, const char *, uint16_t, uint32_t);
    int (*read)(void *, uint8_t *, size_t, uint32_t);
    int (*write)(void *, const uint8_t *, size_t, uint32_t);
    void (*close)(void *);
} tls_stream_port;

typedef struct tls_stream tls_stream;
/* Diagnostic stages, independent of the public HTTP/UCI response format. */
typedef enum {
    TLS_STAGE_VALIDATE, TLS_STAGE_TIME, TLS_STAGE_RANDOM, TLS_STAGE_CONFIG,
    TLS_STAGE_TRUST, TLS_STAGE_SETUP, TLS_STAGE_HOSTNAME, TLS_STAGE_TCP_OPEN,
    TLS_STAGE_HANDSHAKE, TLS_STAGE_READ, TLS_STAGE_WRITE
} tls_stage;
typedef struct {
    tls_stage stage;
    https_status status;
    int code;                    /* Original Mbed TLS/trust return code, or zero. */
    uint32_t verify_flags;        /* Meaningful after a handshake attempt. */
} tls_stream_diagnostics;
tls_stream_diagnostics tls_stream_diagnostic(const tls_stream *);
/* One exchange per instance; not thread safe. All request and response bytes
 * are HTTP byte streams; this layer does not parse or render HTTP messages.
 * The owner limits concurrent sessions and releases the instance on all paths.
 */
tls_stream *tls_stream_create(const tls_stream_port *port);
https_status tls_stream_open(tls_stream *, const char *host, uint16_t port, uint32_t timeout_ms);
int tls_stream_read(tls_stream *, uint8_t *, size_t);
int tls_stream_write(tls_stream *, const uint8_t *, size_t);
https_status tls_stream_status(const tls_stream *);
void tls_stream_destroy(tls_stream *);

#ifdef __cplusplus
}
#endif
#endif
