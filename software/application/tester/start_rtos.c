/*
 * start_rtos.c
 *
 *  Created on: Nov 14, 2016
 *      Author: gideon
 */

#include <stdio.h>
#include "system.h"
#include <stdint.h>
#include <string.h>
#include <stdlib.h>

#include "mdio.h"
#include "alt_types.h"
#include "dump_hex.h"
#include "FreeRTOS.h"
#include "task.h"
#include "iomap.h"
#include "itu.h"
#include "profiler.h"
#include "usb_nano.h"
#include "u2p.h"

void RmiiRxInterruptHandler(void) __attribute__ ((weak));
uint8_t command_interface_irq(void) __attribute__ ((weak));
uint8_t tape_recorder_irq(void) __attribute__ ((weak));
uint8_t usb_irq(void) __attribute__ ((weak));

uint8_t command_interface_irq(void)
{

}

void RmiiRxInterruptHandler()
{

}

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

void main_task(void *context);
void USb2512Init();

int main(int argc, char *argv[])
{
	//printf("AA");
	/* When re-starting a debug session (rather than cold booting) we want
    to ensure the installed interrupt handlers do not execute until after the
    scheduler has been started. */
    portDISABLE_INTERRUPTS();

    NANO_START = 0;
    U2PIO_ULPI_RESET = 1;
    uint16_t *dst = (uint16_t *)NANO_BASE;
    for(int i=0; i<2048; i+=2) {
    	*(dst++) = 0;
    }
    USb2512Init();
    U2PIO_ULPI_RESET = 0;

    ioWrite8(UART_DATA, 0x31);

    xTaskCreate( main_task, "Tester Main", configMINIMAL_STACK_SIZE, NULL, tskIDLE_PRIORITY + 1, NULL );

    ioWrite8(UART_DATA, 0x32);

    if ( -EINVAL == alt_irq_register( 0, 0x0, ituIrqHandler ) ) {
		puts("Failed to install ITU IRQ handler.");
	}

	ioWrite8(ITU_IRQ_TIMER_EN, 0);
	ioWrite8(ITU_IRQ_DISABLE, 0xFF);
	ioWrite8(ITU_IRQ_CLEAR, 0xFF);
	ioWrite8(ITU_IRQ_TIMER_HI, 3);
	ioWrite8(ITU_IRQ_TIMER_LO, 208); // 0x03D0 => 200 Hz
	ioWrite8(ITU_IRQ_TIMER_EN, 1);
	ioWrite8(ITU_IRQ_ENABLE, 0x01); // timer only : other modules shall enable their own interrupt
    ioWrite8(UART_DATA, 0x33);
	ioWrite8(ITU_IRQ_GLOBAL, 1);

    // Finally start the scheduler.
    vTaskStartScheduler();

    // Should not get here as the processor is now under control of the
    // scheduler!
    ioWrite8(UART_DATA, 'X');
}
