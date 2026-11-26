#ifndef ROUTE_SELECT_H
#define ROUTE_SELECT_H

#include "lwip/netif.h"
#include "lwip/ip4_addr.h"

// Source-based IPv4 route selection, called by lwIP's ip4_route_src() before its
// own lookup when there is a source, and again from ip4_route() when nothing
// on-link matched (LWIP_HOOK_IP4_ROUTE_SRC). lwIP's lookup returns the first netif in
// netif_list whose subnet contains the destination, and netif_add() puts the
// newest netif at the head, so with Ethernet and WiFi on one subnet the WiFi
// netif, whose netif_add() comes after the wired one's, takes everything.
//
//  - A destination that is the address of an up netif, with no source or that same
//    address as source: that netif, so a connection to the device's own WiFi address
//    still loops back instead of going on the wire. An explicit different source goes on
//    to the next rule: a UDP socket bound to one address and sent to the other must get
//    the netif that owns its local address, or udp_sendto_if() returns ERR_RTE.
//  - A source address that belongs to an up netif: that netif. A reply to a
//    connection that came in on .81 leaves from .81 and does not change interface.
//    Only when the destination is on that netif's subnet or the netif has a gateway.
//  - No source yet (an unbound connect or sendto): among the up netifs whose
//    subnet contains the destination, a wired one before a wireless one.
//
// Anything else returns NULL, and lwIP carries on as before: off-link
// destinations still go to netif_default, and sockets bound with
// SO_BINDTODEVICE never get here.
// A netif that can carry traffic: up, with link and an address.
static inline int route_usable(const struct netif *n)
{
    return netif_is_up(n) && netif_is_link_up(n) && !ip4_addr_isany_val(*netif_ip4_addr(n));
}

static inline struct netif *route_select(const ip4_addr_t *src, const ip4_addr_t *dest,
                                         int (*is_wireless)(const struct netif *))
{
    const int have_src = (src != NULL) && !ip4_addr_isany(src);
    struct netif *best = NULL;
    struct netif *n;

    NETIF_FOREACH(n) {
        if (route_usable(n) && ip4_addr_eq(dest, netif_ip4_addr(n)) &&
            (!have_src || ip4_addr_eq(src, dest))) {
            return n;
        }
    }
    NETIF_FOREACH(n) {
        if (!route_usable(n)) {
            continue;
        }
        if (have_src) {
            // An off-link destination needs a gateway on that netif; without one the
            // packet would be dropped, so leave it to lwIP as before.
            if (ip4_addr_eq(src, netif_ip4_addr(n)) &&
                (ip4_addr_net_eq(dest, netif_ip4_addr(n), netif_ip4_netmask(n)) ||
                 !ip4_addr_isany_val(*netif_ip4_gw(n)))) {
                return n;
            }
            continue;
        }
        if (!ip4_addr_net_eq(dest, netif_ip4_addr(n), netif_ip4_netmask(n))) {
            continue;
        }
        if (!best || (is_wireless(best) && !is_wireless(n))) {
            best = n;
        }
    }
    return best;
}

#endif
