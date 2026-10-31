/*
 * disabled_task_test.cpp
 *
 * The WiFi application while WiFi is disabled: opening the AP list sends a rescan, which
 * must not start the radio (on every product, the Elite II / C64 Ultimate included); the
 * list is empty once the radio is off; and a pick from a list that was shown earlier
 * sends no connect request, which would turn the radio back on.
 */

#include "wifi_task_test.h"

#if U64 == 2
TEST(WifiDisabledTask, no_scan_while_disabled)
{
    reset_world();
    world.enabled = true;
    boot({ EVENT_DISABLED, EVENT_RESCAN });
    CHECK(wifi.getState() == eWifi_Disabled, "Disabled");
    CHECK(strchr(rlog, 'C') == NULL, "no scan while Disabled");
}

TEST(WifiDisabledTask, scan_while_enabled)
{
    reset_world();
    world.enabled = true;
    boot({ EVENT_RESCAN });
    CHECK(strchr(rlog, 'C') != NULL, "a scan, as before");
}
#endif

TEST(WifiDisabledTask, scan_after_enable)
{
    reset_world();
    world.enabled = true;
    boot({ EVENT_DISABLED, EVENT_DISCONNECTED, EVENT_RESCAN });
    CHECK(strchr(rlog, 'C') != NULL, "once the radio is on again, a scan");
}

// A list scanned before the radio went off is not shown after it: its entries cannot be
// picked
TEST(WifiDisabledTask, ap_list_is_empty_once_disabled)
{
    reset_world();
    world.enabled = true;
    wifi_aps.num_records = 3;
    boot({ EVENT_DISABLED });
    CHECK(wifi.getState() == eWifi_Disabled, "Disabled");
    CHECK(wifi_aps.num_records == 0, "no AP is kept");
    IndexedList<Browsable *> list(0, NULL);
    CHECK(wifi.getAccessPointItems(NULL, list) == 0, "no AP to pick");
    BrowsableStatic *row = (list.get_elements() == 1) ? dynamic_cast<BrowsableStatic *>(list[0]) : NULL;
    CHECK(row && strcmp(row->text, "WiFi is disabled") == 0, "the list says so");
}

TEST(WifiDisabledTask, ap_list_is_shown_while_enabled)
{
    reset_world();
    world.enabled = true;
    wifi_aps.num_records = 3;
    boot({ EVENT_CONNECTED });
    IndexedList<Browsable *> list(0, NULL);
    CHECK(wifi.getAccessPointItems(NULL, list) == 3, "the scanned APs");
    CHECK(list.get_elements() == 3, "are listed");
}

// Every scan or connect action of the menu while WiFi is disabled asks "Enable it?". Yes does
// what the Enable action does (the module is asked to turn its radio on), and then the action
// goes on; No sends nothing and leaves the state as it was.
static void act_show_aps(void) { UserInterface ui; NetworkLWIP_WiFi :: cfg_show_aps(&ui, NULL); }
static void act_enter_ap(void) { UserInterface ui; NetworkLWIP_WiFi :: cfg_enter_ap(&ui, NULL); }
static void act_conn_last(void) { UserInterface ui; NetworkLWIP_WiFi :: cfg_conn_last(&ui, NULL); }

// the action's own mark in the task's log, and what it sends to the module
struct Action_t { const char *name; void (*run)(void); char mark; };
static const Action_t actions[] = {
    { "Select AP from list", act_show_aps, 'C' }, // the scan, once the radio is on
    { "Enter AP manually", act_enter_ap, 'k' },
    { "Connect to last AP", act_conn_last, 'a' },
    { "pick from the list", pick, '!' },        // marks are checked below by requests
};

static void ask_case(const Action_t &a, bool yes)
{
    reset_world();
    world.enabled = true;
    world.popup_answer = yes ? BUTTON_YES : BUTTON_NO;
    if (yes) {
        boot({ EVENT_DISABLED, a.run, EVENT_DISCONNECTED });
    } else {
        boot({ EVENT_DISABLED, a.run });
    }
    char what[80];
    snprintf(what, sizeof(what), "%s: asked once", a.name);
    CHECK(popups == 1, what);
    const char *asked = after('?'); // what the task did after the question
    bool done = (a.run == pick) ? (requests == 1) : (strchr(asked, a.mark) != NULL);
    const char *enable = strchr(asked, 'N');
    if (yes) {
        snprintf(what, sizeof(what), "%s: Yes enables the radio", a.name);
        CHECK(enable != NULL, what);
        snprintf(what, sizeof(what), "%s: and then the action goes on", a.name);
        CHECK(done, what);
        if (a.run != pick && enable && a.mark != 'C') {
            snprintf(what, sizeof(what), "%s: after the Enable, not before", a.name);
            CHECK(strchr(asked, a.mark) > enable, what);
        }
        if (a.run == act_show_aps) {
            CHECK(strchr(asked, 'T') != NULL, "the list is opened");
        }
    } else {
        snprintf(what, sizeof(what), "%s: No sends nothing", a.name);
        CHECK(enable == NULL && !done && requests == 0 && strchr(asked, 'T') == NULL, what);
        CHECK(wifi.getState() == eWifi_Disabled, "and the state is unchanged");
        CHECK(wifi.event_when_enabled == 0, "and no scan is left waiting for the radio");
    }
}

TEST(WifiDisabledTask, yes_enables_and_the_action_goes_on)
{
    for (const Action_t &a : actions) {
        ask_case(a, true);
    }
}

// After a Yes the state is consistent with "radio on => not Disabled": "WiFi Enabled" reads
// Enabled, saved, as if it had been set in the menu, on every product
TEST(WifiDisabledTask, yes_leaves_wifi_enabled_set_and_saved)
{
    for (const Action_t &a : actions) {
        reset_world();
        world.enabled = false; // "WiFi Enabled" stored as Disabled
        boot({ EVENT_DISABLED, a.run, EVENT_DISCONNECTED });
        CHECK(strchr(after('?'), 'N') != NULL, "the radio is asked to come on");
        CHECK(world.enabled, "the setting reads Enabled");
        CHECK(saves == 1 && saved_enabled, "and it is saved, so it stays so after a reboot");
    }
}

TEST(WifiDisabledTask, no_leaves_the_setting_alone)
{
    for (const Action_t &a : actions) {
        reset_world();
        world.enabled = false;
        world.popup_answer = BUTTON_NO;
        boot({ EVENT_DISABLED, a.run });
        CHECK(!world.enabled && saves == 0, "No changes and saves nothing");
    }
}

TEST(WifiDisabledTask, no_does_nothing)
{
    for (const Action_t &a : actions) {
        ask_case(a, false);
    }
}

// The scan waits for the radio to be on; if WiFi is disabled again first, it is not sent
TEST(WifiDisabledTask, a_scan_waiting_for_the_radio_is_dropped_by_a_disable)
{
    reset_world();
    world.enabled = true;
    boot({ EVENT_DISABLED, act_show_aps, EVENT_DISABLED, EVENT_DISCONNECTED });
    CHECK(strchr(after('?'), 'C') == NULL, "no scan after the Disable");
}

TEST(WifiDisabledTask, no_prompt_while_active)
{
    for (const Action_t &a : actions) {
        reset_world();
        world.enabled = true;
        boot({ EVENT_CONNECTED, a.run });
        CHECK(popups == 0, "not asked while WiFi is on");
    }
}

TEST(WifiDisabledTask, pick_while_active_connects)
{
    reset_world();
    world.enabled = true;
    boot({ EVENT_CONNECTED, pick });
    CHECK(requests == 1, "a connect request");
    CHECK(((rpc_header_t *)request.data)->command == CMD_WIFI_CONNECT, "CMD_WIFI_CONNECT");
    CHECK(strstr(messages, "Can only connect") == NULL, "no refusal");
}
