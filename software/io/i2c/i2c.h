/*
 * i2c.h
 *
 *  Created on: Apr 30, 2016
 *      Author: gideon
 */

#ifndef I2C_H_
#define I2C_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void i2c_scan_bus(void);
uint8_t i2c_read_byte(uint8_t devaddr, uint8_t regaddr);
int i2c_read_block(uint8_t devaddr, uint8_t regaddr, uint8_t *data, int length);
int i2c_write_byte(uint8_t devaddr, uint8_t regaddr, uint8_t data);
int i2c_write_block(uint8_t devaddr, uint8_t regaddr, uint8_t *data, int length);
void USb2512Init(void);

#ifdef __cplusplus
}
#endif
#endif /* I2C_H_ */
