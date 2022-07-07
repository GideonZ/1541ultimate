/*
 * i2c_drv.cc
 *
 *  Created on: Oct 26, 2016
 *      Author: gideon
 */

#include <stdint.h>
#include <stdio.h>

#include "i2c_drv.h"

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

void I2C_Driver :: i2c_scan_bus(void)
{
	for(int i=0;i<255;i+=1) {
		i2c_start();
		int res = i2c_send_byte((uint8_t)i);
		printf("[%2x:%c] ", i, (res < 0)?'-':'X');
		i2c_stop();
		i2c_start();
		i2c_stop();
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
		printf("Error sending read address (%d)\n", *res);
		return 0xFF;
	}
	*res = i2c_send_byte(regaddr);
	if(*res) {
		printf("Error sending register address (%d)\n", *res);
	}
	i2c_restart();
	*res = i2c_send_byte(devaddr | 1);
	if(*res) {
		printf("Error sending read address (%d)\n", *res);
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

#define HUB_ADDRESS_2513 0x58
#define HUB_ADDRESS_2503 0x5A

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

#define USB2503_STCD    0x00
#define USB2503_VIDL    0x01
#define USB2503_VIDM    0x02
#define USB2503_PIDL    0x03
#define USB2503_PIDM    0x04
#define USB2503_DIDL    0x05
#define USB2503_DIDM    0x06
#define USB2503_CFG1    0x07
#define USB2503_CFG2    0x08
#define USB2503_NRD     0x09
#define USB2503_PDS     0x0A
#define USB2503_PDB     0x0B
#define USB2503_MAXPS   0x0C
#define USB2503_MAXPB   0x0D
#define USB2503_HCMCS   0x0E
#define USB2503_HCMCB   0x0F
#define USB2503_PWRT    0x10

static uint8_t hub_registers_2503[] = {
	USB2503_VIDL,  0x24,
	USB2503_VIDM,  0x04,
	USB2503_PIDL,  0x03,
	USB2503_PIDM,  0x25,
	USB2503_DIDL,  0xA0,
	USB2503_DIDM,  0x0B,
	USB2503_CFG1,  0x97, // Self powered, no leds, hs enabled, multi tt, normal EOP, no current sense 10, ganged switch
	USB2503_CFG2,  0x20, // copied from 2513
	USB2503_NRD,   0x00, // all ports removable
	USB2503_PDS,   0x00, // all ports enabled
	USB2503_PDB,   0x00, // all ports enabled
	USB2503_MAXPS, 0x01, // 2 mA, as per spec
	USB2503_MAXPB, 0x32, // 100 mA?
	USB2503_HCMCS, 0x01, // 2 mA
	USB2503_HCMCB, 0x32, // 100 mA?
	USB2503_PWRT,  0x32, // 100 ms power on time
	USB2503_STCD,  0x03, // start!
};

static uint8_t hub_registers_2513[] = {
        0x24, 0x04, 0x13, 0x25, 0xA0, 0x0B, 0x9A, 0x20,
        0x02, 0x00, 0x00, 0x00, 0x01, 0x32, 0x01, 0x32,
        0x32, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };

#define HUB_RESET_0   U2PIO_HUB_RESET = 1
#define HUB_RESET_1   U2PIO_HUB_RESET = 0

extern "C" {
    void USB2513Init(void)
    {
        uint8_t status;

        HUB_RESET_0;
        for(int i=0;i<50;i++) {
            (void)U2PIO_HUB_RESET;
        }
        HUB_RESET_1;
        for(int i=0;i<50;i++) {
            (void)U2PIO_HUB_RESET;
        }

        uint8_t buffer[32];
        const uint8_t reset = 0x02;
        const uint8_t start = 0x01;

        I2C_Driver i2c;

        // Reset the USB hub
        if (i2c.i2c_write_block(HUB_ADDRESS_2513, STCD, &reset, 1)) {
            puts("Failed to reset the USB hub");
            return;
        }

        // Configure the USB hub
        if (i2c.i2c_write_block(HUB_ADDRESS_2513, 0, hub_registers_2513, sizeof(hub_registers_2513))) {
            puts("Failed to configure the USB hub");
            return;
        }

        // Start the USB hub with the new settings
        if (i2c.i2c_write_block(HUB_ADDRESS_2513, STCD, &start, 1)) {
            puts("Failed to start the USB hub");
            return;
        }
        puts("USB Hub successfully configured.");
    }

    void USB2503Init(void)
    {
        uint8_t status;

        HUB_RESET_0;
        for(int i=0;i<50;i++) {
            (void)U2PIO_HUB_RESET;
        }
        HUB_RESET_1;
        for(int i=0;i<50;i++) {
            (void)U2PIO_HUB_RESET;
        }

        I2C_Driver i2c;

        // Configure the USB hub
		for(int i=0; i<sizeof(hub_registers_2503); i+=2) {
			if (i2c.i2c_write_byte(
					HUB_ADDRESS_2503,
					hub_registers_2503[i],
					hub_registers_2503[i+1])) {
				puts("Failed to write byte to USB hub");
				return;
			}
		}
        puts("USB Hub successfully configured.");
    }
}
