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

TEST(WifiPersistTask, menu_disable_sets_and_saves_the_setting)
{
    reset_world();
    world.enabled = true;
    boot({ EVENT_CONNECTED });
    rlog[0] = 0;
    NetworkLWIP_WiFi :: disable(NULL);
    CHECK(!world.enabled, "the setting reads Disabled");
    CHECK(saves == 1 && !saved_enabled, "and it is saved");
    CHECK(strcmp(rlog, "O") == 0, "the radio is asked to go off, once");
    NetworkLWIP_WiFi :: disable(NULL);
    CHECK(saves == 1, "nothing is written when it did not change");
}

TEST(WifiPersistTask, menu_enable_sets_and_saves_the_setting)
{
    reset_world();
    world.enabled = false;
    boot({ EVENT_DISABLED });
    rlog[0] = 0;
    NetworkLWIP_WiFi :: enable(NULL);
    CHECK(world.enabled, "the setting reads Enabled");
    CHECK(saves == 1 && saved_enabled, "and it is saved");
    CHECK(strcmp(rlog, "N") == 0, "the radio is asked to come on");
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
#endif
