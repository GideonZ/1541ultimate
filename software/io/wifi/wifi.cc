/*
 * wifi.cc
 *
 *  Created on: Sep 2, 2018
 *      Author: gideon
 */

#include "wifi.h"
#include "socket.h"
#include "u64.h"
#include "dump_hex.h"
#include <stdarg.h>
#include "userinterface.h"
#include "rpc_calls.h"
#include "filemanager.h"
#include "network_esp32.h"
#include "init_function.h"

#define DEBUG_INPUT  0
#define EVENT_START  0xF1
#define EVENT_TERM   0xF2

WiFi wifi;
ultimate_ap_records_t wifi_aps;
InitFunction wifi_init("WiFi Application", WiFi::Init, NULL, NULL, 52); // after LWIP

WiFi :: WiFi()
{
    runModeTask = NULL;
    state = eWifi_Off;
    bzero(my_mac, 6);
    wifi_aps.num_records  = 0;
    my_ip = 0;
    my_gateway = 0;
    my_netmask = 0;
}

void WiFi :: Init(void *obj, void *param)
{
    esp32.AttachApplication(&wifi);
    wifi.netstack = new NetworkLWIP_WiFi(&wifi, wifi_tx_packet, wifi_free);
    wifi.netstack->attach_config();

#if U64 == 2
    wifi.Enable(); // Unconditionally enable the WiFi module
#endif
    wifi.netstack->effectuate_settings(); // may start the WiFi application
}

void WiFi :: RefreshRoot()
{
    // Quick hack to refresh the screen
    FileManager :: getFileManager() -> sendEventToObservers(eRefreshDirectory, "/", "");    
}

void WiFi :: RefreshAPs()
{
    FileManager :: getFileManager() -> sendEventToObservers(eRefreshDirectory, "/WiFi", "");    
}

void WiFi :: Enable()
{
    esp32.StartApp(); 
}

void WiFi :: Disable()
{
    esp32.StopApp();
}

void WiFi :: RadioOn()
{
    wifi_enable();
}

void WiFi :: RadioOff()
{
    wifi_disable();
}

BaseType_t wifi_rx_isr(command_buf_context_t *context, command_buf_t *buf, BaseType_t *w);

// From Esp32Application
void WiFi :: Init(DmaUART *uart, command_buf_context_t *packets)
{
    this->uart = uart;
    this->packets = packets;
    wifi_command_init();
}

void WiFi :: Start()
{
    if (runModeTask) {
        printf("** Trying to start Wifi Application, but task is already running?\n");
        return;
    }
    state = eWifi_NotDetected;
    xTaskCreate( WiFi :: RunModeTaskStart, "WiFi Command Task", configMINIMAL_STACK_SIZE, this, PRIO_DRIVER, &runModeTask );
}

void WiFi :: Terminate()
{
    if (!runModeTask) {
        printf("** Trying to terminate Wifi Application, but task is not running?\n");
        return;
    }
    vTaskDelete(runModeTask);
    runModeTask = NULL;
    netstack->stop(); // remove from list with available network stacks
    uart->ResetReceiveCallback();
    state = eWifi_Off;
    RefreshRoot();
}

bool WiFi::RequestEcho(void)
{
    command_buf_t *buf;
    uart->txDebug = false;

    uart->SetBaudRate(115200);

    while (uart->ReceivePacket(&buf, 100) == pdTRUE) {
        printf("%d Bytes received in buffer %d\n", buf->size, buf->bufnr);
        uart->FreeBuffer(buf);
    }

    char string[40];
    uint16_t major, minor;
    if (wifi_detect(&major, &minor, string, 40) == pdTRUE) {
        printf("ESP32 module V%d.%d detected: %s\n", major, minor, string);
    }

    const int rates[] = { 115200, 230400, 460800, 921600, 1000000, 2000000, 4000000, 6666666 };

    for(int rate = 0; rate < 8; rate ++) {
        wifi_setbaud(rates[rate], 1); // uart->SetBaudRate(rate); + remote switch as well     uart->FlowControl(false);

        for(int i=800; i<=1000;i+=50) {
            uint16_t start, stop;
            if (uart->GetBuffer(&buf, 100) != pdTRUE) {
                return false;
            }
            memset(buf->data, 0x00, CMD_BUF_SIZE);
            rpc_identify_req *req = (rpc_identify_req *)buf->data;
            req->hdr.command = CMD_ECHO;
            buf->data[i-1] = 2;
            int txbuf = buf->bufnr;
            buf->size = i;
            uart->TransmitPacket(buf, &start);

            if (uart->ReceivePacket(&buf, 200) == pdTRUE) {
                stop = getMsTimer();
                int t = 0;
                for (int j=0;j<buf->size;j++) {
                    t += (int)buf->data[j];
                }
                bool ok = (t == 3) && (buf->data[0] == 1) && (buf->data[i-1] == 2) && (buf->size == i);
                printf("%d Bytes received from buffer %d in buffer %d after %d ms Verdict: %s\n", buf->size, txbuf, buf->bufnr, stop - start, ok ? "OK!" : "FAIL!");
                if (!ok) {
                    dump_hex_relative(buf->data, buf->size);
                }
                uart->FreeBuffer(buf);
            }
        }
    }
    uart->txDebug = false;
    return true;
}

void WiFi::RunModeTaskStart(void *context)
{
    WiFi *w = (WiFi *) context;
    w->RunModeThread();
}

void WiFi :: RunModeThread()
{
    uint16_t major, minor;
    BaseType_t suc = pdFALSE;
    command_buf_t *buf;
    rpc_header_t *hdr;
    // rpc_wifi_connect_req *conn_req;
    event_pkt_connected *ev2;
    event_pkt_got_ip *ev;
    int result;
    state = eWifi_NotDetected;
    uint8_t conn;
    uint32_t ip;

    RefreshRoot();
    static char boot_message[256];
    int len;
    voltages_t voltages;

    while(1) {
        switch(state) {
        case eWifi_Off:
            printf("RunMode Thread running, but state is off...\n");
            vTaskDelay(200);
            break;

        case eWifi_NotDetected:
#ifndef U64 // wifi module is always present in U64, but not on the U2+L cartridge, so we detect it here
            len = esp32.DetectModule(boot_message, 256);
            if(len) {
                const char *bm = boot_message;
                for(int i=0;i<3;i++) {
                    if (*bm != 'E') {
                        bm++;
                    }
                }
                if (strncmp(bm, "ESP-ROM:", 8) == 0) {
                    for(int i=8;i<32;i++) {
                        if (bm[i] == '-') {
                            moduleType[i-8] = 0;
                            break;
                        } else {
                            moduleType[i-8] = bm[i];
                        }
                    }
                } else {
                    strcpy(moduleType, "esp32-wroom");
                }
                dump_hex(boot_message, len);
                state = eWifi_ModuleDetected;
                RefreshRoot();
                esp32.EnableRunMode();
                wifi_command_init();
            } else {
                printf("No Boot message.\n");
                vTaskDelay(portMAX_DELAY); // basically stall
            }
#else // U64 
            // Make sure it is on, without turning it off (might have been off)
            wifi.uart->ModuleCtrl(ESP_MODE_RUN);
            strcpy(moduleType, "ESP32 on U64");
            wifi_command_init();

            // esp32.EnableRunMode(); on the U64, the machine should boot with the module in run mode
            // on the U64, this should be enforced in the init of the esp32.cc code, and for the U64-II
            // the module is already running before the FPGA even loads. So we're NOT calling EnableRunMode here.
            state = eWifi_ModuleDetected;
            RefreshRoot();
#endif

        case eWifi_ModuleDetected:
            uart->txDebug = false;
            if (wifi_detect(&major, &minor, moduleName, 32)) {
                state = eWifi_AppDetected;

#if U64 == 2
                memset(&voltages, 0, sizeof(voltages));
                result = wifi_get_voltages(&voltages);
                printf("Result get voltages: %d %d %d %d %d %d %d %d\n", result, voltages.vbus, voltages.vaux, voltages.v50, voltages.v33, voltages.v18, voltages.v10, voltages.vusb);
                if (voltages.vbus < 8500) {
                    UserInterface :: postMessage("Low input voltage.");
                }
#endif
                RefreshRoot();
            }
            break;

        case eWifi_AppDetected:
            uart->txDebug = false;

            wifi_getmac(my_mac);
            netstack->set_mac_address(my_mac);
            netstack->start(); // always starts in link down state
            state = eWifi_NotConnected;
            if (wifi_is_connected(conn) == 0) {
                if (conn) {
                    wifi_modem_enable(true); // take control!
                    uart->txDebug = false;
                    netstack->link_up();
                    state = eWifi_Connected;
                }
            }
            RefreshRoot();
            break;

        case eWifi_Scanning:
            wifi_aps.num_records = 0;
            wifi_scan(&wifi_aps);
            state = eWifi_NotConnected;
            RefreshRoot();
            RefreshAPs();
            break;

        case eWifi_NotConnected:
        case eWifi_Connected:
        case eWifi_Disabled:
            cmd_buffer_received(packets, &buf, portMAX_DELAY);
            hdr = (rpc_header_t *)(buf->data);
#if DEBUG_INPUT
            printf("Pkt %d. Ev %b. Sz %d. Seq: %d\n", buf->bufnr, hdr->command, buf->size, hdr->sequence);
#endif
            switch(hdr->command) {
            case EVENT_RESCAN: // requested from user interface
                uart->FreeBuffer(buf);
                netstack->link_down();
                state = eWifi_Scanning;
                RefreshRoot();
                break;

            case EVENT_DISABLED:
                uart->FreeBuffer(buf);
                state = eWifi_Disabled;
                netstack->link_down();
                RefreshRoot();
                break;

            case EVENT_CONNECTED:
                ev2 = (event_pkt_connected *)buf->data;
                memcpy(last_ap, ev2->ssid, 32);
                uart->FreeBuffer(buf);
                if (state == eWifi_Connected) { // already connected!
                    netstack->link_down();
                }
                wifi_modem_enable(true); // take control!
                state = eWifi_Connected;
                uart->txDebug = false;
                netstack->link_up();

                RefreshRoot();
                break;

            case EVENT_DISCONNECTED:
                uart->FreeBuffer(buf);
                state = eWifi_NotConnected;
                if(netstack) {
                    netstack->link_down();
                }
                RefreshRoot();
                break;

            case EVENT_RECV_PACKET:
                if (netstack) {
                    rpc_rx_pkt *pkt = (rpc_rx_pkt *)buf->data;
#if DEBUG_INPUT
                    printf("In %d\n", pkt->len);
                    dump_hex_relative(&pkt->data, 64);
#endif
                    netstack->input(buf, (uint8_t*)&pkt->data, pkt->len);
                } else {
                    puts("Packet received, but no net stack!");
                    uart->FreeBuffer(buf);
                }
                // no free, as the packet needs to live on in the network stack
                break;

            case EVENT_GOTIP:
                ev = (event_pkt_got_ip *)buf->data;
                ip = ntohl(ev->ip);
                printf("-> ESP32 received IP from DHCP: %d.%d.%d.%d (changed: %d)\n", (ip >> 24),
                    (ip >> 16) & 0xFF, (ip >> 8) & 0xFF, ip & 0xFF, ev->changed);
                uart->FreeBuffer(buf);
                break;

            default:
                printf("Unexpected Event type: Pkt %d. Ev %b. Sz %d. Seq: %d. Thread: %d.\n", buf->bufnr, hdr->command, buf->size, hdr->sequence, hdr->thread);
                uart->FreeBuffer(buf);
                break;
            }
        }
    }

    // Wait forever
    vTaskDelay(portMAX_DELAY);
}

void WiFi :: sendEvent(uint8_t code)
{
    command_buf_t *buf;
    cmd_buffer_get(packets, &buf, portMAX_DELAY);
    rpc_header_t *hdr = (rpc_header_t *)(buf->data);
    hdr->command = code;
    hdr->sequence = 0;
    hdr->thread = 0xFF;
    cmd_buffer_loopback(packets, buf); // internal event
}

// This event also updates the configuration.
void WiFi :: sendConnectEvent(const char *ssid, const char *pass, uint8_t auth)
{
    command_buf_t *buf;
    cmd_buffer_get(packets, &buf, portMAX_DELAY);
    // reuse of the rpc structure; but of course it is not sent to the module
    rpc_wifi_connect_req *args = (rpc_wifi_connect_req *)(buf->data);
    args->hdr.command = EVENT_CONNECTED;
    args->hdr.sequence = 0;
    args->hdr.thread = 0xFF;
    bzero(args->ssid, 32);
    bzero(args->password, 64);
    strncpy(args->ssid, ssid, 32);
    strncpy(args->password, pass, 64);
    args->auth_mode = auth;
    cmd_buffer_loopback(packets, buf); // internal event
}

void WiFi :: freeBuffer(command_buf_t *buf)
{
    uart->FreeBuffer(buf);
}

void WiFi ::getAccessPointItems(Browsable *parent, IndexedList<Browsable *> &list)
{
    ultimate_ap_records_t *aps = &wifi_aps;
    if (aps->num_records == 0) {
        list.append(new BrowsableStatic("Scanning..."));
        return;
    }
    ENTER_SAFE_SECTION;
    for (int i = 0; i < aps->num_records; i++) {
        list.append(new BrowsableWifiAP(parent, (char *)aps->aps[i].ssid, aps->aps[i].rssi, aps->aps[i].authmode));
    }
    LEAVE_SAFE_SECTION;
}

typedef struct {
    const char *timezone;
    const char *utc;
    const char *location;
    const char *posix;
} timezone_entry_t;

const timezone_entry_t zones[] = {
    { "AoE",   "UTC -12",   "US Baker Island", "AOE12" },
    { "NUT",   "UTC -11",   "America Samoa",   "NUT11" },
    { "HST",   "UTC -10",   "Hawaii", "HST11HDT,M3.2.0/2:00:00,M11.1.0/2:00:00" },
    { "MART",  "UTC -9:30", "French Polynesia", "MART9:30,M3.2.0/2:00:00,M11.1.0/2:00:00" },
    { "AKST",  "UTC -9",    "Alaska", "ASKT9AKDT,M3.2.0/2:00:00,M11.1.0/2:00:00" },
    { "PDT",   "UTC -8",    "Los Angeles", "PST8PDT,M3.2.0/2:00:00,M11.1.0/2:00:00" },
    { "MST",   "UTC -7",    "Denver, Colorado", "MST7MDT,M3.2.0/2:00:00,M11.1.0/2:00:00" },
    { "MST",   "UTC -7",    "Phoenix, Arizona", "MST7" },
    { "CST",   "UTC -6",    "Chicago, Illinois", "CST6CDT,M3.2.0/2:00:00,M11.1.0/2:00:00" },
    { "EST",   "UTC -5",    "New York", "EST5EDT,M3.2.0/2:00:00,M11.1.0/2:00:00" },
    { "ART",   "UTC -3",    "Argentina", "ART3" },
    { "NDT",   "UTC -2:30", "Newfoundland", "NDT3:30NST,M3.2.0/2:00:00,M11.1.0/2:00:00" },
    { "WGST",  "UTC -2",    "Greenland", "WGST3WGT,M3.2.0/2:00:00,M11.1.0/2:00:00" },
    { "CVT",   "UTC -1",    "Cabo Verde", "CVT1" },
    { "GMT",   "UTC 0",     "Iceland", "GMT" },
    { "BST",   "UTC +1",    "UK", "BST0GMT,M3.2.0/2:00:00,M11.1.0/2:00:00" },
    { "CEST",  "UTC +2",    "Germany", "CEST-1CET,M3.2.0/2:00:00,M11.1.0/2:00:00" },
    { "MSK",   "UTC +3",    "Greece", "MSK-3" },
    { "GST",   "UTC +4",    "Azerbaijan", "GST-4" },
    { "IRDT",  "UTC +4:30", "Iran", "IRDT3:30IRST,M3.2.0/2:00:00,M11.1.0/2:00:00" },
    { "UZT",   "UTC +5",    "Pakistan", "UZT-5" },
    { "IST",   "UTC +5:30", "India", "IST-5:30" },
    { "NPT",   "UTC +5:45", "Nepal", "NPT-5:45" },
    { "BST",   "UTC +6",    "Bangladesh", "BST-6" },
    { "MMT",   "UTC +6:30", "Myanmar", "MMT-6:30" },
    { "WIB",   "UTC +7",    "Indonesia", "WIB-7" },
    { "CST",   "UTC +8",    "China", "CST-8" },
    { "ACWST", "UTC +8:45", "Western Australia", "ACWST-8:45" },
    { "JST",   "UTC +9",    "Japan", "JST-9" },
    { "ACST",  "UTC +9:30", "Central Australia", "ACST8:30ACDT,M3.2.0/2:00:00,M11.1.0/2:00:00" },
    { "AEST",  "UTC +10",   "Eastern Australia", "AEST9AEDT,M3.2.0/2:00:00,M11.1.0/2:00:00" },
    { "LHST",  "UTC +10:30","Lord Howe Island", "LHST9:30LHDT,M3.2.0/2:00:00,M11.1.0/2:00:00" },
    { "SBT",   "UTC +11",   "Solomon Islands", "SBT-11" },
    { "ANAT",  "UTC +12",   "New Zealand", "ANAT-12" },
    { "CHAST", "UTC +12:45","Chatham Islands", "CHAST11:45CHADT,M3.2.0/2:00:00,M11.1.0/2:00:00" },
    { "TOT",   "UTC +13",   "Tonga", "TOT-12TOST,M3.2.0/2:00:00,M11.1.0/2:00:00" },
    { "LINT",  "UTC +14",   "Christmas Island", "LINT-14" },
};


#include "subsys.h"
void BrowsableWifiAP :: fetch_context_items(IndexedList<Action *>&items)
{
    items.append(new Action("Connect", BrowsableWifiAP :: connect_ap, (int)this, 0));
}

SubsysResultCode_e BrowsableWifiAP :: connect_ap(SubsysCommand *cmd)
{
    char password[64] = { 0 };
    BrowsableWifiAP *bap = (BrowsableWifiAP *)cmd->functionID;
    if (bap->auth) {
        if(cmd->user_interface) {
            cmd->user_interface->string_box("Enter password", password, 64);
        }
    }
    int result = wifi_wifi_connect(bap->ssid, password, bap->auth);
    printf("Connect result: %d\n", result);
    if (result == 0) {
        //wifi.sendEvent(EVENT_CONNECTED);
        wifi.sendConnectEvent(bap->ssid, password, bap->auth);
    }
    return SSRET_OK;
}

err_t wifi_tx_packet(void *driver, void *buffer, int length)
{
    if (wifi.getState() != eWifi_Connected) {
        return ERR_CONN;
    }
    
    BUFARGS(send_eth, CMD_SEND_PACKET);

    uint32_t chunkSize = length;
    if (chunkSize > CMD_BUF_PAYLOAD) {
        chunkSize = CMD_BUF_PAYLOAD;
    }
    args->length = chunkSize;
    memcpy(&args->data, buffer, chunkSize);
    buf->size += (chunkSize - 4);

    esp32.uart->TransmitPacket(buf);
    //TRANSMIT(espcmd);
    //RETURN_ESP;
    return ERR_OK;
}

void wifi_free(void *driver, void *buffer)
{
    // driver points to the WiFi object
    // buffer points to the cmd_buffer_t object
    WiFi *w = (WiFi *)driver;
    w->freeBuffer((command_buf_t *)buffer);
}

extern "C" {
    void print_uart_status()
    {
        wifi.PrintUartStatus();
    }
}