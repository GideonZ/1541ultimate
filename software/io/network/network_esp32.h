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

    // from ConfigurableObject
    void effectuate_settings(void);
};




#endif /* SOFTWARE_IO_NETWORK_NETWORK_ESP32_H_ */
