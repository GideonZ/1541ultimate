#ifndef SDIO_H
#define SDIO_H

#include "integer.h"
#include "iomap.h"

#define SDIO_DATA     *((volatile BYTE *)(SDCARD_BASE + 0x00))
#define SDIO_DATA_32  *((volatile DWORD*)(SDCARD_BASE + 0x00))
#define SDIO_SPEED    *((volatile BYTE *)(SDCARD_BASE + 0x04))
#define SDIO_CTRL     *((volatile BYTE *)(SDCARD_BASE + 0x08))
#define SDIO_CRC      *((volatile BYTE *)(SDCARD_BASE + 0x0C))
#define SDIO_SWITCH   *((volatile BYTE *)(SDCARD_BASE + 0x08))

#define SPI_FORCE_SS 0x01
#define SPI_LEVEL_SS 0x02

#define SD_CARD_DETECT  0x01
#define SD_CARD_PROTECT 0x02


int  sdio_sense(void);
void sdio_init(void);
void sdio_send_command(BYTE, WORD, WORD);
void sdio_set_speed(int);
void sdio_read_block(BYTE *);
bool sdio_write_block(const BYTE *);

#endif
