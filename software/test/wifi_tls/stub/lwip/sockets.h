// Host stand-in for lwIP's sockets.h: u16_t and the socket types that rpc_calls.h needs.
#ifndef LWIP_SOCKETS_H_STUB
#define LWIP_SOCKETS_H_STUB
#include <stdint.h>
#include <stddef.h>
#include <sys/types.h>
#include <sys/time.h>

typedef uint16_t u16_t;
typedef uint32_t u32_t;
typedef u32_t socklen_t_stub;

struct in_addr_stub { uint32_t s_addr; };
struct sockaddr_stub { uint8_t sa_len; uint8_t sa_family; char sa_data[14]; };
struct sockaddr_in_stub { uint8_t sin_len; uint8_t sin_family; uint16_t sin_port; struct in_addr_stub sin_addr; char sin_zero[8]; };
#define sockaddr    sockaddr_stub
#define sockaddr_in sockaddr_in_stub
#define in_addr     in_addr_stub

#define AF_INET      2
#define SOCK_STREAM  1
#define SOL_SOCKET   0xfff
#define SO_SNDTIMEO  0x1005
#define SO_RCVTIMEO  0x1006
#define MSG_DONTWAIT 0x08
#define FIONBIO      0x8004667e
#define PP_HTONS(x)  ((uint16_t)((((x) & 0xff) << 8) | (((x) & 0xff00) >> 8)))

int lwip_socket(int domain, int type, int protocol);
int lwip_connect(int s, const struct sockaddr *name, socklen_t_stub namelen);
int lwip_ioctl(int s, long cmd, void *argp);
int lwip_setsockopt(int s, int level, int optname, const void *optval, socklen_t_stub optlen);
ssize_t lwip_send(int s, const void *dataptr, size_t size, int flags);
ssize_t lwip_recv(int s, void *mem, size_t len, int flags);
int lwip_close(int s);
#endif
