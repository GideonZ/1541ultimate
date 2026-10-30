/*
 * wifi_switch_test.cpp
 *
 * Unit test of the radio switch logic (io/wifi/wifi_switch.h): which module events the WiFi
 * application drops while it waits for the radio to go off, when the wait ends, and when
 * the start of the application applies a Disable or an Enable. The task around it is in
 * switch_task_test.cpp.
 */

#include "../../usb/tests/host_test/host_test.h"
#include <string.h>
#include "wifi_switch.h"

#if U64 != 2

// The WiFi application as far as these events go: what the link does on each event the
// application does not drop (wifi.cc, RunModeThread)
struct app {
    wifi_switch_t sw;
    bool link_up;
    bool disabled;
    int dropped;
};

static void app_start(struct app *a, bool enabled)
{
    a->link_up = false;
    a->disabled = false;
    a->dropped = 0;
    if (wifi_switch_wants_off(&a->sw, enabled)) {
        wifi_switch_asked_off(&a->sw, 1000, 100);
        a->disabled = true;
    }
}

static void event(struct app *a, uint8_t code)
{
    if (wifi_switch_drop_event(&a->sw, code)) {
        a->dropped++;
        return;
    }
    switch (code) {
    case EVENT_CONNECTED:
        a->link_up = true;
        a->disabled = false;
        break;
    case EVENT_DISCONNECTED:
        a->link_up = false;
        a->disabled = false;
        break;
    case EVENT_DISABLED:
        a->link_up = false;
        a->disabled = true;
        break;
    default:
        break;
    }
}

static void fresh(struct app *a)
{
    memset(a, 0, sizeof(*a));
}

// The module made an association at boot and reports it before it has turned off
TEST(WifiSwitch, stored_off_drops_the_boot_association)
{
    struct app a;
    fresh(&a);
    app_start(&a, false);
    EXPECT_TRUE(a.disabled);
    event(&a, EVENT_CONNECTED);
    event(&a, EVENT_GOTIP);
    EXPECT_FALSE(a.link_up);
    EXPECT_EQ(a.dropped, 2);
    event(&a, EVENT_DISABLED);
    EXPECT_TRUE(a.disabled && !a.link_up);
    // Once confirmed, the events are the module's again: an Enable brings the link up
    event(&a, EVENT_DISCONNECTED);
    event(&a, EVENT_CONNECTED);
    EXPECT_TRUE(a.link_up);
    EXPECT_EQ(a.dropped, 2);
}

TEST(WifiSwitch, stored_off_confirmed_first)
{
    struct app a;
    fresh(&a);
    app_start(&a, false);
    event(&a, EVENT_DISABLED);
    EXPECT_TRUE(a.disabled);
    event(&a, EVENT_DISCONNECTED);
    EXPECT_EQ(a.dropped, 0);
}

TEST(WifiSwitch, stored_on_drops_nothing)
{
    struct app a;
    fresh(&a);
    app_start(&a, true);
    EXPECT_FALSE(a.disabled);
    event(&a, EVENT_CONNECTED);
    event(&a, EVENT_GOTIP);
    EXPECT_TRUE(a.link_up);
    EXPECT_EQ(a.dropped, 0);
}

TEST(WifiSwitch, packets_and_internal_events_pass)
{
    struct app a;
    fresh(&a);
    app_start(&a, false);
    event(&a, EVENT_RECV_PACKET);
    event(&a, EVENT_RESCAN);
    event(&a, EVENT_KEEPALIVE);
    EXPECT_EQ(a.dropped, 0);
}

// An Enable before the module has confirmed the radio off ends the wait: the module turns
// off, then on, and the link comes up
TEST(WifiSwitch, enable_ends_the_wait_for_the_confirmation)
{
    struct app a;
    fresh(&a);
    app_start(&a, false);
    wifi_switch_on(&a.sw, true);
    EXPECT_FALSE(a.sw.awaiting_off);
    EXPECT_FALSE(a.sw.override_off);
    event(&a, EVENT_DISABLED);
    event(&a, EVENT_DISCONNECTED);
    event(&a, EVENT_CONNECTED);
    EXPECT_TRUE(a.link_up);
    EXPECT_EQ(a.dropped, 0);
}

// The module never confirmed the radio off (its stop failed). An Enable ends the wait, so
// the link can come up again.
TEST(WifiSwitch, enable_ends_an_unconfirmed_wait)
{
    struct app a;
    fresh(&a);
    app_start(&a, false);
    wifi_switch_on(&a.sw, true);
    event(&a, EVENT_CONNECTED);
    EXPECT_TRUE(a.link_up);
    EXPECT_EQ(a.dropped, 0);
}

// After the application was stopped (a module without the switch is held off as a whole),
// Enable starts it again, and that start skips the stored Disabled, once
TEST(WifiSwitch, enable_with_the_application_stopped)
{
    struct app a;
    fresh(&a);
    wifi_switch_on(&a.sw, false);
    EXPECT_TRUE(a.sw.override_off);
    app_start(&a, false);
    EXPECT_FALSE(a.disabled);
    event(&a, EVENT_CONNECTED);
    EXPECT_TRUE(a.link_up);
    app_start(&a, false);
    EXPECT_TRUE(a.disabled);
}

// An Enable on a running application asks the module; a later start still applies the
// stored Disabled
TEST(WifiSwitch, enable_on_a_running_application_asks_the_module)
{
    struct app a;
    fresh(&a);
    app_start(&a, false);
    event(&a, EVENT_DISABLED);
    wifi_switch_on(&a.sw, true);
    EXPECT_FALSE(a.sw.override_off);
    app_start(&a, false);
    EXPECT_TRUE(a.disabled);
}

// The wait for the confirmation has a deadline, and ends once, when it is passed
TEST(WifiSwitch, confirmation_deadline)
{
    wifi_switch_t sw;
    memset(&sw, 0, sizeof(sw));
    EXPECT_EQ(wifi_switch_wait(&sw, 0), -1); // no wait before the radio off is asked for
    wifi_switch_wants_off(&sw, false);
    wifi_switch_asked_off(&sw, 0xFFFFFFF0u, 100); // across the wrap of the clock
    EXPECT_EQ(wifi_switch_wait(&sw, 0xFFFFFFF0u), 100);
    EXPECT_EQ(wifi_switch_wait(&sw, 0x00000010u), 68);
    EXPECT_FALSE(wifi_switch_expired(&sw, 0x00000010u));
    EXPECT_EQ(wifi_switch_wait(&sw, 0x00000054u), 0);
    EXPECT_TRUE(wifi_switch_expired(&sw, 0x00000054u));
    EXPECT_FALSE(sw.awaiting_off);
    EXPECT_FALSE(wifi_switch_expired(&sw, 0x00000100u)); // only once
    EXPECT_EQ(wifi_switch_wait(&sw, 0x00000100u), -1);
}

// Overdue is a wait of 0, not "as long as it takes": the application must not sleep on
TEST(WifiSwitch, overdue_wait_is_zero)
{
    wifi_switch_t sw;
    memset(&sw, 0, sizeof(sw));
    wifi_switch_asked_off(&sw, 100, 50);
    EXPECT_EQ(wifi_switch_wait(&sw, 400), 0);
    EXPECT_TRUE(wifi_switch_expired(&sw, 400));
}

// The application starts again (after a flash, or an Enable of a held-off module): a wait
// of the earlier run is over
TEST(WifiSwitch, a_start_ends_a_stale_wait)
{
    wifi_switch_t sw;
    memset(&sw, 0, sizeof(sw));
    wifi_switch_asked_off(&sw, 100, 50);
    EXPECT_FALSE(wifi_switch_wants_off(&sw, true));
    EXPECT_FALSE(sw.awaiting_off);
    EXPECT_EQ(wifi_switch_wait(&sw, 120), -1);
}

TEST(WifiSwitch, confirmed_in_time)
{
    wifi_switch_t sw;
    memset(&sw, 0, sizeof(sw));
    wifi_switch_wants_off(&sw, false);
    wifi_switch_asked_off(&sw, 0, 100);
    wifi_switch_drop_event(&sw, EVENT_DISABLED);
    EXPECT_EQ(wifi_switch_wait(&sw, 50), -1);
    EXPECT_FALSE(wifi_switch_expired(&sw, 200));
}

// A Disable or Enable while the application starts, before it knows the module: the start
// applies it, whatever is stored
TEST(WifiSwitch, switched_while_starting)
{
    wifi_switch_t sw;
    memset(&sw, 0, sizeof(sw));
    wifi_switch_later(&sw, false);
    EXPECT_TRUE(wifi_switch_wants_off(&sw, true)); // a Disable then: off, though Enabled is stored
    EXPECT_FALSE(wifi_switch_wants_off(&sw, true)); // once only
    wifi_switch_later(&sw, true);
    EXPECT_FALSE(wifi_switch_wants_off(&sw, false)); // an Enable then: on, though Disabled is stored
    wifi_switch_later(&sw, false);
    wifi_switch_later(&sw, true);
    EXPECT_FALSE(wifi_switch_wants_off(&sw, true)); // the last one counts
}

#endif
