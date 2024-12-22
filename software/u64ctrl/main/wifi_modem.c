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
#include "pinout.h"

#define NET_CMD_BUFSIZE 512

static const char *TAG = "raw_bridge";

// Allocate some buffers to work with
command_buf_context_t work_buffers;

// do not put it on the stack
wifi_ap_record_t ap_info[DEFAULT_SCAN_LIST_SIZE];
uint16_t ap_count = 0;
uint16_t connected_g = 0;

esp_netif_t *my_sta_netif = NULL;
netif_input_fn default_input;
netif_output_fn default_output;

// typedef err_t (*netif_input_fn)(struct pbuf *p, struct netif *inp);

static err_t hacked_recv(struct pbuf *p, struct netif *inp)
{
    static uint16_t seq = 0;
    command_buf_t *reply;
    if ((memcmp(p->payload, inp->hwaddr, 6) == 0) || 
        (memcmp(p->payload, "\xff\xff\xff\xff\xff\xff", 6) == 0)) {

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

static void wifi_event_handler(void *esp_netif, esp_event_base_t base, int32_t event_id, void *data)
{
    ESP_LOGW(TAG, "WiFi Event %ld, data %p", event_id, data);

    uint8_t evcode = 0;
    switch(event_id) {
        case WIFI_EVENT_STA_DISCONNECTED:
            evcode = EVENT_DISCONNECTED;
            connected_g = 0;
            break;
        case WIFI_EVENT_STA_CONNECTED:
            evcode = EVENT_CONNECTED;
            connected_g = 1;
            break;
        default:
            break;
    }

    if (evcode) {
        command_buf_t *reply;
        if (my_uart_get_buffer(UART_NUM_1, &reply, 100)) {
            rpc_header_t *ev = (rpc_header_t *)reply->data;
            ev->command = evcode;
            ev->thread = 0xFF;
            ev->sequence = 0;
            reply->size = sizeof(rpc_header_t);
            my_uart_transmit_packet(UART_NUM_1, reply);
        } else {
            ESP_LOGW(TAG, "No buffer to send event.");
        }
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
    if (connected_g) {
        err = esp_wifi_disconnect();
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "SCAN requested while connected. Disconnect returned error %d", err);
            return err;
        }
        vTaskDelay(100 / portTICK_PERIOD_MS); // disconnect event should be sent here
        connected_g = 0; // no longer connected
    }

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

esp_err_t wifi_store_ap(const char *ssid, const char *password, uint8_t auth_mode)
{
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
            ESP_LOGE(TAG, "SSID does not exist yet: %d", err);
            err = nvs_set_str(handle, key, ssid);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "Failed to store SSID: %d", err);
                return err;
            }
            err = store_password(handle, key, password, auth_mode);
            stored = 1;
            break;
        } else {
            if (strcmp(ssid_buf, ssid) == 0) {
                ESP_LOGI(TAG, "SSID %s already stored, overwriting...", ssid);
                err = store_password(handle, key, password, auth_mode);
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
        err = store_password(handle, key, password, auth_mode);
    }
    nvs_close(handle);
    return err;
}

void attempt_connect(const char *ssid, const char *passwd, uint8_t authmode)
{
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
    ESP_LOGI(TAG, "Connecting to %s, with password %s, mode %d", ssid, passwd, authmode);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to connect: %d", err);
    }
}

void attempt_auto_connect(void)
{
    nvs_handle_t handle;
    static int index = 0;
    esp_err_t err = ESP_OK;
    
    err = nvs_open("recent_aps", NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS: %d", err);
        return;
    }

    char key[4] = {0};
    char passwd[64];
    char ssid[64];
    size_t len = 32;    
    key[0] = 's';
    key[1] = 'a' + index;
    key[2] = '0'; // SSID sa, sb, sc, sd, ...

    err = nvs_get_str(handle, key, ssid, &len);
    if (err == ESP_OK) {
        index ++;
        key[0] = 'p'; // password pa, pb, pc, pd, ...
        len = 64;
        err = nvs_get_str(handle, key, passwd, &len);
        if (err == ESP_OK) {
            key[0] = 'm'; // authmode ma, mb, mc, md, ...
            uint8_t authmode;
            err = nvs_get_u8(handle, key, &authmode);
            if (err == ESP_OK) {
                attempt_connect(ssid, passwd, authmode);
            }
        }
    } else {
        index = 0;
    }
    nvs_close(handle);
}

void wifi_check_connection() // poll function, called every 2 seconds
{
    static int scan_delay = 0;

    if (!connected_g) {
        if (scan_delay == 0) {
            scan_delay = 5; // every 10 seconds
            // wifi_scan(NULL); // store the results in ap_info
            attempt_auto_connect();
        }
    } else { // connected, once we disconnect, try to scan after 10 seconds
        scan_delay = 5;
    }
    if (scan_delay > 0) {
        scan_delay--;
    }
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
    wifi_scan(NULL);

//    obtain_time();
//    print_time();    

    ESP_LOGI(TAG, "dispatch... switching UART pins!");
    vTaskDelay(100 / portTICK_PERIOD_MS);
    ESP_ERROR_CHECK( uart_set_pin(UART_NUM_0, 0, -1, -1, -1));
    ESP_ERROR_CHECK( uart_set_pin(UART_NUM_1, IO_UART_TXD, IO_UART_RXD, IO_UART_RTS, IO_UART_CTS));
    start_dispatch(work_buffers.receivedQueue);
}
