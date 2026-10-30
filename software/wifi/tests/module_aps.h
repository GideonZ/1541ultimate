/*
 * module_aps.h
 *
 * The air of the AP selection cases: one network, three APs, and a stronger AP of another
 * network (module_strongest_test.cpp, module_pick_test.cpp).
 */

#ifndef MODULE_APS_H_
#define MODULE_APS_H_

#include "module_script.h"

extern "C" {
esp_err_t air_attempt_connect(const char *ssid, const char *pw, uint8_t auth, const uint8_t *bssid, uint8_t channel);
}

/* One network, three APs: the far one on channel 1, the near one on 6, one between on 11,
 * and a stronger AP of another network. */
static int far_ap, near_ap, mid_ap, other_ap;

static void three_aps(void)
{
    air_reset();
    far_ap   = air_add("Home",  0x01,  1, -82, WIFI_AUTH_WPA2_PSK);
    near_ap  = air_add("Home",  0x06,  6, -41, WIFI_AUTH_WPA2_PSK);
    mid_ap   = air_add("Home",  0x0B, 11, -63, WIFI_AUTH_WPA2_PSK);
    other_ap = air_add("Other", 0x03,  3, -30, WIFI_AUTH_WPA2_PSK);
}

#endif /* MODULE_APS_H_ */
