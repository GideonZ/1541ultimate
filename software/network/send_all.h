/*
 * send_all.h -- hand a whole buffer to a TCP socket.
 *
 * send() may take fewer bytes than it was given and return how many it took;
 * the rest is the caller's to send again. A free function that calls send() by
 * name, so the host tests in software/io/network/tests/ can supply their own.
 */
#ifndef NETWORK_SEND_ALL_H
#define NETWORK_SEND_ALL_H

#include <stddef.h>
#include <sys/socket.h>

/* As send_all below, and *sent tells how many bytes were taken, also when it
 * fails: a caller whose SO_SNDTIMEO ran out can wait again for the rest.
 */
static inline int send_part(int socket_fd, const char *data, int len, int *sent)
{
    *sent = 0;
    while (*sent < len) {
        int r = send(socket_fd, data + *sent, (size_t)(len - *sent), 0);
        if (r <= 0) {
            return -1;
        }
        *sent += r;
    }
    return 0;
}

/* Sends len bytes from data, calling send() until all of them are taken. It
 * waits while the peer is not reading, so a slow receiver gets everything; how
 * long one send() waits is the socket's SO_SNDTIMEO. That bounds each call, not
 * the whole buffer: lwIP returns a partial count when the time runs out, and
 * only the next call, after another full wait, fails.
 * Returns 0 when all bytes were sent, or a negative value when send() failed,
 * timed out or stopped taking data; errno is as send() left it.
 */
static inline int send_all(int socket_fd, const char *data, int len)
{
    int sent;
    return send_part(socket_fd, data, len, &sent);
}

#endif
