/*
 * wifi.cc
 *
 *  Created on: Sep 2, 2018
 *      Author: gideon
 */

#include "wifi.h"
#include "wifi_connect.h"
#include "socket.h"
#include "u64.h"
#include "dump_hex.h"
#include <stdarg.h>
#include "userinterface.h"
#include "rpc_calls.h"
#include "filemanager.h"
#include "network_esp32.h"
#if U64 == 2
#include "u64_config.h"
#endif
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
    state_before_scan = eWifi_NotConnected;
    event_when_enabled = 0;
    bzero(my_mac, 6);
    wifi_aps.num_records  = 0;
    my_ip = 0;
    my_gateway = 0;
    my_netmask = 0;
#if U64 != 2
    memset(&radio_switch, 0, sizeof(radio_switch));
#endif
}

void WiFi :: Init(void *obj, void *param)
{
    esp32.AttachApplication(&wifi);
    wifi.netstack = new NetworkLWIP_WiFi(&wifi, wifi_tx_packet, wifi_free);
    wifi.netstack->attach_config();

    // Unconditionally enable the WiFi module: it always runs, also with "WiFi Enabled"
    // stored as Disabled, which means its radio is off. On the U64 and U2+L the
    // application tells the module, which starts with its radio off, to turn it on or not.
    wifi.Enable();
    wifi.netstack->effectuate_registered_settings();
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

#if U64 != 2
// The states in which the WiFi application is running and the module answers it
bool WiFi :: TalksToModule()
{
    switch (state) {
    case eWifi_AppDetected:
    case eWifi_Scanning:
    case eWifi_NotConnected:
    case eWifi_Connected:
    case eWifi_Disabled:
        return true;
    default:
        return false;
    }
}
#endif

void WiFi :: RadioOn()
{
#if U64 != 2
    if (state == eWifi_Off) {
        wifi_switch_on(&radio_switch, false);
        Enable();
        return;
    }
    if (!TalksToModule()) {
        // still starting: the start applies it
        wifi_switch_later(&radio_switch, true);
        return;
    }
    // Ends a wait for a radio-off confirmation. A module without the switch is never
    // talked to here: it is held off (state Off) at boot and by RadioOff.
    wifi_switch_on(&radio_switch, true);
#endif
    wifi_enable();
}

void WiFi :: RadioOff()
{
#if U64 != 2
    if (TalksToModule()) {
        if (!wifi_switch_unsupported(wifi_disable())) {
            return;
        }
        // A module known to lack the radio switch: stop the WiFi application, as Disable
        // always did here, and hold the module off, as at boot, so that its radio is off too
        Disable();
        if (uart) {
            uart->ModuleCtrl(ESP_MODE_OFF);
        }
        return;
    }
    if ((state == eWifi_NotDetected) || (state == eWifi_ModuleDetected)) {
        // Still starting, and whether the module can switch is not known yet: the start
        // applies the Disable (a module with the switch keeps running, its radio off)
        wifi_switch_later(&radio_switch, false);
        return;
    }
    // Off already, or being flashed: stop the WiFi application, as Disable always did
    Disable();
#else
    wifi_disable();
#endif
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
    WifiConnectFeedback feedback;
    char feedback_msg[80];
    TickType_t wait_ticks;

    RefreshRoot();
    static char boot_message[256];
    int len;
    voltages_t voltages;
#if U64 != 2
    int32_t wait;
#endif

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
                // These settings live in the module's NVS; keep them in sync.
                U64Config :: pushPowerOnMode();
                U64Config :: pushWakeOnWifi();
                U64Config :: pushRecoveryUrl();
#endif
                RefreshRoot();
            }
            break;

        case eWifi_AppDetected:
            uart->txDebug = false;

            wifi_getmac(my_mac);
            netstack->set_mac_address(my_mac);
#if U64 != 2
            // Stored as Disabled: the radio goes off before the interface is up, and the
            // link stays down. Disabled is shown once the module confirms (wifi_switch.h).
            if (wifi_switch_wants_off(&radio_switch, netstack->enabled_in_config())) {
                result = wifi_disable();
                netstack->start(); // link down
                // Only a module without the switch is held off; any other error, or a
                // request that never left, is asked again at the deadline
                if (wifi_switch_unsupported(result)) {
                    // A module without the switch cannot keep its radio off: hold the
                    // module off. It had to run to tell (its radio is on from its own
                    // boot, so it may have associated for a moment), where a stored
                    // Disabled used to keep it from starting at all. An Enable starts the
                    // application again (wifi_switch_on), and that start skips the
                    // stored Disabled.
                    printf("WiFi module cannot turn its radio off: %d\n", result);
                    uart->ModuleCtrl(ESP_MODE_OFF);
                    state = eWifi_Off;
                    RefreshRoot();
                    vTaskSuspend(NULL); // until that start ends this task
                    break;
                }
                wifi_switch_asked_off(&radio_switch, xTaskGetTickCount(), pdMS_TO_TICKS(WIFI_SWITCH_CONFIRM_MS));
                state = eWifi_NotConnected;
                RefreshRoot();
                break;
            }
            // Stored as Enabled: the module starts with its radio off and turns it on now.
            // A module without the switch, whose radio is on already, ignores this.
            wifi_enable();
#endif
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
            // Back to where it was: a scan does not take the link down. Events the module
            // sent while it scanned are taken next, from the queue (as many as the receive
            // buffers hold while this task waits for the scan's answer).
            state = state_before_scan;
            RefreshRoot();
            RefreshAPs();
            break;

        case eWifi_NotConnected:
        case eWifi_Connected:
        case eWifi_Disabled:
            if (feedback.onTick(xTaskGetTickCount(), feedback_msg, sizeof(feedback_msg))) {
                UserInterface :: postMessage(feedback_msg);
            }
            wait_ticks = feedback.waitTicks(xTaskGetTickCount());
#if U64 != 2
            wait = wifi_switch_wait(&radio_switch, xTaskGetTickCount());
            if ((wait >= 0) && ((TickType_t)wait < wait_ticks)) {
                wait_ticks = wait;
            }
#endif
            if (cmd_buffer_received(packets, &buf, wait_ticks) != pdTRUE) {
#if U64 != 2
                // The radio off was not confirmed in time: take the link state from the
                // module again
                if (wifi_switch_expired(&radio_switch, xTaskGetTickCount())) {
                    printf("WiFi module did not confirm the radio off\n");
                    // Ask once more first: a Disable that never left (no transmit buffer)
                    // or that a module restart lost would otherwise let the connection
                    // query start the radio of a module that takes it as an application
                    // without the switch (wifi_modem.c, off_asked)
                    wifi_disable();
                    state = eWifi_NotConnected;
                    if ((wifi_is_connected(conn) == 0) && conn) {
                        wifi_modem_enable(true); // take control!
                        netstack->link_up();
                        state = eWifi_Connected;
                    }
                    RefreshRoot();
                }
#endif
                break; // the wait for the outcome of a connect ran out; the next pass reports it
            }
            hdr = (rpc_header_t *)(buf->data);
#if DEBUG_INPUT
            printf("Pkt %d. Ev %b. Sz %d. Seq: %d\n", buf->bufnr, hdr->command, buf->size, hdr->sequence);
#endif
#if U64 != 2
            if (wifi_switch_drop_event(&radio_switch, hdr->command)) {
                uart->FreeBuffer(buf);
                break;
            }
#endif
            switch(hdr->command) {
            case EVENT_RESCAN: // requested from user interface
                uart->FreeBuffer(buf);
                // Disabled means no RF, also while the radio off waits to be confirmed:
                // the module does not scan with its radio off
                if ((state == eWifi_Disabled) || RadioOffAwaited()) {
                    break;
                }
                // The module scans while it stays associated, so the interface stays up;
                // no packet goes out until the scan is done (wifi_tx_packet sends only
                // when Connected)
                state_before_scan = state;
                state = eWifi_Scanning;
                RefreshRoot();
                break;

#if U64 != 2
            // The module restarted on its own (radio_boot.h). It put its radio back as it
            // was, or, with no record of that, turned it on: while Disabled, the radio off
            // is asked for again, and confirmed as at boot.
            case EVENT_RESTARTED:
                uart->FreeBuffer(buf);
                if (state == eWifi_Disabled) {
                    wifi_disable();
                    wifi_switch_asked_off(&radio_switch, xTaskGetTickCount(), pdMS_TO_TICKS(WIFI_SWITCH_CONFIRM_MS));
                }
                break;

#endif
            case EVENT_DISABLED:
                uart->FreeBuffer(buf);
                state = eWifi_Disabled;
                event_when_enabled = 0;
                wifi_aps.num_records = 0; // nothing to pick while the radio is off
                netstack->link_down();
                RefreshRoot();
                break;

            case EVENT_CONNECTED:
                ev2 = (event_pkt_connected *)buf->data;
                memcpy(last_ap, ev2->ssid, 32);
                feedback.onConnected();
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

            case EVENT_CONNECT_PENDING: // the module accepted a connect from the AP list
                ev2 = (event_pkt_connected *)buf->data;
                feedback.begin(ev2->ssid, xTaskGetTickCount());
                uart->FreeBuffer(buf);
                break;

            case EVENT_DISCONNECTED:
                // the reason is read before the buffer goes back to the UART, which may refill it
                if (feedback.onDisconnected(wifi_disconnect_reason(buf->data, buf->size), feedback_msg, sizeof(feedback_msg))) {
                    UserInterface :: postMessage(feedback_msg);
                }
                uart->FreeBuffer(buf);
                state = eWifi_NotConnected;
                if(netstack) {
                    netstack->link_down();
                }
                if (event_when_enabled) { // the radio is on again: what the user asked for
                    sendEvent(event_when_enabled);
                    event_when_enabled = 0;
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
                    // A copy: an unread socket holds its segments, and one connection can queue
                    // more of them than the UART has receive buffers, which stops the interface.
                    netstack->input_copy((uint8_t*)&pkt->data, pkt->len);
                } else {
                    puts("Packet received, but no net stack!");
                }
                uart->FreeBuffer(buf);
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

// The module accepted a connect request; its own event will tell the outcome.
bool WiFi :: sendConnectPending(const char *ssid)
{
    command_buf_t *buf;
    cmd_buffer_get(packets, &buf, portMAX_DELAY);
    event_pkt_connected *ev = (event_pkt_connected *)(buf->data);
    ev->hdr.command = EVENT_CONNECT_PENDING;
    ev->hdr.sequence = 0;
    ev->hdr.thread = 0xFF;
    bzero(ev->ssid, 32);
    strncpy((char *)ev->ssid, ssid, 32);
    if (cmd_buffer_loopback(packets, buf) != pdTRUE) { // internal event
        uart->FreeBuffer(buf);
        return false;
    }
    return true;
}

void WiFi :: freeBuffer(command_buf_t *buf)
{
    uart->FreeBuffer(buf);
}

int WiFi ::getAccessPointItems(Browsable *parent, IndexedList<Browsable *> &list)
{
    ultimate_ap_records_t *aps = &wifi_aps;
    if (aps->num_records == 0) {
        // While Disabled nothing scans, so the list is empty until WiFi is enabled
        list.append(new BrowsableStatic((state == eWifi_Disabled) ? "WiFi is disabled" : "Scanning..."));
        return 0;
    }
    ENTER_SAFE_SECTION;
    for (int i = 0; i < aps->num_records; i++) {
        list.append(new BrowsableWifiAP(parent, (char *)aps->aps[i].ssid, aps->aps[i].rssi, aps->aps[i].authmode,
                                        aps->aps[i].bssid, aps->aps[i].channel));
    }
    LEAVE_SAFE_SECTION;
    return aps->num_records;
}

#include "subsys.h"
void BrowsableWifiAP :: fetch_context_items(IndexedList<Action *>&items)
{
    items.append(new Action("Connect", BrowsableWifiAP :: connect_ap, (int)this, 0));
}

SubsysResultCode_e BrowsableWifiAP :: connect_ap(SubsysCommand *cmd)
{
    char password[65] = { 0 }; // string_box takes 64 characters plus the terminator
    BrowsableWifiAP *bap = (BrowsableWifiAP *)cmd->functionID;
    if ((wifi.getState() == eWifi_Disabled) && !NetworkLWIP_WiFi :: ask_enable(cmd->user_interface)) {
        return SSRET_OK;
    }
    if (bap->auth) {
        if(cmd->user_interface) {
            if (cmd->user_interface->string_box("Enter password", password, 64) <= 0) {
                return SSRET_OK; // cancelled: nothing to connect with
            }
        }
    }
    // The AP picked, not just any AP with its SSID
    int result = wifi_wifi_connect(bap->ssid, password, bap->auth, bap->bssid, bap->channel);
    printf("Connect result: %d\n", result);
    if (result != 0 || !wifi.sendConnectPending(bap->ssid)) {
        char msg[64];
        snprintf(msg, sizeof(msg), "Could not connect to %.32s", bap->ssid);
        UserInterface :: postMessage(msg);
    }
    return SSRET_OK;
}

err_t wifi_tx_packet(void *driver, void *buffer, int length)
{
    if (wifi.getState() != eWifi_Connected) {
        return ERR_CONN;
    }
    
    BUFARGS_NOREPLY(send_eth, CMD_SEND_PACKET, ERR_MEM); // the module does not answer frames

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