// Host environment for route_select.h: lwIP's own ip4.c, netif.c and udp.c (compiled
// by the Makefile with the hook the firmware configures) over a hand-built
// netif_list, so what the tests assert is what lwIP's ip4_route_src() and
// udp_sendto() do with the firmware's hook, not a re-statement of them.
#ifndef ROUTE_SELECT_ENV_H
#define ROUTE_SELECT_ENV_H

extern "C" {
#include "lwip/netif.h"
#include "lwip/ip4.h"
#include "lwip/ip4_addr.h"
#include "lwip/udp.h"
#include "lwip/pbuf.h"
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

extern "C" u32_t sys_now(void) { return 0; }

// The netif a datagram last left by, recorded by every netif's output callback.
inline struct netif *&last_output() { static struct netif *n; return n; }

extern "C" err_t test_netif_output(struct netif *n, struct pbuf *, const ip4_addr_t *)
{
    last_output() = n;
    return ERR_OK;
}

inline void reset_netifs() { netif_list = NULL; netif_default = NULL; }

// `up` is netif_set_up()/netif_set_down(), which is all the firmware's link_down() does
// (NetworkInterface::link_down()): NETIF_FLAG_LINK_UP is set once at init and stays. `link`
// is there to model a netif that lost its link, which the firmware never does.
inline void add_netif(struct netif *n, uint32_t addr, uint32_t mask, bool up, bool wireless,
                      uint32_t gw = 0, bool link = true)
{
    memset(n, 0, sizeof(*n));
    n->ip_addr.addr = addr;
    n->netmask.addr = mask;
    n->gw.addr = gw;
    n->flags = (up ? NETIF_FLAG_UP : 0) | (link ? NETIF_FLAG_LINK_UP : 0);
    n->mtu = 1500;
    n->output = test_netif_output;
    n->state = wireless ? (void *)1 : NULL;
    n->next = netif_list;          // netif_add() prepends
    netif_list = n;
    if (!wireless) {
        netif_default = n;         // set_default_interface(): the wired netif is the default
    }
}

// The hook lines of the firmware's lwipopts.h reach lwIP through lwip_host/lwipopts.h
// (fw_hook_opts.h, extracted by the Makefile); deleting the define from the firmware
// stops the tests from building.
#ifndef ROUTE_SELECT_LEGACY
#ifndef LWIP_HOOK_IP4_ROUTE_SRC
#error "software/network/config/lwipopts.h no longer defines LWIP_HOOK_IP4_ROUTE_SRC"
#endif
#endif

#include "../route_select.h"

// network_interface.cc's adapter, with a test marker for "wireless".
extern "C" struct netif *ultimate_ip4_route_src(const ip4_addr_t *src, const ip4_addr_t *dest)
{
    return route_select(src, dest, test_is_wireless);
}

// The interface lwIP's ip4_route_src() picks for a packet from `src` to `dest`. A
// pointer to 0.0.0.0 is what every socket with no local address passes (ip_route()
// hands over &pcb->local_ip); a NULL pointer is what a plain ip4_route() caller
// passes (etharp_add_static_entry(); nothing else in lwIP once the hook is
// configured), and that never reaches the hook before the on-link lookup.
inline struct netif *route(const ip4_addr_t *src, const ip4_addr_t *dest)
{
    (void)src;                     // the legacy build's ip4_route_src() is ip4_route(dest)
    return ip4_route_src(src, dest);
}

#endif
