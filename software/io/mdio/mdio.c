/*
 * mdio.c
 *
 *  Created on: May 5, 2016
 *      Author: gideon
 */

#include <stdint.h>

#include "system.h"
#include "u2p.h"

#define SET_MDC_LOW   U2PIO_SET_MDC = 0
#define SET_MDC_HIGH  U2PIO_SET_MDC = 1
#define SET_MDIO_LOW  U2PIO_SET_MDIO = 0
#define SET_MDIO_HIGH U2PIO_SET_MDIO = 1
#define GET_MDIO      U2PIO_GET_MDIO
#define GET_IRQ       1 //U2PIO_GET_ETHIRQ

// Read:  32x'1', "01", "10", "000AA", "RRRRR", "TT", 16xData, Z
// Write: 32x'1', "01", "01", "000AA", "RRRRR", "10", 16xData, Z (1)

static void _wait() {
    for(int i=0;i<2;i++)
        ;
}

static void mdio_bit(int val)
{
    if (val) {
        SET_MDIO_HIGH;
    } else {
        SET_MDIO_LOW;
    }
    SET_MDC_HIGH;
    SET_MDC_LOW;
}

void mdio_reset()
{
/*
    ASSERT_ETH_RESET;
    _wait;
    DEASSERT_ETH_RESET;
*/
    SET_MDC_LOW;
}

void mdio_write(uint8_t reg, uint16_t data)
{
    for (int i=0; i<33; i++) {
        mdio_bit(1);
    }
    mdio_bit(0);
    mdio_bit(1);

    mdio_bit(0);
    mdio_bit(1);

    mdio_bit(0);
    mdio_bit(0);
    mdio_bit(0);
    mdio_bit(0);
    mdio_bit(0);

    for (int i=0; i<5; i++) {
        mdio_bit(reg & 0x10);
        reg <<= 1;
    }

    mdio_bit(1);
    mdio_bit(0);

    for (int i=0; i<16; i++) {
        mdio_bit(data & 0x8000);
        data <<= 1;
    }
    mdio_bit(1);
}

uint16_t mdio_read(uint8_t reg)
{
    for (int i=0; i<33; i++) {
        mdio_bit(1);
    }
    mdio_bit(0);
    mdio_bit(1);

    mdio_bit(1);
    mdio_bit(0);

    mdio_bit(0);
    mdio_bit(0);
    mdio_bit(0);
    mdio_bit(0);
    mdio_bit(0);

    for (int i=0; i<5; i++) {
        mdio_bit(reg & 0x10);
        reg <<= 1;
    }

    mdio_bit(1);

    uint16_t result = 0;
    for (int i=0; i<16; i++) {
        result <<= 1;
        mdio_bit(1);
        if (GET_MDIO) {
            result |= 1;
        }
    }
    return result;
}

int mdio_get_irq(void)
{
    if(GET_IRQ)
        return 0;
    return 1;
}
