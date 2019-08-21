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
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"
#include "esp_wifi.h"
#include "esp_log.h"
#include "esp_event_loop.h"
#include "nvs_flash.h"

/*Set the SSID and Password via "make menuconfig"*/
#define DEFAULT_SSID CONFIG_WIFI_SSID
#define DEFAULT_PWD CONFIG_WIFI_PASSWORD

#if CONFIG_WIFI_ALL_CHANNEL_SCAN
#define DEFAULT_SCAN_METHOD WIFI_ALL_CHANNEL_SCAN
#elif CONFIG_WIFI_FAST_SCAN
#define DEFAULT_SCAN_METHOD WIFI_FAST_SCAN
#else
#define DEFAULT_SCAN_METHOD WIFI_FAST_SCAN
#endif /*CONFIG_SCAN_METHOD*/

#if CONFIG_WIFI_CONNECT_AP_BY_SIGNAL
#define DEFAULT_SORT_METHOD WIFI_CONNECT_AP_BY_SIGNAL
#elif CONFIG_WIFI_CONNECT_AP_BY_SECURITY
#define DEFAULT_SORT_METHOD WIFI_CONNECT_AP_BY_SECURITY
#else
#define DEFAULT_SORT_METHOD WIFI_CONNECT_AP_BY_SIGNAL
#endif /*CONFIG_SORT_METHOD*/

#if CONFIG_FAST_SCAN_THRESHOLD
#define DEFAULT_RSSI CONFIG_FAST_SCAN_MINIMUM_SIGNAL
#if CONFIG_EXAMPLE_OPEN
#define DEFAULT_AUTHMODE WIFI_AUTH_OPEN
#elif CONFIG_EXAMPLE_WEP
#define DEFAULT_AUTHMODE WIFI_AUTH_WEP
#elif CONFIG_EXAMPLE_WPA
#define DEFAULT_AUTHMODE WIFI_AUTH_WPA_PSK
#elif CONFIG_EXAMPLE_WPA2
#define DEFAULT_AUTHMODE WIFI_AUTH_WPA2_PSK
#else
#define DEFAULT_AUTHMODE WIFI_AUTH_OPEN
#endif
#else
#define DEFAULT_RSSI -127
#define DEFAULT_AUTHMODE WIFI_AUTH_OPEN
#endif /*CONFIG_FAST_SCAN_THRESHOLD*/

static const char *TAG = "scan";
static SemaphoreHandle_t mySemaphore;

static esp_err_t event_handler(void *ctx, system_event_t *event)
{
    switch (event->event_id) {
        case SYSTEM_EVENT_STA_START:
            ESP_LOGI(TAG, "SYSTEM_EVENT_STA_START");
            //ESP_ERROR_CHECK(esp_wifi_connect());
            break;
        case SYSTEM_EVENT_STA_GOT_IP:
            ESP_LOGI(TAG, "SYSTEM_EVENT_STA_GOT_IP");
            ESP_LOGI(TAG, "Got IP: %s\n",
                     ip4addr_ntoa(&event->event_info.got_ip.ip_info.ip));
            break;
        case SYSTEM_EVENT_STA_DISCONNECTED:
            ESP_LOGI(TAG, "SYSTEM_EVENT_STA_DISCONNECTED");
            ESP_ERROR_CHECK(esp_wifi_connect());
            break;
        case SYSTEM_EVENT_SCAN_DONE:
            if (mySemaphore) {
                xSemaphoreGive(mySemaphore);
            }
            break;
        default:
            ESP_LOGI(TAG, "Other event: %d", event->event_id);
            break;
    }
    return ESP_OK;
}

static void my_scan(void)
{
    mySemaphore = xSemaphoreCreateBinary();
    uint16_t number = 24;
    static wifi_ap_record_t ap_records[24];

    tcpip_adapter_init();
    ESP_ERROR_CHECK(esp_event_loop_init(event_handler, NULL));

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    wifi_scan_config_t scan_config = {0};
    scan_config.show_hidden = true;
    //scan_config.scan_time.passive = 1000;
    scan_config.scan_time.active.min = 150;
    scan_config.scan_time.active.max = 500;
    scan_config.scan_type = WIFI_SCAN_TYPE_ACTIVE;

    esp_wifi_stop();
    ESP_ERROR_CHECK( esp_wifi_set_mode (WIFI_MODE_STA) );
    ESP_ERROR_CHECK( esp_wifi_start() );

    uint32_t total = 0;
    uint32_t scans = 0;
    while(1) {
        ESP_LOGI(TAG, "Start Scan");
        ESP_ERROR_CHECK(esp_wifi_scan_start(&scan_config, false));
        if (xSemaphoreTake(mySemaphore, 2000)) {
            number = 24;
            ESP_ERROR_CHECK(esp_wifi_scan_get_ap_records(&number, ap_records));
            total += number;
            scans ++;
            printf("Scan complete. %d APs found. (Average: %u/100 APs. %u scans)\n", number, 100 * total / scans, scans);
            for (int i=0; i<number; i++) {
                printf("%-32s Channel: %2d Signal: %4d Mode: %d\n", ap_records[i].ssid, ap_records[i].primary, ap_records[i].rssi, ap_records[i].authmode);
            }
        } else {
            ESP_LOGI(TAG, "Timeout...");
        }
    }
    //esp_wifi_stop();
}

/*
 Initialize Wi-Fi as sta and set scan method
static void wifi_scan(void)
{
    tcpip_adapter_init();
    ESP_ERROR_CHECK(esp_event_loop_init(event_handler, NULL));

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = "blah",
            .password = "blah",
            .scan_method = WIFI_ALL_CHANNEL_SCAN,
            .sort_method = DEFAULT_SORT_METHOD,
            .threshold.rssi = DEFAULT_RSSI,
            .threshold.authmode = WIFI_AUTH_OPEN,
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    wifi_scan_config_t scan = {
        .ssid = NULL,  *< SSID of AP
        .bssid = NULL, *< MAC address of AP
        .channel = 0,  *< channel, scan the specific channel
        .show_hidden = true,    *< enable to scan AP whose SSID is hidden
        .scan_type = WIFI_SCAN_TYPE_PASSIVE,  *< scan type, active or passive
        .scan_time = { .passive = 1000 }, //.active = { .min = 150, .max = 250 }},  //  scan time per channel
    };
}
*/

void app_main()
{
    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK( ret );

    my_scan();

    printf("That's all folks!\n");
    // Wait forever
    vTaskDelay(portMAX_DELAY);
}
