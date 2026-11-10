/*
 * wifi_radio.c
 *
 * See wifi_radio.h. Free of ESP-IDF, so that the host test tests what ships.
 */

#include "wifi_radio.h"
#include "wifi_radio_port.h"

// Written by the task that starts and stops the radio, read by any
static volatile bool radio_on = false;

bool radio_is_on(void)
{
    return radio_on;
}

int radio_start(void)
{
    if (radio_on) {
        return 0;
    }
    int rc = wr_port_wifi_start();
    if (rc == 0) {
        radio_on = true;
    }
    return rc;
}

int radio_stop(void)
{
    if (!radio_on) {
        return 0;
    }
    int rc = wr_port_wifi_stop();
    if (rc == 0) {
        radio_on = false;
    }
    return rc;
}
