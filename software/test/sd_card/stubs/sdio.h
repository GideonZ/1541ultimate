// Replaces io/sd_card/sdio.h for the host test: the registers are the simulated card.
#ifndef SDIO_H
#define SDIO_H
#include "sd_sim.h"
#define SDIO_DATA     sd_sim_data
#define SDIO_DATA_32  sd_sim_data32
#define SDIO_SPEED    sd_sim_speed
#define SDIO_CTRL     sd_sim_ctrl
#define SDIO_CRC      sd_sim_crc
#define SDIO_SWITCH   sd_sim_ctrl
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
