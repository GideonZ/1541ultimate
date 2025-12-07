/*
 * u2pl_init.cc
 *
 *  Created on: Dec 7, 2025
 *      Author: gideon
 */

#include <stdio.h>

#include "u2p.h"
#include "i2c_drv.h"
#include "mdio.h"
#include "usb_nano.h"

void initialize_usb_hub();

extern "C" {
void codec_init();
void custom_hardware_init()
{
	puts("** U2+L RiscV System Init **");

    if (!i2c) {
        i2c = new I2C_Driver();
    }
    codec_init();

    NANO_START = 0;
    U2PIO_ULPI_RESET = 1;
    uint16_t *dst = (uint16_t *)NANO_BASE;
    for (int i = 0; i < 2048; i += 2) {
        *(dst++) = 0;
    }

#ifndef RECOVERYAPP
    // Initialize USB hub
    initialize_usb_hub();
#endif
    U2PIO_ULPI_RESET = 0;

    // enable buffer
    U2PIO_ULPI_RESET = U2PIO_UR_BUFFER_ENABLE;
}
}