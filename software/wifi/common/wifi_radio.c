/*
 * wifi_radio.c
 *
 * See wifi_radio.h. Free of ESP-IDF, so that the host test tests what ships.
 */

#include "wifi_radio.h"
#include "wifi_radio_port.h"
#include "radio_entropy.h"

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

// Every change runs under the exclusive hold of the entropy gate, which also switches the
// RNG's source between the radio and the SAR ADC (radio_entropy.h)
int radio_start(void)
{
    int rc = radio_entropy_change_begin();
    if (rc != 0) {
        return rc;
    }
    if (!radio_on) {
        radio_entropy_changing(true);
        rc = wr_port_wifi_start();
        if (rc == 0) {
            radio_on = true;
        }
        radio_entropy_changed(radio_on);
    }
    radio_entropy_change_end();
    return rc;
}

int radio_stop(void)
{
    int rc = radio_entropy_change_begin();
    if (rc != 0) {
        return rc;
    }
    if (radio_on) {
        // A GET_RANDOM draw that overlaps the stop must not count as drawn with the radio on
        epoch++;
        radio_on = false;
        radio_entropy_changing(false);
        rc = wr_port_wifi_stop();
        if (rc != 0) {
            radio_on = true;
        }
        radio_entropy_changed(radio_on);
    }
    radio_entropy_change_end();
    return rc;
}
