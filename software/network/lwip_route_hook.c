/*
 * lwip_route_hook.c
 *
 * The lwIP side of the interface preference: read the stack's netif list,
 * hand it to route_choose, and turn the answer back into a netif.
 *
 * Built into the lwIP library rather than into the application, because ip4.c
 * refers to `ultimate_route_src` and every application that links the library
 * therefore has to have it. An application that registers no preference gets
 * lwIP's own behaviour: with every interface at ROUTE_PREFERENCE_NONE the
 * decision is the first interface in netif_list that can reach the
 * destination, which is what ip4_route answers.
 */

#include "lwip/opt.h"
#include "lwip/netif.h"
#include "lwip/ip4_addr.h"

#include "lwip_hooks.h"
#include "route_policy.h"

/* Ethernet, WiFi, and room for the two USB Ethernet adapters the file manager
 * can bring up while a run is happening. An interface past this is not ranked
 * and is still reachable through lwIP's own list walk. */
#define MAX_RANKED_INTERFACES 4

static struct netif *ranked[MAX_RANKED_INTERFACES];
static int ranks[MAX_RANKED_INTERFACES];

void route_hook_set_preference(struct netif *netif, int preference)
{
    int index;
    int free_slot = -1;

    if (netif == NULL) {
        return;
    }
    for (index = 0; index < MAX_RANKED_INTERFACES; index++) {
        if (ranked[index] == netif) {
            ranks[index] = preference;
            if (preference == ROUTE_PREFERENCE_NONE) {
                ranked[index] = NULL;
            }
            return;
        }
        if (ranked[index] == NULL && free_slot < 0) {
            free_slot = index;
        }
    }
    if (preference != ROUTE_PREFERENCE_NONE && free_slot >= 0) {
        ranked[free_slot] = netif;
        ranks[free_slot] = preference;
    }
}

static int preference_of(const struct netif *netif)
{
    int index;

    for (index = 0; index < MAX_RANKED_INTERFACES; index++) {
        if (ranked[index] == netif) {
            return ranks[index];
        }
    }
    return ROUTE_PREFERENCE_NONE;
}

struct netif *ultimate_route_src(const ip4_addr_t *src, const ip4_addr_t *dest)
{
    RouteCandidate candidates[MAX_RANKED_INTERFACES];
    struct netif *found[MAX_RANKED_INTERFACES];
    struct netif *netif;
    int count = 0;
    int chosen;

    if (dest == NULL) {
        return NULL;
    }
    /* Multicast has an administratively selected interface of its own in
     * ip4_route, and the video and audio streams use it. Leaving it alone
     * keeps this change to ordinary unicast traffic. */
    if (ip4_addr_ismulticast(dest)) {
        return NULL;
    }

    NETIF_FOREACH(netif) {
        if (count >= MAX_RANKED_INTERFACES) {
            break;
        }
        found[count] = netif;
        candidates[count].address = netif_ip4_addr(netif)->addr;
        candidates[count].netmask = netif_ip4_netmask(netif)->addr;
        candidates[count].usable =
            (netif_is_up(netif) && netif_is_link_up(netif)
             && !ip4_addr_isany_val(*netif_ip4_addr(netif))) ? 1 : 0;
        candidates[count].preference = preference_of(netif);
        count++;
    }

    chosen = route_choose(candidates, count,
                          (src != NULL) ? ip4_addr_get_u32(src) : 0u,
                          ip4_addr_get_u32(dest));
    return (chosen >= 0) ? found[chosen] : NULL;
}
