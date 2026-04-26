#include "hw_i2c_drv.h"
#include <stdio.h>

#define _wait_busy() while(this->i2c_regs->status & I2C_STATUS_BUSY) { __asm__("nop"); }
#define wait_busy(st) do { st = this->i2c_regs->status; } while(st & I2C_STATUS_BUSY)

void Hw_I2C_Driver :: set_channel(int channel)
{
    this->channel = channel;
}

void Hw_I2C_Driver :: i2c_start()
{
    i2c_regs->channel = this->channel;
    _wait_busy();
}

void Hw_I2C_Driver :: i2c_restart()
{
    i2c_regs->repeated_start = 1;                
    _wait_busy();
}


void Hw_I2C_Driver :: i2c_stop()
{
    i2c_regs->stop = 1;
    _wait_busy();
}

int Hw_I2C_Driver :: i2c_send_byte(const uint8_t byte)
{
    i2c_regs->data_out = byte;
    uint8_t st;
    wait_busy(st);
    return (st & I2C_STATUS_ERROR) ? -1 : 0;
}

uint8_t Hw_I2C_Driver :: i2c_receive_byte(int ack)
{
    if(ack) {
        i2c_regs->receive_ack = 1;
    } else {
        i2c_regs->receive = 1;
    }
    _wait_busy();
    return i2c_regs->data_out;
}

bool Hw_I2C_Driver :: enable_scan(bool enable, bool automatic)
{
    if (automatic) {
        if (scanning) { // was it manually enabled?
            i2c_regs->scan_enable = enable ? 1 : 0;
            if (!enable) {
                vTaskDelay(2);
            }
        }
    } else { // manual call to this function
        i2c_regs->scan_enable = enable ? 1 : 0;
        scanning = enable;
        if (!enable) {
            vTaskDelay(2);
        }
    }
    return true;
}
