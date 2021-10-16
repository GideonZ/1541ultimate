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
#define GPIO_WIFI_MISO 34 // Input Only
#define GPIO_WIFI_MOSI 35 // Input Only
#define GPIO_WIFI_CLK  32 // I/O, can be set to output
#define GPIO_WIFI_CSn  33 // I/O, but shared with IO0, should be input on ESP32

#define PIN_UART_RXD 3
#define PIN_UART_TXD 1
#define PIN_UART_RTS GPIO_WIFI_CLK
#define PIN_UART_CTS GPIO_WIFI_MISO

const uart_config_t uart_config = {
    .baud_rate = 115200,
    .data_bits = UART_DATA_8_BITS,
    .parity = UART_PARITY_DISABLE,
    .stop_bits = UART_STOP_BITS_1,
    .flow_ctrl = UART_HW_FLOWCTRL_DISABLE, // UART_HW_FLOWCTRL_CTS_RTS,
    .rx_flow_ctrl_thresh = 122,
};

const int uart_buffer_size = (1024 * 2);
QueueHandle_t uart_queue;

#define DEFAULT_SCAN_LIST_SIZE 24

static const char *TAG = "scan";

// #define sendchar(x) {  putchar(x); }

static uint8_t txbuf[128];
static int txcnt=0;

__inline static void tx_purge()
{
    if(txcnt) {
        uart_write_bytes(UART_NUM_1, txbuf, txcnt);
        txcnt = 0;
    }
}

__inline static void sendchar(uint8_t x)
{
    txbuf[txcnt++] = x;
    if (txcnt >= 128) {
        tx_purge();
    }
}

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

    tx_purge();
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
    // ESP_LOGI(TAG, "Total APs scanned = %u", ap_count);
    if (ap_count > DEFAULT_SCAN_LIST_SIZE) {
    	ap_count = DEFAULT_SCAN_LIST_SIZE;
    }
    for (int i = 0; i < ap_count; i++) {
//		printf("%-32s Channel: %2d Signal: %4d Mode: %d\n", ap_info[i].ssid, ap_info[i].primary, ap_info[i].rssi, ap_info[i].authmode);
        memcpy(ult_records.aps[i].bssid, ap_info[i].bssid, 6);
        memcpy(ult_records.aps[i].ssid, ap_info[i].ssid, 33);
        ult_records.aps[i].channel = ap_info[i].primary;
        ult_records.aps[i].rssi = ap_info[i].rssi;
        ult_records.aps[i].authmode = ap_info[i].authmode;
    }

    ult_records.packet_type = 0x01;
    ult_records.num_records = (uint8_t)ap_count;

    send_slip_packet(&ult_records, sizeof(ultimate_ap_records_t));
}

void uart_task(void *a)
{
    uart_event_t event;
    while(1) {
        xQueueReceive(uart_queue, &event, portMAX_DELAY);
        printf("UartEvent: T:%d, SZ:%d, F:%d\n", event.type, event.size, event.timeout_flag);
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

    ESP_ERROR_CHECK( uart_set_pin(UART_NUM_0, GPIO_LED0, GPIO_LED1, -1, -1));
    ESP_ERROR_CHECK( uart_set_pin(UART_NUM_1, PIN_UART_TXD, PIN_UART_RXD, PIN_UART_RTS, PIN_UART_CTS));

    // Configure UART parameters
    ESP_ERROR_CHECK(uart_param_config(UART_NUM_1, &uart_config));

    // Now install the driver for UART1
    // Setup UART buffered IO with event queue
    ESP_ERROR_CHECK(uart_driver_install(UART_NUM_1, uart_buffer_size, 0, 10, &uart_queue, 0));

    wifi_scan();

    xTaskCreate( uart_task, "UART1 Task", configMINIMAL_STACK_SIZE, 0, tskIDLE_PRIORITY + 1, NULL );

    printf("That's all folks!\n");
    // Wait forever
    vTaskDelay(portMAX_DELAY);
}
