/*
 * send_all_legacy.h -- one chunk of HttpRequest::send_request as it was before
 * GideonZ/1541ultimate#1039, so the same suite can be run against it.
 *
 * This is the red half of red/green, a model of the defect: one non-blocking
 * send() per chunk, with only a negative result taken as a failure. Not built
 * into any firmware, so it is kept beside the tests.
 */
#ifndef NETWORK_SEND_ALL_LEGACY_H
#define NETWORK_SEND_ALL_LEGACY_H

#include <stddef.h>
#include <sys/socket.h>

/* http_request.cc before the fix:
 *
 *     r = send(socket_fd, buffer, n, MSG_DONTWAIT);
 *     if (r < 0) {
 *         return r;
 *     }
 *
 * A short count went unnoticed, and the bytes it left out were never sent.
 */
static inline int send_all(int socket_fd, const char *data, int len)
{
    if (len > 0) {
        int r = send(socket_fd, data, (size_t)len, MSG_DONTWAIT);
        if (r < 0) {
            return r;
        }
    }
    return 0;
}

#endif
