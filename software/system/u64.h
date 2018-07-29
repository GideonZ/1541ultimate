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
#define U64_ROMS_BASE    0xA00A0000
#define U64_BASIC_BASE   (U64_ROMS_BASE + 0x0000)
#define U64_KERNAL_BASE  (U64_ROMS_BASE + 0x2000)
#define U64_CHARROM_BASE (U64_ROMS_BASE + 0x4000)
#endif

#define U64_HDMI_REG       (*(volatile uint8_t *)(U64_IO_BASE + 0x00))
#define U64_POWER_REG      (*(volatile uint8_t *)(U64_IO_BASE + 0x01))
#define U64_RESTORE_REG    (*(volatile uint8_t *)(U64_IO_BASE + 0x02))
#define U64_AUDIO_SEL_REG  (*(volatile uint8_t *)(U64_IO_BASE + 0x03))
#define U64_HDMI_PLL_RESET (*(volatile uint8_t *)(U64_IO_BASE + 0x04))
#define U64_WIFI_CONTROL   (*(volatile uint8_t *)(U64_IO_BASE + 0x05))
#define U64_EXT_I2C_SCL    (*(volatile uint8_t *)(U64_IO_BASE + 0x06))
#define U64_EXT_I2C_SDA    (*(volatile uint8_t *)(U64_IO_BASE + 0x07))
#define U64_HDMI_ENABLE    (*(volatile uint8_t *)(U64_IO_BASE + 0x08))

#define U64_HDMI_DDC_ENABLE     0x20
#define U64_HDMI_DDC_DISABLE    0x10
#define U64_HDMI_HPD_RESET      0x08
#define U64_HDMI_HPD_WASLOW		0x08
#define U64_HDMI_HPD_CURRENT    0x04

#define U64_POWER_OFF_1			0x2B
#define U64_POWER_OFF_2			0xB2

#define C64_SCANLINES    (*(volatile uint8_t *)(C64_IO_BASE + 0x00))
#define C64_VIDEOFORMAT  (*(volatile uint8_t *)(C64_IO_BASE + 0x01))
#define C64_LINE_PHASE   (*(volatile uint8_t *)(C64_IO_BASE + 0x02))
#define C64_PHASE_INCR   (*(volatile uint8_t *)(C64_IO_BASE + 0x03))
#define C64_LUMA_DELAY   (*(volatile uint8_t *)(C64_IO_BASE + 0x04))
#define C64_CHROMA_DELAY (*(volatile uint8_t *)(C64_IO_BASE + 0x05))
#define C64_BURST_PHASE  (*(volatile uint8_t *)(C64_IO_BASE + 0x06))
#define C64_VIC_TEST     (*(volatile uint8_t *)(C64_IO_BASE + 0x07))
#define C64_SID1_BASE    (*(volatile uint8_t *)(C64_IO_BASE + 0x08))
#define C64_SID2_BASE    (*(volatile uint8_t *)(C64_IO_BASE + 0x09))
#define C64_EMUSID1_BASE (*(volatile uint8_t *)(C64_IO_BASE + 0x0A))
#define C64_EMUSID2_BASE (*(volatile uint8_t *)(C64_IO_BASE + 0x0B))
#define C64_SID1_MASK	 (*(volatile uint8_t *)(C64_IO_BASE + 0x0C))
#define C64_SID2_MASK	 (*(volatile uint8_t *)(C64_IO_BASE + 0x0D))
#define C64_EMUSID1_MASK (*(volatile uint8_t *)(C64_IO_BASE + 0x0E))
#define C64_EMUSID2_MASK (*(volatile uint8_t *)(C64_IO_BASE + 0x0F))
#define C64_CORE_VERSION (*(volatile uint8_t *)(C64_IO_BASE + 0x10))
#define C64_SID1_EN  	 (*(volatile uint8_t *)(C64_IO_BASE + 0x11))
#define C64_SID2_EN  	 (*(volatile uint8_t *)(C64_IO_BASE + 0x12))
#define C64_PADDLE_EN  	 (*(volatile uint8_t *)(C64_IO_BASE + 0x13))

typedef struct {
    uint8_t VID_HSYNCPOL;
    uint8_t VID_HSYNCTIME;
    uint8_t VID_HBACKPORCH;
    uint8_t VID_HACTIVE;
    uint8_t VID_HFRONTPORCH;
    uint8_t VID_HREPETITION;
    uint8_t padding; // 6
    uint8_t VID_Y;
    uint8_t VID_VSYNCPOL;
    uint8_t VID_VSYNCTIME;
    uint8_t VID_VBACKPORCH;
    uint8_t VID_VACTIVE;
    uint8_t VID_VFRONTPORCH;
    uint8_t VID_VIC;
    uint8_t VID_REPCN;
    uint8_t VID_IRCYQ;
} t_video_timing_regs;

#endif /* U64_H_ */
