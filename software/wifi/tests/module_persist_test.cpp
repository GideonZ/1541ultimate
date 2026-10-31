/*
 * module_persist_test.cpp
 *
 * "WiFi Enabled" kept in the control module (u64ctrl, the Elite II / C64 Ultimate): the module
 * reads its copy of the setting before it starts the radio, so a Disabled unit never starts
 * its radio after mains-in; the Ultimate's Enable and Disable update the copy, and win over a
 * stale one; and the recovery's Autoconnect still brings the radio up and leaves the copy
 * alone. The cases run against the module's real boot start (wifi_start_at_boot), connector
 * and command handling. The U64 and U2+L modules keep their record elsewhere (radio_boot.h).
 */

#include <string.h>
#include "module_script.h"

#ifdef MODULE_U64CTRL

// A stored Disabled survives a simulated boot: the radio is not started, the connector
// waits in Disabled and tries nothing
TEST(Persist, stored_disabled_boot_starts_no_radio)
{
    const uint8_t cmds[] = { 0 };
    nvs_wifi_enabled = 0;
    run(false, cmds, 0);
    CHECK(strchr(boot_trace, 'T') == NULL, "the radio is not started at boot");
    CHECK(strpbrk(trace, "TLScs") == NULL, "and not by the connector: no scan, no connect attempt");
    CHECK(strcmp(ended, "waiting for a command") == 0, "the connector waits in Disabled");
    CHECK(nvs_wifi_writes == 0, "the copy is not written by a boot");
}

TEST(Persist, stored_enabled_boot_starts_the_radio)
{
    const uint8_t cmds[] = { 0 };
    run(false, cmds, 0);
    CHECK(strchr(boot_trace, 'T') != NULL, "the radio is started at boot, as before");
    CHECK(strchr(trace, 'L') != NULL, "and the connector walks to the last AP");
}

// The Ultimate's Disable and Enable update the copy
TEST(Persist, disable_and_enable_update_the_copy)
{
    const uint8_t dis[] = { CONNECTOR_SEND_DISABLE };
    run(false, dis, 1);
    CHECK(nvs_wifi_last == 0 && nvs_wifi_writes == 1, "a Disable writes the copy");
    CHECK(strchr(trace, 'P') != NULL && strchr(trace, 'X') != NULL, "and stops the radio, confirmed");

    nvs_wifi_enabled = 0;
    const uint8_t ena[] = { CONNECTOR_SEND_ENABLE };
    run(false, ena, 1);
    CHECK(nvs_wifi_last == 1 && nvs_wifi_writes == 1, "an Enable writes the copy");
    CHECK(strchr(trace, 'T') != NULL, "and starts the radio");
}

// The Ultimate's value overrides a stale copy of the module (a module swap, a failed write)
TEST(Persist, ultimates_setting_wins_over_a_stale_copy)
{
    // stale Disabled, Ultimate says Enabled
    nvs_wifi_enabled = 0;
    const uint8_t ena[] = { CONNECTOR_SEND_ENABLE };
    run(false, ena, 1);
    CHECK(strchr(boot_trace, 'T') == NULL, "the stale Disabled kept the radio off at boot");
    CHECK(strchr(trace, 'T') != NULL, "the Ultimate's Enable starts it");
    CHECK(strchr(trace, 'X') == NULL, "no EVENT_DISABLED");
    CHECK(nvs_wifi_last == 1, "and the copy now reads Enabled");
}

TEST(Persist, ultimates_disable_wins_over_a_stale_enabled_copy)
{
    // stale Enabled (radio started at boot), Ultimate says Disabled
    const uint8_t dis[] = { CONNECTOR_SEND_DISABLE };
    run(false, dis, 1);
    CHECK(strchr(boot_trace, 'T') != NULL, "the stale Enabled started the radio at boot");
    CHECK(strchr(trace, 'P') != NULL, "the Ultimate's Disable stops it");
    CHECK(strchr(trace, 'X') != NULL, "and EVENT_DISABLED confirms it");
    CHECK(strcmp(ended, "waiting for a command") == 0, "Disabled");
    CHECK(nvs_wifi_last == 0, "and the copy now reads Disabled");
}

// A Disable for a radio that is off already (stored Disabled) is confirmed, not stopped again
TEST(Persist, disable_when_already_off_is_confirmed)
{
    nvs_wifi_enabled = 0;
    const uint8_t dis[] = { CONNECTOR_SEND_DISABLE };
    run(false, dis, 1);
    CHECK(strchr(trace, 'P') == NULL, "no second stop");
    CHECK(strchr(trace, 'X') != NULL, "EVENT_DISABLED confirms it");
    CHECK(strchr(trace, 'T') == NULL, "the radio stays off");
}

// An Enable for a radio that is on is a no-op (the Ultimate sends one at every handshake)
TEST(Persist, enable_when_already_on_does_nothing)
{
    const uint8_t cmds[] = { CONNECTOR_SEND_ENABLE };
    run(false, cmds, 1);
    CHECK(strchr(trace, 'P') == NULL && strchr(trace, 'X') == NULL, "the radio is left alone");
}

// Network recovery asks for the radio with an Autoconnect: it comes up from a stored Disabled,
// and the stored setting is left alone
TEST(Persist, recovery_autoconnect_starts_the_radio_and_leaves_the_copy)
{
    nvs_wifi_enabled = 0;
    const uint8_t cmds[] = { CMD_WIFI_AUTOCONNECT };
    run(false, cmds, 1);
    CHECK(strchr(boot_trace, 'T') == NULL, "off at boot");
    CHECK(strchr(trace, 'T') != NULL, "the Autoconnect starts the radio");
    CHECK(nvs_wifi_writes == 0, "the copy is not written");
    CHECK(nvs_wifi_last == 0, "and still reads Disabled for the next boot");
}

#endif
