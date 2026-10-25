#ifndef TLS_HTTP_CONNECTION_H
#define TLS_HTTP_CONNECTION_H

#include "http_connection.h"
// Some firmware headers remap the POSIX close function with a macro. Keep it
// out of the external callback structure without changing the caller's macros.
#pragma push_macro("close")
#undef close
extern "C" {
#include "tls_stream.h"
}
#pragma pop_macro("close")

// The callback context outlives this connection. TLS state belongs to the
// adapter; HttpRequest borrows the adapter and closes its session on failure.
// One instance serves one exchange, including its original total deadline.
class TlsHttpConnection : public HttpConnection
{
    tls_stream_port port;
    uint32_t timeout_ms;
    tls_stream *stream;
    bool attempted;
    tls_stream_diagnostics last;
    TlsHttpConnection(const TlsHttpConnection&);
    TlsHttpConnection& operator=(const TlsHttpConnection&);
    int invalid(tls_stage stage);
public:
    TlsHttpConnection(const tls_stream_port& callbacks, uint32_t timeout);
    ~TlsHttpConnection();
    int open(const char *hostname, uint16_t port);
    int read(void *buffer, int length);
    int write(const void *buffer, int length);
    void close_session();
    tls_stream_diagnostics diagnostic() const;
};

#endif
