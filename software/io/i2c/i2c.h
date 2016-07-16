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
uint8_t i2c_read_byte(const uint8_t devaddr, const uint8_t regaddr);
uint16_t i2c_read_word(const uint8_t devaddr, const uint16_t regaddr);
int i2c_read_block(const uint8_t devaddr, const uint8_t regaddr, uint8_t *data, const int length);
int i2c_write_byte(const uint8_t devaddr, const uint8_t regaddr, const uint8_t data);
int i2c_write_word(const uint8_t devaddr, const uint16_t regaddr, const uint16_t data);
int i2c_write_block(const uint8_t devaddr, const uint8_t regaddr, const uint8_t *data, const int length);
void USb2512Init(void);

#ifdef __cplusplus
}
#endif
#endif /* I2C_H_ */
