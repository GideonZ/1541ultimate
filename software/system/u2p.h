/*
 * u2p.h
 *
 *  Created on: Jul 29, 2016
 *      Author: Gideon
 */

#ifndef U2P_H_
#define U2P_H_

#include <stdint.h>

#ifndef U2P_IO_BASE
#define U2P_IO_BASE 0xA0000000
#endif

#define DDR2_BASE       (U2P_IO_BASE + 0x0100)
#define REMOTE_BASE     (U2P_IO_BASE + 0x0200)
#define MATRIX_KEYB     (U2P_IO_BASE + 0x0300)
#define U2P_AUDIO_MIXER (U2P_IO_BASE + 0x10000)
#define U2P_DEBUG_ETH   (U2P_IO_BASE + 0x11000)

#define DDR2_ADDR_LOW  (*(volatile uint8_t *)(DDR2_BASE + 0x00))
#define DDR2_ADDR_HIGH (*(volatile uint8_t *)(DDR2_BASE + 0x01))
#define DDR2_COMMAND   (*(volatile uint8_t *)(DDR2_BASE + 0x02))
#define DDR2_READMODE  (*(volatile uint8_t *)(DDR2_BASE + 0x08))
#define DDR2_PLLPHASE  (*(volatile uint8_t *)(DDR2_BASE + 0x09))
#define DDR2_ENABLE    (*(volatile uint8_t *)(DDR2_BASE + 0x0C))

#define DDR2_MEASURE   (*(volatile uint8_t *)(DDR2_BASE + 0x04))
#define DDR2_PHASEDONE (*(volatile uint8_t *)(DDR2_BASE + 0x05))

#define U2PIO_SET_SCL  (*(volatile uint8_t *)(U2P_IO_BASE + 0x08))
#define U2PIO_SET_SDA  (*(volatile uint8_t *)(U2P_IO_BASE + 0x09))
#define U2PIO_SET_MDC  (*(volatile uint8_t *)(U2P_IO_BASE + 0x0A))
#define U2PIO_SET_MDIO (*(volatile uint8_t *)(U2P_IO_BASE + 0x0B))
#define U2PIO_GET_SCL  (*(volatile uint8_t *)(U2P_IO_BASE + 0x00))
#define U2PIO_GET_SDA  (*(volatile uint8_t *)(U2P_IO_BASE + 0x02))
#define U2PIO_GET_MDIO (*(volatile uint8_t *)(U2P_IO_BASE + 0x06))
#define U2PIO_SPEAKER_EN (*(volatile uint8_t *)(U2P_IO_BASE + 0x0C))
#define U2PIO_HUB_RESET  (*(volatile uint8_t *)(U2P_IO_BASE + 0x0D))
#define U2PIO_SW_IEC     (*(volatile uint8_t *)(U2P_IO_BASE + 0x0E))
#define U2PIO_ULPI_RESET (*(volatile uint8_t *)(U2P_IO_BASE + 0x0F))
#define U2PIO_BOARDREV   (*(volatile uint8_t *)(U2P_IO_BASE + 0x0C))

#define REMOTE_RECONFIG     (*(volatile uint8_t *)(REMOTE_BASE + 0x06))
#define REMOTE_FLASHSEL_0   (*(volatile uint8_t *)(REMOTE_BASE + 0x0A)) = 1
#define REMOTE_FLASHSEL_1   (*(volatile uint8_t *)(REMOTE_BASE + 0x0B)) = 1
#define REMOTE_FLASHSELCK_0 (*(volatile uint8_t *)(REMOTE_BASE + 0x08)) = 1
#define REMOTE_FLASHSELCK_1 (*(volatile uint8_t *)(REMOTE_BASE + 0x09)) = 1
#define REMOTE_READ_PARAM   (*(volatile uint8_t *)(REMOTE_BASE + 0x04)) = 1
#define REMOTE_BUSY         (*(volatile uint8_t *)(REMOTE_BASE + 0x05))
#define REMOTE_READ_RESULT  (*(volatile uint32_t *)(REMOTE_BASE + 0x00))

#define U2PIO_UR_BUFFER_ENABLE  0x80
#define U2PIO_UR_BUFFER_DISABLE 0x40

#endif /* U2P_H_ */
