/*
 * switch_task_test.cpp
 *
 * The radio switch in the WiFi application of the U64 and the U2+L, through the real
 * WiFi :: RunModeThread, RadioOn, RadioOff and the menu actions, "WiFi Enabled" hook and
 * effectuate_settings of network_esp32.cc: the stored setting at boot (and its default),
 * the confirmation of a radio off and its deadline, a module without the switch, and the
 * menu's Disable and Enable, the "WiFi Enabled" hook and a reset of the settings. A change
 * of the setting is queued for the WiFi task (EVENT_RADIO_SETTING), which sends the request.
 */

#include "wifi_task_test.h"
#include "wifi_enable_default.h"

#if U64 != 2
// The menu's Enable and Disable: the context menu and the config menu
static void menu_enable(void) { NetworkLWIP_WiFi :: enable(NULL); }
static void menu_disable(void) { NetworkLWIP_WiFi :: disable(NULL); }
static void cfg_menu_enable(void) { NetworkLWIP_WiFi :: cfg_enable(NULL, NULL); }
static void cfg_menu_disable(void) { NetworkLWIP_WiFi :: cfg_disable(NULL, NULL); }
#endif

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
    boot({ EVENT_CONNECTED, Step(menu_disable) });
    CHECK(strchr(after('U'), 'O') != NULL, "menu: asked");
    CHECK(strchr(rlog, 'A') == NULL && strchr(rlog, 'F') == NULL, "menu: neither stopped nor held off");
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

// The "WiFi Enabled" hook, as the setting changes (the setting reads its new value when the
// hook runs): it queues the change for the WiFi task, sends nothing itself and does not stop
// the application; every change is queued, whatever the row shows (the row can lag behind
// the module, which takes a request for the state it is in as a no-op)
static void hook(int value)
{
    world.enabled = (value != 0);
    ConfigItem it;
    it.value = value;
    NetworkLWIP_WiFi :: wifi_enable_change(&it);
}

TEST(WifiSwitchTask, menu_and_setting)
{
    reset_world();
    world.enabled = true;
    boot({ EVENT_CONNECTED });
    const struct { WifiState_t state; int value; const char *what; } queued_cases[] = {
        { eWifi_Connected, 0, "the setting to Disabled" },
        { eWifi_Disabled, 1, "the setting to Enabled" },
        { eWifi_Connected, 1, "Enabled while on: passed on" },
        { eWifi_Disabled, 0, "Disabled while off: passed on" },
        { eWifi_NotConnected, 1, "Enabled while the boot's radio off waits for its confirmation" },
        { eWifi_AppDetected, 0, "Disabled while the task sets up its link" },
        { eWifi_Scanning, 0, "Disabled while an AP list scan runs" },
    };
    for (const auto &c : queued_cases) {
        wifi.state = c.state;
        take_queued(0);
        rlog[0] = 0;
        hook(c.value);
        CHECK(take_queued(EVENT_RADIO_SETTING) == 1, c.what);
        CHECK(rlog[0] == 0, c.what);
    }
    wifi.state = eWifi_Off; // a module without the switch, held off
    rlog[0] = 0;
    hook(0);
    CHECK(rlog[0] == 0 && take_queued(EVENT_RADIO_SETTING) == 0, "Disabled with the application stopped: nothing to do");
    hook(1);
    CHECK(strcmp(rlog, "B") == 0, "the setting to Enabled starts the application again");
    CHECK(take_queued(EVENT_RADIO_SETTING) == 0, "and queues nothing");
    wifi.state = eWifi_ModuleDetected;
    rlog[0] = 0;
    hook(0);
    CHECK(rlog[0] == 0 && take_queued(EVENT_RADIO_SETTING) == 0, "while the module is being found, the setting is left to the boot");
}

// The task applies the queued change as the setting reads when it takes it: Disabled, then
// Enabled, then the confirmation of the radio off that the boot asked for is not awaited
TEST(WifiSwitchTask, enabled_while_the_boot_radio_off_waits)
{
    reset_world();
    world.enabled = false;
    boot({ Step(cfg_menu_enable), EVENT_DISCONNECTED });
    CHECK(strchr(after('O'), 'N') != NULL, "the radio on follows the boot's radio off");
    CHECK(!wifi.radio_switch.awaiting_off, "and ends the wait for its confirmation");
    CHECK(wifi.getState() == eWifi_NotConnected, "the module's event is taken: the radio is on");
}

// A module without the switch, stored Disabled: it is held off; an Enable (which stores
// Enabled, as every Enable does) restarts the application instead of asking the module,
// which cannot
TEST(WifiSwitchTask, enable_with_a_module_without_the_switch)
{
    reset_world();
    world.disable_answer = kNotSupported;
    boot({});
    CHECK(wifi.getState() == eWifi_Off, "Off");
    rlog[0] = 0;
    wifi.RadioOn();
    CHECK(strcmp(rlog, "B") == 0, "the application is started again, the module not asked");
}

// One switch: "WiFi Enabled" is what the menu's Enable and Disable (context menu and config
// menu) set and save; its change hook does the radio. A write happens only when the value
// changes, and a reboot reads the saved value.

TEST(WifiSwitchTask, menu_enable_sets_and_saves_the_setting)
{
    void (*enables[])(void) = { menu_enable, cfg_menu_enable };
    for (void (*act)(void) : enables) {
        reset_world();
        world.enabled = false; // stored Disabled
        boot({ EVENT_DISABLED, Step(act) });
        CHECK(world.enabled, "the setting reads Enabled");
        CHECK(saves == 1 && saved_enabled, "and it is saved");
        CHECK(strcmp(after('u'), "N") == 0, "the radio is asked to come on");
    }
}

TEST(WifiSwitchTask, menu_disable_sets_and_saves_the_setting)
{
    void (*disables[])(void) = { menu_disable, cfg_menu_disable };
    for (void (*act)(void) : disables) {
        reset_world();
        world.enabled = true;
        boot({ EVENT_CONNECTED, Step(act), EVENT_DISABLED });
        CHECK(!world.enabled, "the setting reads Disabled");
        CHECK(saves == 1 && !saved_enabled, "and it is saved");
        CHECK(strcmp(after('U'), "Ou") == 0, "the radio is asked to go off");
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
    boot({ EVENT_DISABLED, Step(menu_enable) }); // Disabled at run time, the setting still Enabled
    CHECK(saves == 0, "nothing is written");
    CHECK(strcmp(after('u'), "N") == 0, "but the radio is asked to come on");
    reset_world();
    world.enabled = false;
    boot({ EVENT_DISABLED, Step(menu_disable) });
    CHECK(saves == 0, "nor for a Disable of a setting that is Disabled");
}

// With a module that cannot switch, menu Disable holds the module off, as at boot: the link
// goes down and the task waits for an Enable. The task does not stop the application
// itself (that would end the task while it runs); the Enable starts it again.
TEST(WifiSwitchTask, menu_disable_old_module)
{
    reset_world();
    world.enabled = true;
    world.disable_answer = kNotSupported;
    boot({ EVENT_CONNECTED, Step(menu_disable) });
    CHECK(strcmp(after('U'), "OuF") == 0, "asked, refused, link down, module held off");
    CHECK(strchr(rlog, 'A') == NULL, "the task does not stop the application it runs in");
    CHECK(wifi.getState() == eWifi_Off, "Off");
    CHECK(strcmp(ended, "suspended") == 0, "the task waits for an Enable");
    rlog[0] = 0;
    NetworkLWIP_WiFi :: enable(NULL);
    CHECK(strcmp(rlog, "B") == 0, "Enable starts the application again");
}
#endif

#if U64 != 2
// Quick changes of "WiFi Enabled", faster than the module's events: the task applies the
// setting as it reads when the task gets to it, whatever the row shows, so the last setting
// wins; changes that come before that are applied once. (The row followed the module's
// events only: Enable then Disable from Disabled left the radio on and the link up with the
// setting Disabled, and Disable then Enable from Connected left it off with Enabled.)
static void enable_then_disable(void) { cfg_menu_enable(); cfg_menu_disable(); }
static void disable_then_enable(void) { cfg_menu_disable(); cfg_menu_enable(); }

TEST(WifiSwitchTask, quick_enable_then_disable_ends_off)
{
    reset_world();
    world.enabled = false;
    boot({ EVENT_DISABLED, Step(enable_then_disable),
           EVENT_DISCONNECTED, EVENT_CONNECTED, EVENT_GOTIP, EVENT_DISABLED });
    CHECK(strcmp(after('s'), "uOu") == 0, "the last setting is asked for: radio off");
    CHECK(wifi.getState() == eWifi_Disabled, "Disabled: link events while the radio off waits are dropped");
    CHECK(!world.enabled, "the setting reads Disabled");
}

TEST(WifiSwitchTask, quick_disable_then_enable_ends_on)
{
    reset_world();
    world.enabled = true;
    boot({ EVENT_CONNECTED, Step(disable_then_enable),
           EVENT_DISABLED, EVENT_DISCONNECTED, EVENT_CONNECTED });
    const char *n = strchr(after('U'), 'N');
    CHECK(n != NULL && strchr(n + 1, 'N') == NULL && strchr(after('U'), 'O') == NULL, "the last setting is asked for: radio on");
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
    boot({ EVENT_CONNECTED, Step(menu_disable), EVENT_GOTIP });
    CHECK(strstr(after('O'), "OQ") != NULL, "asked again at the deadline, then the link state is asked for");
}
#endif

#if U64 != 2
// A Disable while the task waits for an event without a bound (connected, nothing pending,
// a quiet module): the wait for its confirmation is bounded all the same, and when the
// module never confirms, the radio off is asked again and the link state taken again
TEST(WifiSwitchTask, runtime_disable_deadline_without_further_events)
{
    reset_world();
    world.enabled = true;
    world.link = 1;
    boot({ EVENT_CONNECTED, Step(menu_disable) });
    bool bounded = false;
    for (int i = 0; i < timed_waits_n; i++) {
        bounded |= (timed_waits[i] == pdMS_TO_TICKS(WIFI_SWITCH_CONFIRM_MS));
    }
    CHECK(bounded, "the wait for the confirmation is bounded");
    CHECK(strstr(after('O'), "OQ") != NULL, "asked again at the deadline, then the link state is asked for");
    CHECK(!wifi.radio_switch.awaiting_off, "nothing is awaited after the deadline");
}

// The menu, the REST API and the setting do not wait for the module: the request goes out
// from the WiFi task
static bool sent_by_caller;
static void menu_disable_watched(void)
{
    size_t n = strlen(rlog);
    menu_disable();
    sent_by_caller = strchr(rlog + n, 'O') != NULL;
}
static void menu_enable_watched(void)
{
    size_t n = strlen(rlog);
    menu_enable();
    sent_by_caller = strchr(rlog + n, 'N') != NULL;
}

TEST(WifiSwitchTask, the_caller_does_not_wait_for_the_module)
{
    reset_world();
    world.enabled = true;
    sent_by_caller = true;
    boot({ EVENT_CONNECTED, Step(menu_disable_watched), EVENT_DISABLED });
    CHECK(!sent_by_caller, "Disable: the caller sends nothing");
    CHECK(strchr(after('U'), 'O') != NULL, "the radio off is asked for");
    CHECK(wifi.getState() == eWifi_Disabled, "Disabled once confirmed");
    sent_by_caller = true;
    boot({ EVENT_DISABLED, Step(menu_enable_watched), EVENT_DISCONNECTED });
    CHECK(!sent_by_caller, "Enable: the caller sends nothing");
    CHECK(strchr(after('O'), 'N') != NULL, "the radio on is asked for");
}

// Another task's Enable while the Disable waits for its answer: the Enable is the last
// change, so no radio off is awaited after it, and the link it brings is taken
static bool awaited_after_both;
static void note_awaited(void) { awaited_after_both = wifi.radio_switch.awaiting_off; }

TEST(WifiSwitchTask, enable_while_the_disable_waits_for_its_answer)
{
    reset_world();
    world.enabled = true;
    world.during_disable = cfg_menu_enable;
    awaited_after_both = true;
    boot({ EVENT_CONNECTED, Step(menu_disable), Step(note_awaited),
           EVENT_DISABLED, EVENT_DISCONNECTED, EVENT_CONNECTED });
    CHECK(!awaited_after_both, "no radio off awaited after the later Enable");
    CHECK(strchr(after('O'), 'N') != NULL, "the Disable, then the Enable reach the module");
    CHECK(world.enabled, "the setting reads Enabled");
    CHECK(wifi.getState() == eWifi_Connected, "Connected");
}

// A reset or a load of the settings sets "WiFi Enabled" without its hook, then effectuates
// them (REST reset_to_default, a config load): the radio follows the setting
static void reset_to_enabled(void) { world.enabled = true; net.effectuate_settings(); }
static void reset_to_disabled(void) { world.enabled = false; net.effectuate_settings(); }

TEST(WifiSwitchTask, a_reset_of_the_settings_applies_wifi_enabled)
{
    reset_world();
    world.enabled = false;
    boot({ EVENT_DISABLED, Step(reset_to_enabled), EVENT_DISCONNECTED });
    CHECK(strchr(after('s'), 'N') != NULL, "to Enabled: the radio on is asked for");
    CHECK(wifi.getState() == eWifi_NotConnected, "and the radio is on");
    reset_world();
    world.enabled = true;
    boot({ EVENT_CONNECTED, Step(reset_to_disabled), EVENT_DISABLED });
    CHECK(strchr(after('U'), 'O') != NULL, "to Disabled: the radio off is asked for");
    CHECK(wifi.getState() == eWifi_Disabled, "Disabled once confirmed");
}

TEST(WifiSwitchTask, a_reset_of_the_settings_starts_a_held_off_module)
{
    reset_world();
    world.disable_answer = kNotSupported;
    boot({});
    CHECK(wifi.getState() == eWifi_Off, "held off");
    rlog[0] = 0;
    world.enabled = true;
    net.effectuate_settings();
    CHECK(strcmp(rlog, "B") == 0, "the application is started again");
}

// The task applies the setting itself while it sets up its link; the effectuate_settings
// that the interface's start runs in the task sends nothing more
TEST(WifiSwitchTask, the_start_of_the_interface_sends_nothing_more)
{
    reset_world();
    world.enabled = true;
    boot({ EVENT_CONNECTED });
    CHECK(strchr(after('N'), 'N') == NULL && strchr(rlog, 'O') == NULL, "Enabled: one radio on");
    reset_world();
    boot({ EVENT_DISABLED });
    CHECK(strchr(after('O'), 'O') == NULL && strchr(rlog, 'N') == NULL, "Disabled: one radio off");
}
#endif

#if U64 != 2
// The task's queue is full when the setting changes (its frames fill it while it waits for
// a request): the change is not lost, and the buffer of the refused event goes back
static int frees_in_act;
static void disable_with_a_full_queue(void)
{
    world.loopback_fails = true;
    frees = 0;
    menu_disable();
    frees_in_act = frees;
    world.loopback_fails = false;
}

TEST(WifiSwitchTask, a_change_with_a_full_queue_is_not_lost)
{
    reset_world();
    world.enabled = true;
    boot({ EVENT_CONNECTED, Step(disable_with_a_full_queue), EVENT_GOTIP, EVENT_DISABLED });
    CHECK(frees_in_act == 1, "the refused event's buffer goes back to the UART");
    CHECK(strchr(after('U'), 'O') != NULL, "the radio off is asked for on the task's next pass");
    CHECK(wifi.getState() == eWifi_Disabled, "Disabled once confirmed");
}

// A module without the switch refuses the radio off, and the setting is Enabled again by
// then (another task's Enable while it answered): the module is not held off
TEST(WifiSwitchTask, enable_while_a_module_without_the_switch_answers)
{
    reset_world();
    world.enabled = true;
    world.disable_answer = kNotSupported;
    world.during_disable = cfg_menu_enable;
    boot({ EVENT_CONNECTED, Step(menu_disable) });
    CHECK(world.enabled, "the setting reads Enabled");
    CHECK(strchr(rlog, 'F') == NULL && wifi.getState() == eWifi_Connected, "not held off: still Connected");
    // the same at boot, with Disabled stored
    reset_world();
    world.enabled = false;
    world.link = 1;
    world.disable_answer = kNotSupported;
    world.during_disable = cfg_menu_enable;
    boot({});
    CHECK(world.enabled, "boot: the setting reads Enabled");
    CHECK(strchr(rlog, 'F') == NULL && wifi.getState() == eWifi_Connected, "boot: not held off, the link taken from the module");
}

// A module without the switch: Disable (held off), Enable (the application starts again),
// and Disable while the new task is still finding the module: the new start applies the
// stored Disabled
TEST(WifiSwitchTask, off_on_off_with_a_module_without_the_switch)
{
    reset_world();
    world.enabled = false;
    world.disable_answer = kNotSupported;
    boot({});
    hook(1);
    wifi.state = eWifi_NotDetected; // as WiFi :: Start leaves it
    hook(0);
    world.keep_switch = true;
    world.disable_answer = 0;
    boot({});
    CHECK(strchr(rlog, 'O') != NULL && strchr(rlog, 'N') == NULL, "the stored Disabled is applied");
}

// The settings are effectuated each time the WiFi settings page closes: without a change of
// "WiFi Enabled" nothing goes to the module
static void close_wifi_settings(void) { net.effectuate_settings(); }

TEST(WifiSwitchTask, effectuate_without_a_change_sends_nothing)
{
    reset_world();
    world.enabled = true;
    boot({ EVENT_CONNECTED, Step(close_wifi_settings), Step(close_wifi_settings) });
    CHECK(strchr(after('N'), 'N') == NULL, "Enabled: no radio on after the boot's");
    reset_world();
    world.enabled = false;
    boot({ EVENT_DISABLED, Step(close_wifi_settings), Step(close_wifi_settings) });
    CHECK(strchr(after('O'), 'O') == NULL, "Disabled: no radio off after the boot's");
}

// A reset of the settings while the task sets up its link, whose effectuate the task's own
// netstack->start() takes (the store is no longer stale when the REST API effectuates it)
static void reset_without_effectuate(void) { world.enabled = true; }

TEST(WifiSwitchTask, a_reset_while_the_task_sets_up_is_applied)
{
    reset_world();
    world.enabled = false;
    world.during_disable = reset_without_effectuate;
    boot({ EVENT_DISABLED, EVENT_DISCONNECTED });
    CHECK(strchr(after('s'), 'N') != NULL, "the radio on follows the boot's radio off");
    CHECK(wifi.getState() == eWifi_NotConnected, "and the radio is on");
}
#endif
