/*
 * i2c.c
 *
 *  Created on: Apr 30, 2016
 *      Author: gideon
 */

#include <stdint.h>
#include <stdio.h>

#include "system.h"
//#include "altera_avalon_pio_regs.h"
#include "u64.h"
#include "dump_hex.h"

#define SET_SCL_LOW   U64_EXT_I2C_SCL = 0
#define SET_SCL_HIGH  U64_EXT_I2C_SCL = 1
#define SET_SDA_LOW   U64_EXT_I2C_SDA = 0
#define SET_SDA_HIGH  U64_EXT_I2C_SDA = 1
#define GET_SCL       U64_EXT_I2C_SCL
#define GET_SDA       U64_EXT_I2C_SDA

static void _wait() {
	for(int i=0;i<2;i++)
		;
}

static void ext_i2c_start()
{
	SET_SCL_HIGH;
	SET_SDA_HIGH;
	_wait();
	SET_SDA_LOW;
	_wait();
	SET_SCL_LOW;
	_wait();
}

static void ext_i2c_restart()
{
	// SET_MARKER;
	if (!GET_SDA) {
		printf("On restart, SDA should be 1\n");
	}
	SET_SCL_HIGH;
	_wait();
	// CLR_MARKER;
}


static void ext_i2c_stop()
{
	SET_SDA_LOW;
	_wait();
	SET_SCL_HIGH;
	_wait();
	SET_SDA_HIGH;
	_wait();
}

static int ext_i2c_send_byte(const uint8_t byte)
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

static uint8_t ext_i2c_receive_byte(int ack)
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

void ext_i2c_scan_bus(void)
{
	for(int i=1;i<255;i++) {
		ext_i2c_start();
		int res = ext_i2c_send_byte((uint8_t)i);
		printf("[%2x:%d] ", i, res);
		ext_i2c_stop();
	}
	printf("\n");
}

uint8_t ext_i2c_read_byte(const uint8_t devaddr, const uint8_t regaddr, int *res)
{
	ext_i2c_start();
	*res = 0;
	*res = ext_i2c_send_byte(devaddr);
	if(*res) {
		printf("Error sending read address (%d)\n", *res);
		return 0xFF;
	}
	*res = ext_i2c_send_byte(regaddr);
	if(*res) {
		printf("Error sending register address (%d)\n", *res);
	}
	ext_i2c_restart();
	*res = ext_i2c_send_byte(devaddr | 1);
	if(*res) {
		printf("Error sending read address (%d)\n", *res);
	}
	uint8_t result = ext_i2c_receive_byte(0);
	ext_i2c_stop();
	return result;
}

int ext_i2c_write_byte(const uint8_t devaddr, const uint8_t regaddr, const uint8_t data)
{
	ext_i2c_start();
	int res;
	res = ext_i2c_send_byte(devaddr);
	if(res) {
		printf("Error sending write address (%d)\n", res);
		ext_i2c_stop();
		return -1;
	}
	res = ext_i2c_send_byte(regaddr);
	if(res) {
		printf("Error sending register address (%d)\n", res);
		ext_i2c_stop();
		return -1;
	}
	res = ext_i2c_send_byte(data);
	if(res) {
		printf("Error sending register data (%d)\n", res);
	}
	ext_i2c_stop();
	return res;
}

uint16_t ext_i2c_read_word(const uint8_t devaddr, const uint16_t regaddr)
{
	ext_i2c_start();
	int res;
	res = ext_i2c_send_byte(devaddr);
	if(res) {
		printf("Error sending write address (%d)\n", res);
	}
	res = ext_i2c_send_byte(regaddr >> 8);
	res += ext_i2c_send_byte(regaddr & 0xFF);
	if(res) {
		printf("Error sending register address (%d)\n", res);
	}
	ext_i2c_restart();
	res = ext_i2c_send_byte(devaddr | 1);
	if(res) {
		printf("Error sending read address (%d)\n", res);
	}
	uint16_t result = ((uint16_t)ext_i2c_receive_byte(1)) << 8;
	result |= ext_i2c_receive_byte(0);
	ext_i2c_stop();
	return result;
}

int ext_i2c_write_word(const uint8_t devaddr, const uint16_t regaddr, const uint16_t data)
{
	ext_i2c_start();
	int res;
	res = ext_i2c_send_byte(devaddr);
	if(res) {
		printf("Error sending write address (%d)\n", res);
		ext_i2c_stop();
		return -1;
	}
	res = ext_i2c_send_byte(regaddr >> 8);
	res += ext_i2c_send_byte(regaddr & 0xFF);
	if(res) {
		printf("Error sending register address (%d)\n", res);
		ext_i2c_stop();
		return -1;
	}
	res = ext_i2c_send_byte(data >> 8);
	res += ext_i2c_send_byte(data & 0xFF);
	if(res) {
		printf("Error sending register data (%d)\n", res);
	}
	ext_i2c_stop();
	return res;
}

int ext_i2c_write_block(const uint8_t devaddr, const uint8_t regaddr, const uint8_t *data, int length)
{
	ext_i2c_start();
	int res;
	res = ext_i2c_send_byte(devaddr);
	if(res) {
		printf("Error sending write address (%d)\n", res);
		ext_i2c_stop();
		return -1;
	}
	res = ext_i2c_send_byte(regaddr);
	if(res) {
		printf("Error sending register address (%d)\n", res);
		ext_i2c_stop();
		return -1;
	}
	res = ext_i2c_send_byte((uint8_t)length);
	if(res) {
		printf("Error sending block length (%d)\n", res);
		ext_i2c_stop();
		return -1;
	}
	for(int i=0;i<length;i++, data++) {
		res = ext_i2c_send_byte(*data);
		if(res) {
			printf("Error sending register data %02x (%d)\n", *data, res);
			ext_i2c_stop();
			return -2;
		}
	}
	ext_i2c_stop();
	return 0;
}

int ext_i2c_read_block(const uint8_t devaddr, const uint8_t regaddr, uint8_t *data, const int length)
{
	if(!length)
		return 0;

	ext_i2c_start();
	int res = ext_i2c_send_byte(devaddr);
	if(res) {
		printf("NACK on address %02x (%d)\n", devaddr, res);
		ext_i2c_stop();
		return -1;
	}
	res = ext_i2c_send_byte(regaddr);
	if(res) {
		printf("NACK on register address %02x (%d)\n", regaddr, res);
		ext_i2c_stop();
		return -1;
	}
	ext_i2c_restart();
	res = ext_i2c_send_byte(devaddr | 1);
	if(res) {
		printf("NACK on address %02x (%d)\n", devaddr | 1, res);
		ext_i2c_stop();
		return -2;
	}
	for(int i=0;i<length;i++) {
		*(data++) = ext_i2c_receive_byte((i == length-1)? 0 : 1);
	}
	ext_i2c_stop();
	return 0;
}
