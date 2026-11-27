// Host test of how NetworkInterface (software/io/network/network_interface.cc) starts
// and stops the DHCP client. The member functions under test are cut out of that file
// as they are (extract.awk, run by the Makefile) and built against a stand-in for the
// few lwIP calls they make. The stand-ins copy the behaviour of the lwIP in
// software/lwip (src/core/ipv4/dhcp.c); each carries the line it copies.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <ctype.h>

// ---------------------------------------------------------------- lwIP stand-in
enum { DHCP_STATE_OFF = 0, DHCP_STATE_SELECTING = 6, DHCP_STATE_BOUND = 10 };
struct dhcp { uint8_t state; };
struct ip_addr_t { uint32_t addr; };
struct netif {
    void *state;
    void *dhcp_data;     // netif_get_client_data(netif, LWIP_NETIF_CLIENT_DATA_INDEX_DHCP)
    bool is_up, link_up;
    ip_addr_t ip_addr, netmask, gw;
};
typedef void (*tcpip_callback_fn)(void *);

static int g_discovers;      // DHCPDISCOVER messages sent
static int g_releases;       // DHCPRELEASE messages sent
static int g_live_clients;   // struct dhcp allocated and not freed

#define netif_dhcp_data(n) ((struct dhcp *)(n)->dhcp_data)
static bool netif_is_link_up(struct netif *n) { return n->link_up; }
static void netif_set_up(struct netif *n) { n->is_up = true; }
static void netif_set_down(struct netif *n) { n->is_up = false; }
static void netif_remove(struct netif *) { }
static void netif_set_addr(struct netif *n, ip_addr_t *ip, ip_addr_t *mask, ip_addr_t *gw)
{ n->ip_addr = *ip; n->netmask = *mask; n->gw = *gw; }

// dhcp.c:1989 dhcp_supplied_address
static bool dhcp_supplied_address(struct netif *n)
{ return n->dhcp_data && netif_dhcp_data(n)->state == DHCP_STATE_BOUND; }

// dhcp.c:810 dhcp_start: allocates the client when there is none (818-846); an existing
// client is restarted, whatever state it is in (846-856); then dhcp_discover (869).
static int dhcp_start(struct netif *n)
{
    if (!n->is_up) return -1;                          // dhcp.c:826 netif_is_up
    if (!n->dhcp_data) { n->dhcp_data = calloc(1, sizeof(struct dhcp)); g_live_clients++; }
    memset(n->dhcp_data, 0, sizeof(struct dhcp));      // dhcp.c:858
    netif_dhcp_data(n)->state = DHCP_STATE_SELECTING;  // dhcp.c:1037 dhcp_discover
    g_discovers++;                                     // dhcp.c:1039 DHCP_DISCOVER sent
    return 0;
}
// dhcp.c:1325 dhcp_release_and_stop (dhcp_stop, 1412, calls it): no client or state OFF is
// a no-op (1333-1341); a bound lease is released and the address removed (1356-1376); the
// state becomes OFF (1373, 1385) and the client STAYS attached to the netif.
static void dhcp_stop(struct netif *n)
{
    if (!n->dhcp_data) return;
    if (netif_dhcp_data(n)->state == DHCP_STATE_OFF) return;
    if (dhcp_supplied_address(n)) {
        g_releases++;
        ip_addr_t zero = {0};
        netif_set_addr(n, &zero, &zero, &zero);
    }
    netif_dhcp_data(n)->state = DHCP_STATE_OFF;
}
// dhcp.c:781 dhcp_cleanup. netif_remove (netif.c:764) does not call it.
static void dhcp_cleanup(struct netif *n)
{
    if (n->dhcp_data) { free(n->dhcp_data); g_live_clients--; n->dhcp_data = NULL; }
}
// What the stack does when a lease arrives: BOUND with an address (dhcp.c dhcp_bind)
static void server_answers(struct netif *n)
{
    netif_dhcp_data(n)->state = DHCP_STATE_BOUND;
    n->ip_addr.addr = 0x0a000007; n->netmask.addr = 0xffffff00; n->gw.addr = 0x0a000001;
}

// ---------------------------------------------------------------- firmware stand-ins
#define CFG_NET_DHCP_EN 0xE0
#define CFG_NET_IP 0xE1
#define CFG_NET_NETMASK 0xE2
#define CFG_NET_GATEWAY 0xE3
#define CFG_NET_DNS 0xE4
#define CFG_NETWORK_HOSTNAME 0x01
static int cfg_dhcp = 1;
static char cfg_hostname[24] = "ultimate";
struct FakeCfg {
    int get_value(int id) { return id == CFG_NET_DHCP_EN ? cfg_dhcp : 0; }
    const char *get_string(int id) {
        switch (id) { case CFG_NET_IP: return "10.0.0.50"; case CFG_NET_NETMASK: return "255.255.255.0";
        case CFG_NET_GATEWAY: return "10.0.0.1"; case CFG_NET_DNS: return ""; default: return cfg_hostname; }
    }
    void set_string(int, const char *) { }
};
static struct { FakeCfg *cfg; } networkConfig;
static uint32_t inet_addr(const char *s)   // dotted quad, host order is enough here
{ unsigned a, b, c, d; sscanf(s, "%u.%u.%u.%u", &a, &b, &c, &d); return (a << 24) | (b << 16) | (c << 8) | d; }
static void set_dns_server_unsafe(void *) { }
static void tcpip_callback(tcpip_callback_fn, void *) { }
struct FileManagerStub { void sendEventToObservers(int, const char *, const char *) { }
    static FileManagerStub *getFileManager() { static FileManagerStub f; return &f; } };
typedef FileManagerStub FileManager;
enum { eRefreshDirectory };

class NetworkInterface {
public:
    FakeCfg fake; FakeCfg *cfg;
    struct netif my_net_if;
    int dhcp_enable; bool if_up;
    ip_addr_t my_ip, my_netmask, my_gateway, my_dns;
    char hostname[24];
    static void registerNetworkInterface(NetworkInterface *) { }
    static void unregisterNetworkInterface(NetworkInterface *) { }
    static void set_default_interface() { }
    NetworkInterface() { cfg = &fake; networkConfig.cfg = &fake; memset(&my_net_if, 0, sizeof(my_net_if));
        my_net_if.state = this; dhcp_enable = 0; if_up = false; }
    ~NetworkInterface();
    void link_up();
    void link_down();
    void effectuate_settings();
};

// The code under test, cut from the firmware source
#include DHCP_UNDER_TEST

// ---------------------------------------------------------------- the tests
static int failures;
#define CHECK(c, ...) do { if (!(c)) { failures++; printf("FAIL %s:%d  %s  ", __FILE__, __LINE__, #c); printf(__VA_ARGS__); printf("\n"); } } while (0)

static void reset() { g_discovers = g_releases = g_live_clients = 0; cfg_dhcp = 1; }
static void bring_up(NetworkInterface &ni)   // netif added, link up, DHCP configured on
{
    ni.my_net_if.link_up = true;
    ni.effectuate_settings();   // 'dhcp_enable' read; netif not yet up, so nothing starts
    ni.link_up();               // netif up + dhcp_start
}

static void test_toggle_off_on()
{
    reset();
    NetworkInterface *ni = new NetworkInterface; bring_up(*ni);
    CHECK(g_discovers == 1, "first start sent %d discovers", g_discovers);
    server_answers(&ni->my_net_if);

    cfg_dhcp = 0; ni->effectuate_settings();            // DHCP off: static address, client stopped
    CHECK(ni->my_net_if.ip_addr.addr == 0x0a000032, "static address not applied: %08x", ni->my_net_if.ip_addr.addr);
    CHECK(g_releases == 1, "lease not released");

    cfg_dhcp = 1; ni->effectuate_settings();            // DHCP on again
    CHECK(g_discovers == 2, "DHCP off then on: %d discovers sent in all, wanted 2 (client not restarted)", g_discovers);
    CHECK(netif_dhcp_data(&ni->my_net_if) && netif_dhcp_data(&ni->my_net_if)->state != DHCP_STATE_OFF,
          "client left in state OFF after turning DHCP on");
    dhcp_cleanup(&ni->my_net_if); delete ni;
}

// What the guard from the REST fix protects: applying the same DHCP setting again must not
// stop or restart a running client (that cleared the address and aborted every connection)
static void test_reapply_keeps_running_client()
{
    reset();
    NetworkInterface *ni = new NetworkInterface; bring_up(*ni);
    server_answers(&ni->my_net_if);
    ni->effectuate_settings(); ni->effectuate_settings();
    CHECK(g_discovers == 1, "re-applying with a bound lease restarted the client: %d discovers", g_discovers);
    CHECK(g_releases == 0, "re-applying released the lease");
    CHECK(ni->my_net_if.ip_addr.addr == 0x0a000007, "address cleared by re-applying");
    dhcp_cleanup(&ni->my_net_if); delete ni;

    // and while still negotiating (SELECTING)
    reset();
    NetworkInterface *nj = new NetworkInterface; bring_up(*nj);
    nj->effectuate_settings();
    CHECK(g_discovers == 1, "re-applying during negotiation restarted it: %d discovers", g_discovers);
    dhcp_cleanup(&nj->my_net_if); delete nj;
}

static void test_link_cycle()
{
    reset();
    NetworkInterface *ni = new NetworkInterface; bring_up(*ni);
    server_answers(&ni->my_net_if);
    ni->link_down(); ni->link_up();
    CHECK(g_discovers == 2, "link down/up: %d discovers", g_discovers);
    dhcp_cleanup(&ni->my_net_if); delete ni;
}

static void test_destructor_frees_client()
{
    reset();
    NetworkInterface *ni = new NetworkInterface; bring_up(*ni);
    CHECK(g_live_clients == 1, "no client allocated");
    delete ni;
    CHECK(g_live_clients == 0, "%d DHCP client(s) leaked when the interface was removed", g_live_clients);
}

int main()
{
    test_toggle_off_on();
    test_reapply_keeps_running_client();
    test_link_cycle();
    test_destructor_frees_client();
    printf(failures ? "dhcp_restart_test: %d FAILED\n" : "dhcp_restart_test: all passed\n", failures);
    return failures ? 1 : 0;
}
