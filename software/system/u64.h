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
#define U64_IO_BASE  0xA0000400
#define VID_IO_BASE  0xA0040000
#define C64_IO_BASE  0xA0080000
#define C64_SID_BASE 0xA0090000
#endif

#define U64_HDMI_REG       (*(volatile uint8_t *)(U64_IO_BASE + 0x00))
#define U64_POWER_REG      (*(volatile uint8_t *)(U64_IO_BASE + 0x01))
#define U64_RESTORE_REG    (*(volatile uint8_t *)(U64_IO_BASE + 0x02))
#define U64_AUDIO_SEL_REG  (*(volatile uint8_t *)(U64_IO_BASE + 0x03))
#define U64_HDMI_PLL_RESET (*(volatile uint8_t *)(U64_IO_BASE + 0x04))

#define U64_HDMI_DDC_ENABLE     0x20
#define U64_HDMI_DDC_DISABLE    0x10
#define U64_HDMI_HPD_RESET      0x08
#define U64_HDMI_HPD_WASLOW		0x08
#define U64_HDMI_HPD_CURRENT    0x04

#define U64_POWER_OFF_1			0x2B
#define U64_POWER_OFF_2			0xB2

#define C64_SCANLINES    (*(volatile uint8_t *)(C64_IO_BASE + 0x00))
#define C64_SID1_BASE    (*(volatile uint8_t *)(C64_IO_BASE + 0x08))
#define C64_SID2_BASE    (*(volatile uint8_t *)(C64_IO_BASE + 0x09))
#define C64_EMUSID1_BASE (*(volatile uint8_t *)(C64_IO_BASE + 0x0A))
#define C64_EMUSID2_BASE (*(volatile uint8_t *)(C64_IO_BASE + 0x0B))
#define C64_SID1_MASK	 (*(volatile uint8_t *)(C64_IO_BASE + 0x0C))
#define C64_SID2_MASK	 (*(volatile uint8_t *)(C64_IO_BASE + 0x0D))
#define C64_EMUSID1_MASK (*(volatile uint8_t *)(C64_IO_BASE + 0x0E))
#define C64_EMUSID2_MASK (*(volatile uint8_t *)(C64_IO_BASE + 0x0F))

#define VID_HSYNCPOL     (*(volatile uint8_t *)(VID_IO_BASE + 0x00))
#define VID_HSYNCTIME    (*(volatile uint8_t *)(VID_IO_BASE + 0x01)) // in steps of 2 clocks
#define VID_HBACKPORCH   (*(volatile uint8_t *)(VID_IO_BASE + 0x02)) // in steps of 2 clocks
#define VID_HACTIVE      (*(volatile uint8_t *)(VID_IO_BASE + 0x03)) // in steps of 8 clocks
#define VID_HFRONTPORCH  (*(volatile uint8_t *)(VID_IO_BASE + 0x04)) // in steps of 2 clocks
#define VID_HREPETITION  (*(volatile uint8_t *)(VID_IO_BASE + 0x05)) // factor 2 on or off only
#define VID_VSYNCPOL     (*(volatile uint8_t *)(VID_IO_BASE + 0x08))
#define VID_VSYNCTIME    (*(volatile uint8_t *)(VID_IO_BASE + 0x09))
#define VID_VBACKPORCH   (*(volatile uint8_t *)(VID_IO_BASE + 0x0A))
#define VID_VACTIVE      (*(volatile uint8_t *)(VID_IO_BASE + 0x0B)) // in steps of 8 lines
#define VID_VFRONTPORCH  (*(volatile uint8_t *)(VID_IO_BASE + 0x0C))

#define VID_Y		     (*(volatile uint8_t *)(VID_IO_BASE + 0x07))
#define VID_VIC			 (*(volatile uint8_t *)(VID_IO_BASE + 0x0D))
#define VID_REPCN		 (*(volatile uint8_t *)(VID_IO_BASE + 0x0E))
#define VID_ITCYQ		 (*(volatile uint8_t *)(VID_IO_BASE + 0x0F))

#endif /* U64_H_ */
