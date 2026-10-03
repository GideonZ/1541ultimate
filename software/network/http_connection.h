#ifndef HTTP_CONNECTION_H
#define HTTP_CONNECTION_H

#include <stdint.h>

/* Byte stream below the existing HTTP renderer and response parser.
 * A secure implementation must authenticate the certificate chain, hostname
 * and dates before open succeeds. It owns a total exchange deadline and must
 * bound all operations, including destruction. Partial reads/writes are legal.
 * No plaintext fallback. The factory is installed once during initialization.
 */
class HttpConnection
{
public:
    virtual ~HttpConnection() {}
    virtual int open(const char *hostname, uint16_t port) = 0;
    virtual int read(void *buffer, int length) = 0;
    virtual int write(const void *buffer, int length) = 0;
    /* Optional read-only diagnostics. Never changes HTTP/UCI status or bytes. */
    virtual void failure(const char *stage, int code, int detail) {
        (void)stage; (void)code; (void)detail;
    }
};

typedef HttpConnection *(*HttpSecureConnectionFactory)();
void http_set_secure_connection_factory(HttpSecureConnectionFactory factory);

#endif
