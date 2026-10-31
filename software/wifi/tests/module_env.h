/*
 * module_env.h
 *
 * The world the WiFi modules' code under test (the functions Makefile cuts out of
 * wifi_modem.c and rpc_dispatch.c of raw_u64, raw_c3 and u64ctrl) sees on the build host:
 * the module's own rpc_calls.h and wifi_modem.h, and declarations of the ESP-IDF, FreeRTOS
 * and UART calls that module_stubs.c provides. Included by that code (C) and by the cases
 * (C++).
 *
 * The station config below copies the field names and enum values of ESP-IDF 5.3.1
 * (components/esp_wifi/include/esp_wifi_types_generic.h): the values matter, because the
 * module clears the config with memset first, and 0 is WIFI_FAST_SCAN and
 * WIFI_CONNECT_AP_BY_SIGNAL there.
 */

#ifndef MODULE_ENV_H_
#define MODULE_ENV_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"

typedef int esp_err_t;
#define ESP_OK                0
#define ESP_FAIL              (-1)
#define ESP_ERR_NO_MEM        0x101
#define ESP_ERR_INVALID_ARG   0x102
#define ESP_ERR_INVALID_STATE 0x103
#define ESP_ERR_NOT_FOUND     0x105
#define ESP_ERR_NOT_SUPPORTED 0x106
typedef const char *esp_event_base_t;

#ifdef __cplusplus
extern "C" {
#endif
#include "cmd_buffer.h"
#include "rpc_calls.h"
#include "wifi_modem.h"
#ifdef __cplusplus
}
#endif
#define UART_NUM_1 1
#define UART_CHAN  UART_NUM_1
#define IDENT_MAJOR  1
#define IDENT_MINOR  2
#define IDENT_STRING "module test"

#define ESP_LOGI(tag, ...) ((void)0)
#define ESP_LOGW(tag, ...) ((void)0)
#define ESP_LOGE(tag, ...) ((void)0)

#define WIFI_RETRY_MIN_MS  5000
#define WIFI_RETRY_MAX_MS 60000

typedef enum { WIFI_FAST_SCAN = 0, WIFI_ALL_CHANNEL_SCAN } wifi_scan_method_t;
typedef enum { WIFI_CONNECT_AP_BY_SIGNAL = 0, WIFI_CONNECT_AP_BY_SECURITY } wifi_sort_method_t;
typedef enum {
    WIFI_AUTH_OPEN = 0, WIFI_AUTH_WEP, WIFI_AUTH_WPA_PSK, WIFI_AUTH_WPA2_PSK,
    WIFI_AUTH_WPA_WPA2_PSK, WIFI_AUTH_ENTERPRISE, WIFI_AUTH_WPA3_PSK, WIFI_AUTH_WPA2_WPA3_PSK,
} wifi_auth_mode_t;
typedef struct { int8_t rssi; wifi_auth_mode_t authmode; } wifi_scan_threshold_t;
typedef struct { bool capable; bool required; } wifi_pmf_config_t;
typedef struct {
    uint8_t ssid[32];
    uint8_t password[64];
    wifi_scan_method_t scan_method;
    bool bssid_set;
    uint8_t bssid[6];
    uint8_t channel;
    uint16_t listen_interval;
    wifi_sort_method_t sort_method;
    wifi_scan_threshold_t threshold;
    wifi_pmf_config_t pmf_cfg;
    uint8_t failure_retry_cnt;
} wifi_sta_config_t;
typedef union { wifi_sta_config_t sta; } wifi_config_t;
typedef enum { WIFI_IF_STA = 0 } wifi_interface_t;
typedef struct {
    uint8_t bssid[6];
    uint8_t ssid[33];
    uint8_t primary;
    int8_t rssi;
    wifi_auth_mode_t authmode;
} wifi_ap_record_t;
typedef struct {
    uint8_t ssid[33];
    uint8_t ssid_len;
    uint8_t bssid[6];
    uint8_t reason;
    int8_t rssi;
} wifi_event_sta_disconnected_t;
enum { WIFI_EVENT_STA_START, WIFI_EVENT_STA_CONNECTED, WIFI_EVENT_STA_DISCONNECTED };

#if !defined(MODULE_U64CTRL) && __has_include("radio_boot.h")
#include "radio_boot.h" // the U64 and U2+L modules' restart record
#define MODULE_HAS_SWITCH 1
#endif

#ifdef __cplusplus
extern "C" {
#endif

// What the modules define themselves (the extracted code uses them; module_stubs.c has them)
extern const char *TAG;
extern volatile bool user_disconnected;
extern volatile bool off_asked;
extern uint16_t connected_g, disabled_g, ap_count;
extern ConnectCommand_t last_connect;
extern wifi_ap_record_t ap_info[DEFAULT_SCAN_LIST_SIZE];
extern QueueHandle_t connect_events, connect_commands;
extern SemaphoreHandle_t connect_semaphore;
extern int wake_machine_on; // u64ctrl
void wake_on_wifi_update(int machine_on);
void wifi_note_identify(void);
#ifdef MODULE_HAS_SWITCH
extern radio_record_t radio_record;
extern radio_boot_t boot_mode;
#endif

// ESP-IDF, the radio and the UART
esp_err_t esp_wifi_start(void);
esp_err_t esp_wifi_stop(void);
esp_err_t esp_wifi_disconnect(void);
esp_err_t esp_wifi_connect(void);
esp_err_t esp_wifi_set_config(wifi_interface_t interface, wifi_config_t *conf);
int radio_start(void);
int radio_stop(void);
int my_uart_get_buffer(int uart, command_buf_t **buf, int ticks);
BaseType_t my_uart_transmit_packet(uint8_t uart_num, command_buf_t *buf);

// The rest of wifi_modem.c that the connector calls but this test does not cut out
esp_err_t wifi_connect_to_last(void);
esp_err_t wifi_connect_to_scanned(int index);
esp_err_t wifi_connect_to_stored(int index);
esp_err_t wifi_connect_to_hotspot(void);
esp_err_t wifi_store_ap(ConnectCommand_t *cmd);
esp_err_t wifi_set_last_ap(int index);
esp_err_t wifi_find_password(const uint8_t *ssid, char *pw, size_t maxlen, int16_t *list_idx);
void wifi_list_aps(void);
bool recovery_running(void);
uint8_t wifi_get_connection(void);
#ifdef __cplusplus
}
#endif

#endif /* MODULE_ENV_H_ */
