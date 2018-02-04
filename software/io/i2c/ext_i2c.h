/*
 * i2c.h
 *
 *  Created on: Apr 30, 2016
 *      Author: gideon
 */

#ifndef EXT_I2C_H_
#define EXT_I2C_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void ext_i2c_scan_bus(void);
uint8_t ext_i2c_read_byte(const uint8_t devaddr, const uint8_t regaddr, int *res);
uint16_t ext_i2c_read_word(const uint8_t devaddr, const uint16_t regaddr);
int ext_i2c_read_block(const uint8_t devaddr, const uint8_t regaddr, uint8_t *data, const int length);
int ext_i2c_write_byte(const uint8_t devaddr, const uint8_t regaddr, const uint8_t data);
int ext_i2c_write_word(const uint8_t devaddr, const uint16_t regaddr, const uint16_t data);
int ext_i2c_write_block(const uint8_t devaddr, const uint8_t regaddr, const uint8_t *data, const int length);

#ifdef __cplusplus
}
#endif
#endif /* I2C_H_ */
