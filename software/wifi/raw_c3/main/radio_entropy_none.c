/*
 * radio_entropy_none.c
 *
 * The U2+L module does not serve GET_RANDOM, so it has no entropy source to switch when
 * wifi_radio.c starts or stops the radio: its hooks do nothing here.
 */

#include <stdbool.h>
#include "radio_entropy.h"

int radio_entropy_change_begin(void)
{
    return RE_OK;
}

void radio_entropy_changing(bool starting)
{
}

void radio_entropy_changed(bool radio_on)
{
}

void radio_entropy_change_end(void)
{
}
