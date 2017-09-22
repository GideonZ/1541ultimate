/*
 * u64.h
 *
 *  Created on: Jul 29, 2017
 *      Author: Gideon
 */

#ifndef U64_H_
#define U64_H_

#include <stdint.h>

#ifndef U64_IO_BASE
#define U64_IO_BASE 0xA0000400
#endif

#define U64_HDMI_REG    (*(volatile uint8_t *)(U64_IO_BASE + 0x00))
#define U64_POWER_REG   (*(volatile uint8_t *)(U64_IO_BASE + 0x01))
#define U64_RESTORE_REG (*(volatile uint8_t *)(U64_IO_BASE + 0x02))

#define U64_HDMI_DDC_ENABLE     0x20
#define U64_HDMI_DDC_DISABLE    0x10
#define U64_HDMI_HPD_RESET      0x08
#define U64_HDMI_HPD_WASLOW		0x08
#define U64_HDMI_HPD_CURRENT    0x04

#define U64_POWER_OFF_1			0x2B
#define U64_POWER_OFF_2			0xB2

#endif /* U64_H_ */
