/*
 * wifi_task_env.h
 *
 * The world the Ultimate's WiFi application sees on the build host. The code under test is
 * cut out of wifi.cc, wifi_cmd.cc and network_esp32.cc as it is (by the Makefile, with
 * extract.awk) and put behind this header. The real wifi.h, esp32.h, wifi_cmd.h and
 * wifi_switch.h and wifi_connect.h are used; the RTOS, the UART,
 * the browsable tree and the network interface are the stand-ins of stubs/ and below,
 * which record what is called (wifi_task_stubs.cpp).
 *
 * The test reads and sets WiFi's private members (the task's state, the radio switch):
 * the Makefile builds with -fno-access-control, which is the one place that does it.
 */

#ifndef WIFI_TASK_ENV_H_
#define WIFI_TASK_ENV_H_

#include <arpa/inet.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

typedef int err_t;

#include "dma_uart.h"
#include "esp32.h"
#include "browsable.h"
#include "dump_hex.h"
#include "wifi.h"
#include "wifi_connect.h"
#include "wifi_enable_default.h" // CFG_WIFI_ENABLE and the default of that setting, from network_esp32.cc / .h

// The AP list's browser, as far as the menu uses it
class BrowsableWifiAPList : public Browsable {};
class TreeBrowser {
public:
    bool has_border, allow_exit, has_path;
    TreeBrowser(UserInterface *intf, Browsable *root) {}
    void init(void) {}
    void setCleanup(void) {}
};

class ConfigItem;

// The configuration store of the WiFi settings, as far as the menu uses it
class ConfigStore {
public:
    void set_value(uint8_t id, int value);
    int get_value(uint8_t id);
    void write(void);
};
class ConfigManager {
public:
    static ConfigManager *getConfigManager(void);
    ConfigStore *find_store(uint32_t page_id);
};

// The network interface, as far as the application uses it
class NetworkLWIP_WiFi {
public:
    NetworkLWIP_WiFi(void *driver, err_t (*out)(void *, void *, int), void (*free)(void *, void *)) {}
    void attach_config(void) {}
    void effectuate_registered_settings(void) {}
    void set_mac_address(uint8_t *mac) {}
    void start(void);
    void stop(void) {}
    void link_up(void);
    void link_down(void);
    void input_copy(uint8_t *data, int len) {}
    bool enabled_in_config(void);
    static SubsysResultCode_e enable(SubsysCommand *cmd);
    static void set_enabled(bool on);
    static SubsysResultCode_e disable(SubsysCommand *cmd);
    static SubsysResultCode_e manual_connect(SubsysCommand *cmd);
    static bool ask_enable(UserInterface *intf);
    static void cfg_show_aps(UserInterface *intf, ConfigItem *it);
    static void cfg_enter_ap(UserInterface *intf, ConfigItem *it);
    static void cfg_conn_last(UserInterface *intf, ConfigItem *it);
    static int wifi_enable_change(class ConfigItem *it);
};

class ConfigItem {
public:
    int value;
    int getValue(void) { return value; }
};

class U64Config {
public:
    static void pushPowerOnMode(void);
    static void pushWakeOnWifi(void);
    static void pushRecoveryUrl(void);
};

extern ultimate_ap_records_t wifi_aps;

#endif /* WIFI_TASK_ENV_H_ */
