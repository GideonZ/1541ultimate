/*
 * analog.c
 *
 *  Created on: Nov 13, 2016
 *      Author: gideon
 */

#include <stdio.h>
#include "system.h"
#include <stdint.h>
#include <string.h>
#include <stdlib.h>

#include "altera_avalon_spi.h"
#include "altera_avalon_pio_regs.h"
#include "dump_hex.h"

void configure_adc(void)
{
	uint8_t reset = 0x10;
	uint8_t setup = 0x68; // 01101000 always on reference
	uint8_t averaging = 0x3C;
	alt_avalon_spi_command(SPI_0_BASE, 0, 1, &reset, 0, NULL, 0);
	alt_avalon_spi_command(SPI_0_BASE, 0, 1, &setup, 0, NULL, 0);
	alt_avalon_spi_command(SPI_0_BASE, 0, 1, &averaging, 0, NULL, 0);
}

void adc_read_all(uint8_t *buffer)
{
	uint8_t convert_all = 0xB8; // 1.0111.00.x
	alt_avalon_spi_command(SPI_0_BASE, 0, 1, &convert_all, 0, NULL, 0);
	int timeout = 5000;
	while(((IORD_ALTERA_AVALON_PIO_DATA(PIO_1_BASE) >> 12) & 1) && (timeout)) {
		timeout--;
	}
	if (!timeout) {
		printf("Timeout.");
	}
	alt_avalon_spi_command(SPI_0_BASE, 0, 0, NULL, 16, buffer, 0);
	// AIN: isense vcc_half, v50_half, v43_half, v33, v25, v18, v12
}

const uint32_t factors[8] = { 125, 2, 2, 2, 1, 1, 1, 1 };

uint16_t adc_read_channel(int channel)
{
	uint8_t buffer[16];
	adc_read_all(buffer);
	return (((uint16_t)buffer[2*channel]) << 8) | ((uint16_t)buffer[2*channel+1]);
}

uint32_t adc_read_channel_corrected(int channel)
{
	uint32_t raw = adc_read_channel(channel);
	return raw * factors[channel];
}

uint16_t adc_read_current(void)
{
	uint32_t val = adc_read_channel(0);
	return (val * 125) / 1000;
}


void report_analog(void)
{
	const char *nodes[8] = { "Isup", "VCC", "v50", "v43", "v33", "v25", "v18", "v12" };

	// Current measurement over 0.16 Ohm, with a gain of 50.
	// Every mV (= adc lsb) equals
	// 20 uV on the shunt resistor
	// which equals 125 uA through the resistor

	uint8_t buffer[16];
	adc_read_all(buffer);
	for(int i=0;i<8;i++) {
		uint32_t value = ((uint32_t)buffer[2*i]) << 8;
		value |= ((uint32_t)buffer[2*i+1]);
		// uint32_t value = adc_read_channel(i);
		value *= factors[i];
		if (i==0) {
			printf("%s: %d mA, ", nodes[i], (value / 1000));
		} else {
			printf("%s: %d.%03dV, ", nodes[i], (value / 1000), (value % 1000));
		}
	}
	printf("\n");
}

int validate_analog(int mode)
{
	uint8_t buffer[16];
	uint32_t adc_values[8];

	adc_read_all(buffer);
	for(int i=0;i<8;i++) {
		adc_values[i] = ((uint32_t)buffer[2*i]) << 8;
		adc_values[i] |= ((uint32_t)buffer[2*i+1]);
		adc_values[i] *= factors[i];
	}
	int errors = 0;
	// current
	if ((adc_values[0] < 30000) || (adc_values[0] > 100000)) {
		printf("Current is out of range.\n");
		errors ++;
	}
	// VCC
	if ((adc_values[1] > 1000) && (mode == 0)) {
		printf("Expected VCC not to source when powered by ext USB.\n");
		errors ++;
	}
	if (((adc_values[1] < 4800) || (adc_values[1] > 5200)) && (mode == 1)) {
		printf("Expected VCC to be powered, or not in range..\n");
		errors ++;
	}

	// V50
	if ((adc_values[2] < 4800) || (adc_values[2] > 5200)) {
		printf("Expected voltage on 5.0V node is out of range.\n");
		errors ++;
	}
	// V43
	if ((adc_values[3] > 4429) || (adc_values[3] < 4171)) {
		printf("Expected voltage on 4.3V node is out of range.\n");
		errors ++;
	}
	// V33
	if ((adc_values[4] > 3400) || (adc_values[3] < 3200)) {
		printf("Expected voltage on 3.3V node is out of range.\n");
		errors ++;
	}
	// V25
	if ((adc_values[5] > 2600) || (adc_values[5] < 2400)) {
		printf("Expected voltage on 2.5V node is out of range.\n");
		errors ++;
	}
	// V18
	if ((adc_values[6] > 1872) || (adc_values[5] < 1728)) {
		printf("Expected voltage on 1.8V node is out of range.\n");
		errors ++;
	}
	// V12
	if ((adc_values[7] > 1250) || (adc_values[7] < 1150)) {
		printf("Expected voltage on 1.2V node is out of range.\n");
		errors ++;
	}
	return errors;
}
