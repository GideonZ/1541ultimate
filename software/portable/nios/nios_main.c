/*
 * nios_main.c
 *
 *  Created on: May 6, 2016
 *      Author: gideon
 */

#include <stdint.h>
#include <stdio.h>

#include "i2c.h"
#include "mdio.h"
#include "alt_types.h"
#include "dump_hex.h"
#include "FreeRTOS.h"
#include "task.h"
#include "iomap.h"
#include "itu.h"
#include "profiler.h"


void RmiiRxInterruptHandler(void);
uint8_t command_interface_irq(void);
uint8_t tape_recorder_irq(void);
uint8_t usb_irq(void);

static void ituIrqHandler(void *context)
{
    static uint8_t pending;

	PROFILER_SUB = 1;
/* Which interrupts are pending? */
	pending = ioRead8(ITU_IRQ_ACTIVE);
	ioWrite8(ITU_IRQ_CLEAR, pending);

	BaseType_t do_switch = pdFALSE;

	if (pending & 0x20) {
		RmiiRxInterruptHandler();
		do_switch = pdTRUE;
	}
	if (pending & 0x10) {
		do_switch = command_interface_irq();
	}
	if (pending & 0x08) {
		do_switch |= tape_recorder_irq();
	}
	if (pending & 0x04) {
		do_switch |= usb_irq();
	}
/*
	if (pending & 0x02) {
		do_switch |= uart_irq();
	}

*/
	if (pending & 0x01) {
		do_switch |= xTaskIncrementTick();
	}
	if (do_switch != pdFALSE) {
		vTaskSwitchContext();
	}
}
int alt_irq_register(int, int, void(*)(void*));
void ultimate_main(void *context);

#include "system.h"
#include "altera_avalon_pio_regs.h"
#include "dump_hex.h"

#define SET_FREQ(x)     IOWR_ALTERA_AVALON_PIO_CLEAR_BITS(PIO_0_BASE, 0xFFFF0000); IOWR_ALTERA_AVALON_PIO_SET_BITS(PIO_0_BASE, x << 16)
#define ENABLE_SPEAKER  IOWR_ALTERA_AVALON_PIO_SET_BITS(PIO_0_BASE, 0x100)
#define DISABLE_SPEAKER IOWR_ALTERA_AVALON_PIO_CLEAR_BITS(PIO_0_BASE, 0x100)

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

static void test_i2c_mdio(void)
{
    //i2c_write_byte(0xA2, 0, 0x58);

    printf("RTC read: ");
    for(int i=0;i<11;i++) {
    	printf("%02x ", i2c_read_byte(0xA2, i));
    } printf("\n");
/*
    uint8_t rtc_data[12];
    i2c_read_block(0xA2, 0, rtc_data, 11);
    dump_hex_relative(rtc_data, 11);
*/

	// mdio_reset();
	mdio_write(0x1B, 0x0500); // enable link up, link down interrupts
	mdio_write(0x16, 0x0002); // disable factory reset mode
	for(int i=0;i<31;i++) {
		printf("MDIO Read %2x: %04x\n", i, mdio_read(i));
	}
//	printf("MDIO Read 1B: %04x\n", mdio_read(0x1B));
//	printf("MDIO Read 1F: %04x\n", mdio_read(0x1F));

    printf("Audio Codec ChipID: %04x\n", i2c_read_word(0x14, 0x0000));

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

    SET_FREQ(0x131);
    ENABLE_SPEAKER;

    for(int i=0;i<64;i+=2) {
        uint16_t temp = i2c_read_word(0x14, (uint16_t)i);
        printf("%04x: %04x\n", i, temp);
    }


    USb2512Init();
}


/*

#define CODEC_STATUS			0x00
#define CODEC_JACK_SENSE		0x01
#define CODEC_AUX_HIGH			0x02
#define CODEC_AUX_LOW			0x03
#define CODEC_IRQ_ENABLE		0x04
#define CODEC_SYSTEM_CLOCK		0x05
#define CODEC_AUDIO_CLOCK_HI 	0x06
#define CODEC_AUDIO_CLOCK_LO 	0x07
#define CODEC_INTERFACE_MODE1	0x08
#define CODEC_INTERFACE_MODE2	0x09
#define CODEC_FILTERS			0x0A
#define CODEC_SIDE_TONE			0x0B
#define CODEC_DAC_LEVEL			0x0C
#define CODEC_ADC_LEVEL			0x0D
#define CODEC_LEFT_INPUT_LEVEL  0x0E
#define CODEC_RIGHT_INPUT_LEVEL 0x0F
#define CODEC_LEFT_VOLUME_CTRL  0x10
#define CODEC_RIGHT_VOLUME_CTRL 0x11
#define CODEC_LEFT_MIC_GAIN		0x12
#define CODEC_RIGHT_MIC_GAIN	0x13
#define CODEC_CFG_ADC_INPUT	    0x14
#define CODEC_CFG_ADC_MIC		0x15
#define CODEC_CFG_MODE			0x16
#define CODEC_SHUTDOWN			0x17
#define CODEC_REVISION			0xFF

static void Max9867Init(void) {
	// Shutdown device
	i2c_write_byte(0x30, CODEC_SHUTDOWN, 0x00); // Disable everything

	// Clock Control: Use MCLK and BCLK from FPGA for now (slave mode)
	// Clock from FPGA is close to 12.288 MHz, we run at "close to" 48 kHz.
	// PSMODE = 01, FREQ = 0000 => 0001_0000 => 0x10 (Prescaler: 10-20 MHz, FREQ: No effect)
    i2c_write_byte(0x30, CODEC_SYSTEM_CLOCK, 0x10);
    i2c_write_byte(0x30, CODEC_AUDIO_CLOCK_HI, 0x60); // PLL on L/R clock disabled
    i2c_write_byte(0x30, CODEC_AUDIO_CLOCK_LO, 0x00); // $6000

    // Interface mode
    i2c_write_byte(0x30, CODEC_INTERFACE_MODE1, 0x18); // DLY (I2S mode) | HIZOFF
	i2c_write_byte(0x30, CODEC_INTERFACE_MODE2, 0x00); // BSEL = 0, TDM = 0, DMONO = 0

	// Levels
	i2c_write_byte(0x30, CODEC_SIDE_TONE, 0x00); // disable side tone
	i2c_write_byte(0x30, CODEC_DAC_LEVEL, 0x07); // Mute = 0, Gain = 00 (=0dB), A = 0dB
	i2c_write_byte(0x30, CODEC_ADC_LEVEL, 0x33); // ADC = 0 dB for both channels

	// Playback volume control
	i2c_write_byte(0x30, CODEC_LEFT_VOLUME_CTRL, 0x02); // Single ended mode: 0 dB
	i2c_write_byte(0x30, CODEC_RIGHT_VOLUME_CTRL, 0x02); // Single ended mode: 0 dB

	// Do not route line in to headphone amplifier
	i2c_write_byte(0x30, CODEC_LEFT_INPUT_LEVEL, 0x4C); // LIxM = '1', gain = 0 dB
	i2c_write_byte(0x30, CODEC_RIGHT_INPUT_LEVEL, 0x4C); // LIxM = '1', gain = 0 dB

	// Configuration
	i2c_write_byte(0x30, CODEC_CFG_ADC_INPUT, 0xA0); // Select line in for ADC
	i2c_write_byte(0x30, CODEC_CFG_MODE, 0x8A); // slow volume changes (80)
												// Jack detect enable (08)
												// Capacitorless stereo headphones (02)

	// Take device out of shutdown
	i2c_write_byte(0x30, CODEC_SHUTDOWN, 0xEF); // Enable everything
}
*/

int main(int argc, char *argv[])
{
    /* When re-starting a debug session (rather than cold booting) we want
    to ensure the installed interrupt handlers do not execute until after the
    scheduler has been started. */
    portDISABLE_INTERRUPTS();

    ioWrite8(UART_DATA, 0x33);

    xTaskCreate( ultimate_main, "U-II Main", configMINIMAL_STACK_SIZE, NULL, tskIDLE_PRIORITY + 2, NULL );

    ioWrite8(UART_DATA, 0x34);

    test_i2c_mdio();

    while(1)
    	;

    if ( -EINVAL == alt_irq_register( 1, 0x0, ituIrqHandler ) ) {
		puts("Failed to install ITU IRQ handler.");
	}

	ioWrite8(ITU_IRQ_TIMER_EN, 0);
	ioWrite8(ITU_IRQ_DISABLE, 0xFF);
	ioWrite8(ITU_IRQ_CLEAR, 0xFF);
	ioWrite8(ITU_IRQ_TIMER_HI, 3);
	ioWrite8(ITU_IRQ_TIMER_LO, 208); // 0x03D0 => 200 Hz
	ioWrite8(ITU_IRQ_TIMER_EN, 1);
	ioWrite8(ITU_IRQ_ENABLE, 0x01); // timer only : other modules shall enable their own interrupt
    ioWrite8(UART_DATA, 0x35);

    // Finally start the scheduler.
    vTaskStartScheduler();

    // Should not get here as the processor is now under control of the
    // scheduler!
}
