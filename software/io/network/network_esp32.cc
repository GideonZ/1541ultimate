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
    { CFG_WIFI_ENABLE, CFG_TYPE_ENUM,   "WiFi Enabled",                  "%s", en_dis,     0,  1, 1 },
    { CFG_NET_DHCP_EN, CFG_TYPE_ENUM,   "Use DHCP",                      "%s", en_dis,     0,  1, 1 },
	{ CFG_NET_IP,      CFG_TYPE_STRING, "Static IP",					 "%s", NULL,       7, 16, (int)"192.168.2.64" },
	{ CFG_NET_NETMASK, CFG_TYPE_STRING, "Static Netmask",				 "%s", NULL,       7, 16, (int)"255.255.255.0" },
	{ CFG_NET_GATEWAY, CFG_TYPE_STRING, "Static Gateway",				 "%s", NULL,       7, 16, (int)"192.168.2.1" },
	{ CFG_NET_DNS,     CFG_TYPE_STRING, "Static DNS",		   		 "%s", NULL,       7, 16, (int)"" },
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
        sprintf(buffer, "WiFi    IP: %#s\eELink Up", width - 21, getIpAddrString(ip, 16));
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
}

// from ConfigurableObject
void NetworkLWIP_WiFi :: effectuate_settings(void)
{
    NetworkInterface :: effectuate_settings();

	my_net_if.name[0] = 'W';
	my_net_if.name[1] = 'I';

#if U64 == 2
    if (cfg->get_value(CFG_WIFI_ENABLE) && wifi.getState() == eWifi_Disabled) {
        wifi.RadioOn();
    } else if (!(cfg->get_value(CFG_WIFI_ENABLE)) && wifi.getState() != eWifi_Disabled) {
        wifi.RadioOff();
    }
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
        items.append(new Action("Rescan APs", NetworkLWIP_WiFi :: rescan, 0, 0));
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
    if (ret < 1) {
        return SSRET_ABORTED_BY_USER;
    }
    ret = cmd->user_interface->string_box("Password", password, 62);
    if (ret < 0) {
        return SSRET_ABORTED_BY_USER;
    }
    if (ret > 0) {
        ret = cmd->user_interface->choice("Authentication", authmodes, 9);
    }
    if (ret < 0) { // either the picker or the password were aborted
        return SSRET_ABORTED_BY_USER;
    }
    authmode = ret;
    wifi_wifi_connect(ssid, password, authmode);
    return SSRET_OK;
}

SubsysResultCode_e NetworkLWIP_WiFi :: list_aps(SubsysCommand *cmd)
{
    if(cmd->user_interface) {
        cmd->user_interface->enterSelection(); // ignore result, as it is just sending a signal
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
