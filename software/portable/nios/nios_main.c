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
#include "u2p.h"
#include "dump_hex.h"

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

    for(int i=0;i<64;i+=2) {
        uint16_t temp = i2c_read_word(0x14, (uint16_t)i);
        printf("%04x: %04x\n", i, temp);
    }

    USb2512Init();
}

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
