/*
 * module_switch_test.cpp
 *
 * The radio switch in the connector of the U64 and U2+L modules (raw_u64, raw_c3), fix 1:
 * the module starts with its radio off and waits for the Ultimate (or starts it for an
 * Ultimate without the switch, which asks for the connection state first); a Disable stops
 * the radio and leaves the connector waiting in Disabled, also when it is taken during the
 * AP walks; an Enable starts it again and reconnects, also after an explicit Disconnect
 * before the Disable; an Autoconnect taken during the walks starts over; and the replay of
 * EVENT_DISABLED for a starting WiFi application stays in order with Disable and Enable.
 * The Elite II / C64 Ultimate module had its switch before: no case here is for it. Except
 * in the boot cases, the first command is the Enable the Ultimate sends at boot with "WiFi
 * Enabled" stored as Enabled. The cases run against the module's real dispatcher commands
 * (cmd_wifi_endisable, cmd_get_connection, cmd_identify) and connector.
 */

#include <string.h>
#include "module_script.h"

// The command numbers the radio switch added to or relies on are the same in the three
// rpc_calls.h (the application has its own copy of them), and the Wake-on-WiFi commands
// of the control module keep theirs: nothing the switch adds moves them.
static_assert(CMD_WIFI_IS_CONNECTED == 0x0B && CMD_WIFI_ENABLE == 0x0D && CMD_WIFI_DISABLE == 0x0E &&
              CMD_CLEAR_APS == 0x11 && CMD_WIFI_AUTOCONNECT == 0x12, "WiFi command numbers");
static_assert(EVENT_DISABLED == 0x46, "EVENT_DISABLED");
#ifdef MODULE_U64CTRL
static_assert(CMD_SET_WAKE_ON_WIFI == 0x18 && CMD_GET_WAKE_ON_WIFI == 0x19, "Wake-on-WiFi command numbers");
#endif

#ifdef MODULE_HAS_SWITCH

// At boot the radio stays off, and nothing is tried, until the Ultimate asks
TEST(Switch, boot_radio_off)
{
    const uint8_t cmds[] = { 0 };
    run(false, cmds, 0);
    CHECK(strpbrk(trace, "TLScs") == NULL, "no radio start, no scan, no connect attempt");
    CHECK(strcmp(ended, "waiting for a command") == 0, "waits in Disabled");
}

// "WiFi Enabled" stored as Disabled: the Ultimate's Disable keeps it off
TEST(Switch, boot_stored_disabled)
{
    const uint8_t cmds[] = { CONNECTOR_SEND_DISABLE };
    run(false, cmds, 1);
    CHECK(strchr(trace, 'T') == NULL, "the radio never starts");
    CHECK(strchr(trace, 'X') != NULL, "EVENT_DISABLED confirms it");
    CHECK(strpbrk(trace, "LScs") == NULL, "no scan, no connect attempt");
    CHECK(strcmp(ended, "waiting for a command") == 0, "waits in Disabled");
}

// An Ultimate application without the radio switch asks for the connection state first
// and never sends Enable or Disable: the radio starts, as the module always did at boot
TEST(Switch, boot_old_application)
{
    const uint8_t cmds[] = { CONNECTOR_ASK };
    last_ap_reachable = true;
    run(false, cmds, 1);
    CHECK(strncmp(trace, "RTZLK", 5) == 0, "the answer, radio on, EVENT_DISCONNECTED, the last AP");
    CHECK(strchr(trace, 'X') == NULL, "no EVENT_DISABLED");
    CHECK(stale_disabled == 0, "disabled_g is cleared before EVENT_DISCONNECTED goes out");
}

// An application with the switch and "WiFi Enabled" stored as Disabled: its Disable comes
// first, so a later connection query (the confirmation deadline) does not start the radio
TEST(Switch, boot_new_application_disabled)
{
    const uint8_t cmds[] = { CONNECTOR_SEND_DISABLE, CONNECTOR_ASK };
    run(false, cmds, 2);
    CHECK(strchr(trace, 'T') == NULL, "the radio never starts");
    CHECK(strstr(trace, "PXX") != NULL, "off and confirmed, then told again for the query");
    CHECK(strcmp(ended, "waiting for a command") == 0, "waits in Disabled");
}

// The same with "WiFi Enabled" stored as Enabled: Enable first, then the query
TEST(Switch, boot_new_application_enabled)
{
    const uint8_t cmds[] = { CMD_WIFI_ENABLE, CONNECTOR_ASK };
    run(false, cmds, 2);
    CHECK(strncmp(trace, "TZ", 2) == 0, "radio on at the Enable");
    CHECK(strchr(after('T'), 'T') == NULL, "started once");
    CHECK(strchr(trace, 'X') == NULL, "no EVENT_DISABLED");
}

// Stored as Enabled: the Ultimate's Enable, or an Autoconnect, starts the radio and walks
TEST(Switch, boot_enable_connects)
{
    const uint8_t enable[] = { CMD_WIFI_ENABLE };
    const uint8_t autoconnect[] = { CMD_WIFI_AUTOCONNECT };
    for (int i = 0; i < 2; i++) {
        last_ap_reachable = true;
        run(false, i ? autoconnect : enable, 1);
        CHECK(strncmp(trace, "TZLK", 4) == 0, "radio on, EVENT_DISCONNECTED, then the last AP");
        CHECK(stale_disabled == 0, "disabled_g is cleared before EVENT_DISCONNECTED goes out");
    }
}

// A Scan at boot is refused like any Scan while Disabled, and it does not count as an
// Ultimate that knows the switch: an old application's connection query after it still
// starts the radio
TEST(Switch, boot_scan_refused)
{
    const uint8_t cmds[] = { CMD_WIFI_SCAN, CONNECTOR_ASK };
    run(false, cmds, 1);
    CHECK(strpbrk(trace, "TS") == NULL, "no radio start and no scan");
    run(false, cmds, 2);
    CHECK(strpbrk(trace, "S") == NULL || strchr(trace, 'S') > strchr(trace, 'T'), "the refused scan does not scan");
    CHECK(strchr(trace, 'T') != NULL, "the old application's query then starts the radio");
}

// The scan of an old application (opening the AP list) comes after its connection query,
// so it finds the radio on; with the switch and "WiFi Enabled" stored as Disabled a scan
// after the Disable neither scans nor starts the radio
TEST(Switch, boot_scans)
{
    // the connection query as the dispatcher queues it (before the application sends its
    // scan, which it does only once it has the answer)
    const uint8_t old_app[] = { CMD_WIFI_IS_CONNECTED, CMD_WIFI_SCAN };
    const uint8_t stored_off[] = { CONNECTOR_SEND_DISABLE, CMD_WIFI_SCAN };
    run_with(3, true, old_app, 2); // settles at once, so the scan is taken while idle
    CHECK(strncmp(trace, "TZS", 3) == 0, "radio on, then the scan");
    run(false, stored_off, 2);
    CHECK(strpbrk(trace, "TS") == NULL, "no radio start and no scan");
}

// A restart of the module's own (radio_boot.h) with the radio on before it: the radio
// goes on again and the connector walks, as before; the Ultimate is told it restarted,
// and its connection query afterwards does not start the radio a second time
TEST(Switch, restart_with_the_radio_on)
{
    const uint8_t cmds[] = { CONNECTOR_ASK };
    boot_mode = RADIO_BOOT_ON;
    last_ap_reachable = true;
    run(false, cmds, 1);
    CHECK(strncmp(trace, "YTZLK", 5) == 0, "EVENT_RESTARTED, radio on, EVENT_DISCONNECTED, the last AP");
    CHECK(strchr(after('T'), 'T') == NULL, "started once");
    CHECK(recorded == 1, "the record says on");
    CHECK(stale_disabled == 0, "disabled_g is cleared before EVENT_DISCONNECTED goes out");
}

// The same with the radio off before it: it stays off, and the connection query that
// follows does not count as an Ultimate without the switch
TEST(Switch, restart_with_the_radio_off)
{
    const uint8_t cmds[] = { CONNECTOR_ASK };
    boot_mode = RADIO_BOOT_OFF;
    run(false, cmds, 1);
    CHECK(trace[0] == 'Y', "EVENT_RESTARTED");
    CHECK(strchr(trace, 'T') == NULL, "the radio stays off");
    CHECK(strchr(after('R'), 'X') != NULL, "and the query is told Disabled");
    CHECK(strcmp(ended, "waiting for a command") == 0, "waits in Disabled");
}

// The record follows every radio start and stop
TEST(Switch, record_follows_the_radio)
{
    const uint8_t enable[] = { CMD_WIFI_ENABLE };
    const uint8_t disable[] = { CMD_WIFI_ENABLE, CMD_WIFI_DISABLE };
    const uint8_t old_app[] = { CONNECTOR_ASK };
    const uint8_t autoconnect[] = { CMD_WIFI_AUTOCONNECT };
    run(false, enable, 1);
    CHECK(recorded == 1, "on after an Enable");
    run(false, disable, 2);
    CHECK(recorded == 0, "off after a Disable");
    run(false, old_app, 1);
    CHECK(recorded == 1, "on after an old application's query");
    run(false, autoconnect, 1);
    CHECK(recorded == 1, "on after an Autoconnect");
    run(false, disable + 1, 1);
    CHECK(recorded == 0, "off after a Disable at boot");
}

// A request for the radio on whose start fails (Enable, Autoconnect, Connect, or an old
// application's query) does not count as a Disable: the next connection query tries the
// start again, because the radio was asked for on
TEST(Switch, failed_start_then_query)
{
    const uint8_t requests[] = { CMD_WIFI_ENABLE, CMD_WIFI_AUTOCONNECT, CMD_WIFI_CONNECT, CMD_WIFI_IS_CONNECTED };
    for (int i = 0; i < 4; i++) {
        const uint8_t cmds[] = { requests[i], CMD_WIFI_IS_CONNECTED };
        start_fails = 1;
        run(false, cmds, 2);
        CHECK(trace[0] == 't', "the first start fails");
        CHECK(strchr(trace, 'T') != NULL, "the query starts the radio after all");
    }
}

// An Ultimate application that starts afresh (it identifies the module) after another one
// asked for the radio off: an application without the switch gets its radio
TEST(Switch, identify_after_a_disable)
{
    // the Disable as the connector takes it, long before the next application starts
    const uint8_t old_app_after[] = { CMD_WIFI_DISABLE, CONNECTOR_IDENTIFY, CONNECTOR_ASK };
    run(false, old_app_after, 3);
    CHECK(strchr(after('P'), 'T') != NULL, "the old application's query starts the radio");
}

// Settled and disconnected, as after a failed boot ladder, then Disable
TEST(Switch, disable_stops_the_radio)
{
    const uint8_t cmds[] = { CMD_WIFI_ENABLE, CONNECTOR_SEND_DISABLE }; // as the Ultimate sends it
    run_with(0, false, cmds, 2);
    CHECK(strstr(trace, "RPX") != NULL, "answered, then the radio is stopped and EVENT_DISABLED sent");
    CHECK(strpbrk(after('P'), "LcsT") == NULL, "nothing else with the radio off");
    CHECK(strcmp(ended, "waiting for a command") == 0, "waits in Disabled");
}

// Taken during the walk over the scanned APs, and over the stored ones (a scan that finds
// nothing goes straight there)
TEST(Switch, disable_in_the_walks)
{
    const uint8_t cmds[] = { CMD_WIFI_ENABLE, CMD_WIFI_DISABLE };
    for (int found = 3; found >= 0; found -= 3) {
        run_with(found, false, cmds, 2);
        CHECK(strstr(trace, "PX") != NULL, "the radio is stopped and EVENT_DISABLED sent");
        CHECK(strpbrk(after('P'), "Lcs") == NULL, "no connect attempt with the radio off");
        CHECK(strcmp(ended, "waiting for a command") == 0, "waits in Disabled");
    }
}

TEST(Switch, disable_in_the_walk_then_enable)
{
    const uint8_t cmds[] = { CMD_WIFI_ENABLE, CMD_WIFI_DISABLE, CMD_WIFI_ENABLE };
    run_with(3, false, cmds, 3);
    CHECK(strstr(trace, "PX") != NULL, "the radio is stopped");
    CHECK(strchr(after('P'), 'T') != NULL, "the Enable is taken and starts it");
    CHECK(strchr(after('T'), 'L') != NULL, "then the connector tries the last AP");
}

TEST(Switch, autoconnect_in_the_walks)
{
    const uint8_t cmds[] = { CMD_WIFI_ENABLE, CMD_WIFI_AUTOCONNECT };
    for (int found = 3; found >= 0; found -= 3) {
        run_with(found, false, cmds, 2);
        CHECK(after('S')[0] == 'L', "the next attempt after the Autoconnect is the last AP");
    }
}

// A WiFi application that starts while the radio is off is told so
TEST(Switch, replay_while_disabled)
{
    const uint8_t cmds[] = { CMD_WIFI_ENABLE, CMD_WIFI_DISABLE, CONNECTOR_ASK };
    run(false, cmds, 3);
    CHECK(strstr(trace, "PXRX") != NULL, "the answer, then EVENT_DISABLED again");
    CHECK(strcmp(ended, "waiting for a command") == 0, "still Disabled");
}

TEST(Switch, no_replay_while_on)
{
    const uint8_t cmds[] = { CMD_WIFI_ENABLE, CONNECTOR_ASK };
    run(false, cmds, 2);
    CHECK((strchr(trace, 'R') != NULL) && (strchr(trace, 'X') == NULL), "an answer, no EVENT_DISABLED");
}

// Asked while off, with an Enable queued already: the replay goes out after the Enable and
// so is not sent at all; it never follows the EVENT_DISCONNECTED of the radio start
TEST(Switch, replay_in_order_with_an_enable)
{
    const uint8_t cmds[] = { CMD_WIFI_ENABLE, CMD_WIFI_DISABLE, CONNECTOR_ASK, CMD_WIFI_ENABLE };
    run(false, cmds, 4);
    const char *on = strstr(trace, "PXRTZ");
    CHECK(on != NULL, "off, the answer, on");
    CHECK((on != NULL) && (strchr(on + 5, 'X') == NULL), "no EVENT_DISABLED after the radio came on");
    CHECK(stale_disabled == 0, "disabled_g is cleared before EVENT_DISCONNECTED goes out");
}

// Disabled means the radio is off: a Scan is refused and does not turn it on
TEST(Switch, scan_refused_while_disabled)
{
    const uint8_t cmds[] = { CMD_WIFI_ENABLE, CMD_WIFI_DISABLE, CMD_WIFI_SCAN };
    run(false, cmds, 3);
    CHECK(strstr(trace, "PX") != NULL, "the radio is stopped");
    CHECK(strpbrk(after('P'), "TS") == NULL, "no radio start and no scan");
    CHECK(strcmp(ended, "waiting for a command") == 0, "still Disabled");
}

// A Connect from Disabled starts the radio; when the attempt fails, the connector goes on
// as after a dropped link (retry timer), not parked in Disabled with the radio on
TEST(Switch, failed_connect_from_disabled)
{
    const uint8_t cmds[] = { CMD_WIFI_ENABLE, CMD_WIFI_DISABLE, CMD_WIFI_CONNECT };
    run(false, cmds, 3);
    CHECK(strstr(trace, "PXTZ") != NULL, "off, then on with EVENT_DISCONNECTED");
    CHECK(stale_disabled == 0, "disabled_g is cleared before EVENT_DISCONNECTED goes out");
    CHECK(strcmp(ended, "idle until the retry timer") == 0, "it retries, it is not parked in Disabled");
}

// An Autoconnect from Disabled starts the radio and walks the ladder
TEST(Switch, autoconnect_from_disabled)
{
    const uint8_t cmds[] = { CMD_WIFI_ENABLE, CMD_WIFI_DISABLE, CMD_WIFI_AUTOCONNECT };
    run(false, cmds, 3);
    CHECK(strstr(trace, "PXTZ") != NULL, "off, then on with EVENT_DISCONNECTED");
    CHECK(strchr(after('T'), 'L') != NULL, "then the last AP");
    CHECK(stale_disabled == 0, "disabled_g is cleared before EVENT_DISCONNECTED goes out");
}

// A Disable while Connected disconnects first, then stops the radio
TEST(Switch, disable_while_connected)
{
    const uint8_t cmds[] = { CMD_WIFI_ENABLE, CMD_WIFI_DISABLE };
    last_ap_reachable = true;
    run(false, cmds, 2);
    CHECK(strncmp(trace, "TZLK", 4) == 0, "connected at once");
    CHECK(strstr(trace, "DPX") != NULL, "disconnect, then radio off, then EVENT_DISABLED");
    CHECK(strcmp(ended, "waiting for a command") == 0, "waits in Disabled");
}

// Disabled with an explicit Disconnect still on record (as after Disconnect, Disable)
TEST(Switch, enable_after_disconnect_and_disable)
{
    const uint8_t cmds[] = { CMD_WIFI_ENABLE };
    run(true, cmds, 1);
    CHECK(trace[0] == 'T', "the radio is started");
    CHECK(strchr(after('T'), 'L') != NULL, "then the connector tries the last AP");
    CHECK(strcmp(ended, "idle without a timeout") != 0, "it does not idle for good");
}

// An Enable with the radio on does nothing: it does not start it a second time
TEST(Switch, enable_while_on_does_nothing)
{
    const uint8_t cmds[] = { CMD_WIFI_ENABLE, CMD_WIFI_ENABLE };
    run(false, cmds, 2);
    CHECK(strchr(after('T'), 'T') == NULL, "the radio is started once");
}

// The first CMD_IDENTIFY of an Ultimate application clears what a Disable of an earlier one
// left, through the module's own cmd_identify (not a call of wifi_note_identify here)
TEST(Switch, identify_clears_off_asked)
{
    const uint8_t cmds[] = { CMD_WIFI_DISABLE };
    run(false, cmds, 1);
    EXPECT_TRUE(off_asked);
    const uint8_t after_identify[] = { CMD_WIFI_DISABLE, CONNECTOR_IDENTIFY };
    run(false, after_identify, 2);
    EXPECT_FALSE(off_asked);
}

#endif /* MODULE_HAS_SWITCH */
