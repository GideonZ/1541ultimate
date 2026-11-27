#ifndef LWIP_HOOKS_H
#define LWIP_HOOKS_H

#include "lwip/ip4_addr.h"

#ifdef __cplusplus
extern "C" {
#endif

struct netif;

/* LWIP_HOOK_IP4_ROUTE_SRC, see lwipopts.h. Implemented in io/network/network_interface.cc. */
struct netif *ultimate_ip4_route_src(const ip4_addr_t *src, const ip4_addr_t *dest);

#ifdef __cplusplus
}
#endif

#endif
