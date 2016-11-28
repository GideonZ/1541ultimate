/*
 * jtag.h
 *
 *  Created on: Nov 13, 2016
 *      Author: gideon
 */

#ifndef JTAG_H_
#define JTAG_H_

#include <stdint.h>

void jtag_reset_to_idle(volatile uint32_t *host);
void jtag_instruction(volatile uint32_t *host, uint32_t inst);
void jtag_senddata(volatile uint32_t *host, uint8_t *data, int length, uint8_t *readback);
void jtag_configure_fpga(volatile uint32_t *host, uint8_t *data, int length);
void jtag_clear_fpga(volatile uint32_t *host);

typedef struct {
	int valid;
	volatile uint32_t *host;
	int drLength;
	uint16_t address;
	int dutRunning;
} JTAG_Access_t;

int FindJtagAccess(volatile uint32_t *host, JTAG_Access_t *access);

void vji_read(JTAG_Access_t *controller, uint16_t addr, uint8_t *dest, int bytes);
void vji_read_fifo(JTAG_Access_t *controller, uint32_t *data, int words);
void vji_write(JTAG_Access_t *controller, uint16_t addr, uint8_t *data, int bytes);
void vji_write_block(JTAG_Access_t *controller, uint32_t *data, int words);
void vji_read_memory(JTAG_Access_t *controller, uint32_t address, int words, uint32_t *dest);
void vji_write_memory(JTAG_Access_t *controller, uint32_t address, int words, uint32_t *src);

#endif /* JTAG_H_ */
