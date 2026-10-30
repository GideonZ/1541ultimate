/*
 * switch_task_test.cpp
 *
 * The radio switch in the WiFi application of the U64 and the U2+L, through the real
 * WiFi :: RunModeThread, RadioOn, RadioOff and the menu actions and "WiFi Enabled" hook of
 * network_esp32.cc: the stored setting at boot (and its default), the confirmation of a
 * radio off and its deadline, a module without the switch, and the menu's Disable and
 * Enable and the "WiFi Enabled" hook.
 */

#include "wifi_task_test.h"
#include "wifi_enable_default.h"

// What wifi_disable() answers when nothing reached the module (wifi_cmd.h)
static const int kNothingSent = -1;
// What a module without the radio switch answers: ESP_ERR_NOT_SUPPORTED
static const int kNotSupported = 0x106;

// The module is found at boot on every product, and the WiFi application is the one that
// starts it (WiFi :: Init), whatever "WiFi Enabled" says
TEST(WifiSwitchTask, init_always_starts_the_module)
{
    reset_world();
    rlog[0] = 0;
    WiFi :: Init((void *)0, (void *)0);
    CHECK(strcmp(rlog, "B") == 0, "the module is started, with no condition on the setting");
}

#if U64 != 2
// "WiFi Enabled" is stored as Enabled until the user changes it: a boot with the default
// turns the radio on, and the link comes up when the module reports one
TEST(WifiSwitchTask, default_setting_is_enabled_and_boots_connected)
{
    reset_world();
    CHECK(WIFI_ENABLE_DEFAULT == 1, "the default of \"WiFi Enabled\" is Enabled");
    world.enabled = WIFI_ENABLE_DEFAULT != 0;
    boot({ EVENT_CONNECTED, EVENT_GOTIP });
    CHECK(strchr(rlog, 'O') == NULL, "no radio off");
    CHECK(strchr(rlog, 'N') != NULL, "radio on asked for");
    CHECK(strchr(after('s'), 'U') != NULL, "the link comes up");
    CHECK(wifi.getState() == eWifi_Connected, "Connected");
    CHECK(!wifi.radio_switch.awaiting_off, "nothing awaited");
}

// Stored Disabled: the radio off is asked for before the interface starts, the module's
// boot association does not bring the link up, and Disabled follows the confirmation
TEST(WifiSwitchTask, stored_disabled_boot)
{
    reset_world();
    boot({ EVENT_CONNECTED, EVENT_GOTIP, EVENT_DISABLED });
    CHECK(strstr(rlog, "dmOs") != NULL, "detect, MAC, radio off, then the interface starts");
#ifndef U64
    CHECK(strncmp(rlog, "DE", 2) == 0, "the U2+L finds the module by its boot message first");
#endif
    CHECK(strchr(rlog, 'U') == NULL, "the link never comes up");
    CHECK(strchr(rlog, 'N') == NULL, "the radio on is not asked for");
    CHECK(strchr(rlog, 'F') == NULL, "the module keeps running");
    CHECK(strchr(rlog, 'Q') == NULL || strchr(rlog, 'Q') > strchr(rlog, 'O'),
          "no connection query before the radio off");
    CHECK(wifi.getState() == eWifi_Disabled, "Disabled once confirmed");
}

TEST(WifiSwitchTask, disabled_only_after_the_confirmation)
{
    reset_world();
    boot({ EVENT_DISABLED });
    CHECK(first_wait_state == eWifi_NotConnected, "not Disabled before EVENT_DISABLED");
    CHECK(wifi.getState() == eWifi_Disabled, "Disabled after it");
}

// The module never confirms (its stop failed, or the request was lost): after the deadline
// the application takes the link state from the module again
TEST(WifiSwitchTask, unconfirmed_radio_off)
{
    reset_world();
    world.link = 1;
    boot({});
    CHECK(strstr(rlog, "dmOs") != NULL, "radio off asked for");
    CHECK(strchr(after('s'), 'Q') != NULL, "the link state is asked for again");
    CHECK(strchr(after('Q'), 'U') != NULL, "and the link follows the module");
    CHECK(wifi.getState() == eWifi_Connected, "Connected, as the module is");
    CHECK(now - 1000 >= pdMS_TO_TICKS(WIFI_SWITCH_CONFIRM_MS), "not before the deadline");
}

// A module without the switch, whose radio is on from its boot: it is held off, as a
// stored Disabled did before, and the link never comes up
TEST(WifiSwitchTask, module_without_the_switch)
{
    reset_world();
    world.disable_answer = kNotSupported;
    boot({ EVENT_CONNECTED, EVENT_GOTIP });
    CHECK(strstr(rlog, "OsF") != NULL, "radio off refused, then the module is held off");
    CHECK(strchr(rlog, 'U') == NULL, "the link never comes up");
    CHECK(wifi.getState() == eWifi_Off, "Off");
    CHECK(strcmp(ended, "suspended") == 0, "the application waits for an Enable");
}

// The boot's Disable never left (no transmit buffer): it is not taken for a module
// without the switch, and when the confirmation is overdue it is asked for again before
// the connection query, which the module would otherwise take as an old application's
TEST(WifiSwitchTask, boot_disable_not_sent)
{
    reset_world();
    world.link = 1;
    world.disable_answer = kNothingSent;
    boot({});
    CHECK(strchr(rlog, 'F') == NULL, "not held off");
    const char *q = strchr(rlog, 'Q');
    const char *again = strchr(after('O'), 'O');
    CHECK((q != NULL) && (again != NULL) && (q > again), "asked again before the query");
}

// The radio off asked for again after a restart could not be sent: the wait for its
// confirmation still ends at the deadline, which asks again, so that link events are not
// dropped for good
TEST(WifiSwitchTask, restart_disable_not_sent)
{
    reset_world();
    world.disable_answer = kNothingSent;
    boot({ EVENT_DISABLED, EVENT_RESTARTED });
    CHECK(strchr(rlog, 'F') == NULL, "not held off");
    CHECK(strchr(rlog, 'Q') != NULL, "the deadline comes, and the link state is asked for");
}

// Only ESP_ERR_NOT_SUPPORTED means a module without the switch. A module with it that
// could not queue the request (ESP_ERR_NO_MEM) is not held off, at boot or from the menu.
TEST(WifiSwitchTask, other_errors_are_not_no_switch)
{
    reset_world();
    world.disable_answer = 0x101;
    boot({});
    CHECK(strchr(rlog, 'F') == NULL, "boot: not held off");
    CHECK(strchr(after('O'), 'O') != NULL, "boot: asked again at the deadline");
    world.enabled = true;
    boot({ EVENT_CONNECTED });
    rlog[0] = 0;
    NetworkLWIP_WiFi :: disable(NULL);
    CHECK(strcmp(rlog, "O") == 0, "menu: asked, neither stopped nor held off");
}

// A Connect on an AP entry while Disabled starts the module's radio, whose link events
// then arrive while the application is still Disabled: they take it out of Disabled, and
// no radio off is asked for (only EVENT_RESTARTED means the module restarted)
TEST(WifiSwitchTask, link_events_take_it_out_of_disabled)
{
    reset_world();
    boot({ EVENT_DISABLED, EVENT_DISCONNECTED, EVENT_CONNECTED });
    CHECK(strchr(after('O'), 'O') == NULL, "the radio off is not asked for again");
    CHECK(strchr(rlog, 'U') != NULL, "the link comes up");
    CHECK(wifi.getState() == eWifi_Connected, "Connected");
}

// The module restarted on its own (radio_boot.h): while Disabled the radio off is asked
// for again and the link events of a radio it turned on are dropped until it confirms;
// while enabled, nothing is asked
TEST(WifiSwitchTask, module_restarted)
{
    reset_world();
    boot({ EVENT_DISABLED, EVENT_RESTARTED, EVENT_DISCONNECTED, EVENT_CONNECTED, EVENT_DISABLED });
    const char *again = strchr(after('O'), 'O');
    CHECK(again != NULL, "the radio off is asked for again");
    CHECK(strchr(rlog, 'U') == NULL, "the link never comes up");
    CHECK(wifi.getState() == eWifi_Disabled, "Disabled");
    world.enabled = true;
    boot({ EVENT_CONNECTED, EVENT_RESTARTED });
    CHECK(strchr(rlog, 'O') == NULL, "enabled: no radio off");
}

// Disabled means no RF: browsing the AP list does not scan
TEST(WifiSwitchTask, no_scan_while_disabled)
{
    reset_world();
    boot({ EVENT_DISABLED, EVENT_RESCAN });
    CHECK(strchr(rlog, 'C') == NULL, "no scan");
    CHECK(wifi.getState() == eWifi_Disabled, "still Disabled");
}

// Also before the module has confirmed the radio off: a rescan then (opening the AP list
// at boot) would reach the module behind the Disable
TEST(WifiSwitchTask, no_scan_while_the_radio_off_waits)
{
    reset_world();
    boot({ EVENT_RESCAN, EVENT_DISABLED });
    CHECK(strchr(rlog, 'C') == NULL, "no scan");
    CHECK(wifi.getState() == eWifi_Disabled, "Disabled once confirmed");
}

TEST(WifiSwitchTask, scan_while_enabled)
{
    reset_world();
    world.enabled = true;
    boot({ EVENT_RESCAN });
    CHECK(strchr(rlog, 'C') != NULL, "a scan, as before");
}

// Stored Enabled: the radio on is asked for before the connection query, which would start
// the radio of a module on its own
TEST(WifiSwitchTask, stored_enabled_asks_before_the_query)
{
    reset_world();
    world.enabled = true;
    boot({ EVENT_CONNECTED });
    CHECK(strchr(after('N'), 'Q') != NULL && strchr(rlog, 'Q') > strchr(rlog, 'N'),
          "radio on, then the connection query");
}

// The menu's Disable and Enable, and the "WiFi Enabled" hook, ask the module; they do not
// stop the application
TEST(WifiSwitchTask, menu_and_setting)
{
    reset_world();
    world.enabled = true;
    boot({ EVENT_CONNECTED });
    rlog[0] = 0;
    NetworkLWIP_WiFi :: disable(NULL);
    CHECK(strcmp(rlog, "O") == 0, "menu Disable asks the module for radio off");
    wifi.state = eWifi_Disabled;
    rlog[0] = 0;
    NetworkLWIP_WiFi :: enable(NULL);
    CHECK(strcmp(rlog, "N") == 0, "menu Enable asks the module for radio on");
    ConfigItem it;
    wifi.state = eWifi_Connected;
    rlog[0] = 0;
    it.value = 0;
    NetworkLWIP_WiFi :: wifi_enable_change(&it);
    CHECK(strcmp(rlog, "O") == 0, "the setting to Disabled turns the radio off");
    wifi.state = eWifi_Disabled;
    rlog[0] = 0;
    it.value = 1;
    NetworkLWIP_WiFi :: wifi_enable_change(&it);
    CHECK(strcmp(rlog, "N") == 0, "the setting to Enabled turns it on");
    // A setting that is the state already is passed on all the same (the row can lag behind
    // the module): the module takes it as a request it is in already
    wifi.state = eWifi_Connected;
    rlog[0] = 0;
    it.value = 1;
    NetworkLWIP_WiFi :: wifi_enable_change(&it);
    CHECK(strcmp(rlog, "N") == 0, "Enabled while on: passed on");
    wifi.state = eWifi_Disabled;
    rlog[0] = 0;
    it.value = 0;
    NetworkLWIP_WiFi :: wifi_enable_change(&it);
    CHECK(strcmp(rlog, "O") == 0, "Disabled while off: passed on");
    wifi.state = eWifi_Off; // a module without the switch, held off
    rlog[0] = 0;
    it.value = 0;
    NetworkLWIP_WiFi :: wifi_enable_change(&it);
    CHECK(rlog[0] == 0, "Disabled with the application stopped: nothing to do");
    it.value = 1;
    NetworkLWIP_WiFi :: wifi_enable_change(&it);
    CHECK(strcmp(rlog, "B") == 0, "the setting to Enabled starts the application again");
    wifi.state = eWifi_ModuleDetected;
    rlog[0] = 0;
    it.value = 0;
    NetworkLWIP_WiFi :: wifi_enable_change(&it);
    CHECK(rlog[0] == 0, "while the module is being found, the setting is left to the boot");
    // The setting to Enabled while the boot's radio off waits for its confirmation (the
    // row still reads as not connected): it is not lost
    wifi.state = eWifi_NotConnected;
    wifi.radio_switch.awaiting_off = true;
    rlog[0] = 0;
    it.value = 1;
    NetworkLWIP_WiFi :: wifi_enable_change(&it);
    CHECK(strcmp(rlog, "N") == 0, "the setting to Enabled during the wait turns the radio on");
    CHECK(!wifi.radio_switch.awaiting_off, "and ends the wait");
}

// A module without the switch, stored Disabled: it is held off; an Enable restarts the
// application (which then skips the stored Disabled) instead of asking the module, which
// cannot
TEST(WifiSwitchTask, enable_with_a_module_without_the_switch)
{
    reset_world();
    world.disable_answer = kNotSupported;
    boot({});
    CHECK(wifi.getState() == eWifi_Off, "Off");
    rlog[0] = 0;
    wifi.RadioOn();
    CHECK(strcmp(rlog, "B") == 0, "the application is started again, the module not asked");
    CHECK(wifi.radio_switch.override_off, "and that start skips the stored Disabled");
}

// While the application is still setting up its link (AppDetected) it talks to the module
TEST(WifiSwitchTask, radio_off_while_setting_up)
{
    reset_world();
    world.enabled = true;
    boot({});
    wifi.state = eWifi_AppDetected;
    rlog[0] = 0;
    wifi.RadioOff();
    CHECK(strcmp(rlog, "O") == 0, "the module is asked, the application not stopped");
}

// One switch: "WiFi Enabled" is what the menu's Enable and Disable (context menu and config
// menu) set and save; its change hook does the radio. A write happens only when the value
// changes, and a reboot reads the saved value.
static void menu_enable(void) { NetworkLWIP_WiFi :: enable(NULL); }
static void menu_disable(void) { NetworkLWIP_WiFi :: disable(NULL); }
static void cfg_menu_enable(void) { NetworkLWIP_WiFi :: cfg_enable(NULL, NULL); }
static void cfg_menu_disable(void) { NetworkLWIP_WiFi :: cfg_disable(NULL, NULL); }

TEST(WifiSwitchTask, menu_enable_sets_and_saves_the_setting)
{
    void (*enables[])(void) = { menu_enable, cfg_menu_enable };
    for (void (*act)(void) : enables) {
        reset_world();
        world.enabled = false; // stored Disabled
        boot({ EVENT_DISABLED });
        rlog[0] = 0;
        act();
        CHECK(world.enabled, "the setting reads Enabled");
        CHECK(saves == 1 && saved_enabled, "and it is saved");
        CHECK(strcmp(rlog, "N") == 0, "the radio is asked to come on");
    }
}

TEST(WifiSwitchTask, menu_disable_sets_and_saves_the_setting)
{
    void (*disables[])(void) = { menu_disable, cfg_menu_disable };
    for (void (*act)(void) : disables) {
        reset_world();
        world.enabled = true;
        boot({ EVENT_CONNECTED });
        rlog[0] = 0;
        act();
        CHECK(!world.enabled, "the setting reads Disabled");
        CHECK(saves == 1 && !saved_enabled, "and it is saved");
        CHECK(strcmp(rlog, "O") == 0, "the radio is asked to go off");
        // a reboot reads what was saved: the radio is asked off at boot, not on
        bool stored = saved_enabled;
        reset_world();
        world.enabled = stored;
        boot({ EVENT_DISABLED });
        CHECK(strchr(rlog, 'O') != NULL && strchr(rlog, 'N') == NULL, "after a reboot the radio is off, not asked on");
    }
}

TEST(WifiSwitchTask, no_write_when_the_setting_does_not_change)
{
    reset_world();
    world.enabled = true;
    boot({ EVENT_DISABLED }); // Disabled at run time, the setting still Enabled
    rlog[0] = 0;
    menu_enable();
    CHECK(saves == 0, "nothing is written");
    CHECK(strcmp(rlog, "N") == 0, "but the radio is asked to come on");
    reset_world();
    world.enabled = false;
    boot({ EVENT_DISABLED });
    menu_disable();
    CHECK(saves == 0, "nor for a Disable of a setting that is Disabled");
}

// With a module that cannot switch, menu Disable stops the application as before
TEST(WifiSwitchTask, menu_disable_old_module)
{
    reset_world();
    world.enabled = true;
    boot({ EVENT_CONNECTED });
    rlog[0] = 0;
    world.disable_answer = kNotSupported;
    NetworkLWIP_WiFi :: disable(NULL);
    CHECK(strcmp(rlog, "OAF") == 0, "asked, refused, application stopped, module held off");
    rlog[0] = 0;
    NetworkLWIP_WiFi :: enable(NULL);
    CHECK(strcmp(rlog, "B") == 0, "Enable starts the application again");
}
#endif

#if U64 != 2
// Quick changes of "WiFi Enabled", faster than the module's events: each one reaches the
// module, whatever the row shows, so the last setting wins. (The row followed the module's
// events only: Enable then Disable from Disabled left the radio on and the link up with
// the setting Disabled, and Disable then Enable from Connected left it off with Enabled.)
static void enable_then_disable(void) { cfg_menu_enable(); cfg_menu_disable(); }
static void disable_then_enable(void) { cfg_menu_disable(); cfg_menu_enable(); }

TEST(WifiSwitchTask, quick_enable_then_disable_ends_off)
{
    reset_world();
    world.enabled = false;
    boot({ EVENT_DISABLED, Step(enable_then_disable),
           EVENT_DISCONNECTED, EVENT_CONNECTED, EVENT_GOTIP, EVENT_DISABLED });
    CHECK(strstr(after('s'), "NO") != NULL, "the Enable and the Disable both reach the module");
    CHECK(wifi.getState() == eWifi_Disabled, "Disabled: the module's link events of the Enable are dropped");
    CHECK(!world.enabled, "the setting reads Disabled");
}

TEST(WifiSwitchTask, quick_disable_then_enable_ends_on)
{
    reset_world();
    world.enabled = true;
    boot({ EVENT_CONNECTED, Step(disable_then_enable),
           EVENT_DISABLED, EVENT_DISCONNECTED, EVENT_CONNECTED });
    CHECK(strstr(after('U'), "ON") != NULL, "the Disable and the Enable both reach the module");
    CHECK(wifi.getState() == eWifi_Connected, "Connected again");
    CHECK(world.enabled, "the setting reads Enabled");
}

// A Disable at runtime is confirmed as the boot's is: Disabled is shown when the module
// says so, its link events are dropped until then, and when it never says so, the radio
// off is asked again and the link state is taken from the module again
TEST(WifiSwitchTask, runtime_disable_is_confirmed)
{
    reset_world();
    world.enabled = true;
    boot({ EVENT_CONNECTED, Step(menu_disable), EVENT_DISCONNECTED, EVENT_GOTIP, EVENT_DISABLED });
    CHECK(wifi.getState() == eWifi_Disabled, "Disabled once confirmed");
    CHECK(strchr(after('O'), 'U') == NULL, "no link comes up after the Disable");
}

TEST(WifiSwitchTask, runtime_disable_unconfirmed)
{
    reset_world();
    world.enabled = true;
    world.link = 1;
    // (the deadline is timed from the task's next pass, which the next event starts)
    boot({ EVENT_CONNECTED, Step(menu_disable), EVENT_GOTIP });
    CHECK(strstr(after('O'), "OQ") != NULL, "asked again at the deadline, then the link state is asked for");
}
#endif

#if U64 != 2
// While an AP list scan is under way the module is still talked to: a Disable reaches it
// and the WiFi application keeps running
TEST(WifiSwitchTask, disable_while_scanning_reaches_the_module)
{
    reset_world();
    world.enabled = true;
    boot({ EVENT_CONNECTED });
    wifi.state = eWifi_Scanning;
    rlog[0] = 0;
    NetworkLWIP_WiFi :: disable(NULL);
    CHECK(strcmp(rlog, "O") == 0, "the module is asked, the application not stopped");
}
#endif
