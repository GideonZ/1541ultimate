/*
 * i2c.c
 *
 *  Created on: Apr 30, 2016
 *      Author: gideon
 */

#include <stdint.h>
#include <stdio.h>

#include "system.h"
#include "altera_avalon_pio_regs.h"
#include "dump_hex.h"

#define SET_SCL_LOW   IOWR_ALTERA_AVALON_PIO_CLEAR_BITS(PIO_0_BASE, 0x1)
#define SET_SCL_HIGH  IOWR_ALTERA_AVALON_PIO_SET_BITS(PIO_0_BASE, 0x1)
#define SET_SDA_LOW   IOWR_ALTERA_AVALON_PIO_CLEAR_BITS(PIO_0_BASE, 0x2)
#define SET_SDA_HIGH  IOWR_ALTERA_AVALON_PIO_SET_BITS(PIO_0_BASE, 0x2)
#define GET_SDA       (IORD_ALTERA_AVALON_PIO_DATA(PIO_0_BASE) & 0x2)
#define GET_SCL       (IORD_ALTERA_AVALON_PIO_DATA(PIO_0_BASE) & 0x1)
#define SET_MARKER    IOWR_ALTERA_AVALON_PIO_SET_BITS(PIO_0_BASE, 0x4)
#define CLR_MARKER    IOWR_ALTERA_AVALON_PIO_CLEAR_BITS(PIO_0_BASE, 0x4)
#define HUB_RESET_0   IOWR_ALTERA_AVALON_PIO_CLEAR_BITS(PIO_0_BASE, 0x80)
#define HUB_RESET_1   IOWR_ALTERA_AVALON_PIO_SET_BITS(PIO_0_BASE, 0x80)

static void _wait() {
	for(int i=0;i<2;i++)
		;
}

static void i2c_start()
{
	SET_SCL_HIGH;
	SET_SDA_HIGH;
	_wait();
	SET_SDA_LOW;
	_wait();
	SET_SCL_LOW;
	_wait();
}

static void i2c_restart()
{
	// SET_MARKER;
	if (!GET_SDA) {
		printf("On restart, SDA should be 1\n");
	}
	SET_SCL_HIGH;
	_wait();
	// CLR_MARKER;
}


static void i2c_stop()
{
	SET_SDA_LOW;
	_wait();
	SET_SCL_HIGH;
	_wait();
	SET_SDA_HIGH;
	_wait();
}

static int i2c_send_byte(const uint8_t byte)
{
	uint8_t data = byte;
	if (GET_SDA) {
		SET_SDA_LOW;
		_wait();
	}
	if (GET_SCL) {
		SET_SCL_LOW;
		_wait();
	}
	for(int i=0;i<8;i++) {
		if (data & 0x80) {
			SET_SDA_HIGH;
		} else {
			SET_SDA_LOW;
		}
		data <<= 1;
		SET_SCL_HIGH;
		_wait();
		SET_SCL_LOW;
		_wait();
	}
	SET_SDA_HIGH;
	_wait();
	SET_SCL_HIGH;
	int ack = GET_SDA;
	_wait();
	SET_SCL_LOW;
	_wait();

	if(ack) {
		return -1;
	}
	return 0;
}

static uint8_t i2c_receive_byte(int ack)
{
	uint8_t result = 0;
	for(int i=0;i<8;i++) {
		result <<= 1;
		SET_SCL_HIGH;
		_wait();
		if (GET_SDA) {
			result |= 1;
		}
		SET_SCL_LOW;
		_wait();
	}
	if (ack) {
		SET_SDA_LOW;
	} else {
		SET_SDA_HIGH;
	}
	_wait();
	SET_SCL_HIGH;
	_wait();
	SET_SCL_LOW;
	SET_SDA_HIGH;
	_wait();

	return result;
}

void i2c_scan_bus(void)
{
	for(int i=1;i<255;i++) {
		i2c_start();
		int res = i2c_send_byte((uint8_t)i);
		printf("[%2x:%d] ", i, res);
		i2c_stop();
	}
	printf("\n");
}

uint8_t i2c_read_byte(const uint8_t devaddr, const uint8_t regaddr)
{
	i2c_start();
	int res;
	res = i2c_send_byte(devaddr);
	if(res) {
		printf("Error sending write address (%d)\n", res);
	}
	res = i2c_send_byte(regaddr);
	if(res) {
		printf("Error sending register address (%d)\n", res);
	}
	i2c_restart();
	res = i2c_send_byte(devaddr | 1);
	if(res) {
		printf("Error sending read address (%d)\n", res);
	}
	uint8_t result = i2c_receive_byte(0);
	i2c_stop();
	return result;
}

int i2c_write_byte(const uint8_t devaddr, const uint8_t regaddr, const uint8_t data)
{
	i2c_start();
	int res;
	res = i2c_send_byte(devaddr);
	if(res) {
		printf("Error sending write address (%d)\n", res);
		i2c_stop();
		return -1;
	}
	res = i2c_send_byte(regaddr);
	if(res) {
		printf("Error sending register address (%d)\n", res);
		i2c_stop();
		return -1;
	}
	res = i2c_send_byte(data);
	if(res) {
		printf("Error sending register data (%d)\n", res);
	}
	i2c_stop();
	return res;
}

uint16_t i2c_read_word(const uint8_t devaddr, const uint16_t regaddr)
{
	i2c_start();
	int res;
	res = i2c_send_byte(devaddr);
	if(res) {
		printf("Error sending write address (%d)\n", res);
	}
	res = i2c_send_byte(regaddr >> 8);
	res += i2c_send_byte(regaddr & 0xFF);
	if(res) {
		printf("Error sending register address (%d)\n", res);
	}
	i2c_restart();
	res = i2c_send_byte(devaddr | 1);
	if(res) {
		printf("Error sending read address (%d)\n", res);
	}
	uint16_t result = ((uint16_t)i2c_receive_byte(1)) << 8;
	result |= i2c_receive_byte(0);
	i2c_stop();
	return result;
}

int i2c_write_word(const uint8_t devaddr, const uint16_t regaddr, const uint16_t data)
{
	i2c_start();
	int res;
	res = i2c_send_byte(devaddr);
	if(res) {
		printf("Error sending write address (%d)\n", res);
		i2c_stop();
		return -1;
	}
	res = i2c_send_byte(regaddr >> 8);
	res += i2c_send_byte(regaddr & 0xFF);
	if(res) {
		printf("Error sending register address (%d)\n", res);
		i2c_stop();
		return -1;
	}
	res = i2c_send_byte(data >> 8);
	res += i2c_send_byte(data & 0xFF);
	if(res) {
		printf("Error sending register data (%d)\n", res);
	}
	i2c_stop();
	return res;
}

int i2c_write_block(const uint8_t devaddr, const uint8_t regaddr, const uint8_t *data, int length)
{
	i2c_start();
	int res;
	res = i2c_send_byte(devaddr);
	if(res) {
		printf("Error sending write address (%d)\n", res);
		i2c_stop();
		return -1;
	}
	res = i2c_send_byte(regaddr);
	if(res) {
		printf("Error sending register address (%d)\n", res);
		i2c_stop();
		return -1;
	}
	res = i2c_send_byte((uint8_t)length);
	if(res) {
		printf("Error sending block length (%d)\n", res);
		i2c_stop();
		return -1;
	}
	for(int i=0;i<length;i++, data++) {
		res = i2c_send_byte(*data);
		if(res) {
			printf("Error sending register data %02x (%d)\n", *data, res);
			i2c_stop();
			return -2;
		}
	}
	i2c_stop();
	return 0;
}

int i2c_read_block(const uint8_t devaddr, const uint8_t regaddr, uint8_t *data, const int length)
{
	if(!length)
		return 0;

	i2c_start();
	int res = i2c_send_byte(devaddr);
	if(res) {
		printf("NACK on address %02x (%d)\n", devaddr, res);
		i2c_stop();
		return -1;
	}
	res = i2c_send_byte(regaddr);
	if(res) {
		printf("NACK on register address %02x (%d)\n", regaddr, res);
		i2c_stop();
		return -1;
	}
	i2c_restart();
	res = i2c_send_byte(devaddr | 1);
	if(res) {
		printf("NACK on address %02x (%d)\n", devaddr | 1, res);
		i2c_stop();
		return -2;
	}
	for(int i=0;i<length;i++) {
		*(data++) = i2c_receive_byte((i == length-1)? 0 : 1);
	}
	i2c_stop();
	return 0;
}


#define HUB_ADDRESS 0x58

#define VIDL    0x00
#define VIDM    0x01
#define PIDL    0x02
#define PIDM    0x03
#define DIDL    0x04
#define DIDM    0x05
#define CFG1    0x06
#define CFG2    0x07
#define CFG3    0x08
#define NRD     0x09
#define PDS     0x0A
#define PDB     0x0B
#define MAXPS   0x0C
#define MAXPB   0x0D
#define HCMCS   0x0E
#define HCMCB   0x0F
#define PWRT    0x10
#define BOOSTUP 0xF6
#define BOOST20 0xF8
#define PRTSP   0xFA
#define PRTR12  0xFB
#define STCD    0xFF

static uint8_t hub_registers[] = {
        0x24, 0x04, 0x12, 0x25, 0xA0, 0x0B, 0x9A, 0x20,
        0x02, 0x00, 0x00, 0x00, 0x01, 0x32, 0x01, 0x32,
        0x32, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };

void USb2512Init(void)
{
    uint8_t status;

	HUB_RESET_0;
	_wait();
	HUB_RESET_1;
	_wait();

    uint8_t buffer[32];
    const uint8_t reset = 0x02;
    const uint8_t start = 0x01;

    // Reset the USB hub
    if (i2c_write_block(HUB_ADDRESS, STCD, &reset, 1)) {
        puts("Failed to reset the USB hub");
        return;
    }

    // Configure the USB hub
    if (i2c_write_block(HUB_ADDRESS, 0, hub_registers, sizeof(hub_registers))) {
        puts("Failed to configure the USB hub");
        return;
    }

    // Start the USB hub with the new settings
    if (i2c_write_block(HUB_ADDRESS, STCD, &start, 1)) {
        puts("Failed to start the USB hub");
        return;
    }
    puts("USB Hub successfully configured.");
}
