/*
 * persist_task_test.cpp
 *
 * "WiFi Enabled" on the Elite II / C64 Ultimate (U64 == 2), where Enable and Disable used to be
 * actions with no stored state: the menu's Enable and Disable set and save the setting, the
 * module is told the setting once it is found (it wins over the module's own copy), and the
 * "WiFi is disabled. Enable it?" Yes sets it too. Through the real WiFi :: RunModeThread and
 * the menu actions of network_esp32.cc.
 */

#include "wifi_task_test.h"

#if U64 == 2
// A stored Disabled is passed to the module at the handshake, before the interface is used;
// a stored Enabled likewise, so that a module that kept a Disabled of its own starts its radio
TEST(WifiPersistTask, handshake_pushes_the_stored_setting)
{
    reset_world();
    world.enabled = false;
    boot({ EVENT_DISABLED });
    CHECK(strchr(rlog, 'O') != NULL, "the stored Disabled is passed to the module");
    CHECK(strchr(rlog, 'N') == NULL, "and the radio on is not asked for");
    CHECK(wifi.getState() == eWifi_Disabled, "Disabled once the module confirms");

    reset_world();
    world.enabled = true;
    boot({ EVENT_CONNECTED });
    CHECK(strchr(rlog, 'N') != NULL, "the stored Enabled is passed to the module (a stale Disabled of its own ends)");
    CHECK(strchr(rlog, 'O') == NULL, "no radio off");
}

TEST(WifiPersistTask, ask_enable_yes_sets_and_saves_the_setting)
{
    reset_world();
    world.enabled = false;
    boot({ EVENT_DISABLED });
    UserInterface ui;
    CHECK(NetworkLWIP_WiFi :: ask_enable(&ui), "Yes goes on");
    CHECK(world.enabled && saves == 1 && saved_enabled, "Enabled and saved");
}
static int count_of(char c)
{
    int n = 0;
    for (const char *p = rlog; *p; p++) {
        n += (*p == c);
    }
    return n;
}

// The stored Disabled reaches no module, or the module never confirms it: the link the module
// reports is never taken. The radio off is asked for again at each deadline; after
// WIFI_SWITCH_ASKS of them the user is told, and it is asked for at the slow pace from then on.
TEST(WifiPersistTask, an_unconfirmed_radio_off_does_not_let_the_link_up)
{
    static const int answers[] = { -1 /* nothing sent */, 0x101 /* rejected */, 0 /* queued, never done */ };
    for (int answer : answers) {
        reset_world();
        world.enabled = false;
        world.disable_answer = answer;
        world.link = 1;
        boot({ EVENT_CONNECTED, EVENT_GOTIP });
        const char *told = strchr(rlog, '!');
        CHECK(strchr(rlog, 'U') == NULL && strchr(rlog, 'M') == NULL, "no link up, also after the user is told");
        CHECK(strcmp(messages, "WiFi module did not turn its radio off\n") == 0, "the user is told, once");
        int fast = 0;
        for (const char *p = rlog; told && p < told; p++) {
            fast += (*p == 'O');
        }
        CHECK(fast == WIFI_SWITCH_ASKS + 1, "asked at each deadline before that");
        int slow = count_of('O') - fast;
        CHECK(slow >= 2 && slow <= (int)(QUIET_TICKS / pdMS_TO_TICKS(WIFI_SWITCH_SLOW_MS)) + 1, "then at the slow pace");
        CHECK(wifi.getState() == eWifi_NotConnected, "Link Down");
    }
}

// A recovery on the control module downloads through the radio: it refuses the radio off
// (ESP_ERR_INVALID_STATE) and stores the setting for the next boot. Nothing waits for a
// confirmation that does not come, and the user is not told of a failure.
TEST(WifiPersistTask, a_radio_off_refused_for_a_recovery_is_not_a_failure)
{
    reset_world();
    world.enabled = false;
    world.disable_answer = 0x103;
    world.link = 1;
    boot({ EVENT_CONNECTED });
    CHECK(messages[0] == 0, "no message");
    CHECK(count_of('O') == 1, "asked once");
    CHECK(wifi.getState() == eWifi_Connected, "the link follows the module");
}

// "WiFi Enabled" changes right after the WiFi task read it to tell the module, before its
// request goes out: the module ends as the setting is, not as it was read
static void flip_to_enabled(void) { NetworkLWIP_WiFi :: enable(NULL); }
static void flip_to_disabled(void) { NetworkLWIP_WiFi :: disable(NULL); }
static char last_request(void)
{
    const char *o = strrchr(rlog, 'O');
    const char *n = strrchr(rlog, 'N');
    return (o == NULL && n == NULL) ? 0 : ((o > n) ? 'O' : 'N');
}

TEST(WifiPersistTask, a_change_while_the_module_is_told_wins)
{
    reset_world();
    world.enabled = false;
    world.after_read = flip_to_enabled;
    boot({ EVENT_DISABLED, EVENT_DISCONNECTED });
    CHECK(world.enabled, "the setting reads Enabled");
    CHECK(last_request() == 'N', "the last request is the radio on");

    reset_world();
    world.enabled = true;
    world.after_read = flip_to_disabled;
    boot({ EVENT_CONNECTED, EVENT_DISABLED });
    CHECK(!world.enabled, "the setting reads Disabled");
    CHECK(last_request() == 'O', "the last request is the radio off");
    CHECK(wifi.getState() == eWifi_Disabled, "Disabled once the module confirms");
}
// Wake on Wi-Fi listens on the station interface, which a radio that is off does not have,
// also after a power cycle now: a Disable with Wake on Wi-Fi on tells the user (the Disable
// is not refused). The Wake on Wi-Fi side is in U64Config :: setWakeOnWifi.
TEST(WifiPersistTask, disable_with_wake_on_wifi_tells_the_user)
{
    reset_world();
    world.enabled = true;
    world.wake_on_wifi = true;
    boot({ EVENT_CONNECTED });
    messages[0] = 0;
    NetworkLWIP_WiFi :: disable(NULL);
    CHECK(!world.enabled && saves == 1, "Disabled and saved all the same");
    CHECK(strcmp(messages, "Wake on Wi-Fi needs WiFi enabled\n") == 0, "the user is told");
    messages[0] = 0;
    NetworkLWIP_WiFi :: enable(NULL);
    CHECK(messages[0] == 0, "nothing is said on an Enable");

    reset_world();
    world.enabled = true;
    boot({ EVENT_CONNECTED });
    messages[0] = 0;
    NetworkLWIP_WiFi :: disable(NULL);
    CHECK(messages[0] == 0, "nor on a Disable without Wake on Wi-Fi");
}
#endif
