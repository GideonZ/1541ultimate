#define _POSIX_C_SOURCE 200809L
#include "tls_stream.h"
#include "mbedtls/x509_crt.h"
#include "psa/crypto.h"
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <time.h>
#include <unistd.h>

static mbedtls_x509_crt roots;
static int fd = -1, opens, closes, fragment, mode;
static int64_t cancel_at;
static int64_t now(void *p)
{
    (void)p; struct timespec t; assert(!clock_gettime(CLOCK_MONOTONIC, &t));
    return (int64_t)t.tv_sec * 1000 + t.tv_nsec / 1000000;
}
static bool time_ready(void *p) { (void)p; return mode != 1; }
static bool cancelled(void *p) { return mode == 2 || (cancel_at && now(p) >= cancel_at); }
static int trust(void *p, mbedtls_ssl_config *config)
{
    (void)p;
    if (mode == 5) return -1;
    mbedtls_ssl_conf_ca_chain(config, &roots, NULL); return 0;
}
static int wait_socket(short events, uint32_t timeout)
{
    struct pollfd p = {fd, events, 0};
    return poll(&p, 1, (int)timeout) > 0;
}
static int tcp_open(void *p, const char *host, uint16_t port, uint32_t timeout)
{
    (void)p; (void)host; opens++;
    if (mode == 4) return -1;
    fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0 || fcntl(fd, F_SETFL, O_NONBLOCK)) return -1;
    struct sockaddr_in address = {0};
    address.sin_family = AF_INET; address.sin_port = htons(port);
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    int result = connect(fd, (struct sockaddr *)&address, sizeof address);
    if (result && errno == EINPROGRESS && wait_socket(POLLOUT, timeout)) {
        int error = 0; socklen_t size = sizeof error;
        if (!getsockopt(fd, SOL_SOCKET, SO_ERROR, &error, &size) && !error) return 0;
    }
    return result;
}
static int tcp_read(void *p, uint8_t *data, size_t length, uint32_t timeout)
{
    (void)p; assert(length <= 512 && timeout > 0);
    if (mode == 6) return (int)length + 1;
    if (length > (size_t)fragment) length = fragment;
    if (!wait_socket(POLLIN, timeout)) return -1;
    return (int)recv(fd, data, length, 0);
}
static int tcp_write(void *p, const uint8_t *data, size_t length, uint32_t timeout)
{
    (void)p; assert(length <= 512 && timeout > 0);
    if (mode == 7) return 0;
    if (length > (size_t)fragment) length = fragment;
    if (!wait_socket(POLLOUT, timeout)) return -1;
    return (int)send(fd, data, length, MSG_NOSIGNAL);
}
static void tcp_close(void *p)
{
    (void)p; closes++; if (fd >= 0) close(fd); fd = -1;
}
int main(int argc, char **argv)
{
    assert(argc == 10);
    assert(psa_crypto_init() == PSA_SUCCESS);
    mbedtls_x509_crt_init(&roots);
    assert(!mbedtls_x509_crt_parse_file(&roots, argv[1]));
    mode = atoi(argv[4]); fragment = atoi(argv[5]); assert(fragment > 0);
    int expected = atoi(argv[6]), timeout = atoi(argv[7]);
    tls_stream_port port = {NULL, now, time_ready, cancelled, trust, tcp_open, tcp_read, tcp_write, tcp_close};
    assert(!tls_stream_create(NULL));
    tls_stream *s = tls_stream_create(&port); assert(s);
    int64_t start = now(NULL);
    if (mode == 3) cancel_at = start + 50;
    https_status result = tls_stream_open(s, argv[3], (uint16_t)atoi(argv[2]), timeout);
    if (result == HTTPS_OK) {
        const char request[] = "GET / HTTP/1.1\r\nHost: localhost\r\n\r\n";
        size_t sent = 0;
        while (sent < sizeof(request) - 1) {
            int n = tls_stream_write(s, (const uint8_t *)request + sent, sizeof(request) - 1 - sent);
            if (n < 0) break;
            sent += (size_t)n;
        }
        uint8_t response[256]; size_t used = 0;
        for (;;) {
            assert(used < sizeof response);
            int n = tls_stream_read(s, response + used, sizeof response - used);
            if (n <= 0) break;
            used += (size_t)n;
        }
        result = tls_stream_status(s);
        if (result == HTTPS_OK) {
            const char wanted[] = "HTTP/1.1 200 OK\r\nContent-Length: 3\r\n\r\na\0b";
            assert(used == sizeof(wanted) - 1 && !memcmp(response, wanted, used));
        }
    }
    tls_stream_diagnostics diagnostic=tls_stream_diagnostic(s);
    assert(diagnostic.status==result && diagnostic.stage==(tls_stage)atoi(argv[8]));
    if (atoi(argv[9])) assert(diagnostic.verify_flags && diagnostic.code<0);
    if (mode==5) assert(diagnostic.code==-1);
    if (result!=HTTPS_OK) {
        uint8_t byte=0;
        assert(tls_stream_read(s,&byte,1)==-1 && tls_stream_write(s,&byte,1)==-1);
        tls_stream_diagnostics after=tls_stream_diagnostic(s);
        assert(after.stage==diagnostic.stage && after.status==diagnostic.status &&
               after.code==diagnostic.code && after.verify_flags==diagnostic.verify_flags);
    }
    assert(tls_stream_diagnostic(NULL).status==HTTPS_INVALID_ARGUMENT);
    tls_stream_destroy(s);
    assert(opens == closes && fd == -1);
    if (mode == 1 || mode == 2 || mode == 5) assert(!opens);
    printf("status=%d expected=%d opens=%d closes=%d elapsed=%lld\n", result, expected, opens, closes, (long long)(now(NULL)-start));
    assert(result == (https_status)expected);
    assert(now(NULL) - start < timeout + 1500);
    mbedtls_x509_crt_free(&roots);
    return 0;
}
