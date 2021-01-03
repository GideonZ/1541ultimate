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

#define GPIO_LED0 4
#define GPIO_LED1 16
#define GPIO_LED2 19

#define DEFAULT_SCAN_LIST_SIZE 24

static const char *TAG = "scan";

#define sendchar(x) {   putchar(x); }

void send_slip_data(const uint8_t *buffer, int length)
{
    // Escaped Body
    for (int i = 0; i < length; i++) {
        uint8_t data = *(buffer++);
        if (data == 0xC0) {
            sendchar(0xDB);
            sendchar(0xDC);
        } else if (data == 0xDB) {
            sendchar(0xDB);
            sendchar(0xDD);
        } else {
            sendchar(data);
        }
    }
}

void send_slip_packet(const void *buffer, int length)
{
    // Start Character
    sendchar(0xC0);
    // Body
    send_slip_data((uint8_t*)buffer, length);
    // End Character
    sendchar(0xC0);
}

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

ultimate_ap_records_t ult_records;

// do not put it on the stack
wifi_ap_record_t ap_info[DEFAULT_SCAN_LIST_SIZE];

// Initialize Wi-Fi as sta and set scan method
static void wifi_scan(void)
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
    ESP_LOGI(TAG, "Total APs scanned = %u", ap_count);
    if (ap_count > DEFAULT_SCAN_LIST_SIZE) {
    	ap_count = DEFAULT_SCAN_LIST_SIZE;
    }
    for (int i = 0; i < ap_count; i++) {
		printf("%-32s Channel: %2d Signal: %4d Mode: %d\n", ap_info[i].ssid, ap_info[i].primary, ap_info[i].rssi, ap_info[i].authmode);


        memcpy(ult_records.aps[i].bssid, ap_info[i].bssid, 6);
        memcpy(ult_records.aps[i].ssid, ap_info[i].ssid, 33);
        ult_records.aps[i].channel = ap_info[i].primary;
        ult_records.aps[i].rssi = ap_info[i].rssi;
        ult_records.aps[i].authmode = ap_info[i].authmode;
    }

    ult_records.packet_type = 0x01;
    ult_records.num_records = (uint8_t)ap_count;

    send_spi_packet(&ult_records, sizeof(ultimate_ap_records_t));
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

	spi_init();
	ESP_LOGI(TAG, "SPI Initialization done.");

    gpio_pad_select_gpio(GPIO_LED0);
    gpio_pad_select_gpio(GPIO_LED1);
    gpio_pad_select_gpio(GPIO_LED2);
    gpio_set_direction(GPIO_LED0, GPIO_MODE_OUTPUT);
    gpio_set_direction(GPIO_LED1, GPIO_MODE_OUTPUT);
    gpio_set_direction(GPIO_LED2, GPIO_MODE_OUTPUT);

    wifi_scan();

    printf("That's all folks!\n");
    // Wait forever
    vTaskDelay(portMAX_DELAY);
}
