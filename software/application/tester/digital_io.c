/*
 * digital_io.c
 *
 *  Created on: Nov 13, 2016
 *      Author: gideon
 */

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "system.h"
#include "altera_avalon_pio_regs.h"
#include "digital_io.h"

#define EXP_SCLR 0x100
#define EXP_OE   0x200
#define EXP_RCLK 0x400
#define EXP_SCLK 0x800
#define EXP_DATA 0x1000

static int get_bit(uint8_t *array, int pos)
{
	uint8_t byte = array[pos >> 3];

	if (byte & (1 << (pos & 7)))
		return 1;
	return 0;
}

int TestIOPins(JTAG_Access_t *access)
{
	uint8_t vji_data[8];

	const int expected_positions[] = {
			 3, 19,  2, 18,  1, 17,  0, 16,
			 7, 23,  6, 22,  5, 21,  4, 20,
			11, 27, 10, 29,  9, 34,  8, 33,
			15, 37, 14, 28, 13, 31, 12, 32,
			41, 40, 30, 36, 25, 26, 38, 35,
			-2, 47, 46, 45, 44, 42, 43, 39
	};
	const char *pins[] = {
			"A[0]", "A[1]", "A[2]", "A[3]",
			"A[4]", "A[5]", "A[6]", "A[7]",
			"A[8]", "A[9]", "A[10]", "A[11]",
			"A[12]", "A[13]", "A[14]", "A[15]",
			"D[0]", "D[1]", "D[2]", "D[3]",
			"D[4]", "D[5]", "D[6]", "D[7]",
			"VCC", "NMI#", "IRQ#", "IO2#",
			"IO1#", "ROML#", "ROMH#", "GAME#",
			"EXROM#", "DMA#", "BA#", "RWn#",
			"RST#", "DOTCLK", "PHI2", "IEC_SRQ",
			"IEC_RESET", "IEC_CLK", "IEC_DATA", "IEC_ATN",
			"CAS_WRITE", "CAS_READ", "CAS_SENSE", "CAS_MOTOR"
	};
	int errors = 0;

	// Reset both shift/register clocks
	IOWR_ALTERA_AVALON_PIO_CLEAR_BITS(PIO_1_BASE, EXP_RCLK | EXP_SCLK);

	// Set clear pin of the register
	IOWR_ALTERA_AVALON_PIO_SET_BITS(PIO_1_BASE, EXP_SCLR);
	IOWR_ALTERA_AVALON_PIO_SET_BITS(PIO_1_BASE, EXP_SCLK);
	IOWR_ALTERA_AVALON_PIO_SET_BITS(PIO_1_BASE, EXP_RCLK);
	IOWR_ALTERA_AVALON_PIO_CLEAR_BITS(PIO_1_BASE, EXP_RCLK | EXP_SCLK);
	IOWR_ALTERA_AVALON_PIO_CLEAR_BITS(PIO_1_BASE, EXP_SCLR);

	// Set output enable pin of the register
	IOWR_ALTERA_AVALON_PIO_SET_BITS(PIO_1_BASE, EXP_OE);

	// Set data to 1
	IOWR_ALTERA_AVALON_PIO_SET_BITS(PIO_1_BASE, EXP_DATA);

	// Repeat for 49 times
	uint8_t stuck_0[] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
	uint8_t stuck_1[] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };

	for (int i=0; i<49; i++) {
		// Read I/O from JTAG
		vji_read(access, 1, vji_data, 6);
		for(int j=5;j>=0;j--) {
		//	printf("%02x", vji_data[j]);
			stuck_0[j] |= vji_data[j];
			stuck_1[j] &= vji_data[j];
		}
		//for(int j=0;j<48;j++) {
		//	if ((j != 24) && (get_bit(vji_data, j) == 1)) {
		//		printf(" %02d", j);
		//	}
		//}
		//printf("\n");

		// Set shift clock
		IOWR_ALTERA_AVALON_PIO_SET_BITS(PIO_1_BASE, EXP_SCLK);
		// Set register clock
		IOWR_ALTERA_AVALON_PIO_SET_BITS(PIO_1_BASE, EXP_RCLK);
		// Set data to 0
		IOWR_ALTERA_AVALON_PIO_CLEAR_BITS(PIO_1_BASE, EXP_DATA);
		// Reset both shift/register clocks
		IOWR_ALTERA_AVALON_PIO_CLEAR_BITS(PIO_1_BASE, EXP_RCLK | EXP_SCLK);
	}
	uint8_t error_map[48];
	memset(error_map, 0, 48);

	for (int i=0; i < 48; i++) {
		int b = get_bit(stuck_0, i);
		if (b == 0) {
			printf("Stuck at 0: %s\n", pins[i]);
			error_map[i] = 1;
			errors ++;
		}
	}
	for (int i=0; i < 48; i++) {
		int b = get_bit(stuck_1, i);
		if ((b == 1) && (i != 24)) {
			printf("Stuck at 1: %s\n", pins[i]);
			error_map[i] = 1;
			errors ++;
		}
	}
	// Set data to 1
	IOWR_ALTERA_AVALON_PIO_SET_BITS(PIO_1_BASE, EXP_DATA);
	// Set shift clock
	IOWR_ALTERA_AVALON_PIO_SET_BITS(PIO_1_BASE, EXP_SCLK);
	// Set register clock
	IOWR_ALTERA_AVALON_PIO_SET_BITS(PIO_1_BASE, EXP_RCLK);
	// Reset both shift/register clocks
	IOWR_ALTERA_AVALON_PIO_CLEAR_BITS(PIO_1_BASE, EXP_RCLK | EXP_SCLK);
	// Set data to 0
	IOWR_ALTERA_AVALON_PIO_CLEAR_BITS(PIO_1_BASE, EXP_DATA);

	uint8_t expected[8];
	for (int i=0; i<48; i++) {
		// Read I/O from JTAG
		vji_read(access, 1, vji_data, 6);
		for(int j=0;j<6;j++) {
			vji_data[j] &= ~stuck_1[j];
		}
		// Set shift clock
		IOWR_ALTERA_AVALON_PIO_SET_BITS(PIO_1_BASE, EXP_SCLK);
		// Set register clock
		IOWR_ALTERA_AVALON_PIO_SET_BITS(PIO_1_BASE, EXP_RCLK);
		// Set data to 0
		IOWR_ALTERA_AVALON_PIO_CLEAR_BITS(PIO_1_BASE, EXP_DATA);
		// Reset both shift/register clocks
		IOWR_ALTERA_AVALON_PIO_CLEAR_BITS(PIO_1_BASE, EXP_RCLK | EXP_SCLK);


		memset(expected, 0, 8);
		int map = expected_positions[i];

		if (error_map[map])
			continue;

		if (map > 0) {
			expected[(map >> 3)] |= (1 << (map & 7));
			if (memcmp(vji_data, expected, 6) != 0) {
				errors++;
				printf("Error on pin %d->%d %s. Got: (", i, map, pins[map]);
				for(int j=0;j<48;j++) {
					if (j == 24)
						continue;
					if (get_bit(vji_data, j) == 1) {
						printf("%s, ", pins[j]);
					}
				}
				printf(")\n");
			}
		}
	}
	printf("Total errors: %d\n", errors);
	return errors;
}
