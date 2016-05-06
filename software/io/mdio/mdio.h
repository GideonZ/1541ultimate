/*
 * mdio.h
 *
 *  Created on: May 5, 2016
 *      Author: gideon
 */

#ifndef SOFTWARE_IO_MDIO_MDIO_H_
#define SOFTWARE_IO_MDIO_MDIO_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void mdio_reset();
uint16_t mdio_read(uint8_t reg);
void mdio_write(uint8_t reg, uint16_t data);
int mdio_get_irq(void);

#ifdef __cplusplus
}
#endif

#endif /* SOFTWARE_IO_MDIO_MDIO_H_ */
