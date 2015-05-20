#ifndef SDIO_H
#define SDIO_H

#include "integer.h"
#include "iomap.h"

#define SDIO_DATA     *((volatile uint8_t *)(SDCARD_BASE + 0x00))
#define SDIO_DATA_32  *((volatile uint32_t*)(SDCARD_BASE + 0x00))
#define SDIO_SPEED    *((volatile uint8_t *)(SDCARD_BASE + 0x04))
#define SDIO_CTRL     *((volatile uint8_t *)(SDCARD_BASE + 0x08))
#define SDIO_CRC      *((volatile uint8_t *)(SDCARD_BASE + 0x0C))
#define SDIO_SWITCH   *((volatile uint8_t *)(SDCARD_BASE + 0x08))

#define SPI_FORCE_SS 0x01
#define SPI_LEVEL_SS 0x02

#define SD_CARD_DETECT  0x01
#define SD_CARD_PROTECT 0x02


int  sdio_sense(void);
void sdio_init(void);
void sdio_send_command(uint8_t, uint16_t, uint16_t);
void sdio_set_speed(int);
uint8_t sdio_read_block(uint8_t *);
bool sdio_write_block(const uint8_t *);

#endif
