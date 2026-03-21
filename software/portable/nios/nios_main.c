/*
 * nios_main.c
 *
 *  Created on: May 6, 2016
 *      Author: gideon
 */

#include <stdint.h>
#include <stdio.h>

#include "alt_types.h"
#include "dump_hex.h"
#include "FreeRTOS.h"
#include "task.h"
#include "iomap.h"
#include "itu.h"
#include "profiler.h"
#include "system.h"
#include "u2p.h"
#include "dump_hex.h"

#ifndef CLOCK_FREQ
#define CLOCK_FREQ 50000000
#endif

typedef uint8_t (*irq_function_t)(void *context);

typedef struct{
    irq_function_t handler;
    void *context;
} IrqHandler_t;

#define HIGH_IRQS 6
static IrqHandler_t high_irqs[HIGH_IRQS];

void install_high_irq(int irqNr, irq_function_t func, void *context)
{
    if (irqNr < HIGH_IRQS) {
        high_irqs[irqNr].handler = func;
        high_irqs[irqNr].context = context;
        ioWrite8(ITU_IRQ_HIGH_EN, ioRead8(ITU_IRQ_HIGH_EN) | (1 << irqNr));
    }
}

void deinstall_high_irq(int irqNr)
{
    if (irqNr < HIGH_IRQS) {
        ioWrite8(ITU_IRQ_HIGH_EN, ioRead8(ITU_IRQ_HIGH_EN) & ~(1 << irqNr));
        high_irqs[irqNr].handler = NULL;
        high_irqs[irqNr].context = NULL;
    }
}

void ResetInterruptHandlerCmdIf(void) __attribute__ ((weak));
void ResetInterruptHandlerU64(void) __attribute__ ((weak));
void RmiiRxInterruptHandler(void) __attribute__ ((weak));
uint8_t command_interface_irq(void) __attribute__ ((weak));
uint8_t tape_recorder_irq(void) __attribute__ ((weak));
uint8_t usb_irq(void) __attribute__ ((weak));

uint8_t command_interface_irq(void) {
    return 0;
}

void RmiiRxInterruptHandler() {

}

void ResetInterruptHandlerCmdIf()
{
}

void ResetInterruptHandlerU64()
{
}

static void ituIrqHandler(void *context) {
	static uint8_t pending;

	PROFILER_SUB = 1;
	/* Which interrupts are pending? */
	pending = ioRead8(ITU_IRQ_ACTIVE);
	ioWrite8(ITU_IRQ_CLEAR, pending);

	BaseType_t do_switch = pdFALSE;

	if (pending & 0x80) {
        ResetInterruptHandlerCmdIf();
	    ResetInterruptHandlerU64();
	    do_switch = pdTRUE;
	}
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

	uint8_t h = ioRead8(ITU_IRQ_HIGH_ACT);
	for(int i=0;(h != 0) && (i < HIGH_IRQS); i++, h>>=1) {
	    if (h & 1) {
	        if (high_irqs[i].handler) {
	            // Run handler with context parameter if installed
	            do_switch |= high_irqs[i].handler(high_irqs[i].context);
	        } else {
	            // Turn off interrupt if no handler was installed
	            ioWrite8(ITU_IRQ_HIGH_EN, ioRead8(ITU_IRQ_HIGH_EN) & ~(1 << i));
	        }
	    }
	}

	if (do_switch != pdFALSE) {
		vTaskSwitchContext();
	}
}
int alt_irq_register(int, int, void (*)(void*));
void ultimate_main(void *context);

void custom_hardware_init() __attribute__ ((weak));
void custom_hardware_init()
{
    printf("\nNo custom hardware init\n");
}


int main(int argc, char *argv[]) {
	/* When re-starting a debug session (rather than cold booting) we want
	 to ensure the installed interrupt handlers do not execute until after the
	 scheduler has been started. */
	portDISABLE_INTERRUPTS();

    puts("-- Custom Hardware Init --");
    custom_hardware_init();

	if (-EINVAL == alt_irq_register(0, 0x0, ituIrqHandler)) {
		puts("Failed to install ITU IRQ handler.");
	}

    puts("-- Setup Timer Interrupt --");
	uint32_t freq = CLOCK_FREQ;
	freq >>= 8;
	freq /= 200;
	freq -= 1;

	ioWrite8(ITU_IRQ_HIGH_EN, 0);
    ioWrite8(ITU_IRQ_TIMER_EN, 0);
	ioWrite8(ITU_IRQ_DISABLE, 0xFF);
	ioWrite8(ITU_IRQ_CLEAR, 0xFF);
	ioWrite8(ITU_IRQ_TIMER_HI, (uint8_t)(freq >> 8));
	ioWrite8(ITU_IRQ_TIMER_LO, (uint8_t)(freq & 0xFF));
	ioWrite8(ITU_IRQ_TIMER_EN, 1);
	ioWrite8(ITU_IRQ_ENABLE, 0x01); // timer only : other modules shall enable their own interrupt
	ioWrite8(ITU_IRQ_GLOBAL, 0x01); // Enable interrupts globally

    puts("-- Start Scheduler --");
    xTaskCreate(ultimate_main, "U-II Main", configMINIMAL_STACK_SIZE, NULL, PRIO_MAIN, NULL);

	// Finally start the scheduler.
	vTaskStartScheduler();

	// Should not get here as the processor is now under control of the
	// scheduler!
}
