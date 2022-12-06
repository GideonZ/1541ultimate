/*
 * i2cdrv.h
 *
 *  Created on: Oct 26, 2019
 *      Author: gideon
 */

#ifndef I2CDRV_H_
#define I2CDRV_H_

#include <stdint.h>
#include "u2p.h"

class I2C_Driver
{
    virtual void _wait(void);
    virtual void SET_SCL_LOW()  { U2PIO_SET_SCL = 0; }
    virtual void SET_SCL_HIGH() { U2PIO_SET_SCL = 1; }
    virtual void SET_SDA_LOW()  { U2PIO_SET_SDA = 0; }
    virtual void SET_SDA_HIGH() { U2PIO_SET_SDA = 1; }
    virtual uint8_t GET_SCL()   { return U2PIO_GET_SCL; }
    virtual uint8_t GET_SDA()   { return U2PIO_GET_SDA; }

public:
    I2C_Driver() {}

    void i2c_start();
    void i2c_restart();
    void i2c_stop();
    int i2c_send_byte(const uint8_t byte);
    uint8_t i2c_receive_byte(int ack);

    void i2c_scan_bus(void);
    uint8_t i2c_read_byte(const uint8_t devaddr, const uint8_t regaddr, int *res);
    uint16_t i2c_read_word(const uint8_t devaddr, const uint16_t regaddr);
    int i2c_read_block(const uint8_t devaddr, const uint8_t regaddr, uint8_t *data, const int length);
    int i2c_write_byte(const uint8_t devaddr, const uint8_t regaddr, const uint8_t data);
    int i2c_write_word(const uint8_t devaddr, const uint16_t regaddr, const uint16_t data);
    int i2c_write_block(const uint8_t devaddr, const uint8_t regaddr, const uint8_t *data, const int length);
    void i2c_read_raw_16(const uint8_t devaddr, uint16_t *dest, int count);
};

extern "C" {
    void USb2512Init(void);
}

#endif /* I2CDEV_H_ */
