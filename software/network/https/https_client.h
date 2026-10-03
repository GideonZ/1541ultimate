#ifndef HTTPS_CLIENT_H
#define HTTPS_CLIENT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define HTTPS_MAX_HOST 253
#define HTTPS_MAX_PATH 384
#define HTTPS_MAX_HEADERS 16
#define HTTPS_MAX_HEADER_BYTES 1024
#define HTTPS_MAX_REQUEST_BODY 4096
#define HTTPS_MAX_RESPONSE_BODY 4096
#define HTTPS_MAX_TIMEOUT_MS 15000

typedef enum {
    HTTPS_OK, HTTPS_INVALID_ARGUMENT, HTTPS_CANCELLED, HTTPS_TIMEOUT,
    HTTPS_TRANSPORT_ERROR, HTTPS_TLS_ERROR, HTTPS_TIME_UNAVAILABLE,
    HTTPS_BODY_TOO_LARGE, HTTPS_TRUNCATED, HTTPS_PROTOCOL_ERROR
} https_status;

typedef struct {
    const char *name;
    const char *value;
} https_header;

typedef struct {
    const char *host;             /* DNS hostname, without scheme or port. */
    uint16_t port;
    const char *path;             /* Origin-form path, optionally with query. */
    const char *method;
    const https_header *headers;
    size_t header_count;
    const uint8_t *body;
    size_t body_length;
    uint32_t timeout_ms;
    bool (*cancelled)(void *context);
    void *cancel_context;
} https_request;

typedef struct {
    uint8_t *body;                /* Caller-owned binary buffer; no terminator. */
    size_t capacity;
    size_t length;                /* Valid only when the fetch returns HTTPS_OK. */
    int http_status;              /* Final HTTP status, independent of transport. */
} https_response;

/* Synchronous port used from a worker, never from the UART dispatcher.
 * open sends the request after authenticated TLS and returns final response
 * metadata. It MUST verify the chain, hostname and dates with fresh UTC time.
 * No redirects, plaintext fallback, decompression or unbounded buffering.
 * read returns decoded HTTP body bytes, zero at EOF, negative on failure.
 * complete distinguishes a complete HTTP message from premature stream EOF.
 * All operations, including end, must be bounded. end is called exactly once
 * after every open attempt, even if it fails. now_ms is monotonic and nonnegative.
 * Callers keep request data alive and unchanged until fetch returns.
 */
typedef struct {
    void *context;
    int64_t (*now_ms)(void *context);
    https_status (*open)(void *context, const https_request *request,
                         uint32_t remaining_ms, int *http_status,
                         int64_t *body_length); /* -1 when unknown */
    int (*read)(void *context, uint8_t *buffer, size_t capacity,
                uint32_t remaining_ms);
    bool (*complete)(void *context);
    void (*end)(void *context);
} https_transport;

bool https_request_valid(const https_request *request);
https_status https_fetch(const https_request *request, https_response *response,
                         const https_transport *transport);

#ifdef __cplusplus
}
#endif
#endif
