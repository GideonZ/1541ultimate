#ifndef LWIP_INET_H_STUB
#define LWIP_INET_H_STUB
#include "lwip/sockets.h"
int fake_inet_aton(const char *cp, struct in_addr *addr);
#define inet_aton(cp, addr) fake_inet_aton(cp, (struct in_addr *)(addr))
#endif
