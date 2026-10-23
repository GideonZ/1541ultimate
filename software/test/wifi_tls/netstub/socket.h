// Host stand-in for lwIP's socket API as network_target.cc and network_target_tls.cc see it (through compat/posix/sys/socket.h): the host's
// types and constants, and lwIP's calls, faked in net_tls_test.cc. The BSD names that the TCP and UDP commands use
// go to the same fakes.
#ifndef SOCKET_H_NETSTUB
#define SOCKET_H_NETSTUB
#include <stdint.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>

// lwIP's sockaddr_in has sin_len, which BSD's has and Linux's does not. network_target.cc sets it right before sin_family,
// so there it lands on sin_family, which is then set; the fakes never read it.
#ifdef __linux__
#define sin_len sin_family
#endif

#define PP_HTONS(x) ((uint16_t)((((x) & 0xff) << 8) | (((x) & 0xff00) >> 8)))

int lwip_socket(int domain, int type, int protocol);
int lwip_connect(int s, const struct sockaddr *name, socklen_t namelen);
int lwip_ioctl(int s, long cmd, void *argp);
int lwip_setsockopt(int s, int level, int optname, const void *optval, socklen_t optlen);
int lwip_getsockopt(int s, int level, int optname, void *optval, socklen_t *optlen);
ssize_t lwip_send(int s, const void *dataptr, size_t size, int flags);
ssize_t lwip_recv(int s, void *mem, size_t len, int flags);
ssize_t lwip_recvmsg(int s, struct msghdr *message, int flags);
int lwip_select(int maxfdp1, fd_set *readset, fd_set *writeset, fd_set *exceptset, struct timeval *timeout);
int lwip_close(int s);

#define socket(a, b, c)          lwip_socket(a, b, c)
#define connect(a, b, c)         lwip_connect(a, b, c)
#define setsockopt(a, b, c, d, e) lwip_setsockopt(a, b, c, d, e)
#endif
