/*
 * confighook_test.cc
 *
 * Host test: does changing "Use DHCP" on its own reach effectuate_settings?
 *
 * ConfigItem :: setChanged (config.cc) marks a store for effectuate only for an item
 * WITHOUT a change hook; a hooked item is expected to apply its change itself. The hook of
 * "Use DHCP" only enables or disables the static address fields, so a change of that item
 * alone was never applied: not when the menu closed, not after a REST PUT (the menu calls
 * at_close_config, route_configs.cc:160/315/406 does the same), until some other item of
 * the store changed or the store was reloaded.
 *
 * This builds the real config.cc, with the hook (NetworkInterface :: dhcp_change) cut out of
 * network_interface.cc as it is. A change goes through ConfigItem :: setValue, as REST does
 * (route_configs.cc, set_item), then the store is closed as the menu and REST do.
 */
#include <stdio.h>
#include <string.h>

#include "config.h"

// the firmware's small_printf writes through this
extern "C" void outbyte(int c) { fputc(c, stdout); }

// ---------------------------------------------------------------- stand-ins
#define CFG_NET_DHCP_EN     0xE0
#define CFG_NET_IP          0xE1
#define CFG_NET_NETMASK     0xE2
#define CFG_NET_GATEWAY     0xE3
#define CFG_NET_DNS         0xE4
#define CFG_OTHER_HOOKED    0xE5
#define CFG_OTHER_PLAIN     0xE6

class NetworkInterface {
public:
    static int dhcp_change(ConfigItem *it);
};

static int checks = 0;
static int failures = 0;
#define CHECK(cond, ...) do { checks++; if (cond) { printf("ok   %s\n", #cond); } else { \
    failures++; printf("FAIL %s:%d %s  ", __FILE__, __LINE__, #cond); printf(__VA_ARGS__); printf("\n"); } } while (0)

#include "under_test.inc"

// ---------------------------------------------------------------- the store
static t_cfg_definition defs[] = {
    { CFG_NET_DHCP_EN,  CFG_TYPE_ENUM,   "Use DHCP",       "%s", en_dis, 0, 1, 1 },
    { CFG_NET_IP,       CFG_TYPE_STRING, "Static IP",      "%s", NULL,   7, 16, (long)"192.168.2.64" },
    { CFG_NET_NETMASK,  CFG_TYPE_STRING, "Static Netmask", "%s", NULL,   7, 16, (long)"255.255.255.0" },
    { CFG_NET_GATEWAY,  CFG_TYPE_STRING, "Static Gateway", "%s", NULL,   7, 16, (long)"192.168.2.1" },
    { CFG_NET_DNS,      CFG_TYPE_STRING, "Static DNS",     "%s", NULL,   7, 16, (long)"8.8.8.8" },
    // two other items: one with a hook that applies its own change (as most hooks do), one plain
    { CFG_OTHER_HOOKED, CFG_TYPE_ENUM,   "Hooked",         "%s", en_dis, 0, 1, 0 },
    { CFG_OTHER_PLAIN,  CFG_TYPE_ENUM,   "Plain",          "%s", en_dis, 0, 1, 0 },
    { CFG_TYPE_END,     CFG_TYPE_END,    "", "", NULL, 0, 0, 0 } };

static int g_applied;        // effectuate_settings calls
static int g_hook_applied;   // the other hook applying its own change

class Applier : public ConfigurableObject {
public:
    void effectuate_settings(void) override { g_applied++; }
};

static int other_hook(ConfigItem *)
{
    g_hook_applied++;    // applies the change itself, asks for nothing else
    return 0;
}

static void close_store(ConfigStore *st)      // what the menu and REST do when they are done
{
    st->at_close_config();
}

int main()
{
    Applier obj;
    ConfigStore *st = new ConfigStore(NULL, "Test Settings", defs, &obj);
    st->set_change_hook(CFG_NET_DHCP_EN, NetworkInterface :: dhcp_change);
    st->set_change_hook(CFG_OTHER_HOOKED, other_hook);
    NetworkInterface :: dhcp_change(st->find_item(CFG_NET_DHCP_EN));   // as attach_config does

    ConfigItem *dhcp = st->find_item(CFG_NET_DHCP_EN);
    close_store(st);                       // settled: a new store starts stale
    g_applied = 0;
    CHECK(!st->need_effectuate(), "store still stale after close");

    // control: a plain item marks the store, and closing it applies the settings
    st->find_item(CFG_OTHER_PLAIN)->setValue(1);
    CHECK(st->need_effectuate(), "plain item did not mark the store");
    close_store(st);
    CHECK(g_applied == 1, "plain item change: effectuate_settings called %d times, wanted 1", g_applied);

    // the defect: DHCP off, on its own
    g_applied = 0;
    dhcp->setValue(0);
    CHECK(st->need_effectuate(), "DHCP off alone: store not marked for effectuate");
    CHECK(st->find_item(CFG_NET_IP)->isEnabled(), "static IP not enabled with DHCP off");
    close_store(st);
    CHECK(g_applied == 1, "DHCP off alone: effectuate_settings called %d times, wanted 1", g_applied);

    // and back on, on its own
    g_applied = 0;
    dhcp->setValue(1);
    CHECK(st->need_effectuate(), "DHCP on alone: store not marked for effectuate");
    CHECK(!st->find_item(CFG_NET_IP)->isEnabled(), "static IP still enabled with DHCP on");
    close_store(st);
    CHECK(g_applied == 1, "DHCP on alone: effectuate_settings called %d times, wanted 1", g_applied);

    // unchanged behaviour: another hooked item that applies itself asks for nothing more
    g_applied = 0; g_hook_applied = 0;
    st->find_item(CFG_OTHER_HOOKED)->setValue(1);
    CHECK(g_hook_applied == 1, "other hook ran %d times, wanted 1", g_hook_applied);
    CHECK(!st->need_effectuate(), "other hooked item marked the store for effectuate");
    close_store(st);
    CHECK(g_applied == 0, "other hooked item: effectuate_settings called %d times, wanted 0", g_applied);

    printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
