#include "tls_stream.h"
#include "mbedtls/entropy.h"
#include "mbedtls/ctr_drbg.h"
#include <stdlib.h>

#if !defined(MBEDTLS_HAVE_TIME_DATE)
#error "HTTPS requires certificate validity date verification"
#endif

struct tls_stream {
    tls_stream_port port;
    mbedtls_ssl_context ssl;
    mbedtls_ssl_config config;
    mbedtls_entropy_context entropy;
    mbedtls_ctr_drbg_context random;
    int64_t start;
    uint32_t timeout;
    https_status status;
    tls_stage stage;
    int code;
    uint32_t verify_flags;
    bool attempted, connected, started, eof;
};

static void close_stream(tls_stream *s)
{
    if (s->attempted) {
        s->attempted = false;
        s->port.close(s->port.context);
    }
    s->connected = false;
}

static uint32_t budget(tls_stream *s)
{
    if (s->status != HTTPS_OK) return 0;
    if (s->port.cancelled && s->port.cancelled(s->port.context)) {
        s->status = HTTPS_CANCELLED; return 0;
    }
    int64_t now = s->port.now_ms(s->port.context);
    if (now < s->start || now - s->start >= s->timeout) {
        s->status = HTTPS_TIMEOUT; return 0;
    }
    return s->timeout - (uint32_t)(now - s->start);
}

static int send_bytes(void *context, const unsigned char *bytes, size_t length)
{
    tls_stream *s = context;
    uint32_t left = budget(s);
    if (!left) return MBEDTLS_ERR_SSL_TIMEOUT;
    size_t chunk = length > 512 ? 512 : length;
    int n = s->port.write(s->port.context, bytes, chunk, left);
    if (!budget(s)) return MBEDTLS_ERR_SSL_TIMEOUT;
    if (n <= 0 || (size_t)n > chunk) {
        s->status = HTTPS_TRANSPORT_ERROR; return MBEDTLS_ERR_SSL_INTERNAL_ERROR;
    }
    return n;
}

static int receive_bytes(void *context, unsigned char *bytes, size_t length)
{
    tls_stream *s = context;
    uint32_t left = budget(s);
    if (!left) return MBEDTLS_ERR_SSL_TIMEOUT;
    size_t chunk = length > 512 ? 512 : length;
    int n = s->port.read(s->port.context, bytes, chunk, left);
    if (!budget(s)) return MBEDTLS_ERR_SSL_TIMEOUT;
    if (n < 0 || (size_t)n > chunk) {
        s->status = HTTPS_TRANSPORT_ERROR; return MBEDTLS_ERR_SSL_INTERNAL_ERROR;
    }
    return n;
}

static bool retry(int n)
{
    return n == MBEDTLS_ERR_SSL_WANT_READ || n == MBEDTLS_ERR_SSL_WANT_WRITE
#ifdef MBEDTLS_ERR_SSL_RECEIVED_NEW_SESSION_TICKET
        || n == MBEDTLS_ERR_SSL_RECEIVED_NEW_SESSION_TICKET
#endif
        ;
}

tls_stream *tls_stream_create(const tls_stream_port *p)
{
    if (!p || !p->now_ms || !p->time_ready || !p->trust || !p->open ||
        !p->read || !p->write || !p->close) return NULL;
    tls_stream *s = calloc(1, sizeof(*s));
    if (!s) return NULL;
    s->port = *p;
    mbedtls_ssl_init(&s->ssl);
    mbedtls_ssl_config_init(&s->config);
    mbedtls_entropy_init(&s->entropy);
    mbedtls_ctr_drbg_init(&s->random);
    return s;
}

https_status tls_stream_open(tls_stream *s, const char *host, uint16_t port, uint32_t timeout)
{
    if (!s) return HTTPS_INVALID_ARGUMENT;
    if (s->started) return HTTPS_INVALID_ARGUMENT;
    s->started = true;
    https_request validation = {.host=host, .port=port, .path="/", .method="GET", .timeout_ms=timeout};
    if (!https_request_valid(&validation)) return s->status = HTTPS_INVALID_ARGUMENT;
    s->start = s->port.now_ms(s->port.context);
    if (s->start < 0) return s->status = HTTPS_INVALID_ARGUMENT;
    s->timeout = timeout;
    if (!budget(s)) return s->status;
    s->stage = TLS_STAGE_TIME;
    if (!s->port.time_ready(s->port.context)) return s->status = HTTPS_TIME_UNAVAILABLE;
    static const unsigned char seed[] = "ultimate-https-stream";
    s->stage = TLS_STAGE_RANDOM;
    s->code = mbedtls_ctr_drbg_seed(&s->random, mbedtls_entropy_func, &s->entropy, seed, sizeof(seed)-1);
    if (s->code) return s->status = HTTPS_TLS_ERROR;
    s->stage = TLS_STAGE_CONFIG;
    s->code = mbedtls_ssl_config_defaults(&s->config, MBEDTLS_SSL_IS_CLIENT,
                                        MBEDTLS_SSL_TRANSPORT_STREAM, MBEDTLS_SSL_PRESET_DEFAULT);
    if (s->code) return s->status = HTTPS_TLS_ERROR;
    s->stage = TLS_STAGE_TRUST;
    s->code = s->port.trust(s->port.context, &s->config);
    if (s->code) return s->status = HTTPS_TLS_ERROR;
    mbedtls_ssl_conf_authmode(&s->config, MBEDTLS_SSL_VERIFY_REQUIRED);
    mbedtls_ssl_conf_min_tls_version(&s->config, MBEDTLS_SSL_VERSION_TLS1_2);
    mbedtls_ssl_conf_rng(&s->config, mbedtls_ctr_drbg_random, &s->random);
    s->stage = TLS_STAGE_SETUP;
    s->code = mbedtls_ssl_setup(&s->ssl, &s->config);
    if (s->code) return s->status = HTTPS_TLS_ERROR;
    s->stage = TLS_STAGE_HOSTNAME;
    s->code = mbedtls_ssl_set_hostname(&s->ssl, host);
    if (s->code) return s->status = HTTPS_TLS_ERROR;
    mbedtls_ssl_set_bio(&s->ssl, s, send_bytes, receive_bytes, NULL);
    uint32_t left = budget(s);
    if (!left) return s->status;
    s->attempted = true;
    s->stage = TLS_STAGE_TCP_OPEN;
    int result = s->port.open(s->port.context, host, port, left);
    if (!budget(s) || result) {
        if (s->status == HTTPS_OK) s->status = HTTPS_TRANSPORT_ERROR;
        close_stream(s); return s->status;
    }
    s->stage = TLS_STAGE_HANDSHAKE;
    do {
        if (!budget(s)) break;
        result = mbedtls_ssl_handshake(&s->ssl);
        s->code = result;
        s->verify_flags = mbedtls_ssl_get_verify_result(&s->ssl);
    } while (retry(result));
    if (!budget(s) || result || mbedtls_ssl_get_verify_result(&s->ssl)) {
        if (s->status == HTTPS_OK) s->status = HTTPS_TLS_ERROR;
        close_stream(s); return s->status;
    }
    s->connected = true;
    return HTTPS_OK;
}

int tls_stream_read(tls_stream *s, uint8_t *bytes, size_t length)
{
    if (!s || !s->connected || !bytes || !length) return -1;
    if (s->eof) return 0;
    s->stage = TLS_STAGE_READ;
    int n;
    do {
        if (!budget(s)) { close_stream(s); return -1; }
        n = mbedtls_ssl_read(&s->ssl, bytes, length > 512 ? 512 : length);
        s->code = n < 0 ? n : 0;
    } while (retry(n));
    if (!budget(s)) { close_stream(s); return -1; }
    if (n == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY) { s->eof = true; return 0; }
    if (n <= 0) {
        if (s->status == HTTPS_OK) s->status = n == 0 ? HTTPS_TRUNCATED : HTTPS_TLS_ERROR;
        close_stream(s); return -1;
    }
    return n;
}

int tls_stream_write(tls_stream *s, const uint8_t *bytes, size_t length)
{
    if (!s || !s->connected || s->eof || !bytes || !length) return -1;
    s->stage = TLS_STAGE_WRITE;
    int n;
    do {
        if (!budget(s)) { close_stream(s); return -1; }
        n = mbedtls_ssl_write(&s->ssl, bytes, length > 512 ? 512 : length);
        s->code = n < 0 ? n : 0;
    } while (retry(n));
    if (!budget(s)) { close_stream(s); return -1; }
    if (n <= 0) {
        if (s->status == HTTPS_OK) s->status = HTTPS_TLS_ERROR;
        close_stream(s); return -1;
    }
    return n;
}

https_status tls_stream_status(const tls_stream *s)
{
    return s ? s->status : HTTPS_INVALID_ARGUMENT;
}

tls_stream_diagnostics tls_stream_diagnostic(const tls_stream *s)
{
    tls_stream_diagnostics d = {TLS_STAGE_VALIDATE, HTTPS_INVALID_ARGUMENT, 0, 0};
    if (s) {
        d.stage=s->stage; d.status=s->status; d.code=s->code; d.verify_flags=s->verify_flags;
    }
    return d;
}

void tls_stream_destroy(tls_stream *s)
{
    if (!s) return;
    close_stream(s);
    mbedtls_ssl_free(&s->ssl);
    mbedtls_ssl_config_free(&s->config);
    mbedtls_ctr_drbg_free(&s->random);
    mbedtls_entropy_free(&s->entropy);
    free(s);
}
