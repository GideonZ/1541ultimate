#ifndef HW_I2C_DRV_H_
#define HW_I2C_DRV_H_

#include "i2c_drv.h"
#include "hw_i2c.h"

class Hw_I2C_Driver : public I2C_Driver
{
    int channel;
    volatile t_hw_i2c *i2c_regs;
    bool scanning;

public:
    Hw_I2C_Driver(volatile t_hw_i2c *regs)
    {
        i2c_regs = regs;
        i2c_regs->scan_enable = 0;
        channel = 0;
        scanning = false;
    }

    void set_channel(int channel);
    void i2c_start();
    void i2c_restart();
    void i2c_stop();
    int i2c_send_byte(const uint8_t byte);
    uint8_t i2c_receive_byte(int ack);
    bool enable_scan(bool enable, bool automatic);
};

#endif /* HW_I2C_DRV_H_ */

