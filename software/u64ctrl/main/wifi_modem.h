#ifndef WIFI_MODEM_H
#define WIFI_MODEM_H

#include "rpc_calls.h"

extern QueueHandle_t connect_commands;
extern SemaphoreHandle_t connect_semaphore;

typedef struct {
    char ssid[32];
    char pw[68];
    uint8_t auth_mode;
    uint8_t command;
    int16_t list_index;
    command_buf_t *buf;
} ConnectCommand_t;

typedef struct {
    uint8_t event_code;
} ConnectEvent_t;

esp_err_t wifi_scan(ultimate_ap_records_t *ult_records);
esp_err_t wifi_clear_aps(void);
void enable_hook();
void disable_hook();
uint8_t wifi_get_connection();
void wifi_send_connected_event();

#endif
