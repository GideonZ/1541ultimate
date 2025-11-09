extern "C" {
    #include "small_printf.h"
}
#include "userinterface.h"
#include <stdlib.h>
#include <string.h>
#include "mystring.h" // my string class
#include "commodore_menu.h"
#include "browsable_root.h"
#include "config_menu.h"
#include "tree_browser.h"
#include "assembly_search.h"
#include "system_info.h"
#include "task_menu.h"

typedef enum {
    e_audio_mixer = 0,
    e_sid_sockets,
    e_ultisid,
    e_u64_specific,
    e_c64_carts,
    e_clock,
    e_network,
    e_ethernet,
    e_wifi,
    e_led_strip,
    e_streams,
    e_softiec,
    e_printer,
    e_modem,
    e_user_interface,
    e_tape,
    e_drive_a,
    e_drive_b,
    e_memory,
    e_video,
    e_audio,
    e_leds,
    e_turbo,
    e_joystick,
} config_menus_t;

const char *config_menu_names[] = {
    "Audio Mixer",
    "SID Sockets Configuration",
    "UltiSID Configuration",
    "C64U Specific Settings",
    "Cartridge and ROM Settings",
    "Clock Settings",
    "Network Settings",
    "Ethernet Settings",
    "WiFi Settings",
    "LED Strip Settings",
    "Data Streams",
    "SoftIEC Settings",
    "Printer Settings",
    "Modem Settings",
    "User Interface Settings",
    "Tape Settings",
    "Drive A Settings",
    "Drive B Settings",
    "Memory Configuration",
    "Video Configuration",
    "Audio Configuration",
    "LED Lighting",
    "Turbo Settings",
    "Joystick Settings",
};

/************************/
/* CommodoreMenu Object */
/************************/
CommodoreMenu :: CommodoreMenu(UserInterface *ui) : ContextMenu(ui, NULL, 0, 0, MENU_HIDE, 0)
{
    // Action *dummy = new Action(" ", S_file_browser, 0);
    // dummy->disable();
    // appendAction(dummy);
    appendAction(new Action("DISK FILE BROWSER", S_file_browser, 0));
    appendAction(new Action("COMMOSERVE FILE SEARCH", S_assembly64, 0));
    appendAction(new Action("MEMORY & ROMS", S_cfg_group, e_memory));
    appendAction(new Action("TURBO BOOST", S_cfg_group, e_turbo));
    appendAction(new Action("VIDEO SETUP", S_cfg_group, e_video));
    appendAction(new Action("AUDIO SETUP", S_cfg_audio, 0));
    appendAction(new Action("JOYSTICK & CONTROLLERS", S_cfg_group, e_joystick));
    appendAction(new Action("LED LIGHTING", S_cfg_group, e_leds));
    appendAction(new Action("NETWORK SERVICES & TIMEZONE", S_cfg_page, e_network));
    appendAction(new Action("WIRED NETWORK SETUP", S_cfg_page, e_ethernet));
    appendAction(new Action("WI-FI NETWORK SETUP", S_cfg_page, e_wifi));
    appendAction(new Action("MODEMS", S_cfg_page, e_modem));
    appendAction(new Action("PRINTERS", S_cfg_page, e_printer));
    appendAction(new Action("USER INTERFACE", S_cfg_page, e_user_interface));
    appendAction(new Action("BUILT-IN DRIVE A OPTIONS", S_cfg_page, e_drive_a));
    appendAction(new Action("BUILT-IN DRIVE B OPTIONS", S_cfg_page, e_drive_b));
    //appendAction(new Action("ADVANCED SETTINGS", S_advanced, 0));
    appendAction(new Action("SYSTEM INFORMATION", S_sysinfo, 0));

    // Instantiate and attach the root tree browser
    Browsable *root = new BrowsableRoot();
    root_tree_browser = new TreeBrowser(ui, root);
    root_tree_browser->allow_exit = true;
}

CommodoreMenu :: ~CommodoreMenu()
{
    printf("Destructing Commodore browser..\n");
}

void CommodoreMenu :: init() // call on root!
{
    this->screen = get_ui()->get_screen();
    this->keyb = get_ui()->get_keyboard();
	window = new Window(screen, (screen->get_size_x() - 40) >> 1, 2, 40, screen->get_size_y()-3);
	window->draw_border();
}

int CommodoreMenu :: poll(int prev)
{
    mstring *msg = this->user_interface->getMessage();
    if (msg) {
        user_interface->popup(msg->c_str(), BUTTON_OK);
        delete msg;
    }

    int ret = 0;
    if(subContext) {
        if(prev < 0) {
        	delete subContext;
            subContext = NULL;
            draw();
        } else if(prev > 0) {
            // 0 is dummy, bec it is of the type ConfigItem. ConfigItem
            // itself knows the value that the actual object (= selected of THIS
            // browser!) needs to be called with. It would be better to just
            // create a return value of a GUI object, and call execute
            // with that immediately.
            draw();
            ret = subContext->executeSelected("");
            delete subContext;
            subContext = NULL;
            if (user_interface->has_focus(this)) {
                draw();
            } else {
                // we lost focus, apparently a new UI element is active
                // state->refresh = true; // refresh as soon as we come back
            }
        }
        return ret;
    }

    return ContextMenu::poll(prev);
}

int CommodoreMenu :: handle_key(int c)
{
    switch(c) {
        case KEY_TASKS:
            subContext = new TaskMenu(user_interface, NULL, NULL);
            subContext->init(window, keyb);
            user_interface->activate_uiobject(subContext);
            return 0;
        case KEY_CONFIG:
            ConfigBrowser::start(user_interface);
            return 0;
        case KEY_SYSINFO:
            SystemInfo::generate(user_interface);
            draw();
            return 0;
        case KEY_SEARCH:
            AssemblyInGui::S_OpenSearch(user_interface);
            return 0;
    }
    return ContextMenu :: handle_key(c);
}

void CommodoreMenu :: redraw()
{
    draw();
    screen->move_cursor(0, screen->get_size_y()-1);
    if (user_interface->navmode == 0) {
#if COMMODORE
        screen->output("\e1 CRSR+TYPE to NAV F3/F5=PGUP/DN F7=HELP");
#else
        screen->output("\e1 CRSR+TYPE to NAV F1/F7=PGUP/DN F3=HELP");
#endif
    } else {
#if COMMODORE
        screen->output("\e1 WASD=NAV F1=MENU F3/F5=PGUP/DN F7=HELP");
#else
        screen->output("\e1 WASD=NAV F5=MENU F1/F7=PGUP/DN F3=HELP");
#endif
    }
    context_state = e_active;
}

int CommodoreMenu :: select_item(void)
{
    selectedAction = actions[item_index];

    selectedAction->direct_func(selectedAction, this);
    return 0;
}


SubsysResultCode_e CommodoreMenu :: S_file_browser(Action *act, void *context)
{
    CommodoreMenu *menu = (CommodoreMenu *)context;
    menu->root_tree_browser->init();
    menu->user_interface->activate_uiobject(menu->root_tree_browser);
    return SSRET_OK;
}

SubsysResultCode_e CommodoreMenu :: S_cfg_page(Action *act, void *context)
{
    // Let's open the config browser, and tell it to go to level 1 directly, looking for the config page.
    CommodoreMenu *menu = (CommodoreMenu *)context;
    ConfigStore *store = ConfigManager :: getConfigManager()->find_store(config_menu_names[act->function]);
    if (store) {
        Browsable *configPage = new BrowsableConfigStore(store);
        ConfigBrowser *configBrowser = new ConfigBrowser(menu->user_interface, configPage, 1);
        configBrowser->init();
        menu->user_interface->activate_uiobject(configBrowser);
    }
    return SSRET_OK;
}

SubsysResultCode_e CommodoreMenu :: S_cfg_group(Action *act, void *context)
{
    // Let's open the config browser, and tell it to go to level 1 directly, looking for the config group.
    CommodoreMenu *menu = (CommodoreMenu *)context;
    ConfigGroup *group = ConfigGroupCollection :: getGroup(config_menu_names[act->function], 0);
    if (group) {
        Browsable *configPage = new BrowsableConfigGroup(group);
        ConfigBrowser *configBrowser = new ConfigBrowser(menu->user_interface, configPage, 1);
        configBrowser->init();
        menu->user_interface->activate_uiobject(configBrowser);
    }
    return SSRET_OK;
}

SubsysResultCode_e CommodoreMenu :: S_cfg_audio(Action *act, void *context)
{
    // Let's open the config browser, with one specific set of pages
    CommodoreMenu *menu = (CommodoreMenu *)context;
    static const char *names[] = { "Audio Mixer", "Speaker Mixer", "SID Sockets Configuration", "UltiSID Configuration", "SID Addressing", "SID Player Behavior" };
    static BrowsableConfigRootPredefined br(6, names);

    ConfigBrowser *configBrowser = new ConfigBrowser(menu->user_interface, &br);
    configBrowser->init();
    menu->user_interface->activate_uiobject(configBrowser);
    return SSRET_OK;
}

SubsysResultCode_e CommodoreMenu :: S_advanced(Action *act, void *context)
{
    CommodoreMenu *menu = (CommodoreMenu *)context;
    Browsable *configRoot = new BrowsableConfigRoot();
    ConfigBrowser *configBrowser = new ConfigBrowser(menu->user_interface, configRoot);
    configBrowser->init();
    menu->user_interface->activate_uiobject(configBrowser);
    return SSRET_OK;
}

SubsysResultCode_e CommodoreMenu :: S_assembly64(Action *act, void *context)
{
    CommodoreMenu *menu = (CommodoreMenu *)context;
    AssemblyInGui :: S_OpenSearch(menu->user_interface);
    return SSRET_OK;
}

SubsysResultCode_e CommodoreMenu :: S_sysinfo(Action *act, void *context)
{
    CommodoreMenu *menu = (CommodoreMenu *)context;
    SystemInfo::generate(menu->user_interface);
    menu->draw();
    return SSRET_OK;
}
