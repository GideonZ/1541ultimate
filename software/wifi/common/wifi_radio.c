/*
 * wifi_radio.c
 *
 * See wifi_radio.h. Free of ESP-IDF, so that the host test tests what ships.
 */

#include "wifi_radio.h"
#include "wifi_radio_port.h"

// Written by the task that starts and stops the radio, read by any
static volatile bool radio_on = false;
static volatile uint32_t epoch = 0;

bool radio_is_on(void)
{
    return radio_on;
}

uint32_t radio_epoch(void)
{
    return epoch;
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
    // A GET_RANDOM draw that overlaps the stop must not count as drawn with the radio on
    epoch++;
    radio_on = false;
    int rc = wr_port_wifi_stop();
    if (rc != 0) {
        radio_on = true;
    }
    return rc;
}
