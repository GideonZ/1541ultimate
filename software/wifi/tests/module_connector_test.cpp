/*
 * module_connector_test.cpp
 *
 * The connector of the modules, fix 2: a Disable or an Autoconnect taken while it walks its
 * AP lists, an Enable after a Disconnect, no scan from Disabled (see module_script.h). The
 * first group is for the Elite II / C64 Ultimate module (u64ctrl); the U64 and U2+L modules
 * have these cases in module_switch_test.cpp, with their radio switch. The last group is
 * for all three: a Connect whose attempt fails.
 */

#include <string.h>
#include "module_script.h"

#ifdef MODULE_U64CTRL
// An Autoconnect handled while the connector walks its scanned APs: the walk ends and the
// connector starts over from the last AP, instead of going on to the stored ones.
TEST(Connector, autoconnect_in_the_walk)
{
    const uint8_t cmds[] = { CMD_WIFI_AUTOCONNECT };
    run(false, cmds, 1);
    CHECK(strncmp(trace, "LS", 2) == 0, "the walk began");
    CHECK(after('S')[0] == 'L', "the next attempt after the Autoconnect is the last AP");
}

// The same in the walk over the stored APs: a scan that finds nothing goes straight there
TEST(Connector, autoconnect_in_the_stored_walk)
{
    const uint8_t cmds[] = { CMD_WIFI_AUTOCONNECT };
    run_with(0, false, cmds, 1);
    CHECK(after('S')[0] == 'L', "the next attempt after the Autoconnect is the last AP");
}

// C13 in the stored walk
TEST(Connector, disable_in_the_stored_walk)
{
    const uint8_t cmds[] = { CMD_WIFI_DISABLE };
    run_with(0, false, cmds, 1);
    CHECK(strpbrk(after('P'), "Lcs") == NULL, "no connect attempt with the radio off");
    CHECK(strcmp(ended, "waiting for a command") == 0, "waits in Disabled");
}

// C13: a Disable handled during the walk turns the radio off, and the connector then waits
// in Disabled for a command, without trying any AP.
TEST(Connector, disable_in_the_walk)
{
    const uint8_t cmds[] = { CMD_WIFI_DISABLE };
    run(false, cmds, 1);
    CHECK(strstr(trace, "PX") != NULL, "the radio is stopped and EVENT_DISABLED sent");
    CHECK(strpbrk(after('P'), "Lcs") == NULL, "no connect attempt with the radio off");
    CHECK(strcmp(ended, "waiting for a command") == 0, "waits in Disabled");
}

// C16: Disconnect, then Disable, then Enable: the connector reconnects.
TEST(Connector, enable_after_disconnect_and_disable)
{
    const uint8_t cmds[] = { CMD_WIFI_DISABLE, CMD_WIFI_ENABLE };
    run(true, cmds, 2);
    CHECK(strstr(trace, "PX") != NULL, "the radio is stopped");
    CHECK(strchr(after('P'), 'T') != NULL, "and started again");
    CHECK(strchr(after('T'), 'L') != NULL, "then the connector tries the last AP");
    CHECK(strcmp(ended, "idle without a timeout") != 0, "it does not idle for good");
}

// Disabled means the radio is off: a Scan is refused and does not turn it on (the menu's
// rescan, a rescan sent before the Ultimate saw EVENT_DISABLED, or any other sender)
TEST(Connector, scan_refused_while_disabled)
{
    const uint8_t cmds[] = { CMD_WIFI_DISABLE, CMD_WIFI_SCAN };
    run(true, cmds, 2);
    CHECK(strstr(trace, "PX") != NULL, "the radio is stopped");
    CHECK(strpbrk(after('P'), "TS") == NULL, "no radio start and no scan");
    CHECK(strcmp(ended, "waiting for a command") == 0, "still Disabled");
}

// A Connect from Disabled (a stale AP entry's Connect) starts the radio and leaves
// Disabled: when the attempt fails, the connector retries as after a dropped link
// instead of idling in Disabled with the radio on
TEST(Connector, failed_connect_from_disabled)
{
    const uint8_t cmds[] = { CMD_WIFI_DISABLE, CMD_WIFI_CONNECT };
    run(true, cmds, 2);
    CHECK(strstr(trace, "PX") != NULL, "the radio is stopped");
    CHECK(strchr(after('P'), 'T') != NULL, "the Connect starts it");
    CHECK(strcmp(ended, "idle until the retry timer") == 0, "it retries, it is not parked in Disabled");
}

// An Autoconnect from Disabled starts the radio and walks the ladder
TEST(Connector, autoconnect_from_disabled)
{
    const uint8_t cmds[] = { CMD_WIFI_DISABLE, CMD_WIFI_AUTOCONNECT };
    run(false, cmds, 2);
    CHECK(strstr(trace, "PX") != NULL, "the radio is stopped");
    CHECK(strchr(after('P'), 'T') != NULL, "the Autoconnect starts it");
    CHECK(strchr(after('T'), 'L') != NULL, "then the last AP");
}

#endif /* MODULE_U64CTRL */

// The U64 and U2+L modules start with the radio off: their first command is the Enable
#ifdef MODULE_HAS_SWITCH
#define ENABLE_FIRST CMD_WIFI_ENABLE,
#else
#define ENABLE_FIRST
#endif

// A Connect that the ESP accepts and that then fails (EVENT_DISCONNECTED) while the
// connector is Connected: it ends in Disconnected, and the retry timer runs the last AP
// again, instead of the connector idling in Connected without a timeout (review M1)
TEST(Connector, failed_connect_while_connected_retries)
{
    const uint8_t cmds[] = { ENABLE_FIRST CMD_WIFI_CONNECT };
    last_ap_reachable = true;
    retry_timeouts = 1;
    run(false, cmds, sizeof(cmds));
    CHECK(strstr(trace, "LK") != NULL, "connected to the last AP first");
    CHECK(strchr(trace, 'c') != NULL, "the Connect was tried");
    CHECK(strcmp(ended, "idle without a timeout") != 0, "it does not idle in Connected");
    CHECK(strchr(strchr(trace, 'c'), 'L') != NULL, "after the failed Connect the retry runs the last AP");
    CHECK(strcmp(ended, "idle until the retry timer") == 0, "and it keeps retrying");
}

// The same for a Connect taken in the walk over the scanned APs: the walk ends, the
// stored APs are not walked, the connector retries
TEST(Connector, failed_connect_in_the_walk_retries)
{
    const uint8_t cmds[] = { ENABLE_FIRST CMD_WIFI_CONNECT };
    retry_timeouts = 1;
    run(false, cmds, sizeof(cmds));
    CHECK(strchr(trace, 'S') != NULL, "the walk began");
    CHECK(strchr(after('S'), 'L') != NULL, "after the failed Connect the retry runs the last AP");
}

// ... and from Disabled, after a Disable (a stale AP entry's Connect)
TEST(Connector, failed_connect_from_disabled_retries)
{
    const uint8_t cmds[] = { ENABLE_FIRST CMD_WIFI_DISABLE, CMD_WIFI_CONNECT };
    retry_timeouts = 1;
    run(false, cmds, sizeof(cmds));
    CHECK(strchr(after('P'), 'T') != NULL, "the Connect starts the radio");
    CHECK(strchr(after('T'), 'L') != NULL, "and the retry runs the last AP");
}

// A Connect is a request to be on the network: an explicit Disconnect from before is spent, so
// a failed Connect retries instead of waiting for the user
TEST(Connector, connect_spends_an_earlier_disconnect)
{
    const uint8_t cmds[] = { CMD_WIFI_CONNECT };
    run(true, cmds, 1);
    CHECK(strchr(trace, 'c') != NULL, "the Connect was tried");
    CHECK(strcmp(ended, "idle until the retry timer") == 0, "and the connector retries");
}
