/*
 * socket_slices.h -- wait on a socket in short slices, so that the one task that owns
 * the socket sees a cancel from another task within a slice.
 *
 * With NETCONN_FULLDUPLEX 0, no other task may close or shut down a socket that a task
 * is blocked on. So the owner never blocks for long: a connect is non-blocking and waits
 * in select(), and sends and receives use an SO_SNDTIMEO / SO_RCVTIMEO of one slice.
 * Between slices the owner checks its cancel flag, and adds up the time it waited
 * without progress.
 */
#ifndef NETWORK_SOCKET_SLICES_H
#define NETWORK_SOCKET_SLICES_H

#include <errno.h>
#include <sys/socket.h>
#include <stdint.h>
#include "send_all.h"
#ifdef RUNS_ON_PC
#include <time.h>
#include <netinet/in.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#define ioctlsocket ioctl
#else
#include "FreeRTOS.h"
#include "task.h"
#endif

#define SOCKET_SLICE_MS 100

// The idle time is measured, not counted in slices: on lwIP a send slice lasts about a
// second, not SOCKET_SLICE_MS (SO_SNDTIMEO is checked only when the write is re-entered).
static inline uint32_t slice_now_ms(void)
{
#ifdef RUNS_ON_PC
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)(ts.tv_sec * 1000UL + ts.tv_nsec / 1000000UL);
#else
    return (uint32_t)xTaskGetTickCount() * portTICK_PERIOD_MS;
#endif
}

class SliceWait
{
    volatile bool *cancel;
    uint32_t last_progress;
    uint32_t limit_ms;
public:
    // limit_ms: time without progress before the owner gives up; 0 waits for ever.
    SliceWait(volatile bool *c, int limit) : cancel(c), last_progress(slice_now_ms()), limit_ms(limit) { }
    void progress(void) { last_progress = slice_now_ms(); }
    bool cancelled(void) { return cancel && *cancel; }
    // The cancel flag may belong to a job that goes before the exchange does.
    void detach(void) { cancel = NULL; }
    // After a slice that timed out: whether to wait another one.
    bool again(void)
    {
        return !cancelled() && (!limit_ms || ((uint32_t)(slice_now_ms() - last_progress) < limit_ms));
    }
};

static inline bool socket_timed_out(void)
{
    return (errno == EAGAIN) || (errno == EWOULDBLOCK);
}

static inline void set_socket_slices(int socket_fd)
{
    struct timeval tv;
    tv.tv_sec = SOCKET_SLICE_MS / 1000;
    tv.tv_usec = (SOCKET_SLICE_MS % 1000) * 1000;
    setsockopt(socket_fd, SOL_SOCKET, SO_RCVTIMEO, (char *)&tv, sizeof(tv));
    setsockopt(socket_fd, SOL_SOCKET, SO_SNDTIMEO, (char *)&tv, sizeof(tv));
}

static inline int set_socket_blocking(int socket_fd, bool blocking)
{
    int nonblocking = blocking ? 0 : 1;
    return ioctlsocket(socket_fd, FIONBIO, &nonblocking);
}

// A blocking connect cannot be stopped, and lwIP takes ~18.5 s to give up on a host that
// does not answer. This one waits in slices instead. Leaves the socket blocking.
static inline int connect_sliced(int sock_fd, struct sockaddr_in *addr, SliceWait& wait)
{
    if (set_socket_blocking(sock_fd, false) < 0) {
        return -1;
    }
    int result = connect(sock_fd, (struct sockaddr *)addr, sizeof(*addr));
    if ((result < 0) && (errno == EINPROGRESS)) {
        do {
            // lwIP reports a failed connect only as an exception.
            fd_set writable, failed;
            FD_ZERO(&writable);
            FD_SET(sock_fd, &writable);
            FD_ZERO(&failed);
            FD_SET(sock_fd, &failed);
            struct timeval tv = { SOCKET_SLICE_MS / 1000, (SOCKET_SLICE_MS % 1000) * 1000 };
            result = select(sock_fd + 1, NULL, &writable, &failed, &tv);
        } while ((result == 0) && wait.again());
        if (result > 0) {
            int error = 0;
            socklen_t length = sizeof(error);
            result = getsockopt(sock_fd, SOL_SOCKET, SO_ERROR, &error, &length);
            if (error) {
                errno = error;
                result = -1;
            }
        } else {
            if (result == 0) {
                errno = ETIMEDOUT;
            }
            result = -1;
        }
    }
    if ((result < 0) || (set_socket_blocking(sock_fd, true) < 0)) {
        return -1;
    }
    wait.progress();
    return 0;
}

// Sends all len bytes on a socket whose SO_SNDTIMEO is one slice. Returns 0, or -1 when
// send() failed, the wait was cancelled, or its limit ran out; *sent says how much went.
// The cancel is checked before every send(), not only after one that timed out.
static inline int send_sliced(int socket_fd, const char *data, int len, SliceWait& wait, int *sent)
{
    *sent = 0;
    while (*sent < len) {
        if (wait.cancelled()) { // also while the peer keeps reading
            return -1;
        }
        int now;
        int r = send_part(socket_fd, data + *sent, len - *sent, &now);
        *sent += now;
        if (now) {
            wait.progress();
        }
        if ((r < 0) && !(socket_timed_out() && wait.again())) {
            return -1;
        }
    }
    return 0;
}

#endif
