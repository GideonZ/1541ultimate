/*
 * connect_feedback_task_test.cpp
 *
 * What a connect from the AP list does to the WiFi interface and tells the user, through
 * the real connect_ap and WiFi :: RunModeThread: the interface stays down and the state
 * stays as it was until the module reports an outcome, a refused client ends in a message
 * and no link, a joined one in the link, last_ap and a message, and no outcome ends in a
 * message after the timeout. And the wait of the task when both a radio-off confirmation
 * and a connect outcome are due: it wakes at the earlier one.
 */

#include "wifi_task_test.h"

static bool linked_up(void) { return strchr(rlog, 'U') != NULL; }

// The module accepted the request; nothing has come from it yet
TEST(WifiConnectFeedbackTask, accepted_is_not_connected)
{
    reset_world();
    world.enabled = true;
    boot({ pick });
    CHECK(!linked_up(), "no link before the outcome");
    CHECK(wifi.getState() == eWifi_NotConnected, "not Connected before the outcome");
}

TEST(WifiConnectFeedbackTask, wrong_password)
{
    reset_world();
    world.enabled = true;
    boot({ pick, disconnected(15) });
    CHECK(!linked_up(), "the link never comes up");
    CHECK(wifi.getState() == eWifi_NotConnected, "not Connected");
    CHECK(strcmp(messages, "Incorrect password\n") == 0, "told so");
}

TEST(WifiConnectFeedbackTask, refused_for_other_reasons)
{
    reset_world();
    world.enabled = true;
    boot({ pick, disconnected(201) });
    CHECK(strcmp(messages, "Could not connect to HomeNet\n") == 0, "no AP: generic message");
    boot({ pick, Step(EVENT_DISCONNECTED) });
    CHECK(strcmp(messages, "Could not connect to HomeNet\n") == 0, "old module: generic message");
    CHECK(!linked_up(), "no link");
}

TEST(WifiConnectFeedbackTask, joined)
{
    reset_world();
    world.enabled = true;
    boot({ pick, Step(EVENT_CONNECTED, "HomeNet") });
    CHECK(linked_up(), "the link comes up on the outcome");
    CHECK(wifi.getState() == eWifi_Connected, "Connected");
    CHECK(strcmp(wifi.getLastAP(), "HomeNet") == 0, "last_ap is the ssid, not shifted");
    CHECK(messages[0] == 0, "no message: the row going up says it");
}

// Connected to another AP: its drop is not the outcome of the pick
TEST(WifiConnectFeedbackTask, switching)
{
    reset_world();
    world.enabled = true;
    world.link = 1;
    boot({ disconnected(8), pick, Step(EVENT_CONNECTED, "HomeNet") });
    CHECK(messages[0] == 0, "nothing to report");
    CHECK(wifi.getState() == eWifi_Connected, "Connected");
    boot({ disconnected(8), pick, disconnected(202) });
    CHECK(strcmp(messages, "Incorrect password\n") == 0, "the refusal of the pick is reported");
    CHECK(wifi.getState() == eWifi_NotConnected, "not Connected");
}

// The module's leave of the AP it was on is part of the connect, whenever it is reported
TEST(WifiConnectFeedbackTask, ordering)
{
    reset_world();
    world.enabled = true;
    world.link = 1;
    boot({ disconnected(8), pick, Step(EVENT_CONNECTED, "HomeNet") });
    CHECK(messages[0] == 0 && wifi.getState() == eWifi_Connected, "leave, pick, joined");
    boot({ pick, disconnected(8), Step(EVENT_CONNECTED, "HomeNet") });
    CHECK(messages[0] == 0 && wifi.getState() == eWifi_Connected, "pick, leave, joined");
    boot({ pick, disconnected(8), disconnected(202) });
    CHECK(strcmp(messages, "Incorrect password\n") == 0, "pick, leave, refused");
    boot({ pick, disconnected(202) });
    CHECK(strcmp(messages, "Incorrect password\n") == 0, "picked while Connected, refused");
    boot({ pick, disconnected(8) });
    CHECK(strcmp(messages, "Could not connect to HomeNet\n") == 0 && waited > 0, "the leave alone is no outcome");
}

TEST(WifiConnectFeedbackTask, long_ssid)
{
    reset_world();
    world.enabled = true;
    const char *name = "0123456789abcdef0123456789abcdef";
    world.pick_ssid = name;
    boot({ pick, Step(EVENT_DISCONNECTED) });
    CHECK(strcmp(messages, "Could not connect to 0123456789abcdef0123456789abcdef\n") == 0, "32 characters");
    boot({ pick, Step(EVENT_CONNECTED, name) });
    CHECK(memcmp(wifi.last_ap, name, 32) == 0, "last_ap, 32 characters");
}

// No outcome: the wait is bounded
TEST(WifiConnectFeedbackTask, no_outcome)
{
    reset_world();
    world.enabled = true;
    boot({ pick });
    CHECK(strcmp(messages, "Could not connect to HomeNet\n") == 0, "told after the timeout");
    CHECK(message_at - 1000 == (20 * configTICK_RATE_HZ), "not before it");
    CHECK(!linked_up(), "no link");
    CHECK(strcmp(ended, "waiting for an event") == 0, "and the task waits without a bound again");
}

// The module did not accept the request, or the task cannot be told: nothing is pending
TEST(WifiConnectFeedbackTask, not_accepted)
{
    reset_world();
    world.enabled = true;
    world.connect_result = 0x103; // ESP_ERR_INVALID_STATE
    boot({ pick });
    CHECK(strcmp(messages, "Could not connect to HomeNet\n") == 0, "told");
    CHECK(waited == 0, "without waiting for an outcome");
    CHECK(!linked_up(), "no link");
    world.connect_result = 0;
    world.connect_noreply = true;
    boot({ pick });
    CHECK(strcmp(messages, "Could not connect to HomeNet\n") == 0, "told when the module does not answer");
    CHECK(waited == 0, "at once");
    world.connect_noreply = false;
    world.loopback_fails = true;
    boot({ pick });
    CHECK(strcmp(messages, "Could not connect to HomeNet\n") == 0, "told when the event is refused");
    CHECK(waited == 0, "at once");
}

// Cancelling the password box connects to nothing
TEST(WifiConnectFeedbackTask, cancelled)
{
    reset_world();
    world.enabled = true;
    world.pick_auth = 1;
    string_box_result = -1;
    boot({ pick });
    CHECK(requests == 0, "no request");
    CHECK(messages[0] == 0, "no message");
    string_box_result = 1;
    boot({ pick });
    CHECK(requests == 1, "a password is sent");
}

#if U64 != 2
// A radio-off confirmation due in 10 s, and a connect outcome due in 20 s: the task wakes
// when the radio-off one is due, then when the other is. The pick comes while the boot's
// radio off waits for its confirmation (the task is not Disabled yet).
TEST(WifiConnectFeedbackTask, wakes_for_the_radio_off_deadline_first)
{
    reset_world();
    world.enabled = false;
    boot({ pick });
    CHECK(timed_waits_n >= 2, "two bounded waits");
    CHECK(timed_waits[0] == pdMS_TO_TICKS(WIFI_SWITCH_CONFIRM_MS), "the first ends at the radio off deadline, 10 s");
    CHECK(strcmp(messages, "Could not connect to HomeNet\n") == 0, "then the outcome is reported");
    CHECK(message_at == 1000 + (20 * configTICK_RATE_HZ), "exactly at its deadline, 20 s");
}

// The reverse: the radio off is asked again (the module restarted) when the connect outcome
// is almost due. The task wakes for the outcome, which is due first, not 10 s later.
TEST(WifiConnectFeedbackTask, wakes_for_the_connect_deadline_first)
{
    reset_world();
    world.enabled = true;
    const TickType_t picked_at = 1000;
    boot({ pick,
           late(Step(EVENT_DISABLED), 1000),
           late(Step(EVENT_RESTARTED), (20 * configTICK_RATE_HZ) - 1500) });
    // the pick at 1000 gives a deadline at 1000 + 4000; the restart comes at 4500
    CHECK(strchr(rlog, 'O') != NULL, "the radio off is asked again");
    CHECK(strcmp(messages, "Could not connect to HomeNet\n") == 0, "the outcome is reported");
    CHECK(message_at == picked_at + (20 * configTICK_RATE_HZ), "at its deadline, not at the radio off one");
}
#endif
