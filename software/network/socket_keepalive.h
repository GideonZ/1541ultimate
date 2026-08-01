#ifndef SOCKET_KEEPALIVE_H
#define SOCKET_KEEPALIVE_H

#include "lwip/sockets.h"

/*
 * One abandoned-peer policy for every listener that accepts client connections.
 *
 * A peer that vanishes at the network level (WiFi drop, powered-off phone, AP
 * roam, a client process killed) sends neither FIN nor RST. Without keepalive
 * the server never learns the connection is dead: the session task keeps
 * polling, and its slot out of the small per-service session pool is held
 * indefinitely. Every service here caps concurrent sessions to bound lwIP
 * netconn use, so one vanished peer permanently reduces capacity and enough of
 * them wedge the service for everyone.
 *
 * TCP keepalive is the mechanism the protocol already provides for this, so
 * rather than each service inventing its own idle timer, they all enable it
 * with the same settings and therefore recover at the same speed.
 *
 * Detection takes idle + (count * interval) = 20 + 3*5 = 35 seconds. The two
 * halves of that budget carry different risk, which is why they are not tuned
 * together:
 *
 *   idle             how long a quiet connection waits before the first probe.
 *                    Probing a healthy peer costs one small packet and is
 *                    answered immediately, so a short idle is close to free.
 *   count * interval the loss-tolerance window. A peer only counts as gone
 *                    after three unanswered probes spread over fifteen seconds,
 *                    so a brief WiFi drop or a congested link does not kill a
 *                    healthy session. This half is deliberately left long.
 *
 * These are not new numbers. They are exactly what the telnet server has used
 * since the half-open session leak was fixed, measured on hardware at ~37s end
 * to end by tests/e2e/network/telnet_stale_session_test.py. Moving them into a
 * header changes no timing for telnet; it lets the FTP control connection, which
 * had no abandoned-peer detection at all, adopt the same already-proven policy.
 *
 * Requires LWIP_TCP_KEEPALIVE=1 in lwipopts.h.
 *
 * The HTTP server is not listed here because it already solves the same problem
 * by a different route: MicroHTTPServer (software/httpd/c-version) reaps any
 * connection idle for HTTP_CONN_IDLE_TIMEOUT (15 s) seconds. That works for HTTP
 * because a request/response connection with no traffic is by definition stuck.
 * It would be wrong for telnet and FTP, where a session is legitimately idle
 * while a user reads the screen or thinks about the next command; those need
 * keepalive, which distinguishes "idle but alive" from "gone". Different
 * mechanisms, each already chosen correctly for its protocol, so neither is
 * retuned here.
 */
#define NET_KEEPALIVE_IDLE_SECONDS      20  /* idle before the first probe */
#define NET_KEEPALIVE_INTERVAL_SECONDS   5  /* between probes */
#define NET_KEEPALIVE_PROBE_COUNT        3  /* unacked probes -> peer is gone */

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
