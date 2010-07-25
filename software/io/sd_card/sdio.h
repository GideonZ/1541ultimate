#ifndef SDIO_H
#define SDIO_H

#include "integer.h"

#define SDIO_DATA     *((volatile BYTE *)0x4060000)
#define SDIO_DATA_32  *((volatile DWORD*)0x4060000)
#define SDIO_SPEED    *((volatile BYTE *)0x4060004)
#define SDIO_CTRL     *((volatile BYTE *)0x4060008)
#define SDIO_CRC      *((volatile BYTE *)0x406000C)
#define SDIO_SWITCH   *((volatile BYTE *)0x4060008)

#define SPI_FORCE_SS 0x01
#define SPI_LEVEL_SS 0x02

#define SD_CARD_DETECT  0x01
#define SD_CARD_PROTECT 0x02


int  sdio_sense(void);
void sdio_init(void);
void sdio_send_command(BYTE, WORD, WORD);
void sdio_set_speed(int);
void sdio_read_block(BYTE *);
void sdio_write_block(const BYTE *);

#endif
