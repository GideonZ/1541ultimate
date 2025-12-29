/*
 * network_esp32.c
 *
 *  Created on: Nov 11, 2021
 *      Author: gideon
 */

#include "FreeRTOS.h"
#include "network_esp32.h"
#include "rpc_calls.h"
#include <string.h>
#include "dump_hex.h"
#include "subsys.h"
#include "userinterface.h"
#include "tree_browser.h"
// Concept:
// 1) Take buffer from cmd buffer free queue (may block)
// 2) Fill buffer with command and task and sequence code
// 3) Fill buffer with params
// 4) Send buffer by putting it in the transmit queue
// 5) Block task with 'wait notify'
// 6) Use notification value as pointer to receive packet
// 7) Take response from receive packet, like retval and errno, and/or data
// 8) Return buffer to cmd buffer free queue

//#define ENTRY(t) cmd_buffer_get

#include "wifi.h"
#include "itu.h"

const char *authmodes[] = { "Open", "WEP", "WPA PSK", "WPA2 PSK", "WPA/WPA2 PSK", "WPA2 Enterprise", "WPA3 PSK", "WPA2/WPA3 PSK", "WAPI PSK" };

//-----------------------------------
struct t_cfg_definition wifi_config[] = {
#if U64 == 2
    { CFG_WIFI_ENA,    CFG_TYPE_FUNC,   "Enable",                        "",(const char **)NetworkLWIP_WiFi :: cfg_enable, 0, 0, 0 },
    { CFG_WIFI_DIS,    CFG_TYPE_FUNC,   "Disable",                       "",(const char **)NetworkLWIP_WiFi :: cfg_disable, 0, 0, 0 },
#else
    { CFG_WIFI_ENABLE, CFG_TYPE_ENUM,   "WiFi Enabled",                  "%s", en_dis,     0,  1, 1 },
#endif
    { CFG_WIFI_DISCONN,CFG_TYPE_FUNC,   "Disconnect",                    "",(const char **)NetworkLWIP_WiFi :: cfg_disconn, 0, 0, 0 },
    { CFG_WIFI_CONN,   CFG_TYPE_FUNC,   "Connect to last AP",            "",(const char **)NetworkLWIP_WiFi :: cfg_conn_last, 0, 0, 0 },
    { CFG_WIFI_SEL_AP, CFG_TYPE_FUNC,   "Select AP from list",           "",(const char **)NetworkLWIP_WiFi :: cfg_show_aps, 0, 0, 0 },
    { CFG_WIFI_ENT_AP, CFG_TYPE_FUNC,   "Enter AP manually",             "",(const char **)NetworkLWIP_WiFi :: cfg_enter_ap, 0, 0, 0 },
    { CFG_WIFI_FORGET, CFG_TYPE_FUNC,   "Forget APs",                    "",(const char **)NetworkLWIP_WiFi :: cfg_forget, 0, 0, 0 },
    { CFG_SEPARATOR,   CFG_TYPE_SEP,    "",                               "",  NULL,       0,  0, 0 },
    { CFG_NET_DHCP_EN, CFG_TYPE_ENUM,   "Use DHCP",                      "%s", en_dis,     0,  1, 1 },
	{ CFG_NET_IP,      CFG_TYPE_STRING, "Static IP",					 "%s", NULL,       7, 16, (int)"192.168.2.64" },
	{ CFG_NET_NETMASK, CFG_TYPE_STRING, "Static Netmask",				 "%s", NULL,       7, 16, (int)"255.255.255.0" },
	{ CFG_NET_GATEWAY, CFG_TYPE_STRING, "Static Gateway",				 "%s", NULL,       7, 16, (int)"192.168.2.1" },
	{ CFG_NET_DNS,     CFG_TYPE_STRING, "Static DNS",		      		 "%s", NULL,       7, 16, (int)"8.8.8.8" },
    { CFG_SEPARATOR,   CFG_TYPE_SEP,    "",                               "",  NULL,       0,  0, 0 },
    { CFG_WIFI_STATUS, CFG_TYPE_INFO,   "Status",                        "%s", NULL,       0, 32, (int)"" },
    { CFG_WIFI_CUR_AP, CFG_TYPE_INFO,   "Connected to",                  "%s", NULL,       0, 32, (int)"" },
    { CFG_WIFI_CUR_IP, CFG_TYPE_INFO,   "Active IP address",             "%s", NULL,       0, 32, (int)"" },
    { CFG_WIFI_MAC,    CFG_TYPE_INFO,   "Interface MAC",                 "%s", NULL,       0, 32, (int)"" },
	{ CFG_TYPE_END,    CFG_TYPE_END,    "", "", NULL, 0, 0, 0 }
};

NetworkLWIP_WiFi :: NetworkLWIP_WiFi(void *driver, driver_output_function_t out, driver_free_function_t free)
    : NetworkInterface(driver, out, free)
{
}

NetworkLWIP_WiFi :: ~NetworkLWIP_WiFi()
{

}

void NetworkLWIP_WiFi :: getDisplayString(int index, char *buffer, int width)
{
    char ip[16];

    WifiState_t state = wifi.getState();

    switch (state) {
    case eWifi_Off:
        sprintf(buffer, "WiFi    %#s\eLDisabled", width - 17, "");
        break;
    case eWifi_Download:
        sprintf(buffer, "WiFi    %#s\eGBusy", width - 17, "Programming ESP32");
        break;
    case eWifi_NotDetected:
        sprintf(buffer, "WiFi    %#s\eJNo Module", width - 17, "");
        break;
    case eWifi_ModuleDetected:
        sprintf(buffer, "WiFi    %#s\eGFirmware?", width - 17, wifi.getModuleType());
        break;
    case eWifi_AppDetected:               
    case eWifi_Scanning:
        sprintf(buffer, "WiFi    %#s\eGScanning", width - 17, wifi.getModuleName());
        break;
    case eWifi_NotConnected:
        sprintf(buffer, "WiFi    MAC %b:%b:%b:%b:%b:%b%#s\eJLink Down", mac_address[0], mac_address[1], mac_address[2],
                mac_address[3], mac_address[4], mac_address[5], width - 38, "");
        break;
    case eWifi_Connected:
        sprintf(buffer, "WiFi    IP: %#s\eMLink Up", width - 21, getIpAddrString(ip, 16));
        break;
    case eWifi_Disabled:
        sprintf(buffer, "WiFi    %#s\eJDisabled", width - 17, wifi.getModuleName());
        break;
    default:
        sprintf(buffer, "WiFi%#s\eJUnknown", width - 17, "");
        break;
    }
}

void NetworkLWIP_WiFi :: getSubItems(Browsable *parent, IndexedList<Browsable *> &list, int &error)
{
    wifi.getAccessPointItems(parent, list);
    error = 0;
}

void NetworkLWIP_WiFi :: attach_config()
{
	register_store(0x57494649, "WiFi settings", wifi_config);
    cfg->set_change_hook(CFG_NET_DHCP_EN, dhcp_change);
    dhcp_change(cfg->find_item(CFG_NET_DHCP_EN));
}

// from ConfigurableObject
void NetworkLWIP_WiFi :: effectuate_settings(void)
{
    NetworkInterface :: effectuate_settings();

	my_net_if.name[0] = 'W';
	my_net_if.name[1] = 'I';

#if U64 == 2
    // if (cfg->get_value(CFG_WIFI_ENABLE) && wifi.getState() == eWifi_Disabled) {
    //     wifi.RadioOn();
    // } else if (!(cfg->get_value(CFG_WIFI_ENABLE)) && wifi.getState() != eWifi_Disabled) {
    //     wifi.RadioOff();
    // }
#else
    if (wifi.getState() == eWifi_Off && cfg->get_value(CFG_WIFI_ENABLE)) {
        wifi.Enable();
    }
//  For now, we don't disable the Wifi here, because effectuate_settings is also called when the init callback of the network stack occurs, OWWW
//    else if (wifi.getState() != eWifi_Off && !cfg->get_value(CFG_WIFI_ENABLE)) {
//        wifi.Disable();
//    }
#endif
}

void NetworkLWIP_WiFi :: on_edit()
{
    ConfigItem *it = cfg->find_item(CFG_WIFI_CUR_AP);
    it->setEnabled(false);
    it->setString(wifi.getLastAP());

    it = cfg->find_item(CFG_WIFI_STATUS);
    it->setEnabled(false);

    bool ok_to_connect = false;
    WifiState_t state = wifi.getState();
    switch (state) {
    case eWifi_Off:
        it->setString("OFF");
        break;
    case eWifi_Download:
        it->setString("Programming");
        break;
    case eWifi_NotDetected:
        it->setString("No Module Detected");
        break;
    case eWifi_ModuleDetected:
        it->setString("Detected, ...");
        break;
    case eWifi_AppDetected:               
    case eWifi_Scanning:
        it->setString("Scanning...");
        break;
    case eWifi_NotConnected:
        it->setString("Link Down");
        ok_to_connect = true;
        break;
    case eWifi_Connected:
        it->setString("Link Up");
        ok_to_connect = true;
        break;
    case eWifi_Disabled:
        it->setString("Disabled");
        break;
    default:
        it->setString("???");
        break;
    }

    // cfg->find_item(CFG_WIFI_ENT_AP)->setEnabled(ok_to_connect);
    // cfg->find_item(CFG_WIFI_SEL_AP)->setEnabled(ok_to_connect);
    // cfg->find_item(CFG_WIFI_CONN)->setEnabled(ok_to_connect);

    uint8_t mac[8];
    char buf[32];
    wifi.getMacAddr(mac);
    sprintf(buf, "%b:%b:%b:%b:%b:%b", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    it = cfg->find_item(CFG_WIFI_MAC);
    it->setEnabled(false);
    it->setString(buf);

    it = cfg->find_item(CFG_WIFI_CUR_IP);
    it->setEnabled(false);
    it->setString(getIpAddrString(buf, 16));
}

void NetworkLWIP_WiFi :: fetch_context_items(IndexedList<Action *>&items)
{
    if (wifi.getState() == eWifi_Connected) {
        items.append(new Action("Forget APs", NetworkLWIP_WiFi :: clear_aps, 0, 0));
        items.append(new Action("Disconnect", NetworkLWIP_WiFi :: disconnect, 0, 0));
        items.append(new Action("Disable", NetworkLWIP_WiFi :: disable, 0, 0));
    } else if (wifi.getState() == eWifi_NotConnected) {
        items.append(new Action("Show APs..", NetworkLWIP_WiFi :: list_aps, 0, 0));
        items.append(new Action("Connect", NetworkLWIP_WiFi :: auto_connect, 0, 0));
        items.append(new Action("Connect to..", NetworkLWIP_WiFi :: manual_connect, 0, 0));
        // items.append(new Action("Rescan APs", NetworkLWIP_WiFi :: rescan, 0, 0));
        items.append(new Action("Forget APs", NetworkLWIP_WiFi :: clear_aps, 0, 0));
        items.append(new Action("Disable", NetworkLWIP_WiFi :: disable, 0, 0));
    } else if ((wifi.getState() == eWifi_Off) || (wifi.getState() == eWifi_Disabled)) {
        items.append(new Action("Enable", NetworkLWIP_WiFi :: enable, 0, 0));
    } else if (wifi.getState() == eWifi_NotDetected) {
        items.append(new Action("Disable", NetworkLWIP_WiFi :: disable, 0, 0));
    }
}

SubsysResultCode_e NetworkLWIP_WiFi :: disconnect(SubsysCommand *cmd)
{
    wifi_wifi_disconnect();
    return SSRET_OK;
}

SubsysResultCode_e NetworkLWIP_WiFi :: clear_aps(SubsysCommand *cmd)
{
    if (cmd->user_interface) {
        if (cmd->user_interface->popup("Are you sure? Erase all stored APs?", BUTTON_OK|BUTTON_CANCEL) != BUTTON_OK) {
            return SSRET_ABORTED_BY_USER;
        }
    }

    if (wifi_forget_aps() != 0)
        return SSRET_GENERIC_ERROR;
    if (cmd->user_interface) {
        cmd->user_interface->popup("APs cleared and forgotten!", BUTTON_OK);
    }
    return SSRET_OK;
}

SubsysResultCode_e NetworkLWIP_WiFi :: auto_connect(SubsysCommand *cmd)
{
    wifi_wifi_autoconnect();
    return SSRET_OK;
}

SubsysResultCode_e NetworkLWIP_WiFi :: manual_connect(SubsysCommand *cmd)
{
    if (!cmd->user_interface)
        return SSRET_NO_USER_INTERFACE;
    
    char ssid[36];
    char password[64];
    int authmode = 0;
    int ret;
    ssid[0] = 0;
    password[0] = 0;
    ret = cmd->user_interface->string_box("Name of Access Point (SSID)", ssid, 32);
    if ((ret < 1)||(!ssid[0])) { // abort when SSID empty
        return SSRET_ABORTED_BY_USER;
    }
    ret = cmd->user_interface->string_box("Password", password, 62);
    if (ret < 1) {
        return SSRET_ABORTED_BY_USER;
    }
    if (password[0]) { // non-empty password: select authentication mode
        ret = cmd->user_interface->choice("Authentication", authmodes, 9);
    } else {
        ret = 0; // empty password: no authentication
    }
    if (ret < 0) { // either the picker or the password were aborted
        return SSRET_ABORTED_BY_USER;
    }
    authmode = ret;
    wifi_wifi_connect(ssid, password, authmode);
    return SSRET_OK;
}

void NetworkLWIP_WiFi :: cfg_show_aps(UserInterface *intf, ConfigItem *it)
{
    WifiState_t state = wifi.getState();
    if ((state != eWifi_NotConnected) && (state != eWifi_Connected)) {
        intf->popup("Can only show APs when active", BUTTON_OK);
        return;
    }
    wifi.sendEvent(EVENT_RESCAN);
    BrowsableWifiAPList *broot = new BrowsableWifiAPList(); // new root!
    TreeBrowser *tb = new TreeBrowser(intf, broot);
    tb->has_border = true;
    tb->allow_exit = true;
    tb->has_path = false;
    tb->init();
    tb->setCleanup();

    intf->activate_uiobject(tb);
    // There is a new treebrowser now, which will call the fetch items from the BrowsableWifiAPList!!
}

void NetworkLWIP_WiFi :: cfg_enter_ap(UserInterface *intf, ConfigItem *it)
{
    WifiState_t state = wifi.getState();
    if ((state != eWifi_NotConnected) && (state != eWifi_Connected)) {
        intf->popup("Can only connect when active", BUTTON_OK);
        return;
    }
    SubsysCommand cmd(intf, 0, 0, 0, "", "");
    manual_connect(&cmd);
}

void NetworkLWIP_WiFi :: cfg_conn_last(UserInterface *intf, ConfigItem *it)
{
    WifiState_t state = wifi.getState();
    if ((state != eWifi_NotConnected) && (state != eWifi_Connected)) {
        intf->popup("Can only connect when active", BUTTON_OK);
        return;
    }
    wifi_wifi_autoconnect();    
}

void NetworkLWIP_WiFi :: cfg_disconn(UserInterface *intf, ConfigItem *it)
{
    WifiState_t state = wifi.getState();
    if ((state != eWifi_NotConnected) && (state != eWifi_Connected)) {
        intf->popup("Can only disconnect when active", BUTTON_OK);
        return;
    }
    wifi_wifi_disconnect();    
}

void NetworkLWIP_WiFi :: cfg_enable(UserInterface *intf, ConfigItem *it)
{
    wifi.RadioOn();
}

void NetworkLWIP_WiFi :: cfg_disable(UserInterface *intf, ConfigItem *it)
{
    wifi.RadioOff();
}

void NetworkLWIP_WiFi :: cfg_forget(UserInterface *intf, ConfigItem *it)
{
    wifi_forget_aps();
}

SubsysResultCode_e NetworkLWIP_WiFi :: list_aps(SubsysCommand *cmd)
{
    wifi.sendEvent(EVENT_RESCAN);
    if(cmd->user_interface) {
        cmd->user_interface->send_keystroke(KEY_RIGHT);
        return SSRET_OK;
    }
    return SSRET_NO_USER_INTERFACE;
}

SubsysResultCode_e NetworkLWIP_WiFi :: rescan(SubsysCommand *cmd)
{
    wifi.sendEvent(EVENT_RESCAN);
    return SSRET_OK;
}

SubsysResultCode_e NetworkLWIP_WiFi :: disable(SubsysCommand *cmd)
{
#if U64 == 2
    wifi.RadioOff();
#else
    wifi.Disable();
#endif
    return SSRET_OK;
}

SubsysResultCode_e NetworkLWIP_WiFi :: enable(SubsysCommand *cmd)
{
#if U64 == 2
    wifi.RadioOn();
#else
    wifi.Enable();
#endif
    return SSRET_OK;
}

// Browsable APs
IndexedList<Browsable *> *BrowsableWifiAPList :: getSubItems(int &error)
{
    error = 0;
    wifi.getAccessPointItems(this, children);
    return &children;
}

