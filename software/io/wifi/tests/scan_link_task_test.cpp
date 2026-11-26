/*
 * scan_link_task_test.cpp
 *
 * A scan (EVENT_RESCAN, as opening the AP list sends) does not take the WiFi interface
 * down: while Connected the interface stays up and the task is Connected again after the
 * scan (no packet goes out while it scans: wifi_tx_packet sends only when Connected). A
 * connect or disconnect from the module that is queued behind the scan is taken after it,
 * and the state before the scan (not connected, Disabled) is the state after it. The stub
 * scan returns at once, so "during" means "queued behind the scan".
 */

#include "wifi_task_test.h"

// Connected (the module was associated at boot), then a rescan
TEST(WifiScanLinkTask, connected_scan_keeps_the_link)
{
    reset_world();
    world.enabled = true;
    world.link = 1;
    boot({ EVENT_RESCAN });
    CHECK(strchr(after('U'), 'C') != NULL, "linked up, then the scan");
    CHECK(strchr(after('U'), 'u') == NULL, "the interface is not taken down");
    CHECK(wifi.getState() == eWifi_Connected, "still Connected");
}

// The module reports a lost link behind the scan, then gets it back: the task follows
TEST(WifiScanLinkTask, disconnect_behind_the_scan)
{
    reset_world();
    world.enabled = true;
    world.link = 1;
    boot({ EVENT_RESCAN, EVENT_DISCONNECTED });
    CHECK(strchr(after('C'), 'u') != NULL, "the disconnect takes the interface down");
    CHECK(wifi.getState() == eWifi_NotConnected, "not connected");
    boot({ EVENT_RESCAN, EVENT_DISCONNECTED, EVENT_CONNECTED });
    CHECK(strchr(after('u'), 'U') != NULL, "the reconnect brings it up again");
    CHECK(wifi.getState() == eWifi_Connected, "Connected");
}

// Not connected before the scan: not connected after it
TEST(WifiScanLinkTask, not_connected_scan)
{
    reset_world();
    world.enabled = true;
    boot({ EVENT_RESCAN });
    CHECK(strchr(rlog, 'C') != NULL, "a scan");
    CHECK(wifi.getState() == eWifi_NotConnected, "not connected");
}

// Disabled before the scan: the task's state is Disabled after it
TEST(WifiScanLinkTask, disabled_scan)
{
    reset_world();
    world.enabled = true;
    boot({ EVENT_DISABLED, EVENT_RESCAN });
    CHECK(wifi.getState() == eWifi_Disabled, "still Disabled");
}
