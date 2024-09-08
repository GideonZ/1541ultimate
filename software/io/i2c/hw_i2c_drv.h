#ifndef HW_I2C_DRV_H_
#define HW_I2C_DRV_H_

#include "i2c_drv.h"
#include "hw_i2c.h"

class Hw_I2C_Driver : public I2C_Driver
{
    int channel;
    volatile t_hw_i2c *i2c_regs;
public:
    Hw_I2C_Driver(volatile t_hw_i2c *regs)
    {
        channel = 0;
        i2c_regs = regs;
    }

    void set_channel(int channel);
    void i2c_start();
    void i2c_restart();
    void i2c_stop();
    int i2c_send_byte(const uint8_t byte);
    uint8_t i2c_receive_byte(int ack);
};

#endif /* HW_I2C_DRV_H_ */

