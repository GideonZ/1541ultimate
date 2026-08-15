/*
 * route_policy.h
 *
 * Which interface ordinary outbound traffic leaves by when more than one can
 * reach the destination.
 *
 * lwIP answers that question with the first interface in netif_list whose
 * masked address matches the destination (ip4_route in
 * software/lwip/src/core/ipv4/ip4.c). netif_add prepends, so netif_list is
 * the reverse of the order the interfaces were registered in, and the
 * registration order is a consequence of task timing: the wired interface is
 * added by the RMII driver task during init, and the WiFi interface is added
 * later still, when the ESP32 reports that it has associated. On a machine
 * whose Ethernet and WiFi are on one subnet that makes WiFi the outbound
 * interface for everything, which is how 45430 syslog lines from an Ultimate
 * 64 arrived from its WiFi address while its hostname and its REST surface
 * resolved to its Ethernet address.
 *
 * netif_default does not decide this. ip4_route reads it only after the list
 * walk has failed to match anything, so the firmware's own Ethernet-first
 * choice in NetworkInterface::set_default_interface never applied to a
 * destination both interfaces can reach.
 *
 * The policy is therefore stated here, as a preference per interface, and
 * applied at route time through lwIP's LWIP_HOOK_IP4_ROUTE_SRC. It is a
 * preference and not an exclusion: every interface stays usable, an interface
 * that is down or has no address is not chosen, and an interface that cannot
 * reach the destination is not chosen, so WiFi carries the traffic whenever
 * Ethernet cannot and stops carrying it as soon as Ethernet can again.
 *
 * No lwIP types appear here on purpose: this is the decision, and
 * lwip_route_hook.c is the part that reads a netif. That split is what lets
 * the decision be tested on the build host, which is the only place the
 * combinations that matter can all be produced.
 */

#ifndef ROUTE_POLICY_H
#define ROUTE_POLICY_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Higher wins. Wired Ethernet is preferred because it is the link the device
 * is tested and administered over, it is the one whose address the machine's
 * name resolves to, and it is not shared with everything else on the band. */
#define ROUTE_PREFERENCE_WIRED     20
#define ROUTE_PREFERENCE_WIRELESS  10
/* What an interface that has not declared one counts as. Every interface
 * having the same preference reproduces lwIP's own answer exactly, which is
 * what a build with no policy registered gets. */
#define ROUTE_PREFERENCE_NONE       0

/* One interface, as the decision needs to see it. Addresses are in the byte
 * order lwIP stores them in; the decision only ever masks and compares them,
 * so it never has to know which that is. */
typedef struct {
    uint32_t address;
    uint32_t netmask;
    /* Up, with a link, and holding an address. */
    int usable;
    int preference;
} RouteCandidate;

/*
 * Which of `candidates` outbound traffic to `destination` should leave by, as
 * an index, or -1 for "no opinion", which leaves lwIP to answer as it always
 * did.
 *
 * `source` is the local address the caller has already bound, or 0 for none.
 * A bound source is an answer rather than a hint: the caller has chosen, and
 * sending from that address out of a different interface is what makes a
 * reply unroutable. So a non-zero source selects the interface that holds it,
 * and selects nothing at all when no usable interface holds it.
 *
 * With no bound source the highest preference among the interfaces that can
 * reach the destination wins, and the earliest of them wins a tie, so the
 * result is stable for one set of interfaces.
 */
int route_choose(const RouteCandidate *candidates, int count,
                 uint32_t source, uint32_t destination);

#ifdef __cplusplus
}
#endif

#endif /* ROUTE_POLICY_H */
