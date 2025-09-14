/*
 * network_esp32.h
 *
 *  Created on: Nov 11, 2021
 *      Author: gideon
 */

#ifndef SOFTWARE_IO_NETWORK_NETWORK_ESP32_H_
#define SOFTWARE_IO_NETWORK_NETWORK_ESP32_H_

#include <socket.h>
#include <FreeRTOS.h>
#include "network_interface.h"


#define CFG_WIFI_ENABLE  0xB1
#define CFG_WIFI_SEL_AP  0xB2
#define CFG_WIFI_ENT_AP  0xB3
#define CFG_WIFI_CUR_AP  0xB4
#define CFG_WIFI_CUR_IP  0xB5
#define CFG_WIFI_MAC     0xB6
#define CFG_WIFI_CONN    0xB7
#define CFG_WIFI_DISCONN 0xB8

class NetworkLWIP_WiFi : public NetworkInterface
{
public:
    NetworkLWIP_WiFi(void *driver,
                driver_output_function_t out,
				driver_free_function_t free);
    virtual ~NetworkLWIP_WiFi();

    const char *identify() { return "WiFi"; }
    void attach_config();

    // User Interface
    void getDisplayString(int index, char *buffer, int width);
    void getSubItems(Browsable *parent, IndexedList<Browsable *> &list, int &error);
    void fetch_context_items(IndexedList<Action *>&items);
    static SubsysResultCode_e list_aps(SubsysCommand *cmd);
    static SubsysResultCode_e clear_aps(SubsysCommand *cmd);
    static SubsysResultCode_e disconnect(SubsysCommand *cmd);
    static SubsysResultCode_e rescan(SubsysCommand *cmd);
    static SubsysResultCode_e enable(SubsysCommand *cmd);
    static SubsysResultCode_e disable(SubsysCommand *cmd);
    static SubsysResultCode_e manual_connect(SubsysCommand *cmd);
    static SubsysResultCode_e auto_connect(SubsysCommand *cmd);

    // From Config menu
    static void cfg_show_aps(UserInterface *intf, ConfigItem *it);
    static void cfg_enter_ap(UserInterface *intf, ConfigItem *it);
    static void cfg_conn_last(UserInterface *intf, ConfigItem *it);
    static void cfg_disconn(UserInterface *intf, ConfigItem *it);

    // from ConfigurableObject
    void effectuate_settings(void);
    void on_edit(void);
};

class BrowsableWifiAPList : public Browsable
{
public:
    BrowsableWifiAPList() { }

    IndexedList<Browsable *> *getSubItems(int &error);
    const char *getName() { return "BrowsableWiFiAPList"; }
};



#endif /* SOFTWARE_IO_NETWORK_NETWORK_ESP32_H_ */
