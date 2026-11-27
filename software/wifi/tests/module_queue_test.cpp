/*
 * module_queue_test.cpp
 *
 * The connector's two queues and its wake, in the three modules, with the semaphore as the
 * module has it (strict_semaphore: binary, given once per command or event).
 *  Fix 7: the connector waits only with both queues empty, so a Disable queued behind an
 *         Enable that changes nothing (radio already on) is handled at the same wake.
 *  Fix 8: the wait for the event of its own esp_wifi_disconnect() is bounded, for a Disable
 *         and for a Connect while connected; the call returns ESP_OK also when the station
 *         has already left the AP, and then no event comes.
 *         Should the radio then not stop, the connector retries from Disconnected (fix 11).
 *  Fix 9: events from before a radio start are dropped, so a stale EVENT_CONNECTED, or the
 *         late EVENT_DISCONNECTED of the stop, does not end the first connect attempt after
 *         the next Enable, Autoconnect or query of an application without the switch.
 *  Fix 12: so are those from before a connect attempt (a link drop at the same wake as the
 *         Connect, a disconnect event that came after its wait).
 * The U64 and U2+L modules start with the radio off: their runs begin with the Ultimate's
 * Enable. The control module starts with it on.
 */

#include <string.h>
#include "module_script.h"

#ifdef MODULE_HAS_SWITCH
#define BOOT_ENABLE CMD_WIFI_ENABLE,
#else
#define BOOT_ENABLE
#endif

// Enable and then Disable reach the dispatcher while the connector idles Connected: both
// replies say OK, the two gives leave one token, and the Disable must still be carried out
TEST(Queue, enable_disable_at_one_wake)
{
    const uint8_t cmds[] = { BOOT_ENABLE CONNECTOR_SEND_ENABLE, CONNECTOR_SEND_DISABLE };
    last_ap_reachable = 1;
    strict_semaphore = 1;
    run(false, cmds, sizeof(cmds));
    CHECK(strstr(trace, "LK") != NULL, "connected before the two commands");
    CHECK(strstr(after('K'), "RR") != NULL, "both commands answered");
    CHECK(strchr(after('K'), 'X') != NULL, "the Disable is carried out: EVENT_DISABLED");
    CHECK(strcmp(ended, "waiting for a command") == 0, "waits in Disabled");
    CHECK(uxQueueMessagesWaiting(connect_commands) == 0, "nothing left queued");
}

// Disable while Connected, but the station had already left the AP: no event follows the
// disconnect, and the radio must still be stopped
TEST(Queue, disable_without_disconnect_event)
{
    const uint8_t cmds[] = { BOOT_ENABLE CONNECTOR_SEND_DISABLE };
    last_ap_reachable = 1;
    strict_semaphore = 1;
    silent_disconnects = 1;
    run(false, cmds, sizeof(cmds));
    CHECK(strstr(trace, "LK") != NULL, "connected before the Disable");
    CHECK(strstr(after('K'), "DP") != NULL, "disconnect, then the radio stops");
    CHECK(strchr(after('D'), 'X') != NULL, "EVENT_DISABLED");
    CHECK(strcmp(ended, "waiting for a command") == 0, "waits in Disabled");
}

// The same for a Connect while connected (attempt_connect)
TEST(Queue, connect_without_disconnect_event)
{
    const uint8_t cmds[] = { BOOT_ENABLE CMD_WIFI_CONNECT };
    last_ap_reachable = 1;
    strict_semaphore = 1;
    silent_disconnects = 1;
    run(false, cmds, sizeof(cmds));
    CHECK(strstr(trace, "LK") != NULL, "connected before the Connect");
    CHECK(strstr(after('K'), "Dc") != NULL, "disconnect, then the connect goes ahead");
    CHECK(strchr(after('D'), 'R') != NULL, "the Connect is answered");
}

// The station's EVENT_CONNECTED is queued, then a Disable and an Enable; the Disable is
// taken first (commands before events), so the event outlives the stop. After the Enable
// no AP answers: the first attempt must not take the stale event for its own
TEST(Queue, stale_connected_after_disable)
{
    const uint8_t cmds[] = { BOOT_ENABLE CONNECTOR_EVENT_CONNECTED, CONNECTOR_SEND_DISABLE, CONNECTOR_SEND_ENABLE };
    strict_semaphore = 1;
    run(false, cmds, sizeof(cmds));
    CHECK(strchr(trace, 'X') != NULL, "the Disable is carried out");
    CHECK(strchr(after('X'), 'T') != NULL, "the Enable starts the radio again");
    CHECK(strchr(after('X'), 'c') != NULL, "and the walk tries the scanned APs");
    CHECK(strchr(after('X'), 'K') == NULL, "no association after the Enable: none answers");
    CHECK(strcmp(ended, "idle until the retry timer") == 0, "ends Disconnected, waiting to retry");
}

// The link drops and a Connect arrives at the same wake: the Connect is taken first, and its
// attempt must not take the drop's EVENT_DISCONNECTED for its own outcome
TEST(Queue, connect_after_drop)
{
    const uint8_t cmds[] = { BOOT_ENABLE CONNECTOR_EVENT_DROP, CMD_WIFI_CONNECT };
    last_ap_reachable = 1;
    connect_reachable = true;
    strict_semaphore = 1;
    run(false, cmds, sizeof(cmds));
    CHECK(strstr(trace, "LK") != NULL, "connected before the drop");
    CHECK(strstr(after('c'), "RKW") != NULL, "the Connect joins its AP and stores it");
}

// A Connect while connected, and the event of the disconnect comes after the wait for it
// has run out: the attempt must not take it for its own outcome
TEST(Queue, connect_with_late_disconnect_event)
{
    const uint8_t cmds[] = { BOOT_ENABLE CMD_WIFI_CONNECT };
    last_ap_reachable = 1;
    connect_reachable = true;
    strict_semaphore = 1;
    late_disconnects = 1;
    run(false, cmds, sizeof(cmds));
    CHECK(strstr(trace, "LKDc") != NULL, "connected, disconnect, then the connect");
    CHECK(strstr(after('c'), "RKW") != NULL, "the Connect joins its AP and stores it");
}

// Idle Disconnected, the station associates and drops again before the connector runs: two
// events behind one token. It must end Disconnected and retrying, not Connected without a link
TEST(Queue, connected_then_dropped_at_one_wake)
{
    const uint8_t cmds[] = { BOOT_ENABLE CONNECTOR_EVENT_CONNECTED, CONNECTOR_EVENT_DROP };
    strict_semaphore = 1;
    run(false, cmds, sizeof(cmds));
    CHECK(strcmp(ended, "idle until the retry timer") == 0, "Disconnected, retrying");
}

// Disable while Connected, no event after the disconnect, and the radio does not stop: the
// connector goes on from Disconnected and retries, not idle in Connected without a link
TEST(Queue, disable_stop_fails_after_silent_disconnect)
{
    const uint8_t cmds[] = { BOOT_ENABLE CONNECTOR_SEND_DISABLE };
    last_ap_reachable = 1;
    strict_semaphore = 1;
    silent_disconnects = 1;
    stop_fails = 1;
    run(false, cmds, sizeof(cmds));
    CHECK(strstr(after('K'), "Dp") != NULL, "disconnect, then the stop fails");
    CHECK(strchr(trace, 'X') == NULL, "no EVENT_DISABLED");
    CHECK(strcmp(ended, "idle until the retry timer") == 0, "Disconnected, retrying");
}

// The EVENT_DISCONNECTED of a radio stop comes after the stop, while the connector waits in
// Disabled. Each way of starting the radio again must drop it, or the connect to the last AP
// after the start takes it for its own outcome and goes on to scan
static void late_stop_event_then(const uint8_t *cmds, int n)
{
    last_ap_reachable = 2;
    strict_semaphore = 1;
    late_stop_event = 1;
    run(false, cmds, n);
    CHECK(strchr(trace, 'X') != NULL, "the Disable is carried out");
    CHECK(strstr(after('X'), "TZLK") != NULL, "radio on, and the last AP joined");
}

TEST(Queue, late_stop_event_enable)
{
    const uint8_t cmds[] = { BOOT_ENABLE CMD_WIFI_DISABLE, CMD_WIFI_ENABLE };
    late_stop_event_then(cmds, sizeof(cmds));
}

TEST(Queue, late_stop_event_autoconnect)
{
    const uint8_t cmds[] = { BOOT_ENABLE CMD_WIFI_DISABLE, CMD_WIFI_AUTOCONNECT };
    late_stop_event_then(cmds, sizeof(cmds));
}

#ifdef MODULE_HAS_SWITCH
// An application without the radio switch starts after the Disable: its query starts the radio
TEST(Queue, late_stop_event_old_application)
{
    const uint8_t cmds[] = { CMD_WIFI_ENABLE, CMD_WIFI_DISABLE, CONNECTOR_IDENTIFY, CONNECTOR_ASK };
    late_stop_event_then(cmds, sizeof(cmds));
}
#endif
