/*
 * wifi_radio.h
 *
 * The one place that starts and stops the Wi-Fi radio of the WiFi modules of the
 * Ultimate 64 (software/wifi/raw_u64) and the Ultimate II+L (software/wifi/raw_c3), so
 * that "off" means the same on both: esp_wifi_stop(), the RF is off and the module leaves
 * the AP.
 *
 * The logic is in wifi_radio.c, the chip side in wifi_radio_esp.c, and the host test
 * (target/pc/linux/wifiradio) replaces the latter. It also checks that nothing else in
 * the modules calls esp_wifi_start() or esp_wifi_stop().
 */

#ifndef SOFTWARE_WIFI_COMMON_WIFI_RADIO_H_
#define SOFTWARE_WIFI_COMMON_WIFI_RADIO_H_

#include <stdbool.h>

// Both are idempotent: asking for the state the radio is in already does nothing and
// returns 0. Otherwise they return 0 or the esp_err_t of the start or stop, and a failed
// call leaves the state as it was. Called by one task at a time (the boot, then the
// connector).
int radio_start(void);
int radio_stop(void);

// True from a successful start until a successful stop
bool radio_is_on(void);

#endif /* SOFTWARE_WIFI_COMMON_WIFI_RADIO_H_ */
