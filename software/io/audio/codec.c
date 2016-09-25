/*
 * codec.c
 *
 *  Created on: Sep 10, 2016
 *      Author: gideon
 */

#include "u2p.h"
#include "i2c.h"
#include <stdio.h>

#define SGTL5000_CHIP_ID            0x0000
#define SGTL5000_CHIP_DIG_POWER     0x0002
#define SGTL5000_CHIP_CLK_CTRL      0x0004
#define SGTL5000_CHIP_I2S_CTRL      0x0006
#define SGTL5000_CHIP_SSS_CTRL      0x000A
#define SGTL5000_CHIP_ADCDAC_CTRL   0x000E
#define SGTL5000_CHIP_DAC_VOL       0x0010
#define SGTL5000_CHIP_PAD_STRENGTH  0x0014
#define SGTL5000_CHIP_ANA_ADC_CTRL  0x0020
#define SGTL5000_CHIP_ANA_HP_CTRL   0x0022
#define SGTL5000_CHIP_ANA_CTRL      0x0024
#define SGTL5000_CHIP_LINREG_CTRL   0x0026
#define SGTL5000_CHIP_REF_CTRL      0x0028
#define SGTL5000_CHIP_MIC_CTRL      0x002A
#define SGTL5000_CHIP_LINE_OUT_CTRL 0x002C
#define SGTL5000_CHIP_LINE_OUT_VOL  0x002E
#define SGTL5000_CHIP_ANA_POWER     0x0030
#define SGTL5000_CHIP_PLL_CTRL      0x0032
#define SGTL5000_CHIP_CLK_TOP_CTRL  0x0034
#define SGTL5000_CHIP_SHORT_CTRL    0x003C

void codec_init(void)
{
    printf("RTC read: ");
    for(int i=0;i<11;i++) {
    	printf("%02x ", i2c_read_byte(0xA2, i));
    } printf("\n");

	i2c_write_word(0x14, SGTL5000_CHIP_ANA_POWER, 0x4260);
    i2c_write_word(0x14, SGTL5000_CHIP_CLK_TOP_CTRL, 0x0800);
    i2c_write_word(0x14, SGTL5000_CHIP_ANA_POWER, 0x4A60);
    i2c_write_word(0x14, SGTL5000_CHIP_REF_CTRL, 0x004E);
    i2c_write_word(0x14, SGTL5000_CHIP_LINE_OUT_CTRL, 0x0304); // VAGCNTL = 0.9 => 0.8 + 4 * 25 mV
    i2c_write_word(0x14, SGTL5000_CHIP_REF_CTRL, 0x004F);
    i2c_write_word(0x14, SGTL5000_CHIP_SHORT_CTRL, 0x1106);
    i2c_write_word(0x14, SGTL5000_CHIP_ANA_POWER, 0x6AFF); // to be reviewed; we don't need to power on HP
    i2c_write_word(0x14, SGTL5000_CHIP_DIG_POWER, 0x0073); // I2S in/out DAP, DAC, ADC power
    i2c_write_word(0x14, SGTL5000_CHIP_LINE_OUT_VOL, 0x0F0F); // see table on page 41

    uint16_t clock_control = i2c_read_word(0x14, SGTL5000_CHIP_CLK_CTRL);
    clock_control = (clock_control & ~0xC) | 0x8; // FS = 48 kHz, MCLK = 256*Fs
    clock_control = (clock_control & ~0x3) | 0x0;
    i2c_write_word(0x14, SGTL5000_CHIP_CLK_CTRL, clock_control);

    i2c_write_word(0x14, SGTL5000_CHIP_SSS_CTRL, 0x0150); // I2S => DAP+DAC. ADC => I2S

    i2c_write_word(0x14, SGTL5000_CHIP_DAC_VOL, 0x3C3C);

    // write CHIP_ADCDAC_CTRL to unmute DAC left and right
    i2c_write_word(0x14, SGTL5000_CHIP_ANA_CTRL, 0x0010);
    i2c_write_word(0x14, SGTL5000_CHIP_ADCDAC_CTRL, 0x0200); // unmute
}

