/*
 * i2cdrv.h
 *
 *  Created on: Oct 26, 2019
 *      Author: gideon
 */

#ifndef I2CDRV_SOCKETTEST_H_
#define I2CDRV_SOCKETTEST_H_

#include "i2c_drv.h"
#include "u64_tester.h"

//#define SOCKET1_SCL *((volatile uint8_t *)(U64TESTER_SID1_BASE + 1))
//#define SOCKET1_SDA *((volatile uint8_t *)(U64TESTER_SID1_BASE + 0))
//#define SOCKET2_SCL *((volatile uint8_t *)(U64TESTER_SID2_BASE + 1))
//#define SOCKET2_SDA *((volatile uint8_t *)(U64TESTER_SID2_BASE + 0))

class I2C_Driver_SocketTest : public I2C_Driver
{
    volatile socket_tester_t *tester;
    void SET_SCL_LOW()  { tester->scl = 0; }
    void SET_SCL_HIGH() { tester->scl = 1; }
    void SET_SDA_LOW()  { tester->sda = 0; }
    void SET_SDA_HIGH() { tester->sda = 1; }
    uint8_t GET_SCL()   { return tester->scl; }
    uint8_t GET_SDA()   { return tester->sda; }
public:
    I2C_Driver_SocketTest(volatile socket_tester_t *t)
    {
        tester = t;
    }

    int read_results(const uint8_t devaddr, uint8_t *res) {
        i2c_start();
        int result = i2c_send_byte(devaddr | 1);
        if (!result) {
            res[0] = i2c_receive_byte(1);
            res[1] = i2c_receive_byte(1);
            res[2] = i2c_receive_byte(1);
            res[3] = i2c_receive_byte(0);
        }
        i2c_stop();
        return result;
    }
};

#endif /* I2CDEV_H_ */
