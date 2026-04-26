/*
 * i2c_drv.cc
 *
 *  Created on: Oct 26, 2016
 *      Author: gideon
 */

#include <stdint.h>
#include <stdio.h>

#include "i2c_drv.h"

// Global interface to I2C 
I2C_Driver *i2c = 0;

void I2C_Driver :: _wait() {
	for(int i=0;i<1;i++)
	    GET_SCL();
}

void I2C_Driver :: i2c_start()
{
	SET_SCL_HIGH();
	SET_SDA_HIGH();
	_wait();
	SET_SDA_LOW();
	_wait();
	SET_SCL_LOW();
	_wait();
}

void I2C_Driver :: i2c_restart()
{
	// SET_MARKER;
	if (!GET_SDA()) {
		printf("On restart, SDA should be 1\n");
	}
	SET_SCL_HIGH();
	_wait();
	// CLR_MARKER;
}


void I2C_Driver :: i2c_stop()
{
	SET_SDA_LOW();
	_wait();
	SET_SCL_HIGH();
	_wait();
	SET_SDA_HIGH();
	_wait();
}

int I2C_Driver :: i2c_send_byte(const uint8_t byte)
{
	uint8_t data = byte;
	if (GET_SDA()) {
		SET_SDA_LOW();
		_wait();
	}
	if (GET_SCL()) {
		SET_SCL_LOW();
		_wait();
	}
	for(int i=0;i<8;i++) {
		if (data & 0x80) {
			SET_SDA_HIGH();
		} else {
			SET_SDA_LOW();
		}
		data <<= 1;
        _wait();
		SET_SCL_HIGH();
		_wait();
		SET_SCL_LOW();
		_wait();
	}
	SET_SDA_HIGH();
	_wait();
	SET_SCL_HIGH();
	int ack = GET_SDA();
	_wait();
	SET_SCL_LOW();
	_wait();

	if(ack) {
		return -1;
	}
	return 0;
}

uint8_t I2C_Driver :: i2c_receive_byte(int ack)
{
	uint8_t result = 0;
	for(int i=0;i<8;i++) {
		result <<= 1;
		SET_SCL_HIGH();

		do { // allow clock stretching
		    _wait();
		} while(!GET_SCL());

		if (GET_SDA()) {
			result |= 1;
		}
		SET_SCL_LOW();
		_wait();
	}
	if (ack) {
		SET_SDA_LOW();
	} else {
		SET_SDA_HIGH();
	}
	_wait();
	SET_SCL_HIGH();
	_wait();
	SET_SCL_LOW();
	SET_SDA_HIGH();
	_wait();

	return result;
}

bool I2C_Driver :: i2c_probe(const uint8_t devaddr)
{
    i2c_start();
    int res = i2c_send_byte(devaddr);
    i2c_stop();
    return (res >= 0);
}

void I2C_Driver :: i2c_scan_bus(void)
{
	for(int i=0;i<255;i+=1) {
		i2c_start();
		int res = i2c_send_byte((uint8_t)i);
		printf("|%2x:%c", i, (res < 0)?'-':'X');
		i2c_stop();
		//i2c_start();
		//i2c_stop();
		if (i % 16 == 15) {
			printf("\n");
		}
	}
	printf("\n");
}

uint8_t I2C_Driver :: i2c_read_byte(const uint8_t devaddr, const uint8_t regaddr, int *res)
{
	i2c_start();
	*res = 0;
	*res = i2c_send_byte(devaddr);
	if(*res) {
		printf("Error sending device address (%d)\n", *res);
    	i2c_stop();
		return 0xFF;
	}
	*res = i2c_send_byte(regaddr);
	if(*res) {
		printf("Error sending register address (%d)\n", *res);
    	i2c_stop();
        return 0xFF;
	}
	i2c_restart();
	*res = i2c_send_byte(devaddr | 1);
	if(*res) {
		printf("Error sending read address (%d)\n", *res);
    	i2c_stop();
        return 0xFF;
	}
	uint8_t result = i2c_receive_byte(0);
	i2c_stop();
	return result;
}

int I2C_Driver :: i2c_write_byte(const uint8_t devaddr, const uint8_t regaddr, const uint8_t data)
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

uint16_t I2C_Driver :: i2c_read_word(const uint8_t devaddr, const uint16_t regaddr)
{
	i2c_start();
	int res;
	res = i2c_send_byte(devaddr);
	if(res) {
		printf("Error sending address (%d)\n", res);
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

void I2C_Driver :: i2c_read_raw_16(const uint8_t devaddr, uint16_t *dest, int count)
{
	i2c_start();
	int res;
	res = i2c_send_byte(devaddr | 1);
	if(res) {
		printf("Error sending read address (%d)\n", res);
	}
	for (int i=0;i<count;i++) {
		uint16_t result = ((uint16_t)i2c_receive_byte(1)) << 8;
		result |= i2c_receive_byte((i == count-1) ? 0 : 1);
		*(dest++) = result;
	}
	i2c_stop();
}

int I2C_Driver :: i2c_write_word(const uint8_t devaddr, const uint16_t regaddr, const uint16_t data)
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

int I2C_Driver :: i2c_write_nau(const uint8_t devaddr, const uint8_t regaddr, const uint16_t data)
{
	i2c_start();
	int res;
	res = i2c_send_byte(devaddr);
	if(res) {
		printf("Error sending write address (%d)\n", res);
		i2c_stop();
		return -1;
	}
	res = i2c_send_byte((regaddr << 1) | (data >> 8));
	if(res) {
		printf("Error sending register address (%d)\n", res);
		i2c_stop();
		return -1;
	}
	res = i2c_send_byte(data & 0xFF);
	if(res) {
		printf("Error sending register data (%d)\n", res);
	}
	i2c_stop();
	return res;
}

int I2C_Driver :: i2c_write_block(const uint8_t devaddr, const uint8_t regaddr, const uint8_t *data, int length)
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

int I2C_Driver :: i2c_read_block(const uint8_t devaddr, const uint8_t regaddr, uint8_t *data, const int length)
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

int I2C_Driver :: i2c_read_block_ext(const uint8_t page, const uint8_t devaddr, const uint8_t regaddr, uint8_t *data, const int length)
{
    if(!length)
        return 0;

    i2c_start();
    int res = i2c_send_byte(0x60);
    if (res) {
        printf("NACK on page address (%d)\n", res);
        i2c_stop();
        return -1;
    }
    res = i2c_send_byte(page);
    if (res) {
        printf("NACK on page byte (%d)\n", res);
        i2c_stop();
        return -1;
    }
    i2c_restart();
    res = i2c_send_byte(devaddr);
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
