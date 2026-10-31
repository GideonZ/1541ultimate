/*
 * wifi_radio_esp.c
 *
 * The chip side of wifi_radio.c, for the ESP32 (software/wifi/raw_u64) and the ESP32-C3
 * (software/wifi/raw_c3). This file holds the only calls of esp_wifi_start() and
 * esp_wifi_stop() of those modules.
 */

#include "esp_wifi.h"
#include "wifi_radio_port.h"

int wr_port_wifi_start(void)
{
    return esp_wifi_start();
}

int wr_port_wifi_stop(void)
{
    return esp_wifi_stop();
}
