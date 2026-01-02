/* Wifi modem for the Ultimate 64-II

   This file sets up the the WiFi modem task. It will scan for WiFi networks and
   communicate the APs through the UART to the Ultimate 64-II, which will display
   the networks on the screen. All network traffic is forwarded to the Ultimate 64-II.
*/

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "esp_wifi.h"
#include "esp_log.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_system.h"
#include "my_uart.h"
#include "dump_hex.h"
#include "lwip/sockets.h"
#include "lwip/netdb.h"
#include "esp_vfs_eventfd.h"
#include "../lwip/esp_netif_lwip_internal.h"
#include "driver/gpio.h"
#include "soc/io_mux_reg.h"

#include "sntp.h"
#include "rpc_calls.h"
#include "rpc_dispatch.h"
#include "wifi_modem.h"
#include "pinout.h"

#define NET_CMD_BUFSIZE 512

static const char *TAG = "raw_bridge";

// Allocate some buffers to work with
command_buf_context_t work_buffers;

// do not put it on the stack
wifi_ap_record_t ap_info[DEFAULT_SCAN_LIST_SIZE];
uint16_t ap_count = 0;
uint16_t connected_g = 0;
uint16_t disabled_g = 0;

esp_netif_t *my_sta_netif = NULL;
netif_input_fn default_input;
netif_output_fn default_output;

SemaphoreHandle_t connect_semaphore;
QueueHandle_t connect_commands;
QueueHandle_t connect_events;
ConnectCommand_t last_connect;

typedef enum {
    LastAP, Scanning, ScannedAPs, StoredAPs, Connected, Disconnected, Disabled,
} ConnectState_t;
static const char *states[] = { "LastAP", "Scanning", "ScannedAPs", "StoredAPs", "Connected", "Disconnected", "Disabled" }; 

// typedef err_t (*netif_input_fn)(struct pbuf *p, struct netif *inp);

static err_t hacked_recv(struct pbuf *p, struct netif *inp)
{
    static uint16_t seq = 0;
    command_buf_t *reply;
    if ((memcmp(p->payload, inp->hwaddr, 6) == 0) || 
        (memcmp(p->payload, "\xff\xff\xff\xff\xff\xff", 6) == 0)) {

        // gpio_set_level(IO_ESP_LED, 0); // ON!

        if (my_uart_get_buffer(UART_NUM_1, &reply, 100)) {
            rpc_rx_pkt *pkt = (rpc_rx_pkt *)reply->data;
            pkt->hdr.command = EVENT_RECV_PACKET;
            pkt->hdr.thread = 0xFF;
            pkt->hdr.sequence = seq++;
            pkt->len = p->len;
            memcpy(&pkt->data, p->payload, p->len);
            reply->size = sizeof(rpc_rx_pkt) + p->len - 1;
            pbuf_free(p);
            my_uart_transmit_packet(UART_NUM_1, reply);
            return ERR_OK;
        } else {
            ESP_LOGW(TAG, "No buffer to send packet.");
            return ERR_BUF;
        }
    } else { // dropped, wrong mac and no broadcast
        pbuf_free(p);
        return ERR_OK;
    }
}

static err_t hacked_tx(struct netif *netif, struct pbuf *pbuf, const struct ip4_addr *addy)
{
    //pbuf_free(pbuf);
    return ERR_OK;
}


static void got_ip_event_handler(void *esp_netif, esp_event_base_t base, int32_t event_id, void *data)
{
    const ip_event_got_ip_t *event = (const ip_event_got_ip_t *) data;

    ESP_LOGI(TAG, "Got ip: " IPSTR ", mask: " IPSTR ", gw: " IPSTR,
             IP2STR(&event->ip_info.ip),
             IP2STR(&event->ip_info.netmask),
             IP2STR(&event->ip_info.gw));

    obtain_time();
    print_time();

    ESP_LOGI(TAG, "Time obtained; now reporting to ultimate that we got an IP.");

    command_buf_t *reply;
    if (my_uart_get_buffer(UART_NUM_1, &reply, 100)) {
        event_pkt_got_ip *ev = (event_pkt_got_ip *)reply->data;
        ev->hdr.command = EVENT_GOTIP;
        ev->hdr.thread = 0xFF;
        ev->hdr.sequence = 0;
        ev->ip = event->ip_info.ip.addr;
        ev->netmask = event->ip_info.netmask.addr;
        ev->gw = event->ip_info.gw.addr;
        ev->changed = event->ip_changed;
        reply->size = sizeof(event_pkt_got_ip);
        my_uart_transmit_packet(UART_NUM_1, reply);
    } else {
        ESP_LOGW(TAG, "No buffer to send event.");
    }
}

static void send_event(uint8_t evcode)
{
    command_buf_t *reply;
    if (my_uart_get_buffer(UART_NUM_1, &reply, 100)) {
        rpc_header_t *ev = (rpc_header_t *)reply->data;
        ev->command = evcode;
        ev->thread = 0xFF;
        ev->sequence = 0;
        reply->size = sizeof(rpc_header_t);
        if (evcode == EVENT_CONNECTED) {
            event_pkt_connected *ev2 = (event_pkt_connected *)reply->data;
            memcpy(ev2->ssid, last_connect.ssid, 32);
            reply->size = sizeof(event_pkt_connected);
        }
        ESP_LOGI(TAG, "Sending event %d to ultimate", evcode);
        if(my_uart_transmit_packet(UART_NUM_1, reply) == pdFALSE) {
            ESP_LOGW(TAG, "Failed to send event to ultimate");
        }
    } else {
        ESP_LOGW(TAG, "No buffer to send event.");
    }
}

void wifi_send_connected_event(void)
{
    if(connected_g) {
        send_event(EVENT_CONNECTED);
    } else if(disabled_g) {
        send_event(EVENT_DISABLED);
    }
}

static void wifi_event_handler(void *esp_netif, esp_event_base_t base, int32_t event_id, void *data)
{
    ESP_LOGW(TAG, "WiFi Event %ld, data %p", event_id, data);
    ConnectEvent_t cev;

    uint8_t evcode = 0;
    switch(event_id) {
        case WIFI_EVENT_STA_DISCONNECTED:
            evcode = EVENT_DISCONNECTED;
            cev.event_code = evcode;
            xQueueSend(connect_events, &cev, 10);
            xSemaphoreGive(connect_semaphore);
            connected_g = 0;
            break;
        case WIFI_EVENT_STA_CONNECTED:
            evcode = EVENT_CONNECTED;
            cev.event_code = evcode;
            xQueueSend(connect_events, &cev, 10);
            xSemaphoreGive(connect_semaphore);
            connected_g = 1;
            break;
        default:
            break;
    }
    if (evcode) {
        send_event(evcode);
    }
}
    
void enable_hook()
{
    struct netif *lw = ((struct esp_netif_obj *)my_sta_netif)->lwip_netif;
    if (lw) {
        lw->input = hacked_recv;
        lw->output = hacked_tx;
    }
}

void disable_hook()
{
    struct netif *lw = ((struct esp_netif_obj *)my_sta_netif)->lwip_netif;
    if(lw) {
        lw->input = default_input;
        lw->output = default_output;
    }
}

esp_err_t wifi_clear_aps(void)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open("recent_aps", NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        return err;
    }
    err = nvs_erase_all(handle);
    if (err != ESP_OK) {
        return err;
    }
    err = nvs_commit(handle);
    nvs_close(handle);
    return err;
}

// Initialize Wi-Fi as sta
static void wifi_init()
{
    ESP_ERROR_CHECK(esp_netif_init());

    ESP_ERROR_CHECK(esp_event_loop_create_default());
    my_sta_netif = esp_netif_create_default_wifi_sta();
    assert(my_sta_netif);
    
    configure_sntp_servers();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        NULL));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &got_ip_event_handler,
                                                        NULL,
                                                        NULL));

    struct netif *lw = ((struct esp_netif_obj *)my_sta_netif)->lwip_netif;
    if (lw) {
        default_input = lw->input;
        default_output = lw->output;
    }
}

uint8_t wifi_get_connection()
{
    return (uint8_t)connected_g;
}

// Initialize Wi-Fi issue scan command
esp_err_t wifi_scan(ultimate_ap_records_t *ult_records)
{
    if (ult_records) {
        ult_records->reserved = 0;
        ult_records->num_records = 0;
    }

    esp_err_t err;

    uint16_t number = DEFAULT_SCAN_LIST_SIZE;
    memset(ap_info, 0, sizeof(ap_info));
    ap_count = 0;

    err = esp_wifi_scan_start(NULL, true);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to start scan: %d", err);
        return err;
    }
    err = esp_wifi_scan_get_ap_num(&ap_count);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to get AP count: %d", err);
        return err;
    }
    err = esp_wifi_scan_get_ap_records(&number, ap_info);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to get AP records: %d", err);
        return err;
    }

    ESP_LOGI(TAG, "Total APs scanned = %u", ap_count);
    if (ap_count > DEFAULT_SCAN_LIST_SIZE) {
    	ap_count = DEFAULT_SCAN_LIST_SIZE;
    }

    if (ult_records) {
        for (int i = 0; i < ap_count; i++) {
            memcpy(ult_records->aps[i].bssid, ap_info[i].bssid, 6);
            memcpy(ult_records->aps[i].ssid, ap_info[i].ssid, 33);
            ult_records->aps[i].channel = ap_info[i].primary;
            ult_records->aps[i].rssi = ap_info[i].rssi;
            ult_records->aps[i].authmode = ap_info[i].authmode;
        }

        ult_records->num_records = (uint8_t)ap_count;
    } else {
        ESP_LOGW(TAG, "No buffer to send scan results.");
        for (int i = 0; i < ap_count; i++) {
            ESP_LOGI(TAG, "SSID: %s, RSSI: %d, AUTH: %d", ap_info[i].ssid, ap_info[i].rssi, ap_info[i].authmode);
        }
    }
    return ESP_OK;
}

static esp_err_t store_password(nvs_handle_t handle, char *key, const char *password, uint8_t auth_mode)
{
    key[0] = 'p'; // password pa, pb, pc, pd, ...
    esp_err_t err = nvs_set_str(handle, key, password);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to store password: %d", err);
        return err;
    }
    key[0] = 'm'; // authmode ma, mb, mc, md, ...
    err = nvs_set_u8(handle, key, auth_mode);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to store authmode: %d", err);
        return err;
    }
    err = nvs_commit(handle);
    return err;
}

esp_err_t wifi_store_ap(ConnectCommand_t *cmd)
{
    const char *ssid = (const char *)cmd->ssid;
    const char *password = cmd->pw;
    const uint8_t auth_mode = cmd->auth_mode;

    nvs_handle_t handle;
    esp_err_t err = nvs_open("recent_aps", NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS: %d", err);
        return err;
    }
    int stored = 0;
    char key[4] = {0};

    for(int i=0; i<16; i++) {
        key[0] = 's';
        key[1] = 'a' + i;
        key[2] = '0'; // SSID sa, sb, sc, sd, ...
        size_t len = 32;
        char ssid_buf[32];
        // see if the ssid has been stored already
        err = nvs_get_str(handle, key, ssid_buf, &len);
        if (err != ESP_OK) { // nope, it doesn't exist
            ESP_LOGI(TAG, "SSID does not exist yet: %d", err);
            err = nvs_set_str(handle, key, ssid);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "Failed to store SSID: %d", err);
                return err;
            }
            err = store_password(handle, key, password, auth_mode);
            cmd->list_index = i;
            stored = 1;
            break;
        } else {
            if (strcmp(ssid_buf, ssid) == 0) {
                ESP_LOGI(TAG, "SSID %s already stored, overwriting...", ssid);
                err = store_password(handle, key, password, auth_mode);
                cmd->list_index = i;
                stored = 1;
                break;
            }
        }
    }
    if (!stored) {
        ESP_LOGE(TAG, "No more space to store SSID %s", ssid);
        key[0] = 's';
        key[1] = 'a'; // overwrite first (oldest?) entry
        err = nvs_set_str(handle, key, ssid);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to store SSID: %d", err);
            return err;
        }
        cmd->list_index = 0;
        err = store_password(handle, key, password, auth_mode);
    }
    nvs_close(handle);
    return err;
}

esp_err_t wifi_set_last_ap(int index)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open("recent_aps", NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS: %d", err);
        return err;
    }
    err = nvs_set_i8(handle, "last", (int8_t)index);
    nvs_close(handle);
    return err;
}

esp_err_t attempt_connect(const char *ssid, const char *passwd, uint8_t authmode)
{
    if (connected_g) {
        esp_err_t err = esp_wifi_disconnect();
        ESP_LOGW(TAG, "Connect request, but was connected. Waiting now for disconnect event.");
        ConnectEvent_t ev;
        if (err == ESP_OK) {
            xQueueReceive(connect_events, &ev, portMAX_DELAY);
            if (ev.event_code != EVENT_DISCONNECTED) {
                ESP_LOGE(TAG, "Would have expected disconnect event, %d", ev.event_code);
            } else if (ev.event_code != EVENT_CONNECTED) {
                ESP_LOGI(TAG, "Properly disconnected.");
            } else {
                ESP_LOGE(TAG, "Unreachable code");
            }
        } else {
            ESP_LOGE(TAG, "Failed to disconnect: %d", err);
        }
    }

    wifi_config_t wifi_config_auto;
    memset(&wifi_config_auto, 0, sizeof(wifi_config_t));

    if (authmode > 7) {
        ESP_LOGW(TAG, "Auth mode incorrect %d; changed to 7.", authmode);
        authmode = 7;
    }
    wifi_config_auto.sta.threshold.authmode = authmode;
    memcpy(&wifi_config_auto.sta.ssid, ssid, 32);
    memcpy(&wifi_config_auto.sta.password, passwd, 64);
    wifi_config_auto.sta.pmf_cfg.capable = true;
    wifi_config_auto.sta.pmf_cfg.required = false;

    esp_err_t err = esp_wifi_set_config(WIFI_IF_STA, &wifi_config_auto);
    if (err == ESP_OK) {
        err = esp_wifi_connect();
    }
    ESP_LOGI(TAG, "Connecting to %s, with password %s, mode %d -> Err = %d", ssid, passwd, authmode, err);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to connect: %d", err);
    }
    return err;
}

void wifi_list_aps(void)
{
    nvs_handle_t handle;
    esp_err_t err = ESP_OK;
    char key[4] = {0};
    char ssid_entry[36];
    char pw_entry[68];
    size_t len;
    err = nvs_open("recent_aps", NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS: %d", err);
        return;
    }
    for(int16_t i=0;i<16;i++) {
        key[0] = 's';
        key[1] = 'a' + i;
        key[2] = '0'; // SSID sa0, sb0, sc0, sd0, ...
        len = 36;
        err = nvs_get_str(handle, key, ssid_entry, &len);
        if (err == ESP_OK) {
            key[0] = 'p';
            len = 68;
            err = nvs_get_str(handle, key, pw_entry, &len);
            if (err == ESP_OK) {
                ESP_LOGI(TAG, "Stored AP %d: '%s' '%s'", i, ssid_entry, pw_entry);
            } else {
                ESP_LOGI(TAG, "Stored AP %d: Failed to get password: %d", i, err);
            }
        } else {
            ESP_LOGI(TAG, "Stored AP %d: Failed to get SSID: %d", i, err);
        }
    }
    nvs_close(handle);
}

esp_err_t wifi_find_password(const uint8_t *ssid, char *pw, size_t maxlen, int16_t *list_idx)
{
    nvs_handle_t handle;
    esp_err_t err = ESP_OK;
    char key[4] = {0};
    char ssid_entry[36];
    size_t len;
    err = nvs_open("recent_aps", NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS: %d", err);
        return err;
    }
    *list_idx = -1;
    for(int16_t i=0;i<16;i++) {
        key[0] = 's';
        key[1] = 'a' + i;
        key[2] = '0'; // SSID sa0, sb0, sc0, sd0, ...
        len = 36;
        err = nvs_get_str(handle, key, ssid_entry, &len);
        if (err != ESP_OK) { // entry doesn't exist
            err = ESP_ERR_NOT_FOUND;
        } else if (strcmp((const char *)ssid, ssid_entry) == 0) {
            key[0] = 'p';
            len = maxlen;
            err = nvs_get_str(handle, key, pw, &len);
            if (err == ESP_OK) {
                *list_idx = i;
                break;
            }
        }
    }

    nvs_close(handle);
    return err;
}

esp_err_t wifi_connect_to_scanned(int index)
{
    esp_err_t err;

    // now look up password
    err = ESP_OK;
    if (ap_info[index].authmode) {
        err = wifi_find_password(ap_info[index].ssid, last_connect.pw, 64, &last_connect.list_index);
    } else {
        // no authentication required. But make sure the user has configured this AP.
        err = wifi_find_password(ap_info[index].ssid, last_connect.pw, 64, &last_connect.list_index);
        if ((err == ESP_OK) && (last_connect.pw[0] != 0)) {
            // AP is known and configured, however, with a password. Do not allow to connect.
            err = ESP_ERR_NOT_FOUND;
        }
    }
    if (err == ESP_OK) { // known AP found
        strncpy(last_connect.ssid, (const char *)(ap_info[index].ssid), 32);
        last_connect.auth_mode = ap_info[index].authmode;
        return attempt_connect(last_connect.ssid, last_connect.pw, last_connect.auth_mode);
    }
    return err;
}

esp_err_t wifi_connect_to_stored(int index)
{
    nvs_handle_t handle;
    esp_err_t err = ESP_OK;
    
    if (index >= 16) {
        return ESP_ERR_NOT_FOUND;
    }

    err = nvs_open("recent_aps", NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS: %d", err);
        return err;
    }

    char key[4] = {0};
    key[0] = 's';
    key[1] = 'a' + index;
    key[2] = '0'; // SSID sa, sb, sc, sd, ...

    size_t len = 32;    
    err = nvs_get_str(handle, key, last_connect.ssid, &len);
    if (err == ESP_OK) {
        key[0] = 'p'; // password pa, pb, pc, pd, ...
        len = 64;
        err = nvs_get_str(handle, key, last_connect.pw, &len);
        if (err == ESP_OK) {
            key[0] = 'm'; // authmode ma, mb, mc, md, ...
            err = nvs_get_u8(handle, key, &last_connect.auth_mode);
            if (err == ESP_OK) {
                last_connect.list_index = index;
                last_connect.buf = NULL;
                err = attempt_connect(last_connect.ssid, last_connect.pw, last_connect.auth_mode);
            }
        }
    }
    nvs_close(handle);
    return err;
}

esp_err_t wifi_connect_to_last()
{
    nvs_handle_t handle;
    esp_err_t err = ESP_OK;
    
    err = nvs_open("recent_aps", NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS: %d", err);
        return err;
    }
    int8_t idx = -1;
    err = nvs_get_i8(handle, "last", &idx);
    nvs_close(handle);
    if ((err == ESP_OK) && (idx >= 0)) {
        ESP_LOGI(TAG, "Connecting To Last: %d", idx);
        err = wifi_connect_to_stored(idx);
    } else {
        ESP_LOGW(TAG, "Last AP not set.");
    }
    return err;
}

esp_err_t enable_wifi_if_needed(ConnectState_t state)
{
    if (state == Disabled) {
        ESP_LOGI(TAG, "Starting WiFi.");
        esp_err_t err = esp_wifi_start();
        if (err == ESP_OK) {
            send_event(EVENT_DISCONNECTED);
            disabled_g = 0;
        } else {
            ESP_LOGW(TAG, "Failed to start WiFi: %d", err);
        }
        return err;
    }
    return ESP_OK;
}

int handle_connect_command(ConnectCommand_t *cmd, ConnectState_t *state)
{
    ConnectEvent_t connect_event;
    command_buf_t *buf = cmd->buf;
    esp_err_t err;

    switch(cmd->command) {
        case CMD_WIFI_DISABLE:
            if (*state == Connected) {
                ESP_LOGI(TAG, "Disconnecting from WiFi.");
                err = esp_wifi_disconnect();
                if (err == ESP_OK) {
                    xQueueReceive(connect_events, &connect_event, portMAX_DELAY);
                    if (connect_event.event_code == EVENT_DISCONNECTED) {
                        *state = Disconnected;
                    }
                }
            }
            ESP_LOGI(TAG, "Stopping WiFi.");
            err = esp_wifi_stop();
            if (err == ESP_OK) {
                send_event(EVENT_DISABLED);
                *state = Disabled;
                disabled_g = 1;
                return 1;
            } else {
                ESP_LOGW(TAG, "Failed to stop WiFi: %d", err);
            }
            break;
        case CMD_WIFI_ENABLE:
            if (*state == Disabled) {
                ESP_LOGI(TAG, "Starting WiFi.");
                err = esp_wifi_start();
                if (err == ESP_OK) {
                    send_event(EVENT_DISCONNECTED);
                    *state = LastAP;
                    disabled_g = 0;
                    return 1;
                } else {
                    ESP_LOGW(TAG, "Failed to start WiFi: %d", err);
                }
            } else {
                ESP_LOGE(TAG, "Enabling WiFi is not possible in state %s.", states[*state]);
            } // else, do nothing, not supported
            break;
        case CMD_WIFI_SCAN:
            {
                err = enable_wifi_if_needed(*state);
                rpc_scan_resp *resp = (rpc_scan_resp *)buf->data;
                if (err == ESP_OK) {
                    resp->esp_err = wifi_scan(&resp->rec);
                } else {
                    resp->esp_err = err;
                    resp->rec.num_records = 0;
                }
                buf->size = sizeof(resp->hdr) + sizeof(resp->esp_err) + 2 + (resp->rec.num_records * sizeof(ultimate_ap_record_t));
                my_uart_transmit_packet(UART_NUM_1, buf);
            }
            break;
        case CMD_WIFI_AUTOCONNECT:
            err = enable_wifi_if_needed(*state);
            if (err == ESP_OK) {
                *state = LastAP;
            }
            return 1;
        case CMD_WIFI_CONNECT:
            err = enable_wifi_if_needed(*state);
            {
                last_connect = *cmd;
                last_connect.list_index = -1;
                rpc_espcmd_resp *resp = (rpc_espcmd_resp *)buf->data;
                if (err == ESP_OK) {
                    err = attempt_connect(last_connect.ssid, last_connect.pw, last_connect.auth_mode);
                }
                resp->esp_err = err;
                my_uart_transmit_packet(UART_NUM_1, cmd->buf);

                if (err == ESP_OK) { // connect attempt was accepted by esp
                    xQueueReceive(connect_events, &connect_event, portMAX_DELAY);
                    ESP_LOGI(TAG, "Connect Event %d when waiting connection to %s", connect_event.event_code, last_connect.ssid);
                    if (connect_event.event_code == EVENT_CONNECTED) {
                        err = wifi_store_ap(cmd);
                        if (err == ESP_OK) {
                            wifi_set_last_ap(cmd->list_index);
                        }
                        *state = Connected;
                        return 1;
                    }
                }
            }
            break;
        default:
            ESP_LOGW(TAG, "unknown external connect command %d", cmd->command);
    }
    return 0; // not connected
}

void connect_thread(void *a)
{
    ConnectEvent_t connect_event;
    ConnectCommand_t connect_command;
    ConnectState_t state = LastAP;
    ConnectState_t prev = LastAP;
    esp_err_t err;

    while(1) {
        switch (state) {
            case LastAP:
                err = wifi_connect_to_last();
                if (err == ESP_OK) {
                    xQueueReceive(connect_events, &connect_event, portMAX_DELAY);
                    if (connect_event.event_code == EVENT_CONNECTED) {
                        state = Connected;
                    } else {
                        state = Scanning;
                    }
                } else {
                    state = Scanning;
                }
                break;
            case Scanning:
                wifi_scan(NULL);
                state = ScannedAPs;
                break;
            case ScannedAPs:
                wifi_list_aps();
                for (int i=0; i < ap_count; i++) {
                    if (xQueueReceive(connect_commands, &connect_command, 0) == pdTRUE) {
                        if (handle_connect_command(&connect_command, &state)) {
                            break;
                        }
                    }
                    err = wifi_connect_to_scanned(i);
                    if (err == ESP_OK) {
                        xQueueReceive(connect_events, &connect_event, portMAX_DELAY);
                        if (connect_event.event_code == EVENT_CONNECTED) {
                            if (last_connect.list_index >= 0) {
                                wifi_set_last_ap(last_connect.list_index);
                            }
                            state = Connected;
                            break;
                        }
                    }
                }
                if (state != Connected) {
                    state = StoredAPs;
                }
                break;

            case StoredAPs:
                for (int i=0; i < 16; i++) {
                    if (xQueueReceive(connect_commands, &connect_command, 0) == pdTRUE) {
                        if (handle_connect_command(&connect_command, &state)) {
                            break;
                        }
                    }
                    err = wifi_connect_to_stored(i);
                    if (err == ESP_OK) {
                        xQueueReceive(connect_events, &connect_event, portMAX_DELAY);
                        if (connect_event.event_code == EVENT_CONNECTED) {
                            wifi_set_last_ap(i);
                            state = Connected;
                            break;
                        }
                    }
                }
                if (state != Connected) {
                    state = Disconnected;
                }
                break;
            break;
            case Disconnected:
            case Connected:
                // In this state we are just idling, so both commands can come in as well as
                // events. Although - can we really get an event? In any case, polling both is silly
                // so we just only poll when the semaphore is set. It could happen that the semaphore
                // is set and there is nothing in the queue, because the semaphore was set before and
                // we didn't take it. However, in this case we simply check two queues and are back in
                // the waiting state afterwards; no harm done.
                xSemaphoreTake(connect_semaphore, portMAX_DELAY);
                if (xQueueReceive(connect_commands, &connect_command, 0) == pdTRUE) {
                    if (handle_connect_command(&connect_command, &state)) {
                        break;
                    }
                }
                if (xQueueReceive(connect_events, &connect_event, 0) == pdTRUE) {
                    if (connect_event.event_code == EVENT_CONNECTED) {
                        state = Connected;
                        break;
                    } else if (connect_event.event_code == EVENT_DISCONNECTED) {
                        state = Disconnected;
                        break;
                    }
                }
                break;

            case Disabled:
                // The only way to wake up the WiFi again is to send a connect command
                xQueueReceive(connect_commands, &connect_command, portMAX_DELAY);
                handle_connect_command(&connect_command, &state);
                break;

            default:
                state = LastAP;                        
        }
        if(prev != state) {
            ESP_LOGI(TAG, "State Change '%s' -> '%s'", states[prev], states[state]);
        }
        prev = state;
    }
}

static void start_connector()
{
    connect_events = xQueueCreate(4, sizeof(ConnectEvent_t));
    connect_commands = xQueueCreate(4, sizeof(ConnectCommand_t));
    connect_semaphore = xSemaphoreCreateBinary();

    xTaskCreate(connect_thread, "Connector", 4096, NULL, tskIDLE_PRIORITY + 1, NULL);
}

void setup_modem()
{
	// Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK( ret );

    ESP_LOGI(TAG, IDENT_STRING);
    ESP_LOGI(TAG, "disconnect UART 1 pins...");
    ESP_ERROR_CHECK( uart_set_pin(UART_NUM_1, -1, -1, -1, -1));

    ESP_LOGI(TAG, "cmd_buffer_init...");
    // Initialize the single copy transport buffers, and attach dispatchers to each buffer
    cmd_buffer_init(&work_buffers);

    // Initialize Dispatchers and attach them to the buffers
#if MULTITHREADED
    create_dispatchers(DISPATCHER_STACK, &work_buffers);
#endif
    ESP_LOGI(TAG, "my_uart_init...");
    // Now install the driver for UART1.  We use our own buffer mechanism and interrupt routine
    ESP_ERROR_CHECK(my_uart_init(&work_buffers, UART_NUM_1));

    ESP_LOGI(TAG, "wifi_init...");
    wifi_init();
    ESP_LOGI(TAG, "start wifi connector...");
    start_connector();

    ESP_LOGI(TAG, "dispatch... switching UART pins!");
    vTaskDelay(100 / portTICK_PERIOD_MS);
    ESP_ERROR_CHECK( uart_set_pin(UART_NUM_0, 0, -1, -1, -1));
    ESP_ERROR_CHECK( uart_set_pin(UART_NUM_1, IO_UART_TXD, IO_UART_RXD, IO_UART_RTS, IO_UART_CTS));
    start_dispatch(work_buffers.receivedQueue);
}
