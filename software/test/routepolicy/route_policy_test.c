/*
 * route_policy_test.c
 *
 * Host unit tests for the interface preference in
 * software/network/route_policy.c.
 *
 * This is where the combinations that matter can all be produced. On the
 * device each of them is a cable, an access point and a reboot, and the one
 * that produced the defect - Ethernet and WiFi up on one subnet at once - is
 * also the one whose symptom is silent: traffic leaves by the wrong interface
 * and everything still works, because the two links reach the same network.
 * The only thing that showed it was 45430 syslog lines arriving from an
 * address the machine's name does not resolve to.
 */

#include <stdio.h>

#include "route_policy.h"

static int checks = 0;
static int failures = 0;

static void check(int ok, const char *what)
{
    checks++;
    if (ok) {
        printf("ok   %s\n", what);
    } else {
        failures++;
        printf("FAIL %s\n", what);
    }
}

/* 192.168.1.x/24, in the byte order lwIP stores an address in on a little
 * endian machine. The decision only masks and compares, so the order is not
 * something it can be wrong about; these are written the way a reader of a
 * packet capture would read them. */
static uint32_t address(int a, int b, int c, int d)
{
    return ((uint32_t)a << 24) | ((uint32_t)b << 16)
           | ((uint32_t)c << 8) | (uint32_t)d;
}

#define ETHERNET 0
#define WIFI     1

static void wired_and_wireless(RouteCandidate *out, int ethernet_up,
                               int wifi_up)
{
    out[ETHERNET].address = address(192, 168, 1, 15);
    out[ETHERNET].netmask = address(255, 255, 255, 0);
    out[ETHERNET].usable = ethernet_up;
    out[ETHERNET].preference = ROUTE_PREFERENCE_WIRED;

    out[WIFI].address = address(192, 168, 1, 71);
    out[WIFI].netmask = address(255, 255, 255, 0);
    out[WIFI].usable = wifi_up;
    out[WIFI].preference = ROUTE_PREFERENCE_WIRELESS;
}

int main(void)
{
    RouteCandidate interfaces[2];
    const uint32_t host = address(192, 168, 1, 3);
    const uint32_t elsewhere = address(10, 0, 0, 9);

    printf("-- both links up, both able to reach the destination\n");
    wired_and_wireless(interfaces, 1, 1);
    check(route_choose(interfaces, 2, 0, host) == ETHERNET,
          "ordinary outbound traffic prefers the wired interface");

    printf("-- the order the interfaces are listed in is not the policy\n");
    {
        /* lwIP's own list is the reverse of the registration order, so the
         * wireless interface is first in it. The answer must not change. */
        RouteCandidate reversed[2];
        reversed[0] = interfaces[WIFI];
        reversed[1] = interfaces[ETHERNET];
        check(route_choose(reversed, 2, 0, host) == 1,
              "listing WiFi first still selects the wired interface");
    }

    printf("-- one link at a time\n");
    wired_and_wireless(interfaces, 0, 1);
    check(route_choose(interfaces, 2, 0, host) == WIFI,
          "with Ethernet down the traffic goes over WiFi");
    wired_and_wireless(interfaces, 1, 0);
    check(route_choose(interfaces, 2, 0, host) == ETHERNET,
          "with WiFi down the traffic goes over Ethernet");
    wired_and_wireless(interfaces, 1, 1);
    check(route_choose(interfaces, 2, 0, host) == ETHERNET,
          "and restoring Ethernet restores the preference");

    printf("-- an interface that cannot reach the destination is not chosen\n");
    wired_and_wireless(interfaces, 1, 1);
    interfaces[ETHERNET].address = address(172, 16, 4, 15);
    check(route_choose(interfaces, 2, 0, host) == WIFI,
          "a preferred interface on another subnet does not take the traffic");
    wired_and_wireless(interfaces, 1, 1);
    check(route_choose(interfaces, 2, 0, elsewhere) == -1,
          "a destination neither can reach is left to the stack");

    printf("-- an interface with no address is not usable\n");
    wired_and_wireless(interfaces, 1, 1);
    interfaces[ETHERNET].address = 0;
    check(route_choose(interfaces, 2, 0, host) == WIFI,
          "an interface still waiting for DHCP does not take the traffic");

    printf("-- a bound source address is the answer, not a hint\n");
    wired_and_wireless(interfaces, 1, 1);
    check(route_choose(interfaces, 2, address(192, 168, 1, 71), host) == WIFI,
          "a socket bound to the WiFi address sends over WiFi");
    check(route_choose(interfaces, 2, address(192, 168, 1, 15), host) == ETHERNET,
          "and one bound to the Ethernet address sends over Ethernet");
    check(route_choose(interfaces, 2, address(192, 168, 9, 9), host) == -1,
          "a source no interface holds is left to the stack");
    wired_and_wireless(interfaces, 0, 1);
    check(route_choose(interfaces, 2, address(192, 168, 1, 15), host) == -1,
          "a source held by an interface that is down is left to the stack");

    printf("-- a stack with no preference registered behaves as lwIP does\n");
    wired_and_wireless(interfaces, 1, 1);
    interfaces[ETHERNET].preference = ROUTE_PREFERENCE_NONE;
    interfaces[WIFI].preference = ROUTE_PREFERENCE_NONE;
    check(route_choose(interfaces, 2, 0, host) == ETHERNET,
          "with no preference the first interface that can reach it wins");

    printf("-- nothing to choose from\n");
    check(route_choose(interfaces, 0, 0, host) == -1,
          "no interfaces means no opinion");
    check(route_choose(0, 2, 0, host) == -1,
          "no list means no opinion");

    printf("\n%d checks, %d failed\n", checks, failures);
    return failures ? 1 : 0;
}
