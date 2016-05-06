/*
 * nios_main.c
 *
 *  Created on: May 6, 2016
 *      Author: gideon
 */

#include <stdint.h>
#include <stdio.h>

#include "FreeRTOS.h"
#include "task.h"
#include "itu.h"
#include "i2c.h"
#include "mdio.h"

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

	if (pending & 0x10) {
		do_switch = command_interface_irq();
	}
	if (pending & 0x08) {
		do_switch |= tape_recorder_irq();
	}
	if (pending & 0x04) {
	    ioWrite8(UART_DATA, 0x41);
		do_switch |= usb_irq();
	}
/*
	if (pending & 0x02) {
		do_switch |= uart_irq();
	}
*/
}
int alt_irq_register(int, int, void(*)(void*));
void ultimate_main(void *context);

#include "altera_avalon_pio_regs.h"
#define FLASH_SEL_0   IOWR_ALTERA_AVALON_PIO_CLEAR_BITS(PIO_0_BASE, 0x4)
#define FLASH_SEL_1   IOWR_ALTERA_AVALON_PIO_SET_BITS(PIO_0_BASE, 0x4)
#define FLASH_SELCK_0   IOWR_ALTERA_AVALON_PIO_CLEAR_BITS(PIO_0_BASE, 0x8)
#define FLASH_SELCK_1   IOWR_ALTERA_AVALON_PIO_SET_BITS(PIO_0_BASE, 0x8)

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
	for(int i=0;i<8;i++) {
		printf("MDIO Read %2x: %04x\n", i, mdio_read(i));
	}
	mdio_write(0x1B, 0x0500); // enable link up, link down interrupts
	printf("MDIO Read 1B: %04x\n", mdio_read(0x1B));

    i2c_write_byte(0x30, 0x08, 0x08);
    i2c_write_byte(0x30, 0x09, 0x01);
    printf("Audio Codec read: %02x\n", i2c_read_byte(0x30, 0xFF));
    uint8_t codec_data[24];
    i2c_read_block(0x30, 0, codec_data, 24);
    dump_hex_relative(codec_data, 24);

    FLASH_SEL_0;
    FLASH_SELCK_0;
    FLASH_SELCK_1;

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

    if ( -EINVAL == alt_irq_register( 1, 0x0, ituIrqHandler ) ) {
		puts("Failed to install ITU IRQ handler.");
	}

    // Finally start the scheduler.
    vTaskStartScheduler();

    // Should not get here as the processor is now under control of the
    // scheduler!
}
