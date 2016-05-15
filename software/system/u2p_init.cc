/*
 * u2p_init.cc
 *
 *  Created on: May 8, 2016
 *      Author: gideon
 */

#include "u2p_init.h"
#include <stdio.h>

#include "system.h"
#include "altera_avalon_pio_regs.h"
#define FLASH_SEL_0     IOWR_ALTERA_AVALON_PIO_CLEAR_BITS(PIO_0_BASE, 0x4)
#define FLASH_SEL_1     IOWR_ALTERA_AVALON_PIO_SET_BITS(PIO_0_BASE, 0x4)
#define FLASH_SELCK_0   IOWR_ALTERA_AVALON_PIO_CLEAR_BITS(PIO_0_BASE, 0x8)
#define FLASH_SELCK_1   IOWR_ALTERA_AVALON_PIO_SET_BITS(PIO_0_BASE, 0x8)

U2P_Init hardwareInitializer;

U2P_Init :: U2P_Init()
{
	puts("** U2+ NiosII System **");
	FLASH_SEL_1;
    FLASH_SELCK_0;
    FLASH_SELCK_1;
}
