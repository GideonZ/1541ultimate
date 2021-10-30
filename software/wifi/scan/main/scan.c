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

#define DEFAULT_SCAN_LIST_SIZE 24

static const char *TAG = "scan";

/* 42 byte AP record */

typedef struct {
    uint8_t bssid[6];            // MAC address of AP
    uint8_t ssid[33];            // SSID of AP
    uint8_t channel;             // channel of AP
    int8_t  rssi;                // signal strength of AP
    uint8_t authmode;            // authmode of AP
} ultimate_ap_record_t;

typedef struct {
    uint8_t packet_type;
    uint8_t num_records;
    ultimate_ap_record_t aps[DEFAULT_SCAN_LIST_SIZE];
} ultimate_ap_records_t;

// Allocate some buffers to work with
command_buf_context_t work_buffers;

// do not put it on the stack
wifi_ap_record_t ap_info[DEFAULT_SCAN_LIST_SIZE];

// Initialize Wi-Fi as sta and set scan method
static void wifi_scan(command_buf_t *reply)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_t *sta_netif = esp_netif_create_default_wifi_sta();
    assert(sta_netif);

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    uint16_t number = DEFAULT_SCAN_LIST_SIZE;
    uint16_t ap_count = 0;
    memset(ap_info, 0, sizeof(ap_info));

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_ERROR_CHECK(esp_wifi_scan_start(NULL, true));
    ESP_ERROR_CHECK(esp_wifi_scan_get_ap_records(&number, ap_info));
    ESP_ERROR_CHECK(esp_wifi_scan_get_ap_num(&ap_count));
    // ESP_LOGI(TAG, "Total APs scanned = %u", ap_count);
    if (ap_count > DEFAULT_SCAN_LIST_SIZE) {
    	ap_count = DEFAULT_SCAN_LIST_SIZE;
    }

    ESP_LOGI(TAG, "reply = %p", reply);
    ESP_LOGI(TAG, "reply->data = %p", reply->data);
    ultimate_ap_records_t *ult_records = (ultimate_ap_records_t *)reply->data;
    ESP_LOGI(TAG, "ult_records = %p", ult_records);

    for (int i = 0; i < ap_count; i++) {
//		printf("%-32s Channel: %2d Signal: %4d Mode: %d\n", ap_info[i].ssid, ap_info[i].primary, ap_info[i].rssi, ap_info[i].authmode);
        memcpy(ult_records->aps[i].bssid, ap_info[i].bssid, 6);
        memcpy(ult_records->aps[i].ssid, ap_info[i].ssid, 33);
        ult_records->aps[i].channel = ap_info[i].primary;
        ult_records->aps[i].rssi = ap_info[i].rssi;
        ult_records->aps[i].authmode = ap_info[i].authmode;
    }

    ult_records->packet_type = 0x01;
    ult_records->num_records = (uint8_t)ap_count;
    reply->size = 2 + (ap_count * sizeof(ultimate_ap_record_t));

    my_uart_transmit_packet(UART_NUM_1, reply);
}

#include "soc/uart_periph.h"

void uart_task(void *a)
{
    command_buf_t *b;
    uint32_t debug_uart = UART_FIFO_AHB_REG(0);
    while(1) {
        my_uart_receive_packet(UART_NUM_1, &b, portMAX_DELAY);
        // printf("UartEvent: Core %d, BUF #%d, SZ:%d, DR:%d\n", xPortGetCoreID(), b->bufnr, b->size, b->dropped);
        // dump_hex_relative(b->data, b->size);
        WRITE_PERI_REG(debug_uart, ':');
        my_uart_free_buffer(UART_NUM_1, b);
        //vTaskDelay(1);
    }
}

void app_main()
{
	ESP_LOGI(TAG, "App is Alive!");

	// Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK( ret );
	ESP_LOGI(TAG, "NVS Initialization done.");

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

    // Now install the driver for UART1.  We use our own buffer mechanism and interrupt routine
    ESP_ERROR_CHECK(my_uart_init(&work_buffers, UART_NUM_1));

/*
    command_buf_t *reply;
    if (my_uart_get_buffer(UART_NUM_1, &reply, 100)) {
        wifi_scan(reply);
    } else {
        ESP_LOGW(TAG, "No buffer to do scan.");
    }
*/

    xTaskCreate( uart_task, "UART1 Task", configMINIMAL_STACK_SIZE, 0, tskIDLE_PRIORITY + 1, NULL );

    ESP_LOGI(TAG, "That's all folks!");

    // Wait forever
    vTaskDelay(portMAX_DELAY);
}
