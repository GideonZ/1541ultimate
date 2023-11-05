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

//-----------------------------------
struct t_cfg_definition wifi_config[] = {
    { CFG_WIFI_ENABLE, CFG_TYPE_ENUM,   "WiFi Enabled",                  "%s", en_dis,     0,  1, 1 },
    { CFG_WIFI_SSID,   CFG_TYPE_STRING, "Default SSID",                  "%s", NULL,       3, 22, (int)"" },
    { CFG_WIFI_PASSW,  CFG_TYPE_STRING, "Password",                      "%s", NULL,       3, 22, (int)"" },
    { CFG_NET_DHCP_EN, CFG_TYPE_ENUM,   "Use DHCP",                      "%s", en_dis,     0,  1, 1 },
	{ CFG_NET_IP,      CFG_TYPE_STRING, "Static IP",					 "%s", NULL,       7, 16, (int)"192.168.2.64" },
	{ CFG_NET_NETMASK, CFG_TYPE_STRING, "Static Netmask",				 "%s", NULL,       7, 16, (int)"255.255.255.0" },
	{ CFG_NET_GATEWAY, CFG_TYPE_STRING, "Static Gateway",				 "%s", NULL,       7, 16, (int)"192.168.2.1" },
#ifdef U64
	{ CFG_NET_HOSTNAME,CFG_TYPE_STRING, "Host Name", 					 "%s", NULL,       3, 18, (int)"Ultimate-64" },
#elif FREQUENCY == 62500000
    { CFG_NET_HOSTNAME,CFG_TYPE_STRING, "Host Name",                     "%s", NULL,       3, 18, (int)"Ultimate-II-Plus" },
#else
    { CFG_NET_HOSTNAME,CFG_TYPE_STRING, "Host Name",                     "%s", NULL,       3, 18, (int)"Ultimate-II" },
#endif
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
    case eWifi_Detected:               
    case eWifi_Scanning:
        sprintf(buffer, "WiFi    %#s\eGScanning", width - 17, wifi.getModuleName());
        break;
    case eWifi_Failed:
        sprintf(buffer, "WiFi    MAC %b:%b:%b:%b:%b:%b%#s\eGFailed", mac_address[0], mac_address[1], mac_address[2],
                mac_address[3], mac_address[4], mac_address[5], width - 38, "");
        break;
    case eWifi_NotConnected:
        sprintf(buffer, "WiFi    MAC %b:%b:%b:%b:%b:%b%#s\eJLink Down", mac_address[0], mac_address[1], mac_address[2],
                mac_address[3], mac_address[4], mac_address[5], width - 38, "");
        break;
    case eWifi_Connected:
        sprintf(buffer, "WiFi    IP: %#s\eELink Up", width - 21, getIpAddrString(ip, 16));
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

    wifi.setSsidPass(cfg->get_string(CFG_WIFI_SSID), cfg->get_string(CFG_WIFI_PASSW));

    if (wifi.getState() == eWifi_Off && cfg->get_value(CFG_WIFI_ENABLE)) {
        wifi.Enable();
    }
//  For now, we don't disable the Wifi here, because effectuate_settings is also called when the init callback of the network stack occurs, OWWW
//    else if (wifi.getState() != eWifi_Off && !cfg->get_value(CFG_WIFI_ENABLE)) {
//        wifi.Disable();
//    }
}

// called from wifi driver when a manual connect occurs
void NetworkLWIP_WiFi :: saveSsidPass(const char *ssid, const char *pass)
{
    char ssid_mut[36];
    char pass_mut[68];
    bzero(ssid_mut, 36);
    bzero(pass_mut, 68);
    strncpy(ssid_mut, ssid, 32);
    strncpy(pass_mut, pass, 64);
    cfg->set_string(CFG_WIFI_SSID, ssid_mut);
    cfg->set_string(CFG_WIFI_PASSW, pass_mut);
}

void NetworkLWIP_WiFi :: fetch_context_items(IndexedList<Action *>&items)
{
    if (wifi.getState() == eWifi_Connected) {
        items.append(new Action("Disconnect", NetworkLWIP_WiFi :: disconnect, 0, 0));
        items.append(new Action("Disable", NetworkLWIP_WiFi :: disable, 0, 0));
    } else if ((wifi.getState() == eWifi_NotConnected) || (wifi.getState() == eWifi_Failed)) {
        items.append(new Action("Show APs..", NetworkLWIP_WiFi :: list_aps, 0, 0));
        items.append(new Action("Rescan APs", NetworkLWIP_WiFi :: rescan, 0, 0));
        items.append(new Action("Disable", NetworkLWIP_WiFi :: disable, 0, 0));
    } else if (wifi.getState() == eWifi_Off) {
        items.append(new Action("Enable", NetworkLWIP_WiFi :: enable, 0, 0));
    }
}

int NetworkLWIP_WiFi :: disconnect(SubsysCommand *cmd)
{
    wifi_wifi_disconnect();
    return 0;
}

int NetworkLWIP_WiFi :: list_aps(SubsysCommand *cmd)
{
    if(cmd->user_interface) {
        return cmd->user_interface->enterSelection();
    }
    return 0;
}

int NetworkLWIP_WiFi :: rescan(SubsysCommand *cmd)
{
    wifi.sendEvent(EVENT_RESCAN);
    return 0;
}

int NetworkLWIP_WiFi :: disable(SubsysCommand *cmd)
{
    wifi.Disable();
    return 0;
}

int NetworkLWIP_WiFi :: enable(SubsysCommand *cmd)
{
    wifi.Enable();
    return 0;
}
