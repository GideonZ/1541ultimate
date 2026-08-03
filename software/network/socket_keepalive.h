#ifndef SOCKET_KEEPALIVE_H
#define SOCKET_KEEPALIVE_H

#include "lwip/sockets.h"

/*
 * Shared abandoned-peer policy for the listeners that cap concurrent sessions.
 *
 * A peer that vanishes at the network level sends neither FIN nor RST, so
 * without keepalive the session task polls forever and never frees its slot.
 *
 * Detection takes idle + count * interval = 35s. These are the values telnet
 * has used since the half-open session leak was fixed; the probe window is
 * deliberately long so a brief link drop does not kill a healthy session.
 * Requires LWIP_TCP_KEEPALIVE=1 in lwipopts.h.
 */
#define NET_KEEPALIVE_IDLE_SECONDS      20
#define NET_KEEPALIVE_INTERVAL_SECONDS   5
#define NET_KEEPALIVE_PROBE_COUNT        3

static inline void net_enable_client_keepalive(int socket_fd)
{
    int on = 1;
    setsockopt(socket_fd, SOL_SOCKET, SO_KEEPALIVE, (char *)&on, sizeof(on));

    int idle = NET_KEEPALIVE_IDLE_SECONDS;
    setsockopt(socket_fd, IPPROTO_TCP, TCP_KEEPIDLE, (char *)&idle, sizeof(idle));

    int interval = NET_KEEPALIVE_INTERVAL_SECONDS;
    setsockopt(socket_fd, IPPROTO_TCP, TCP_KEEPINTVL, (char *)&interval, sizeof(interval));

    int count = NET_KEEPALIVE_PROBE_COUNT;
    setsockopt(socket_fd, IPPROTO_TCP, TCP_KEEPCNT, (char *)&count, sizeof(count));
}

#endif /* SOCKET_KEEPALIVE_H */
