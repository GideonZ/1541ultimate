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


#define CFG_WIFI_ENABLE 0xB1
#define CFG_WIFI_SSID   0xB2
#define CFG_WIFI_PASSW  0xB3

class NetworkLWIP_WiFi : public NetworkInterface
{
public:
    NetworkLWIP_WiFi(void *driver,
                driver_output_function_t out,
				driver_free_function_t free);
    virtual ~NetworkLWIP_WiFi();

    const char *identify() { return "NetworkLWIP_WiFi"; }
    void attach_config();

    // User Interface
    void getDisplayString(int index, char *buffer, int width);
    void getSubItems(Browsable *parent, IndexedList<Browsable *> &list, int &error);
    void fetch_context_items(IndexedList<Action *>&items);
    static int list_aps(SubsysCommand *cmd);
    static int disconnect(SubsysCommand *cmd);
    static int rescan(SubsysCommand *cmd);
    static int enable(SubsysCommand *cmd);
    static int disable(SubsysCommand *cmd);

    // from ConfigurableObject
    void effectuate_settings(void);

    // from Wifi Driver
    void saveSsidPass(const char *ssid, const char *pass);
};




#endif /* SOFTWARE_IO_NETWORK_NETWORK_ESP32_H_ */
