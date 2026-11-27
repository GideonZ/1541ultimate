// Interface selection with Ethernet and WiFi on one subnet. lwIP's own lookup
// returns the first netif in netif_list whose subnet contains the destination,
// and netif_add() puts the newest at the head, so the WiFi netif (added after
// the wired one) took replies for the Ethernet address and every outbound
// connect. Every case asserts the interface a packet finally leaves by, from lwIP's
// own ip4_route_src() (ip4.c, built by the Makefile) with the hook that
// software/network/config/lwipopts.h configures; the build stops if that file no
// longer does.
//
// ROUTE_SELECT_LEGACY builds the same cases against lwIP built without the hook,
// which is the lookup before the fix.
//
// The hook is reached through ip4_route_src() only when the source is a pointer
// (src != NULL), also when it points to 0.0.0.0. A socket with no local address
// passes such a pointer (ip_route(&pcb->local_ip, ...)), and the "no source yet"
// preference for Ethernet applies to those. A NULL source (a plain ip4_route(),
// which lwIP itself calls only from etharp_add_static_entry()) takes lwIP's
// on-link lookup first, which the hook does not change.

#include "../../usb/tests/host_test/host_test.h"
#include "route_select_env.h"

namespace {

const uint32_t kEth  = ip(10, 43, 23, 81);
const uint32_t kWifi = ip(10, 43, 23, 83);
const uint32_t kPeer = ip(10, 43, 23, 182);
const uint32_t kMask = ip(255, 255, 255, 0);

// netif_list as on the device: Ethernet added first, WiFi added later, so WiFi is the head
// (eth_head: the other order, after the WiFi netif was added first). A netif that is not up
// is down as the firmware leaves it: UP cleared, LINK_UP still set, address kept.
struct Rig {
    struct netif eth, wifi;
    Rig(bool eth_up = true, bool wifi_up = true, bool eth_head = false)
    {
        reset_netifs();
        if (eth_head) {
            add_netif(&wifi, kWifi, kMask, wifi_up, true);
            add_netif(&eth, kEth, kMask, eth_up, false);
        } else {
            add_netif(&eth, kEth, kMask, eth_up, false);
            add_netif(&wifi, kWifi, kMask, wifi_up, true);
        }
    }
};

struct netif *pick(uint32_t src, uint32_t dest)
{
    ip4_addr_t s, d;
    s.addr = src;
    d.addr = dest;
    return route(&s, &d);
}

// A NULL source pointer, as ip4_route() callers pass it, as opposed to a pointer to 0.0.0.0.
struct netif *pick_null_source(uint32_t dest)
{
    ip4_addr_t d;
    d.addr = dest;
    return ip4_route_src(NULL, &d);
}

// udp_sendto() from a socket bound to `local` (0: unbound) to `dest`, through lwIP's own
// udp.c and ip4.c. Returns lwIP's error code; *out is the netif the datagram left by,
// NULL when it did not leave (or was looped back).
err_t udp_send_from(uint32_t local, uint32_t dest, struct netif **out)
{
    ip4_addr_t l, d;
    l.addr = local;
    d.addr = dest;
    struct udp_pcb *pcb = udp_new();
    struct pbuf *p = pbuf_alloc(PBUF_TRANSPORT, 8, PBUF_RAM);
    last_output() = NULL;
    err_t err = udp_bind(pcb, &l, 4000);
    if (err == ERR_OK) {
        err = udp_sendto(pcb, p, &d, 4001);
    }
    *out = last_output();
    pbuf_free(p);
    udp_remove(pcb);
    return err;
}

} // namespace

TEST(Route, ReplyToTheEthernetAddressLeavesByEthernet)
{
    Rig r;
    EXPECT_TRUE(pick(kEth, kPeer) == &r.eth);
}

TEST(Route, ReplyToTheWifiAddressLeavesByWifi)
{
    Rig r;
    EXPECT_TRUE(pick(kWifi, kPeer) == &r.wifi);
}

TEST(Route, ConnectWithNoSourcePrefersEthernet)
{
    Rig r;
    EXPECT_TRUE(pick(0, kPeer) == &r.eth);
}

TEST(Route, EthernetWinsWhateverTheListOrder)
{
    Rig r;
    // WiFi re-added after a reconnect of the module is still the head; so is the reverse.
    reset_netifs();
    add_netif(&r.wifi, kWifi, kMask, true, true);
    add_netif(&r.eth, kEth, kMask, true, false);
    EXPECT_TRUE(pick(0, kPeer) == &r.eth);
}

TEST(Route, WifiOnlyWhenEthernetIsDown)
{
    Rig r(false, true);
    EXPECT_TRUE(pick(0, kPeer) == &r.wifi);
}

TEST(Route, EthernetOnlyWhenWifiIsDown)
{
    Rig r(true, false);
    EXPECT_TRUE(pick(0, kPeer) == &r.eth);
}

TEST(Route, DifferentSubnetsStayOnTheirOwn)
{
    Rig r;
    EXPECT_TRUE(pick(0, ip(10, 43, 23, 5)) == &r.eth);
    // move WiFi to another subnet
    reset_netifs();
    add_netif(&r.eth, kEth, kMask, true, false);
    add_netif(&r.wifi, ip(192, 168, 7, 9), kMask, true, true);
    EXPECT_TRUE(pick(0, ip(192, 168, 7, 1)) == &r.wifi);
    EXPECT_TRUE(pick(0, kPeer) == &r.eth);
}

TEST(Route, OffLinkAndUnknownSourceAreLeftToLwip)
{
    Rig r;
    EXPECT_TRUE(pick(0, ip(8, 8, 8, 8)) == &r.eth);             // netif_default
    EXPECT_TRUE(pick(ip(10, 43, 23, 99), kPeer) == &r.wifi);    // not our address: lwIP's lookup
}

TEST(Route, ReplyFromADownInterfaceIsNotSteered)
{
    Rig r(false, true);
    EXPECT_TRUE(pick(kEth, kPeer) == &r.wifi);
}

TEST(Route, ConnectionToAnOwnAddressStaysWithThatInterface)
{
    Rig r;
    EXPECT_TRUE(pick(0, kWifi) == &r.wifi);        // lwIP loops it back from there
    EXPECT_TRUE(pick(0, kEth) == &r.eth);
}

// A socket bound to one of the device's addresses can send to the device's other
// address, in either list order: udp_sendto_if() returns ERR_RTE when the netif it is
// given does not own the socket's local address (udp.c, the IPv4 check in
// udp_sendto_if_src_chksum()), so a different explicit source has to reach the rule that
// picks the source's owner.
TEST(Route, UdpBoundToEitherAddressSendsToTheOtherInEitherListOrder)
{
    for (int eth_head = 0; eth_head < 2; eth_head++) {
        Rig r(true, true, eth_head != 0);
        struct netif *out;
        EXPECT_TRUE(udp_send_from(kWifi, kEth, &out) == ERR_OK);
        EXPECT_TRUE(out == &r.wifi);
        EXPECT_TRUE(udp_send_from(kEth, kWifi, &out) == ERR_OK);
        EXPECT_TRUE(out == &r.eth);
    }
}

// Both ends the same address, or no source: the datagram is looped back, not sent.
TEST(Route, UdpToAnOwnAddressIsLoopedBackInEitherListOrder)
{
    for (int eth_head = 0; eth_head < 2; eth_head++) {
        Rig r(true, true, eth_head != 0);
        struct netif *out;
        EXPECT_TRUE(udp_send_from(kEth, kEth, &out) == ERR_OK && out == NULL);
        EXPECT_TRUE(udp_send_from(kWifi, kWifi, &out) == ERR_OK && out == NULL);
        EXPECT_TRUE(udp_send_from(0, kWifi, &out) == ERR_OK && out == NULL);
        EXPECT_TRUE(udp_send_from(0, kEth, &out) == ERR_OK && out == NULL);
    }
}

// What follows for a connection (TCP checks nothing about the local address): between the
// device's two addresses the route is the source's owner's netif, in either list order, and
// the segment goes on the wire because the destination is not that netif's own address. On
// master the route was the list head, which looped it back when the head owned the
// destination (.81 to .83 with WiFi first, .83 to .81 with Ethernet first).
TEST(Route, AnExplicitSourceDecidesTheRouteToTheOtherOwnAddress)
{
    for (int eth_head = 0; eth_head < 2; eth_head++) {
        Rig r(true, true, eth_head != 0);
        EXPECT_TRUE(pick(kWifi, kEth) == &r.wifi);
        EXPECT_TRUE(pick(kEth, kWifi) == &r.eth);
    }
}

// Rule 2 must not make a reply worse than lwIP's lookup: a netif without a gateway
// cannot send to an off-link client (ARP has nobody to ask), while the default
// netif can.
TEST(Route, OffLinkReplyFromANetifWithoutGatewayStaysWithLwip)
{
    Rig r;
    EXPECT_TRUE(pick(kWifi, ip(8, 8, 8, 8)) == &r.eth);        // netif_default, as before
}

TEST(Route, OffLinkReplyFromANetifWithAGatewayLeavesByIt)
{
    Rig r;
    reset_netifs();
    add_netif(&r.eth, kEth, kMask, true, false, ip(10, 43, 23, 1));
    add_netif(&r.wifi, kWifi, kMask, true, true, ip(10, 43, 23, 1));
    EXPECT_TRUE(pick(kWifi, ip(8, 8, 8, 8)) == &r.wifi);
    EXPECT_TRUE(pick(kEth, ip(8, 8, 8, 8)) == &r.eth);
}

TEST(Route, EthernetWithoutAnAddressYetDoesNotTakeTraffic)
{
    Rig r;
    reset_netifs();
    add_netif(&r.eth, 0, 0, true, false);          // up, DHCP has not answered: address and mask 0
    add_netif(&r.wifi, kWifi, kMask, true, true);
    EXPECT_TRUE(pick(0, kPeer) == &r.wifi);
    EXPECT_TRUE(pick(kWifi, kPeer) == &r.wifi);
}

// The guards on the netif being up, with the netif down as the firmware leaves it (UP
// cleared, LINK_UP kept, address kept): a down WiFi's address must not be steered to.
TEST(Route, TheAddressOfADownNetifIsNotSteeredTo)
{
    Rig r(true, false);
    EXPECT_TRUE(pick(0, kWifi) == &r.eth);          // lwIP's lookup: Ethernet is on that subnet
    EXPECT_TRUE(pick(kEth, kWifi) == &r.eth);
    EXPECT_TRUE(pick(kWifi, kPeer) == &r.eth);      // a reply from a down netif's kept address
}

TEST(Route, ANetifWithoutLinkIsNotSteeredTo)
{
    Rig r;
    reset_netifs();
    add_netif(&r.eth, kEth, kMask, true, false);
    add_netif(&r.wifi, kWifi, kMask, true, true, 0, false);     // up, link lost
    EXPECT_TRUE(pick(0, kWifi) == &r.eth);
    EXPECT_TRUE(pick(kWifi, kPeer) == &r.eth);
    EXPECT_TRUE(pick(0, kPeer) == &r.eth);
}

// pick(0, ...) is a pointer to 0.0.0.0, the source of an unbound socket; a NULL source
// pointer never reaches the hook before lwIP's on-link lookup, which returns the
// first netif on the subnet, here the WiFi one. The preference for Ethernet is for the
// former, which is where every UDP and TCP send of the device comes from.
TEST(Route, ANullSourceTakesLwipsOnLinkLookupNotThePreference)
{
    Rig r;
    EXPECT_TRUE(pick(0, kPeer) == &r.eth);
    EXPECT_TRUE(pick_null_source(kPeer) == &r.wifi);
    EXPECT_TRUE(pick_null_source(kEth) == &r.wifi);
}
