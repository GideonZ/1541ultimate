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
    "Drive B Settings"
};

/************************/
/* CommodoreMenu Object */
/************************/
CommodoreMenu :: CommodoreMenu(UserInterface *ui) : ContextMenu(ui, NULL, 1, 0, MENU_HIDE, 1)
{
    Action *dummy = new Action(" ", S_file_browser, 0);
    dummy->disable();
    appendAction(dummy);
    appendAction(new Action("DISK FILE BROWSER", S_file_browser, 0));
    appendAction(new Action("COMMOSERVE FILE SEARCH", S_assembly64, 0));
    appendAction(new Action("STARTUP & MEMORY", S_cfg_page, e_c64_carts));
    appendAction(new Action("VIDEO & SPEED", S_cfg_page, e_u64_specific));
    appendAction(new Action("NETWORK SERVICES & TIMEZONE", S_cfg_page, e_network));
    appendAction(new Action("WIRED NETWORK SETUP", S_cfg_page, e_ethernet));
    appendAction(new Action("WI-FI NETWORK SETUP", S_cfg_page, e_wifi));
    appendAction(new Action("MODEMS", S_cfg_page, e_modem));
    appendAction(new Action("PRINTERS", S_cfg_page, e_printer));
    appendAction(new Action("USER INTERFACE", S_cfg_page, e_user_interface));
    appendAction(new Action("DISK DRIVE A OPTIONS AND ROMS", S_cfg_page, e_drive_a));
    appendAction(new Action("DISK DRIVE B OPTIONS AND ROMS", S_cfg_page, e_drive_b));
    appendAction(new Action("ADVANCED SETTINGS", S_advanced, 0));
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
    return ContextMenu::poll(prev);
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