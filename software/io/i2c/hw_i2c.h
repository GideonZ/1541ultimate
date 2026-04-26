#ifndef HW_I2C_H_
#define HW_I2C_H_

#include <stdint.h>
typedef struct {
    uint8_t data_out;
    uint8_t status;
    uint8_t repeated_start;
    uint8_t stop;
    uint8_t receive;
    uint8_t receive_ack;
    uint8_t channel;
    uint8_t soft_reset;
    uint8_t scan_enable;
} t_hw_i2c;

#define I2C_STATUS_BUSY         0x80
#define I2C_STATUS_STARTED      0x01
#define I2C_STATUS_ERROR        0x04
#define I2C_STATUS_MISSED_WRITE 0x08

#define i2c_spin_busy(x) while((x)->status & I2C_STATUS_BUSY) { __asm__("nop"); }

#endif /* HW_I2C_H_ */
