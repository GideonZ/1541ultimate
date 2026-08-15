/*
 * lwip_hooks.h
 *
 * What LWIP_HOOK_FILENAME points at. lwIP includes this from its own sources
 * so that a hook macro in lwipopts.h has a prototype in scope, so nothing
 * here may depend on the including file having included anything first.
 */

#ifndef LWIP_HOOKS_H
#define LWIP_HOOKS_H

#include "lwip/ip_addr.h"
#include "lwip/netif.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * The outbound interface for one datagram or segment, or NULL to leave the
 * answer to lwIP's own ip4_route. Wired Ethernet is preferred over WiFi when
 * both can reach the destination; see software/network/route_policy.h.
 *
 * Called from ip4_route_src for every packet whose route is not already
 * pinned, so it allocates nothing, takes no lock and runs on the tcpip
 * thread's stack.
 */
struct netif *ultimate_route_src(const ip4_addr_t *src, const ip4_addr_t *dest);

/*
 * Declare how this interface ranks against the others. Called once per
 * interface, after netif_add, with one of the ROUTE_PREFERENCE_ values, and
 * again with ROUTE_PREFERENCE_NONE when the interface goes away.
 *
 * An interface that never declares one counts as ROUTE_PREFERENCE_NONE, and a
 * stack where none of them has declared one produces exactly the netif_list
 * order lwIP would have used by itself.
 */
void route_hook_set_preference(struct netif *netif, int preference);

#ifdef __cplusplus
}
#endif

#endif /* LWIP_HOOKS_H */
