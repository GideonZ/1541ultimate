/*
 * u64.h
 *
 *  Created on: Jul 29, 2017
 *      Author: Gideon
 */

#ifndef U64_H_
#define U64_H_

#include <stdint.h>
#include "u2p.h"

#define U64_CLOCKMEAS     (U2P_IO_BASE + 0x0200)
#define U64_IO_BASE       (U2P_IO_BASE + 0x0400)
#define U64_AUDIO_MIXER   (U2P_IO_BASE + 0x0500)
#define U64_SPEAKER_MIXER (U2P_IO_BASE + 0x0540)
#define U64_RESAMPLER     (U2P_IO_BASE + 0x0580)
#define C64_IO_LED        (U2P_IO_BASE + 0x0600)

#define VID_IO_BASE       (U2P_IO_BASE + 0x40000)
#define U64_OVERLAY_BASE  (VID_IO_BASE + 0x04000)

// U64-II
#define U64II_OVERLAY_BASE (VID_IO_BASE + 0x0000)
#define U64II_HDMI_REGS    (VID_IO_BASE + 0x4000)
#define U64II_HDMI_PALETTE (VID_IO_BASE + 0x5000)
#define U64II_CROPPER_BASE (VID_IO_BASE + 0x8000)

#define U64II_CHARGEN_REGS          ((volatile t_chargen_registers *)(U64II_OVERLAY_BASE + 0x0000))
#define U64II_CHARGEN_SCREEN_RAM    (U64II_OVERLAY_BASE + 0x1000)
#define U64II_CHARGEN_COLOR_RAM     (U64II_OVERLAY_BASE + 0x2000)

#define U64II_HW_I2C_BASE (U2P_IO_BASE + 0x0700)
#define U64II_BLINGBOARD_KEYB (U2P_IO_BASE + 0x0800)
#define U64II_BLINGBOARD_LEDS (U2P_IO_BASE + 0x0900)

#define BLING_RX_DATA  (*(volatile uint8_t *)(U64II_BLINGBOARD_KEYB + 0))
#define BLING_RX_GET   (*(volatile uint8_t *)(U64II_BLINGBOARD_KEYB + 1))
#define BLING_RX_FLAGS (*(volatile uint8_t *)(U64II_BLINGBOARD_KEYB + 2))
#define BLING_RX_IRQEN (*(volatile uint8_t *)(U64II_BLINGBOARD_KEYB + 3))

#define BLINGBOARD_INSTALLED (BLING_RX_FLAGS & 0x04)
//#define BLINGBOARD_INSTALLED (1)

// end U64-II


#define C64_IO_BASE  (U2P_IO_BASE + 0x80000)
#define C64_PALETTE  (U2P_IO_BASE + 0x80800)
#define C64_PLD_ACC  (U2P_IO_BASE + 0x81000)
#define C64_IO_DEBUG (U2P_IO_BASE + 0x81800)

#define C64_SID_BASE     (U2P_IO_BASE + 0x84000)
#define U64_ROMS_BASE    (U2P_IO_BASE + 0x88000)
#define U64_CHARROM_BASE (U2P_IO_BASE + 0x8C000)
#define U64_UDP_BASE     (U2P_IO_BASE + 0x90000)

#define U64_BASIC_BASE   (U64_ROMS_BASE + 0x0000)
#define U64_KERNAL_BASE  (U64_ROMS_BASE + 0x2000)

//#define U64_RAM_BASE     0xA00A0000 // No longer available -> use DMA channel with C64_DMA_MEMONLY

#define U64_HDMI_REG       (*(volatile uint8_t *)(U64_IO_BASE + 0x00))
#define U64_POWER_REG      (*(volatile uint8_t *)(U64_IO_BASE + 0x01))
#define U64_RESTORE_REG    (*(volatile uint8_t *)(U64_IO_BASE + 0x02))
#define U64_CART_DETECT    (*(volatile uint8_t *)(U64_IO_BASE + 0x03)) // Inputs Game (bit 0) and Exrom (bit 1) lines
#define U64_HDMI_PLL_RESET (*(volatile uint8_t *)(U64_IO_BASE + 0x04))
#define U64_USERPORT_EN    (*(volatile uint8_t *)(U64_IO_BASE + 0x05))
#define U64II_KEYB_JOY     (*(volatile uint8_t *)(U64_IO_BASE + 0x06))
#define U64_HDMI_ENABLE    (*(volatile uint8_t *)(U64_IO_BASE + 0x08))
#define U64_INT_CONNECTORS (*(volatile uint8_t *)(U64_IO_BASE + 0x09))
#define U64II_KEYB_COL     (*(volatile uint8_t *)(U64_IO_BASE + 0x0A))
#define U64II_KEYB_ROW     (*(volatile uint8_t *)(U64_IO_BASE + 0x0B))
#define U64_LEDSTRIP_EN    (*(volatile uint8_t *)(U64_IO_BASE + 0x0C)) // Write a 1 to make CIA_PWM pins become LED strip control pins
#define U64_PWM_DUTY       (*(volatile uint8_t *)(U64_IO_BASE + 0x0D)) // any value between 00 (off) and FF (nearly full phase)
#define U64_CASELED_SELECT (*(volatile uint8_t *)(U64_IO_BASE + 0x0E)) // Two nibbles with selectors
#define U64_ETHSTREAM_ENA  (*(volatile uint8_t *)(U64_IO_BASE + 0x0F)) // Ethernet stream generators 0 = vic, 1 = audio, 2 = bus, 3 = iec

#define U64_RESAMPLE_RESET (*(volatile uint8_t *)(U64_RESAMPLER + 0x04))
#define U64_RESAMPLE_LABOR (*(volatile uint8_t *)(U64_RESAMPLER + 0x08))
#define U64_RESAMPLE_FLUSH (*(volatile uint8_t *)(U64_RESAMPLER + 0x09))
#define U64_RESAMPLE_DATA  (*(volatile uint32_t *)(U64_RESAMPLER + 0x00))

#define U64_CLOCK_FREQ     (*(volatile uint32_t *)(U64_CLOCKMEAS + 0x00))

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

#define U64_DEBUG_REGISTER (*(volatile uint8_t *)C64_IO_DEBUG)

#define C64_SCANLINES    (*(volatile uint8_t *)(C64_IO_BASE + 0x00))
#define C64_VIDEOFORMAT  (*(volatile uint8_t *)(C64_IO_BASE + 0x01)) // bit 3 selects NTSC clock ref, bit 2 selects RGB, bit 1 selects 60 Hz mode, bit 0 selects NTSC color encoding
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
    uint8_t resync;
    uint8_t VID_Y;
    uint8_t VID_VSYNCPOL; // 8
    uint8_t VID_VSYNCTIME;
    uint8_t VID_VBACKPORCH;
    uint8_t VID_VACTIVE;
    uint8_t VID_VFRONTPORCH;
    uint8_t VID_VIC;
    uint8_t VID_REPCN;
    uint8_t VID_IRCYQ;
    uint8_t x_offset; // 16
    uint8_t tx_swing;
    uint8_t gearbox;
    uint8_t hscaler;
    uint8_t vscaler;
    uint8_t VID_A;
    uint8_t VID_B;
    uint8_t VID_S;
    uint8_t VID_C; // 24
    uint8_t VID_M;
    uint8_t VID_R;
    uint8_t VID_EC;
    uint8_t VID_SC;
    uint8_t VID_YQ;
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

typedef enum {
    e_480p_576p = 0,
    e_1280x720,
    e_1920x1080,

    e_800x600,
    e_1024x768,
    e_1280x1024,

    e_auto_edid,
} t_hdmi_mode;

typedef enum 
{
    e_INTPLL_1x,
    e_INTPLL_6_5,
    e_INTPLL_25_13,
    e_INTPLL_45_52,
    e_INTPLL_55_32,
} t_intpll_mode;

#endif /* U64_H_ */
