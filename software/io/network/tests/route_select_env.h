// Host environment for route_select.h: real lwIP netif/ip4_addr headers, a
// hand-built netif_list, and either route_select() or the pre-fix lookup.
#ifndef ROUTE_SELECT_ENV_H
#define ROUTE_SELECT_ENV_H

extern "C" {
#include "lwip/netif.h"
#include "lwip/ip4_addr.h"
}
#include <string.h>

inline uint32_t ip(int a, int b, int c, int d)
{
    ip4_addr_t x;
    IP4_ADDR(&x, a, b, c, d);
    return x.addr;
}

// state carries the "wireless" marker, as NetworkInterface::state does on the device
inline int test_is_wireless(const struct netif *n) { return n->state != NULL; }

struct netif *netif_list;   // lwIP declares it extern; the test owns the list

inline void reset_netifs() { netif_list = NULL; }

inline void add_netif(struct netif *n, uint32_t addr, uint32_t mask, bool up, bool wireless)
{
    memset(n, 0, sizeof(*n));
    n->ip_addr.addr = addr;
    n->netmask.addr = mask;
    n->flags = (up ? (NETIF_FLAG_UP | NETIF_FLAG_LINK_UP) : 0);
    n->state = wireless ? (void *)1 : NULL;
    n->next = netif_list;          // netif_add() prepends
    netif_list = n;
}

#ifdef ROUTE_SELECT_LEGACY
// lwIP ip4_route() as it was: first up netif in the list whose subnet holds dest.
inline struct netif *route(const ip4_addr_t *, const ip4_addr_t *dest)
{
    struct netif *n;
    NETIF_FOREACH(n) {
        if (netif_is_up(n) && netif_is_link_up(n) && !ip4_addr_isany_val(*netif_ip4_addr(n)) &&
            ip4_addr_net_eq(dest, netif_ip4_addr(n), netif_ip4_netmask(n))) {
            return n;
        }
    }
    return NULL;
}
#else
#include "../route_select.h"
inline struct netif *route(const ip4_addr_t *s, const ip4_addr_t *d)
{
    return route_select(s, d, test_is_wireless);
}
#endif

#endif
