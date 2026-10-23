// Host environment for route_select.h: real lwIP netif/ip4_addr headers, a
// hand-built netif_list, and lwIP's ip4_route_src() composed from the pieces that
// decide the final route: the hook the firmware configures, lwIP's own on-link
// lookup, then netif_default.
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

struct netif *netif_list;     // lwIP declares both extern; the test owns them
struct netif *netif_default;

inline void reset_netifs() { netif_list = NULL; netif_default = NULL; }

inline void add_netif(struct netif *n, uint32_t addr, uint32_t mask, bool up, bool wireless,
                      uint32_t gw = 0)
{
    memset(n, 0, sizeof(*n));
    n->ip_addr.addr = addr;
    n->netmask.addr = mask;
    n->gw.addr = gw;
    n->flags = (up ? (NETIF_FLAG_UP | NETIF_FLAG_LINK_UP) : 0);
    n->state = wireless ? (void *)1 : NULL;
    n->next = netif_list;          // netif_add() prepends
    netif_list = n;
    if (!wireless) {
        netif_default = n;         // set_default_interface(): the wired netif is the default
    }
}

// Only the hook lines of the firmware's lwipopts.h, extracted by the Makefile:
// that file is not includable on the host. Deleting the define from the firmware
// stops this from compiling.
#include "fw_hook_opts.h"
#ifndef LWIP_HOOK_IP4_ROUTE_SRC
#error "software/network/config/lwipopts.h no longer defines LWIP_HOOK_IP4_ROUTE_SRC"
#endif
// The header lwIP includes for the hook, and so the prototype the firmware is
// held to by the definition below.
#include LWIP_HOOK_FILENAME

#include "../route_select.h"

// network_interface.cc's adapter, with a test marker for "wireless".
extern "C" struct netif *ultimate_ip4_route_src(const ip4_addr_t *src, const ip4_addr_t *dest)
{
    return route_select(src, dest, test_is_wireless);
}

#ifdef ROUTE_SELECT_LEGACY
#undef LWIP_HOOK_IP4_ROUTE_SRC
#define LWIP_HOOK_IP4_ROUTE_SRC(src, dest) ((void)(src), (void)(dest), (struct netif *)NULL)   // the pre-fix firmware had none
#endif

// ip4_route() and ip4_route_src() as in ip4.c (26a22151): the hook first when there
// is a source, then the first up netif whose subnet holds the destination, then the
// hook again without a source, then the default netif.
inline struct netif *lwip_ip4_route(const ip4_addr_t *dest)
{
    struct netif *n;
    NETIF_FOREACH(n) {
        if (netif_is_up(n) && netif_is_link_up(n) && !ip4_addr_isany_val(*netif_ip4_addr(n)) &&
            ip4_addr_net_eq(dest, netif_ip4_addr(n), netif_ip4_netmask(n))) {
            return n;
        }
    }
    n = LWIP_HOOK_IP4_ROUTE_SRC(NULL, dest);
    if (n != NULL) {
        return n;
    }
    if (netif_default == NULL || !netif_is_up(netif_default) || !netif_is_link_up(netif_default) ||
        ip4_addr_isany_val(*netif_ip4_addr(netif_default))) {
        return NULL;
    }
    return netif_default;
}

// The interface a packet from `src` to `dest` leaves by. src 0 is an unbound socket.
inline struct netif *route(const ip4_addr_t *src, const ip4_addr_t *dest)
{
    struct netif *n = LWIP_HOOK_IP4_ROUTE_SRC(src, dest);
    return n != NULL ? n : lwip_ip4_route(dest);
}

#endif
