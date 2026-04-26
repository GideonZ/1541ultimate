/*
 * i2cdrv.h
 *
 *  Created on: Oct 26, 2019
 *      Author: gideon
 */

#ifndef I2CDRV_H_
#define I2CDRV_H_

#include <stdint.h>
#include <stdio.h>
#include "u2p.h"
#include "FreeRTOS.h"
#include "semphr.h"

class I2C_Driver
{
    volatile uint8_t *scl, *sda;
    SemaphoreHandle_t mutex;

    void _wait(void);
    virtual void SET_SCL_LOW()  { *scl = 0; }
    virtual void SET_SCL_HIGH() { *scl = 1; }
    virtual void SET_SDA_LOW()  { *sda = 0; }
    virtual void SET_SDA_HIGH() { *sda = 1; }
    virtual uint8_t GET_SCL()   { return *scl; }
    virtual uint8_t GET_SDA()   { return *sda; }

public:
    I2C_Driver()
    {
        scl = &U2PIO_SCL;
        sda = &U2PIO_SDA;
        mutex = xSemaphoreCreateMutex();
    }

    virtual void set_channel(int channel) {}
    virtual void i2c_start();
    virtual void i2c_restart();
    virtual void i2c_stop();
    virtual int i2c_send_byte(const uint8_t byte);
    virtual uint8_t i2c_receive_byte(int ack);
    virtual bool enable_scan(bool enable, bool automatic) { return false; }

    bool i2c_lock(const char *caller)
    {
        static const char *locker = "none";
        BaseType_t gotLock = xSemaphoreTake(mutex, 5000);
        if (gotLock) {
            enable_scan(false, true);
        } else {
            printf("========= i2c_lock: failed to get lock. Requester: %s, Current Lock: %s\n", caller, locker);
        }      
        locker = caller;
        return gotLock == pdTRUE;
    }

    void i2c_unlock(void)
    {
        enable_scan(true, true);
        xSemaphoreGive(mutex);
    }

    bool i2c_probe(const uint8_t devaddr);
    void i2c_scan_bus(void);
    uint8_t i2c_read_byte(const uint8_t devaddr, const uint8_t regaddr, int *res);
    uint16_t i2c_read_word(const uint8_t devaddr, const uint16_t regaddr);
    int i2c_read_block(const uint8_t devaddr, const uint8_t regaddr, uint8_t *data, const int length);
    int i2c_read_block_ext(const uint8_t page, const uint8_t devaddr, const uint8_t regaddr, uint8_t *data, const int length);
    int i2c_write_byte(const uint8_t devaddr, const uint8_t regaddr, const uint8_t data);
    int i2c_write_word(const uint8_t devaddr, const uint16_t regaddr, const uint16_t data);
    int i2c_write_nau(const uint8_t devaddr, const uint8_t regaddr, const uint16_t data);
    int i2c_write_block(const uint8_t devaddr, const uint8_t regaddr, const uint8_t *data, const int length);
    void i2c_read_raw_16(const uint8_t devaddr, uint16_t *dest, int count);
};

#define I2C_CHANNEL_HDMI 0
#define I2C_CHANNEL_1V8  1
#define I2C_CHANNEL_3V3  2

extern I2C_Driver *i2c;

#endif /* I2CDEV_H_ */
