#ifndef WIFI_MODEM_H
#define WIFI_MODEM_H

#include "rpc_calls.h"
#include "radio_entropy.h"

extern QueueHandle_t connect_commands;
extern SemaphoreHandle_t connect_semaphore;

typedef struct {
    char ssid[33];   // 32 SSID bytes plus the terminator
    char pw[68];
    uint8_t auth_mode;
    uint8_t bssid[6];  // CMD_WIFI_CONNECT: the AP picked, all zero for any
    uint8_t channel;   // and its channel, 0 when not known
    uint8_t command;
    int16_t list_index;
    command_buf_t *buf;
} ConnectCommand_t;

typedef struct {
    uint8_t event_code;
} ConnectEvent_t;

void setup_modem();
void wifi_note_user_disconnect(void);
esp_err_t wifi_scan(ultimate_ap_records_t *ult_records);
esp_err_t wifi_clear_aps(void);
void enable_hook();
void disable_hook();
uint8_t wifi_get_connection();
void wifi_report_disabled(void);
void wifi_note_identify(void);

#endif
