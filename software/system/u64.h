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
#define U64_IO_BASE     0xA0000400
#define U64_AUDIO_MIXER 0xA0000500
#define U64_WIFI_UART   0xA0000600
#define U64_RESAMPLER   0xA0000700

#define VID_IO_BASE  0xA0040000

#define C64_IO_BASE  0xA0080000
#define C64_PALETTE  0xA0080800
#define C64_IO_LED   0xA0081000
#define C64_PLD_ACC  0xA0081800
#define C64_IO_DEBUG 0xA0082000

#define LEDSTRIP_DATA ( (volatile uint8_t *)(C64_IO_LED))
#define LEDSTRIP_FROM (*(volatile uint8_t *)(C64_IO_LED + 0x1FE))
#define LEDSTRIP_LEN  (*(volatile uint8_t *)(C64_IO_LED + 0x1FF))
#define U64_DEBUG_REGISTER (*(volatile uint8_t *)C64_IO_DEBUG)

#define C64_SID_BASE     0xA0084000
#define U64_ROMS_BASE    0xA0088000
#define U64_CHARROM_BASE 0xA008C000
#define U64_UDP_BASE     0xA0090000

#define U64_BASIC_BASE   (U64_ROMS_BASE + 0x0000)
#define U64_KERNAL_BASE  (U64_ROMS_BASE + 0x2000)

//#define U64_RAM_BASE     0xA00A0000 // No longer available -> use DMA channel with C64_DMA_MEMONLY
#endif

#define U64_HDMI_REG       (*(volatile uint8_t *)(U64_IO_BASE + 0x00))
#define U64_POWER_REG      (*(volatile uint8_t *)(U64_IO_BASE + 0x01))
#define U64_RESTORE_REG    (*(volatile uint8_t *)(U64_IO_BASE + 0x02))
#define U64_HDMI_PLL_RESET (*(volatile uint8_t *)(U64_IO_BASE + 0x04))
#define U64_WIFI_CONTROL   (*(volatile uint8_t *)(U64_IO_BASE + 0x05))
#define U64_EXT_I2C_SCL    (*(volatile uint8_t *)(U64_IO_BASE + 0x06))
#define U64_EXT_I2C_SDA    (*(volatile uint8_t *)(U64_IO_BASE + 0x07))
#define U64_HDMI_ENABLE    (*(volatile uint8_t *)(U64_IO_BASE + 0x08))
#define U64_INT_CONNECTORS (*(volatile uint8_t *)(U64_IO_BASE + 0x09))
#define U64_CART_DETECT    (*(volatile uint8_t *)(U64_IO_BASE + 0x0A)) // Inputs Game (bit 0) and Exrom (bit 1) lines
#define U64_MB_RESET       (*(volatile uint8_t *)(U64_IO_BASE + 0x0B)) // Write a 0 to start the microblaze, if available
#define U64_LEDSTRIP_EN    (*(volatile uint8_t *)(U64_IO_BASE + 0x0C)) // Write a 1 to make CIA_PWM pins become LED strip control pins
#define U64_PWM_DUTY       (*(volatile uint8_t *)(U64_IO_BASE + 0x0D)) // any value between 00 (off) and FF (nearly full phase)
#define U64_CASELED_SELECT (*(volatile uint8_t *)(U64_IO_BASE + 0x0E)) // Two nibbles with selectors
#define U64_ETHSTREAM_ENA  (*(volatile uint8_t *)(U64_IO_BASE + 0x0F)) // Ethernet stream generators 0 = vic, 1 = audio, 2 = bus, 3 = iec

#define U64_RESAMPLE_RESET (*(volatile uint8_t *)(U64_RESAMPLER + 0x04))
#define U64_RESAMPLE_LABOR (*(volatile uint8_t *)(U64_RESAMPLER + 0x08))
#define U64_RESAMPLE_FLUSH (*(volatile uint8_t *)(U64_RESAMPLER + 0x09))
#define U64_RESAMPLE_DATA  (*(volatile uint32_t *)(U64_RESAMPLER + 0x00))

#define U64_CARTRIDGE_AUTO     0
#define U64_CARTRIDGE_INTERNAL 1
#define U64_CARTRIDGE_EXTERNAL 2

#define U64_HDMI_DDC_ENABLE     0x20
#define U64_HDMI_DDC_DISABLE    0x10
#define U64_HDMI_HPD_RESET      0x08
#define U64_HDMI_HPD_WASLOW		0x08
#define U64_HDMI_HPD_CURRENT    0x04

#define U64_POWER_OFF_1			0x2B
#define U64_POWER_OFF_2			0xB2

#define C64_SCANLINES    (*(volatile uint8_t *)(C64_IO_BASE + 0x00))
#define C64_VIDEOFORMAT  (*(volatile uint8_t *)(C64_IO_BASE + 0x01))
#define C64_TURBOREGS_EN (*(volatile uint8_t *)(C64_IO_BASE + 0x02))
#define C64_DMA_MEMONLY  (*(volatile uint8_t *)(C64_IO_BASE + 0x03))
#define C64_PHASE_INCR   (*(volatile uint8_t *)(C64_IO_BASE + 0x05))
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
#define C64_STEREO_ADDRSEL (*(volatile uint8_t *)(C64_IO_BASE + 0x14))
#define C64_PADDLE_SWAP   (*(volatile uint8_t *)(C64_IO_BASE + 0x1A))
#define C64_EMUSID1_WAVES (*(volatile uint8_t *)(C64_IO_BASE + 0x20))
#define C64_EMUSID2_WAVES (*(volatile uint8_t *)(C64_IO_BASE + 0x21))
#define C64_EMUSID1_RES   (*(volatile uint8_t *)(C64_IO_BASE + 0x22))
#define C64_EMUSID2_RES   (*(volatile uint8_t *)(C64_IO_BASE + 0x23))
#define C64_EMUSID1_DIGI  (*(volatile uint8_t *)(C64_IO_BASE + 0x27))
#define C64_EMUSID2_DIGI  (*(volatile uint8_t *)(C64_IO_BASE + 0x28))
#define C64_EMUSID_SPLIT  (*(volatile uint8_t *)(C64_IO_BASE + 0x29))
#define C64_BUS_BRIDGE    (*(volatile uint8_t *)(C64_IO_BASE + 0x2A))
#define C64_BUS_INTERNAL  (*(volatile uint8_t *)(C64_IO_BASE + 0x2B))
#define C64_BUS_EXTERNAL  (*(volatile uint8_t *)(C64_IO_BASE + 0x2C))
#define C64_SPEED_PREFER  (*(volatile uint8_t *)(C64_IO_BASE + 0x2D))
#define C64_SPEED_UPDATE  (*(volatile uint8_t *)(C64_IO_BASE + 0x2E))

/*
#define C64_PLD_PORTA      ((volatile uint8_t *)(C64_IO_BASE + 0x16))
#define C64_PLD_PORTB      ((volatile uint8_t *)(C64_IO_BASE + 0x17))
#define C64_PLD_STATE0    (*(volatile uint8_t *)(C64_IO_BASE + 0x18))
#define C64_PLD_STATE1    (*(volatile uint8_t *)(C64_IO_BASE + 0x19))
#define C64_PLD_SIDCTRL2  (*(volatile uint8_t *)(C64_IO_BASE + 0x3A))
#define C64_PLD_SIDCTRL1  (*(volatile uint8_t *)(C64_IO_BASE + 0x3B))
#define C64_PLD_JOYCTRL   (*(volatile uint8_t *)(C64_IO_BASE + 0x3E))
*/

#define C64_PLD_PORTA      ((volatile uint8_t *)(C64_PLD_ACC + 0x10))
#define C64_PLD_PORTB      ((volatile uint8_t *)(C64_PLD_ACC + 0x11))
#define C64_PLD_STATE0    (*(volatile uint8_t *)(C64_PLD_ACC + 0x00))
#define C64_PLD_STATE1    (*(volatile uint8_t *)(C64_PLD_ACC + 0x01))
#define C64_PLD_SIDCTRL2  (*(volatile uint8_t *)(C64_PLD_ACC + 0x1A))
#define C64_PLD_SIDCTRL1  (*(volatile uint8_t *)(C64_PLD_ACC + 0x1B))
#define C64_PLD_JOYCTRL   (*(volatile uint8_t *)(C64_PLD_ACC + 0x1E))

#define C64_VOICE_ADSR(x) (*(volatile uint8_t *)(C64_IO_BASE + 0x80 + x))

#define VIDEO_FMT_NTSC_ENCODING 0x01
#define VIDEO_FMT_60_HZ         0x02
#define VIDEO_FMT_RGB_OUTPUT    0x04
#define VIDEO_FMT_NTSC_FREQ     0x08
#define VIDEO_FMT_CYCLES_63     0x00
#define VIDEO_FMT_CYCLES_64     0x10
#define VIDEO_FMT_CYCLES_65     0x20
#define VIDEO_FMT_RESET_BURST   0x40

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

typedef enum {
    e_PAL_50 = 0,
    e_NTSC_60,
    e_PAL_60,
    e_NTSC_50,
    e_PAL_60_lock,
    e_NTSC_50_lock,
    e_NOT_SET,
} t_video_mode;

#endif /* U64_H_ */
