/*
 * wifi_radio.h
 *
 * The one place that starts and stops the Wi-Fi radio of the WiFi modules of the
 * Ultimate 64 (software/wifi/raw_u64) and the Ultimate II+L (software/wifi/raw_c3), so
 * that "off" means the same on both: esp_wifi_stop(), the RF is off and the module leaves
 * the AP.
 *
 * The logic is in wifi_radio.c, the chip side in wifi_radio_esp.c, and the host test
 * (software/wifi/tests/radio_test.cpp) replaces the latter.
 */

#ifndef SOFTWARE_WIFI_COMMON_WIFI_RADIO_H_
#define SOFTWARE_WIFI_COMMON_WIFI_RADIO_H_

#include <stdbool.h>
#include <stdint.h>

// Both are idempotent: asking for the state the radio is in already does nothing and
// returns 0. Otherwise they return 0 or the esp_err_t of the start or stop, and a failed
// call leaves the state as it was. Called by one task at a time (the boot, then the
// connector). They also return the radio_entropy.h gate's RE_ERR_BUSY or
// RE_ERR_RECURSION, and change nothing then.
int radio_start(void);
int radio_stop(void);

// True from a successful start until a stop begins: while a stop runs the radio no
// longer counts as on, and a stop that fails makes it count again
bool radio_is_on(void);

// The number of stops begun so far, so that a caller can tell the radio went off under it
uint32_t radio_epoch(void);

#endif /* SOFTWARE_WIFI_COMMON_WIFI_RADIO_H_ */
