/*
    FreeRTOS V8.2.0 - Copyright (C) 2015 Real Time Engineers Ltd.
    All rights reserved

    VISIT http://www.FreeRTOS.org TO ENSURE YOU ARE USING THE LATEST VERSION.

    This file is part of the FreeRTOS distribution.

    FreeRTOS is free software; you can redistribute it and/or modify it under
    the terms of the GNU General Public License (version 2) as published by the
    Free Software Foundation >>!AND MODIFIED BY!<< the FreeRTOS exception.

	***************************************************************************
    >>!   NOTE: The modification to the GPL is included to allow you to     !<<
    >>!   distribute a combined work that includes FreeRTOS without being   !<<
    >>!   obliged to provide the source code for proprietary components     !<<
    >>!   outside of the FreeRTOS kernel.                                   !<<
	***************************************************************************

    FreeRTOS is distributed in the hope that it will be useful, but WITHOUT ANY
    WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
    FOR A PARTICULAR PURPOSE.  Full license text is available on the following
    link: http://www.freertos.org/a00114.html

    ***************************************************************************
     *                                                                       *
     *    FreeRTOS provides completely free yet professionally developed,    *
     *    robust, strictly quality controlled, supported, and cross          *
     *    platform software that is more than just the market leader, it     *
     *    is the industry's de facto standard.                               *
     *                                                                       *
     *    Help yourself get started quickly while simultaneously helping     *
     *    to support the FreeRTOS project by purchasing a FreeRTOS           *
     *    tutorial book, reference manual, or both:                          *
     *    http://www.FreeRTOS.org/Documentation                              *
     *                                                                       *
    ***************************************************************************

    http://www.FreeRTOS.org/FAQHelp.html - Having a problem?  Start by reading
	the FAQ page "My application does not run, what could be wrong?".  Have you
	defined configASSERT()?

	http://www.FreeRTOS.org/support - In return for receiving this top quality
	embedded software for free we request you assist our global community by
	participating in the support forum.

	http://www.FreeRTOS.org/training - Investing in training allows your team to
	be as productive as possible as early as possible.  Now you can receive
	FreeRTOS training directly from Richard Barry, CEO of Real Time Engineers
	Ltd, and the world's leading authority on the world's leading RTOS.

    http://www.FreeRTOS.org/plus - A selection of FreeRTOS ecosystem products,
    including FreeRTOS+Trace - an indispensable productivity tool, a DOS
    compatible FAT file system, and our tiny thread aware UDP/IP stack.

    http://www.FreeRTOS.org/labs - Where new FreeRTOS products go to incubate.
    Come and try FreeRTOS+TCP, our new open source TCP/IP stack for FreeRTOS.

    http://www.OpenRTOS.com - Real Time Engineers ltd. license FreeRTOS to High
    Integrity Systems ltd. to sell under the OpenRTOS brand.  Low cost OpenRTOS
    licenses offer ticketed support, indemnification and commercial middleware.

    http://www.SafeRTOS.com - High Integrity Systems also provide a safety
    engineered and independently SIL3 certified version for use in safety and
    mission critical applications that require provable dependability.

    1 tab == 4 spaces!
*/

/*-----------------------------------------------------------
 * Implementation of functions defined in portable.h for the MicroBlaze port.
 *----------------------------------------------------------*/


/* Scheduler includes. */
#include "FreeRTOS.h"
#include "FreeRTOSConfig.h"
#include "task.h"

/* Standard includes. */
#include <string.h>

/* Hardware includes. */
#include "itu.h"
#include "small_printf.h"

/* Tasks are started with interrupts enabled. */
#define portINITIAL_MSR_STATE		( ( StackType_t ) 0x02 )

/* Tasks are started with a critical section nesting of 0 */
#define portINITIAL_NESTING_VALUE	( 0xff )

/* Our hardware setup only uses one counter. */
#define portCOUNTER_0 				0

/* The stack used by the ISR is filled with a known value to assist in
debugging. */
#define portISR_STACK_FILL_VALUE	0x55555555

/* To limit the amount of stack required by each task, this port uses a
separate stack for interrupts. */
uint32_t *pulISRStack;

/* Globally static temporaries */
uint32_t ulPortTempR31;

/* Counts the nesting depth of calls to portENTER_CRITICAL().  Each task 
maintains it's own count, so this variable is saved as part of the task
context. */
volatile UBaseType_t uxCriticalNesting = portINITIAL_NESTING_VALUE;


/*-----------------------------------------------------------*/
/*
 * Sets up the periodic ISR used for the RTOS tick.  This uses timer 0, but
 * could have alternatively used the watchdog timer or timer 1.
 */
static void prvSetupTimerInterrupt( void )
{
	ioWrite8(ITU_IRQ_TIMER_EN, 0);
	ioWrite8(ITU_IRQ_DISABLE, 0xFF);
	ioWrite8(ITU_IRQ_TIMER_HI, 0x3);
	ioWrite8(ITU_IRQ_TIMER_LO, 208); // 0x03D0 => 200 Hz
	ioWrite8(ITU_IRQ_TIMER_EN, 1);
	ioWrite8(ITU_IRQ_ENABLE, 0x01); // timer only : other modules shall enable their own interrupt
    ioWrite8(UART_DATA, 0x35);

}

/*-----------------------------------------------------------*/

/* 
 * Initialize the stack of a task to look exactly as if a call to
 * portSAVE_CONTEXT had been made.
 * 
 * See the header file portable.h.
 */
StackType_t *pxPortInitialiseStack( StackType_t *pxTopOfStack, TaskFunction_t pxCode, void *pvParameters )
{
    extern void *_SDA2_BASE_, *_SDA_BASE_;
    const uint32_t ulR2 = ( uint32_t ) &_SDA2_BASE_;
    const uint32_t ulR13 = ( uint32_t ) &_SDA_BASE_;

    /* Let's get our own "block" of data that we use to store the processor state, and initialize it. */
    // 31 registers, processor State, MSR in location 0, others on offset equal to register number.
    // Entry 32 is reserved for the critical nexting value.

    uint32_t *processorStateBlock = pvPortMalloc(4 * 33);

    int i;
    for (i = 0; i < 32; i++) {
        processorStateBlock[i] = (uint32_t)i;
    }
    processorStateBlock[ 0] = portINITIAL_MSR_STATE;
    processorStateBlock[ 1] = (uint32_t)pxTopOfStack;
    processorStateBlock[ 2] = ulR2;
    processorStateBlock[ 5] = (uint32_t)pvParameters;
    processorStateBlock[13] = ulR13;
    processorStateBlock[14] = (uint32_t)pxCode;
    processorStateBlock[32] = 0;

    // Return our own data block as pxTopOfStack value, such that we can use this as indirect pointer that never changes during execution
    return processorStateBlock;
}

/*-----------------------------------------------------------*/

BaseType_t xPortStartScheduler( void )
{
    extern void ( __FreeRTOS_interrupt_handler )( void );
    extern void ( vStartFirstTask )( void );

	/* Setup the FreeRTOS interrupt handler.   */
    volatile uint32_t *p = (volatile uint32_t *)0x10;
	uint32_t addr = (uint32_t)__FreeRTOS_interrupt_handler;
	*p++ = (0xB0000000 | (addr >> 16));    // imm  #<high>
	*p++ = (0xB8080000 | (addr & 0xFFFF)); // brai <low>

	/* Setup the hardware to generate the tick.  Interrupts are disabled when
	this function is called. */
	prvSetupTimerInterrupt();

	// Allocate the stack to be used by the interrupt handler.
	pulISRStack = ( uint32_t * ) pvPortMalloc( configMINIMAL_STACK_SIZE * sizeof( StackType_t ) );

	// Restore the context of the first task that is going to run.
	if( pulISRStack != NULL )
	{
		// Fill the ISR stack with a known value to facilitate debugging.
		memset( pulISRStack, portISR_STACK_FILL_VALUE, configMINIMAL_STACK_SIZE * sizeof( StackType_t ) );
		pulISRStack += ( configMINIMAL_STACK_SIZE - 1 );

		// Kick off the first task.
		vStartFirstTask();
	}

	/* Should not get here as the tasks are now running! */
	return pdFALSE;
}
/*-----------------------------------------------------------*/

void vPortEndScheduler( void )
{
	/* Not implemented. */
}
/*-----------------------------------------------------------*/
BaseType_t uart_irq() __attribute__ ((weak));
BaseType_t usb_irq() __attribute__ ((weak));
BaseType_t tape_recorder_irq() __attribute__ ((weak));
BaseType_t command_interface_irq() __attribute__ ((weak));
uint8_t acia_irq(void) __attribute__ ((weak));

BaseType_t uart_irq()
{
	return pdFALSE;
}

BaseType_t usb_irq()
{
	return pdFALSE;
}

BaseType_t tape_recorder_irq()
{
	return pdFALSE;
}

BaseType_t command_interface_irq()
{
	return pdFALSE;
}

uint8_t acia_irq(void)
{
    // We shouldn't come here.
    ioWrite8(ITU_IRQ_HIGH_ACT, 0);
    return (BaseType_t)pdFALSE;
}


#include "profiler.h"

void vTaskISRHandler( void )
{
    uint8_t pending;

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
		do_switch |= usb_irq();
	}
	if (pending & 0x02) {
		do_switch |= uart_irq();
	}
	if (pending & 0x01) {
        do_switch |= xTaskIncrementTick();
	}
    if (ioRead8(ITU_IRQ_HIGH_ACT) & ITU_IRQHIGH_ACIA) {
        do_switch |= acia_irq();
    }
	if (do_switch != pdFALSE) {
		vTaskSwitchContext();
	}
}

/*-----------------------------------------------------------*/
void vAssertCalled(const char* fileName, uint16_t lineNo )
{
	printf("ASSERTION FAIL: %s:%d\n", fileName, lineNo);
	while(1)
		;
}
