#ifndef WIFI_MODEM_H
#define WIFI_MODEM_H

#include "rpc_calls.h"

void wifi_check_connection();
esp_err_t wifi_scan(ultimate_ap_records_t *ult_records);
esp_err_t wifi_store_ap(const char *ssid, const char *password, uint8_t auth_mode);
void enable_hook();
void disable_hook();
uint8_t wifi_get_connection();

#endif
