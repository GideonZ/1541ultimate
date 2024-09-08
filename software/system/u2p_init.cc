/*
 * u2p_init.cc
 *
 *  Created on: May 8, 2016
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
	puts("** U2+ / U64 NiosII System Init **");

    // Select Flash bank 1
	REMOTE_FLASHSEL_1;
    REMOTE_FLASHSELCK_0;
    REMOTE_FLASHSELCK_1;

    mdio_write(0x1B, 0x0500); // enable link up, link down interrupts
    mdio_write(0x16, 0x0002); // disable factory reset mode

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

    // Initialize USB hub
    initialize_usb_hub();

    U2PIO_ULPI_RESET = 0;

    // enable buffer
    U2PIO_ULPI_RESET = U2PIO_UR_BUFFER_ENABLE;
}
}