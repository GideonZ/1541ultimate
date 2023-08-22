/*
 * wifi.h
 *
 *  Created on: Sep 2, 2018
 *      Author: gideon
 */

#ifndef WIFI_H_
#define WIFI_H_

#include "fastuart.h"
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include "browsable.h"
#include "size_str.h"

#include "network_interface.h"

extern "C" {
    #include "cmd_buffer.h"
}

// This class provides an interface to the WiFi module, to manage and program it
// When in operational mode, it dispatches the received packets to the
// socket layer wrapper functions.

typedef enum {
    eWifi_NotDetected,
    eWifi_Detected,
    eWifi_NotConnected,
    eWifi_Connected,
    eWifi_Failed,
} WifiState_t;

class WiFi
{
    SemaphoreHandle_t rxSemaphore;
    QueueHandle_t commandQueue;
    TaskHandle_t runModeTask;
    command_buf_context_t *packets;
    NetworkInterface *netstack;

    WifiState_t state;
    char    moduleName[34];
    uint8_t my_mac[6];
    uint32_t my_ip;
    uint32_t my_gateway;
    uint32_t my_netmask;

    bool doClose;
    bool programError;

    void Enable(bool);
    void Disable();
    void Boot();
    int  Download(const uint8_t *binary, uint32_t address, uint32_t length);
    void PackParams(uint8_t *buffer, int numparams, ...);
    bool Command(uint8_t opcode, uint16_t length, uint8_t chk, uint8_t *data, uint8_t *receiveBuffer, int timeout);
    bool UartEcho(void);
    bool RequestEcho(void);
    void RxPacket(command_buf_t *);

    static void CommandTaskStart(void *context);
    static void RunModeTaskStart(void *context);

    void CommandThread();
    void RunModeThread();
public:
    WiFi();
    void Quit();

    BaseType_t doBootMode();
    BaseType_t doStart();
    BaseType_t doDownload(uint8_t *binary, uint32_t address, uint32_t length, bool doFree);
    BaseType_t doDownloadWrap(bool start);
    BaseType_t doRequestEcho(void);
    BaseType_t doUartEcho(void);

    FastUART *uart;

    WifiState_t getState(void) { return state; }
    void  getMacAddr(uint8_t *target) { memcpy(target, my_mac, 6); }
    char *getIpAddrString(char *buf, int max) { sprintf(buf, "%d.%d.%d.%d", (my_ip >> 0) & 0xff, (my_ip >> 8) & 0xff, (my_ip >> 16) & 0xff, (my_ip >> 24) & 0xff); return buf; }
    void sendEvent(uint8_t code);
    void freeBuffer(command_buf_t *buf);

    friend class BrowsableWifi;
};

extern WiFi wifi;

class BrowsableWifiAP : public Browsable
{
    Browsable *parent;
    char ssid[32];
    int8_t dbm;
    uint8_t auth;
public:
    BrowsableWifiAP(Browsable *parent, char *ssid, int8_t dbm, uint8_t auth) {
        this->parent = parent;
        strncpy(this->ssid, ssid, 32);
        this->dbm = dbm;
        this->auth = auth;
    }

    void getDisplayString(char *buffer, int width) {
        sprintf(buffer, "%#s%5d", 31, ssid, dbm);
    }

    void fetch_context_items(IndexedList<Action *>&items);
    static int connect_ap(SubsysCommand *cmd);

};

class BrowsableWifi : public Browsable
{
    Browsable *parent;
public:
    BrowsableWifi(Browsable *parent) {
        this->parent = parent;
    }

    void getDisplayString(char *buffer, int width) {
        uint8_t mac[6];
        char ip[16];

        WifiState_t state = wifi.getState();

        switch(state) {
        case eWifi_NotDetected:
            sprintf(buffer, "WiFi    %#s\eJNo Module", width-17, "");
            break;
        case eWifi_Detected:
            sprintf(buffer, "WiFi    %#s\eJLink Down", width-17, wifi.moduleName);
            break;
        case eWifi_Failed:
            wifi.getMacAddr(mac);
            sprintf(buffer, "WiFi    MAC %b:%b:%b:%b:%b:%b%#s\eGFailed", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5], width-38, "");
            break;
        case eWifi_NotConnected:
            wifi.getMacAddr(mac);
            sprintf(buffer, "WiFi    MAC %b:%b:%b:%b:%b:%b%#s\eJLink Down", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5], width-38, "");
            break;
        case eWifi_Connected:
            sprintf(buffer, "WiFi    IP: %#s\eELink Up", width-21, wifi.getIpAddrString(ip, 16));
            break;
        default:
            sprintf(buffer, "WiFi%#s\eJUnknown", width-17, "");
            break;
        }
    }

    IndexedList<Browsable *> *getSubItems(int &error);
    void fetch_context_items(IndexedList<Action *>&items);

    static int disconnect(SubsysCommand *cmd);
    static int list_aps(SubsysCommand *cmd);
};



#endif /* WIFI_H_ */
