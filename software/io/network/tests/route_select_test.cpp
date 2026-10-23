// Interface selection with Ethernet and WiFi on one subnet. lwIP's own lookup
// returns the first netif in netif_list whose subnet contains the destination,
// and netif_add() puts the newest at the head, so the WiFi netif (added after
// the wired one) took replies for the Ethernet address and every outbound
// connect. route_select() is what lwIP now asks first.
//
// ROUTE_SELECT_LEGACY builds the same cases against a model of the old lookup.
// It is a model (the first on-link netif in the list), not lwIP's code.

#include "../../usb/tests/host_test/host_test.h"
#include "route_select_env.h"

namespace {

const uint32_t kEth  = ip(10, 43, 23, 81);
const uint32_t kWifi = ip(10, 43, 23, 83);
const uint32_t kPeer = ip(10, 43, 23, 182);
const uint32_t kMask = ip(255, 255, 255, 0);

// netif_list as on the device: Ethernet added first, WiFi added later, so WiFi is the head.
struct Rig {
    struct netif eth, wifi;
    Rig(bool eth_up = true, bool wifi_up = true)
    {
        reset_netifs();
        add_netif(&eth, kEth, kMask, eth_up, false);
        add_netif(&wifi, kWifi, kMask, wifi_up, true);
    }
};

struct netif *pick(uint32_t src, uint32_t dest)
{
    ip4_addr_t s, d;
    s.addr = src;
    d.addr = dest;
    return route(&s, &d);
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

TEST(Route, OffLinkAndUnknownSourceFallBackToLwip)
{
    Rig r;
    EXPECT_TRUE(pick(0, ip(8, 8, 8, 8)) == NULL);              // lwIP picks netif_default
    EXPECT_TRUE(pick(ip(10, 43, 23, 99), kPeer) == NULL);       // not our address
}

TEST(Route, ReplyFromADownInterfaceIsNotSteered)
{
    Rig r(false, true);
    EXPECT_TRUE(pick(kEth, kPeer) == NULL);
}

TEST(Route, ConnectionToAnOwnAddressStaysWithThatInterface)
{
    Rig r;
    EXPECT_TRUE(pick(0, kWifi) == &r.wifi);        // lwIP loops it back from there
    EXPECT_TRUE(pick(kEth, kWifi) == &r.wifi);
    EXPECT_TRUE(pick(0, kEth) == &r.eth);
}
