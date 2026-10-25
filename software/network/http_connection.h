#ifndef HTTP_CONNECTION_H
#define HTTP_CONNECTION_H

#include <stdint.h>

// A byte stream below the HTTP renderer and parser. Short reads/writes are
// valid; zero from read means EOF, while write must make progress or fail.
// Open returns a nonnegative value on success. Calls never report more bytes
// than requested. Each session has one owner; a backend is not shared live.
// Negative results are terminal errors, not an instruction to retry.
// Secure providers authenticate before open succeeds, enforce one exchange
// deadline and bounded cancellation/close, and never fall back to plaintext.
class HttpConnection
{
public:
    virtual ~HttpConnection() {}
    virtual int open(const char *hostname, uint16_t port) = 0;
    virtual int read(void *buffer, int length) = 0;
    virtual int write(const void *buffer, int length) = 0;
    // Safe after a failed open. The owner closes the session before destruction.
    virtual void close_session() = 0;
};

// Optional secure-provider factory. The caller owns each returned connection;
// NULL means unavailable. Implementations must return a fresh authenticated
// transport, never a plain socket substitute. Context outlives every session.
class HttpSecureProvider
{
public:
    virtual ~HttpSecureProvider() {}
    virtual HttpConnection *create_connection() = 0;
};

// No heap allocation is needed for the default HTTP transport. A descriptor
// supplied to the constructor is borrowed by the legacy parser helpers.
class HttpSocketConnection : public HttpConnection
{
    int socket_fd;
    HttpSocketConnection(const HttpSocketConnection&);
    HttpSocketConnection& operator=(const HttpSocketConnection&);
public:
    explicit HttpSocketConnection(int fd = -1) : socket_fd(fd) {}
    int open(const char *hostname, uint16_t port);
    int read(void *buffer, int length);
    int write(const void *buffer, int length);
    void close_session();
};

#endif
