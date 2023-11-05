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
#include "network_esp32.h"
#include "filemanager.h"

#define DEBUG_INPUT  1
#define EVENT_START  0xF1
#define EVENT_TERM   0xF2

WiFi :: WiFi()
{
    runModeTask = NULL;
    state = eWifi_Off;

    bzero(my_mac, 6);
    my_ip = 0;
    my_gateway = 0;
    my_netmask = 0;
    netstack = new NetworkLWIP_WiFi(this, wifi_tx_packet, wifi_free);
    netstack->attach_config();
    netstack->effectuate_settings(); // might already turn this thing on!
    esp32.AttachApplication(this);
}

void WiFi :: RefreshRoot()
{
    // Quick hack to refresh the screen
    FileManager :: getFileManager() -> sendEventToObservers(eRefreshDirectory, "/", "");    
}

void WiFi :: Enable()
{
    esp32.doStart();
}

void WiFi :: Disable()
{
    esp32.doDisable();
}

BaseType_t wifi_rx_isr(command_buf_context_t *context, command_buf_t *buf, BaseType_t *w);

// From Esp32Application
void WiFi :: Init(DmaUART *uart, command_buf_context_t *packets)
{
    this->uart = uart;
    this->packets = packets;
    uart->SetReceiveCallback(wifi_rx_isr);
}

void WiFi :: Start()
{
    if (runModeTask) {
        printf("** Trying to start Wifi Application, but task is already running?\n");
        return;
    }
    state = eWifi_NotDetected;
    xTaskCreate( WiFi :: RunModeTaskStart, "WiFi Command Task", configMINIMAL_STACK_SIZE, this, tskIDLE_PRIORITY + 1, &runModeTask );
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
                //dump_hex_relative(buf->data, buf->size);
            }
            //printf("End of packets. Aux data:\n");
            //uart->PrintRxMessage();
        }
    }
    uart->txDebug = false;
}

void WiFi::RunModeTaskStart(void *context)
{
    WiFi *w = (WiFi *) context;
    w->RunModeThread();
}

WiFi wifi;
ultimate_ap_records_t wifi_aps;

void WiFi :: RunModeThread()
{
    uint16_t major, minor;
    BaseType_t suc = pdFALSE;
    command_buf_t *buf;
    rpc_header_t *hdr;
    rpc_wifi_connect_req *conn_req;
    event_pkt_got_ip *ev;
    int result;

    state = eWifi_NotDetected;
    RefreshRoot();

    while(1) {
        switch(state) {
        // case eWifi_Off:
        //     if (cmd_buffer_received(packets, &buf, 200) == pdTRUE) {
        //         hdr = (rpc_header_t *)(buf->data);
        //         if (hdr->command == EVENT_START) {
        //             printf("Starting Wifi!\n");
        //             state = eWifi_NotDetected;
        //         } else {
        //             printf("In off state only the START event is recognized, got: %02x!", hdr->command);
        //         }
        //     } else {
        //         printf("RunMode Thread running, but state is off...\n");
        //     }
        //     break;

        case eWifi_Off:
            printf("RunMode Thread running, but state is off...\n");
            vTaskDelay(200);
            break;

        case eWifi_NotDetected:
            uart->txDebug = true;
            if (wifi_detect(&major, &minor, moduleName, 32)) {
                state = eWifi_Detected;
                RefreshRoot();
            }
            break;

        case eWifi_Detected:
            uart->txDebug = true;
            result = wifi_setbaud(6666666, 1);
            printf("Result of setbaud: %d\n", result);

            wifi_getmac(my_mac);
            netstack->set_mac_address(my_mac);
            netstack->start();

            wifi_scan(&wifi_aps);

            state = eWifi_NotConnected;

            printf("Auto connect to %s with pass %s\n", cfg_ssid.c_str(), cfg_pass.c_str());
            if(cfg_ssid.length() > 0) {
                if (wifi_wifi_connect_known_ssid(cfg_ssid.c_str(), cfg_pass.c_str()) == ERR_OK) {
                    state = eWifi_Connected;
                    uart->txDebug = false;
                    netstack->link_up();
                } else {
                    printf("Unsuccessful auto connect.\n");
                }
            }
            RefreshRoot();
            break;

        case eWifi_NotConnected:
        case eWifi_Failed:
            cmd_buffer_received(packets, &buf, portMAX_DELAY);
            hdr = (rpc_header_t *)(buf->data);
#if DEBUG_INPUT
            printf("Pkt %d. Ev %b. Sz %d. Seq: %d\n", buf->bufnr, hdr->command, buf->size, hdr->sequence);
#endif
            switch(hdr->command) {
            case EVENT_RESCAN:
                cmd_buffer_free(packets, buf);
                state = eWifi_Detected;
                RefreshRoot();
                break;

            case EVENT_CONNECTED:
                // Obtain the password from the event to write it to the network interface
                // in order to have it saved to the config. Very inconvenient, but that's
                // how queues work.
                conn_req = (rpc_wifi_connect_req *)buf->data;
                netstack->saveSsidPass(conn_req->ssid, conn_req->password);
                cmd_buffer_free(packets, buf);
                if (state == eWifi_Connected) { // already connected!
                    netstack->link_down();
                }
                state = eWifi_Connected;
                uart->txDebug = false;
                netstack->link_up();

                RefreshRoot();
                break;

            case EVENT_DISCONNECTED:
                cmd_buffer_free(packets, buf);
                state = eWifi_Failed;
                if(netstack) {
                    netstack->link_down();
                }
                RefreshRoot();
                break;

            default:
                printf("Unexpected Event type in state NotConnected or Failed: Pkt %d. Ev %b. Sz %d. Seq: %d\n", buf->bufnr, hdr->command, buf->size, hdr->sequence);
                cmd_buffer_free(packets, buf);
                break;
            }
            break;

        case eWifi_Connected:
            cmd_buffer_received(packets, &buf, portMAX_DELAY);
            hdr = (rpc_header_t *)(buf->data);
#if DEBUG_INPUT
            printf("Pkt %d. Ev %b. Sz %d. Seq: %d\n", buf->bufnr, hdr->command, buf->size, hdr->sequence);
#endif
            switch(hdr->command) {
            case EVENT_CONNECTED:
                cmd_buffer_free(packets, buf);
                netstack->link_down();
                if(netstack) {
                    netstack->link_up();
                }
                RefreshRoot();
                break;

            case EVENT_DISCONNECTED:
                cmd_buffer_free(packets, buf);
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
                    cmd_buffer_free(packets, buf);
                }
                // no free, as the packet needs to live on in the network stack
                break;

            default:
                printf("Unexpected Event type in Connected: Pkt %d. Ev %b. Sz %d. Seq: %d\n", buf->bufnr, hdr->command, buf->size, hdr->sequence);
                cmd_buffer_free(packets, buf);
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
    cmd_buffer_free(packets, buf);
}

void WiFi ::getAccessPointItems(Browsable *parent, IndexedList<Browsable *> &list)
{
    ultimate_ap_records_t *aps = &wifi_aps;
    ENTER_SAFE_SECTION;
    for (int i = 0; i < aps->num_records; i++) {
        list.append(new BrowsableWifiAP(parent, (char *)aps->aps[i].ssid, aps->aps[i].rssi, aps->aps[i].authmode));
    }
    LEAVE_SAFE_SECTION;
}

/// C like functions to 'talk' with the WiFi Module
static uint16_t sequence_nr = 0;
extern WiFi wifi;
TaskHandle_t tasksWaitingForReply[NUM_BUFFERS];

#define BUFARGS(x, cmd)     command_buf_t *buf; \
                            wifi.uart->GetBuffer(&buf, portMAX_DELAY); \
                            rpc_ ## x ## _req *args = (rpc_ ## x ## _req *)buf->data; \
                            args->hdr.command = cmd; \
                            args->hdr.sequence = sequence_nr++; \
                            args->hdr.thread = (uint8_t)buf->bufnr; \
                            buf->size = sizeof(rpc_ ## x ## _req); \
                            tasksWaitingForReply[buf->bufnr] = xTaskGetCurrentTaskHandle();


#define TRANSMIT(x)         wifi.uart->TransmitPacket(buf); \
                            xTaskNotifyWait(0, 0, (uint32_t *)&buf, portMAX_DELAY); \
                            printf("Received buffer %d, size %d:\n", buf->bufnr, buf->size); \
                            dump_hex_relative(buf->data, buf->size > 32 ? 32 : buf->size); \
                            rpc_ ## x ## _resp *result = (rpc_ ## x ## _resp *)buf->data; \
                            tasksWaitingForReply[result->hdr.thread] = NULL;

#define RETURN_STD          errno = result->xerrno; \
                            int retval = result->retval; \
                            wifi.uart->FreeBuffer(buf); \
                            return retval;

#define RETURN_ESP          int retval = result->esp_err; \
                            wifi.uart->FreeBuffer(buf); \
                            return retval;


void hex(uint8_t h)
{
    static const uint8_t hexchars[] = "0123456789ABCDEF";
    ioWrite8(UART_DATA, hexchars[h >> 4]);
    ioWrite8(UART_DATA, hexchars[h & 15]);
}

BaseType_t wifi_rx_isr(command_buf_context_t *context, command_buf_t *buf, BaseType_t *w)
{
    rpc_header_t *hdr = (rpc_header_t *)buf->data;
    BaseType_t res;

    hex(hdr->thread);
    ioWrite8(UART_DATA, '-');
//    uint32_t th = (uint32_t)tasksWaitingForReply[hdr->thread];
//    hex((uint8_t)(th >> 16));
//    hex((uint8_t)(th >> 8));
//    hex((uint8_t)(th >> 0));
//    ioWrite8(UART_DATA, ';');

    if ((hdr->thread < NUM_BUFFERS) && (tasksWaitingForReply[hdr->thread])) {
        TaskHandle_t thread = tasksWaitingForReply[hdr->thread];
        tasksWaitingForReply[hdr->thread] = NULL;
        ioWrite8(UART_DATA, 'N');
        res = xTaskNotifyFromISR(thread, (uint32_t)buf, eSetValueWithOverwrite, w);
    } else {
        ioWrite8(UART_DATA, '~');
        res = cmd_buffer_received_isr(context, buf, w);
    }
    return res;
}


int wifi_setbaud(int baudrate, uint8_t flowctrl)
{
    BUFARGS(setbaud, CMD_SET_BAUD);

    args->baudrate = baudrate;
    args->flowctrl = flowctrl;
    args->inversions = 0;

    wifi.uart->TransmitPacket(buf);
    vTaskDelay(100); // wait until transmission must have completed (no handshake!) (~ half second, remote side will send reply at new rate after one second)
    wifi.uart->SetBaudRate(baudrate);
    wifi.uart->FlowControl(flowctrl != 0);

    BaseType_t success = xTaskNotifyWait(0, 0, (uint32_t *)&buf, 1000); // 5 seconds
    if (success == pdTRUE) {
        // copy results
        rpc_espcmd_resp *result = (rpc_espcmd_resp *)buf->data;
        wifi.uart->FreeBuffer(buf);
        tasksWaitingForReply[result->hdr.thread] = NULL;
        return result->esp_err;
    }
    return -1;
}

BaseType_t wifi_detect(uint16_t *major, uint16_t *minor, char *str, int maxlen)
{
    BUFARGS(identify, CMD_IDENTIFY);

    wifi.uart->TransmitPacket(buf);
    // now block thread for reply
    BaseType_t success = xTaskNotifyWait(0, 0, (uint32_t *)&buf, 100); // .5 seconds
    if (success == pdTRUE) {
        // From this moment on, "buf" points to the receive buffer!!
        // copy results
        rpc_identify_resp *result = (rpc_identify_resp *)buf->data;
        *major = result->major;
        *minor = result->minor;
        strncpy(str, &result->string, maxlen);
        str[maxlen-1] = 0;
        printf("Identify: %s\n", str);
    } else {
        // 'buf' still points to the transmit buffer.
        // It is freed by the TxIRQ, so the content is invalid
        // Let's try to get messages from the receive buffer to see what has been received.
        printf("Receive queue:\n");
        while (wifi.uart->ReceivePacket(&buf, 0) == pdTRUE) {
            printf("Buf %02x. Size: %4d. Data:\n", buf->bufnr, buf->size);
            dump_hex(buf->data, buf->size > 64 ? 64 : buf->size);
        }
        printf("/end of Receive queue\n");
    }
    return success;
}

int wifi_getmac(uint8_t *mac)
{
    BUFARGS(identify, CMD_WIFI_GETMAC);
    TRANSMIT(getmac);
    if (result->esp_err == 0) {
        memcpy(mac, result->mac, 6);
    } else {
        printf("Get MAC returned %d as error code.\n", result->esp_err);
    }
    RETURN_ESP;
}

int wifi_scan(void *a)
{
    ultimate_ap_records_t *aps = (ultimate_ap_records_t *)a;

    BUFARGS(identify, CMD_WIFI_SCAN);
    TRANSMIT(scan);
    ENTER_SAFE_SECTION;
    memcpy(aps, &(result->rec), sizeof(ultimate_ap_records_t));
    LEAVE_SAFE_SECTION;
    RETURN_ESP;
}

int wifi_wifi_connect(const char *ssid, const char *password, uint8_t auth)
{
    BUFARGS(wifi_connect, CMD_WIFI_CONNECT);

    bzero(args->ssid, 32);
    bzero(args->password, 64);
    strncpy(args->ssid, ssid, 32);
    strncpy(args->password, password, 64);
    args->auth_mode = auth;

    TRANSMIT(scan);
    RETURN_ESP;
}

int wifi_wifi_connect_known_ssid(const char *ssid, const char *password)
{
    for(int i=0; i < wifi_aps.num_records; i++) {
        if (strncmp((char *)wifi_aps.aps[i].ssid, ssid, 32) == 0) {
            return wifi_wifi_connect(ssid, password, wifi_aps.aps[i].authmode);
        }
    }
    // not found
    return 1;
}

int wifi_wifi_disconnect()
{
    BUFARGS(wifi_connect, CMD_WIFI_DISCONNECT);
    TRANSMIT(espcmd);
    RETURN_ESP;
}

uint8_t wifi_tx_packet(void *driver, void *buffer, int length)
{
    BUFARGS(send_eth, CMD_SEND_PACKET);

    uint32_t chunkSize = length;
    if (chunkSize > CMD_BUF_PAYLOAD) {
        chunkSize = CMD_BUF_PAYLOAD;
    }
    args->length = chunkSize;
    memcpy(&args->data, buffer, chunkSize);
    buf->size += (chunkSize - 4);

    wifi.uart->TransmitPacket(buf);
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


#include "subsys.h"
void BrowsableWifiAP :: fetch_context_items(IndexedList<Action *>&items)
{
    items.append(new Action("Connect", BrowsableWifiAP :: connect_ap, (int)this, 0));
}

int BrowsableWifiAP :: connect_ap(SubsysCommand *cmd)
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
    return 0;
}
