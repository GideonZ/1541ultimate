/* Scan Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

/*
    This example shows how to use the All Channel Scan or Fast Scan to connect
    to a Wi-Fi network.

    In the Fast Scan mode, the scan will stop as soon as the first network matching
    the SSID is found. In this mode, an application can set threshold for the
    authentication mode and the Signal strength. Networks that do not meet the
    threshold requirements will be ignored.

    In the All Channel Scan mode, the scan will end only after all the channels
    are scanned, and connection will start with the best network. The networks
    can be sorted based on Authentication Mode or Signal Strength. The priority
    for the Authentication mode is:  WPA2 > WPA > WEP > Open
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

#include "rpc_calls.h"
#include "rpc_dispatch.h"

#define GPIO_LED0 4
#define GPIO_LED1 16
#define GPIO_LED2 19
#define GPIO_WIFI_MISO 34 // Input Only
#define GPIO_WIFI_MOSI 35 // Input Only
#define GPIO_WIFI_CLK  32 // I/O, can be set to output
#define GPIO_WIFI_CSn  33 // I/O, but shared with IO0, should be input on ESP32

#define PIN_DEVKIT_LED 2
#define PIN_UART_RXD 3
#define PIN_UART_TXD 1
#define PIN_UART_RTS GPIO_WIFI_CLK
#define PIN_UART_CTS GPIO_WIFI_MISO

#define NET_CMD_BUFSIZE 512

static const char *TAG = "scan";

// Allocate some buffers to work with
command_buf_context_t work_buffers;

// do not put it on the stack
wifi_ap_record_t ap_info[DEFAULT_SCAN_LIST_SIZE];

static esp_netif_t *sta_netif = NULL;

static void got_ip_event(void *esp_netif, esp_event_base_t base, int32_t event_id, void *data)
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
        ev->hdr.thread = 0;
        ev->hdr.sequence = 0;
        ev->ip = event->ip_info.ip.addr;
        ev->netmask = event->ip_info.netmask.addr;
        ev->gw = event->ip_info.gw.addr;
        ev->changed = event->ip_changed;
        reply->size = sizeof(event_pkt_got_ip);
        my_uart_transmit_packet(UART_NUM_1, reply);
    } else {
        ESP_LOGW(TAG, "No buffer send event.");
    }
}

// Initialize Wi-Fi as sta
static void wifi_init()
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    sta_netif = esp_netif_create_default_wifi_sta();
    assert(sta_netif);

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &got_ip_event,
                                                        NULL,
                                                        NULL));

}

// Initialize Wi-Fi issue scan command
esp_err_t wifi_scan(ultimate_ap_records_t *ult_records)
{
    uint16_t number = DEFAULT_SCAN_LIST_SIZE;
    uint16_t ap_count = 0;
    memset(ap_info, 0, sizeof(ap_info));

    ult_records->reserved = 0;
    ult_records->num_records = 0;

    esp_err_t err;
    err = esp_wifi_scan_start(NULL, true);
    if (err != ESP_OK) {
        return err;
    }
    err = esp_wifi_scan_get_ap_records(&number, ap_info);
    if (err != ESP_OK) {
        return err;
    }
    err = esp_wifi_scan_get_ap_num(&ap_count);
    if (err != ESP_OK) {
        return err;
    }

    ESP_LOGI(TAG, "Total APs scanned = %u", ap_count);
    if (ap_count > DEFAULT_SCAN_LIST_SIZE) {
    	ap_count = DEFAULT_SCAN_LIST_SIZE;
    }

    for (int i = 0; i < ap_count; i++) {
        memcpy(ult_records->aps[i].bssid, ap_info[i].bssid, 6);
        memcpy(ult_records->aps[i].ssid, ap_info[i].ssid, 33);
        ult_records->aps[i].channel = ap_info[i].primary;
        ult_records->aps[i].rssi = ap_info[i].rssi;
        ult_records->aps[i].authmode = ap_info[i].authmode;
    }

    ult_records->num_records = (uint8_t)ap_count;
    return ESP_OK;
}


/*
#include "soc/uart_periph.h"
void uart_task(void *a)
{
    int fd = *((int *)a);
    ESP_LOGI("UartTask", "FD = %d", fd);

    uint64_t my_event = 1;

    command_buf_t *b;
    //uint32_t debug_uart = UART_FIFO_AHB_REG(0);
    while(1) {
        my_uart_receive_packet(UART_NUM_1, &b, portMAX_DELAY);
        int wr = write(fd, &my_event, sizeof(uint64_t)); // :-)
        printf("UartEvent: Core %d, BUF #%d, SZ:%d, DR:%d, WR:%d\n", xPortGetCoreID(), b->bufnr, b->size, b->dropped, wr);
        dump_hex_relative(b->data, b->size);
        parse_command(b);
    }
}


void select_task(void *a)
{
    int fd = *((int *)a);
    ESP_LOGI("SelectTask", "FD = %d", fd);
    ESP_LOGI("SelectTask", "Free Heap: %d Bytes", esp_get_free_heap_size());

    fd_set rfds;
    struct timeval tv;  //for select()
    int retval;
    uint64_t the_event;

    //Wait up to 10 seconds
    tv.tv_sec = 10;
    tv.tv_usec = 0;

    FD_ZERO(&rfds);

    while(1) {
        FD_SET(fd, &rfds);
        retval = select(FD_SETSIZE, &rfds, NULL, NULL, &tv);
        ESP_LOGI("SelectTask", "Select returned with %d. %d", retval, (int)FD_ISSET(fd, &rfds));
        if (FD_ISSET(fd, &rfds)) {
            int a = read(fd, &the_event, sizeof(uint64_t));
            ESP_LOGI("SelectTask", "Reading from eventfd resulted in: %d. Got %d", a, (int)the_event);
        }
    }
}
*/

/*
    esp_vfs_eventfd_config_t eventfd_config = { .max_fds = 2 };

    ESP_ERROR_CHECK(esp_vfs_eventfd_register(&eventfd_config));
    int fd = eventfd(0, 0);
    ESP_LOGI(TAG, "Fd = %d", fd);
*/

void app_main()
{
	// Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK( ret );

    gpio_pad_select_gpio(GPIO_LED0);
    gpio_pad_select_gpio(GPIO_LED1);
    gpio_pad_select_gpio(GPIO_LED2);
    gpio_set_direction(GPIO_LED0, GPIO_MODE_OUTPUT);
    gpio_set_direction(GPIO_LED1, GPIO_MODE_OUTPUT);
    gpio_set_direction(GPIO_LED2, GPIO_MODE_OUTPUT);

    //ESP_ERROR_CHECK( uart_set_pin(UART_NUM_0, GPIO_LED0, GPIO_LED1, -1, -1));
    //ESP_ERROR_CHECK( uart_set_pin(UART_NUM_1, PIN_UART_TXD, PIN_UART_RXD, PIN_UART_RTS, PIN_UART_CTS));

    // Just configure UART1 to RECEIVE through the same pin as UART0... bravo ;)
    ESP_ERROR_CHECK( uart_set_pin(UART_NUM_1, PIN_DEVKIT_LED, PIN_UART_RXD, -1, -1));
    // ESP_ERROR_CHECK( uart_set_pin(UART_NUM_1, PIN_UART_TXD, PIN_UART_RXD, -1, -1));

    // Initialize the single copy transport buffers, and attach dispatchers to each buffer
    cmd_buffer_init(&work_buffers);

    // Initialize Dispatchers and attach them to the buffers
    //create_dispatchers(2048, &work_buffers);

    // Now install the driver for UART1.  We use our own buffer mechanism and interrupt routine
    ESP_ERROR_CHECK(my_uart_init(&work_buffers, UART_NUM_1));

    wifi_init();

    dispatch(work_buffers.receivedQueue);
}
