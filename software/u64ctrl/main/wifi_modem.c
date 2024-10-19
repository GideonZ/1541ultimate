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

#include "rpc_calls.h"
#include "rpc_dispatch.h"
#include "pinout.h"

#define NET_CMD_BUFSIZE 512

static const char *TAG = "raw_bridge";

// Allocate some buffers to work with
command_buf_context_t work_buffers;

// do not put it on the stack
wifi_ap_record_t ap_info[DEFAULT_SCAN_LIST_SIZE];

esp_netif_t *my_sta_netif = NULL;

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

static err_t hacked_tx(struct netif *netif, struct pbuf *pbuf)
{
    //pbuf_free(pbuf);
    return ERR_OK;
}


static void got_ip_event_handler(void *esp_netif, esp_event_base_t base, int32_t event_id, void *data)
{
    const ip_event_got_ip_t *event = (const ip_event_got_ip_t *) data;

    ESP_LOGI(TAG, "HET IS WAT!! ip: " IPSTR ", mask: " IPSTR ", gw: " IPSTR,
             IP2STR(&event->ip_info.ip),
             IP2STR(&event->ip_info.netmask),
             IP2STR(&event->ip_info.gw));

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
            break;
        default:
            break;
    }
    struct netif *lw = ((struct esp_netif_obj *)my_sta_netif)->lwip_netif;
    lw->input = hacked_recv;
    lw->linkoutput = hacked_tx;

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

// Initialize Wi-Fi as sta
static void wifi_init()
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    my_sta_netif = esp_netif_create_default_wifi_sta();
    assert(my_sta_netif);

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());

    struct netif *lw = ((struct esp_netif_obj *)my_sta_netif)->lwip_netif;
    lw->input = hacked_recv;

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
}

// Initialize Wi-Fi issue scan command
esp_err_t wifi_scan(ultimate_ap_records_t *ult_records)
{
    uint16_t number = DEFAULT_SCAN_LIST_SIZE;
    uint16_t ap_count = 0;
    memset(ap_info, 0, sizeof(ap_info));

    if (ult_records) {
        ult_records->reserved = 0;
        ult_records->num_records = 0;
    }
    esp_err_t err;
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
            ESP_LOGI(TAG, "SSID: %s, RSSI: %d", ap_info[i].ssid, ap_info[i].rssi);
        }
    }
    return ESP_OK;
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

    //wifi_scan(NULL);

    ESP_LOGI(TAG, "dispatch... switching UART pins!");

    vTaskDelay(100);
    ESP_ERROR_CHECK( uart_set_pin(UART_NUM_0, 0, -1, -1, -1));
    ESP_ERROR_CHECK( uart_set_pin(UART_NUM_1, IO_UART_TXD, IO_UART_RXD, IO_UART_RTS, IO_UART_CTS));
    start_dispatch(work_buffers.receivedQueue);
}
