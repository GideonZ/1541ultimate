#include "tls_http_connection.h"

// No POSIX calls in this implementation; protect the external callback member.
#undef close

TlsHttpConnection :: TlsHttpConnection(const tls_stream_port& callbacks, uint32_t timeout) :
    port(callbacks), timeout_ms(timeout), stream(NULL), attempted(false)
{
    last = { TLS_STAGE_VALIDATE, TLS_OK, 0, 0 };
}

TlsHttpConnection :: ~TlsHttpConnection()
{
    close_session();
}

int TlsHttpConnection :: open(const char *hostname, uint16_t hostport)
{
    if (attempted) {
        return -1;
    }
    attempted = true;
    if (!port.now_ms || !port.time_ready || !port.entropy || !port.trust ||
        !port.open || !port.read || !port.write || !port.close) {
        return invalid(TLS_STAGE_VALIDATE);
    }
    stream = tls_stream_create(&port);
    if (!stream) {
        last = { TLS_STAGE_SETUP, TLS_ERROR, MBEDTLS_ERR_SSL_ALLOC_FAILED, 0 };
        return -1;
    }
    tls_status result = tls_stream_open(stream, hostname, hostport, timeout_ms);
    if (result != TLS_OK) {
        close_session();
        return -1;
    }
    return 0;
}

int TlsHttpConnection :: invalid(tls_stage stage)
{
    close_session();
    if (last.status == TLS_OK) {
        last = { stage, TLS_INVALID_ARGUMENT, 0, 0 };
    }
    return -1;
}

int TlsHttpConnection :: read(void *buffer, int length)
{
    if (!stream) return -1;
    if (!buffer || length <= 0) return invalid(TLS_STAGE_READ);
    int result = tls_stream_read(stream, static_cast<uint8_t *>(buffer), length);
    if (result < 0) return invalid(TLS_STAGE_READ);
    return result;
}

int TlsHttpConnection :: write(const void *buffer, int length)
{
    if (!stream) return -1;
    if (!buffer || length <= 0) return invalid(TLS_STAGE_WRITE);
    int result = tls_stream_write(stream, static_cast<const uint8_t *>(buffer), length);
    if (result < 0) return invalid(TLS_STAGE_WRITE);
    return result;
}

void TlsHttpConnection :: close_session()
{
    attempted = true; // Closing an unused adapter also retires it permanently.
    if (stream) {
        last = tls_stream_diagnostic(stream);
        tls_stream_destroy(stream);
        stream = NULL;
    }
}

tls_stream_diagnostics TlsHttpConnection :: diagnostic() const
{
    return stream ? tls_stream_diagnostic(stream) : last;
}
