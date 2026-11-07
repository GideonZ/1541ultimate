/*
 * wifi_radio_port.h
 *
 * What wifi_radio.c needs from the chip. wifi_radio_esp.c implements it for the modules;
 * the host test implements it with recording stubs.
 */

#ifndef SOFTWARE_WIFI_COMMON_WIFI_RADIO_PORT_H_
#define SOFTWARE_WIFI_COMMON_WIFI_RADIO_PORT_H_

int wr_port_wifi_start(void); // the Wi-Fi start (esp_wifi_start): 0 or an esp_err_t
int wr_port_wifi_stop(void);  // the Wi-Fi stop (esp_wifi_stop)

#endif /* SOFTWARE_WIFI_COMMON_WIFI_RADIO_PORT_H_ */
