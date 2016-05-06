/*
    FreeRTOS V8.0.1 - Copyright (C) 2014 Real Time Engineers Ltd. 
    All rights reserved

    VISIT http://www.FreeRTOS.org TO ENSURE YOU ARE USING THE LATEST VERSION.

    ***************************************************************************
     *                                                                       *
     *    FreeRTOS provides completely free yet professionally developed,    *
     *    robust, strictly quality controlled, supported, and cross          *
     *    platform software that has become a de facto standard.             *
     *                                                                       *
     *    Help yourself get started quickly and support the FreeRTOS         *
     *    project by purchasing a FreeRTOS tutorial book, reference          *
     *    manual, or both from: http://www.FreeRTOS.org/Documentation        *
     *                                                                       *
     *    Thank you!                                                         *
     *                                                                       *
    ***************************************************************************

    This file is part of the FreeRTOS distribution.

    FreeRTOS is free software; you can redistribute it and/or modify it under
    the terms of the GNU General Public License (version 2) as published by the
    Free Software Foundation >>!AND MODIFIED BY!<< the FreeRTOS exception.

    >>!   NOTE: The modification to the GPL is included to allow you to     !<<
    >>!   distribute a combined work that includes FreeRTOS without being   !<<
    >>!   obliged to provide the source code for proprietary components     !<<
    >>!   outside of the FreeRTOS kernel.                                   !<<

    FreeRTOS is distributed in the hope that it will be useful, but WITHOUT ANY
    WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
    FOR A PARTICULAR PURPOSE.  Full license text is available from the following
    link: http://www.freertos.org/a00114.html

    1 tab == 4 spaces!

    ***************************************************************************
     *                                                                       *
     *    Having a problem?  Start by reading the FAQ "My application does   *
     *    not run, what could be wrong?"                                     *
     *                                                                       *
     *    http://www.FreeRTOS.org/FAQHelp.html                               *
     *                                                                       *
    ***************************************************************************

    http://www.FreeRTOS.org - Documentation, books, training, latest versions,
    license and Real Time Engineers Ltd. contact details.

    http://www.FreeRTOS.org/plus - A selection of FreeRTOS ecosystem products,
    including FreeRTOS+Trace - an indispensable productivity tool, a DOS
    compatible FAT file system, and our tiny thread aware UDP/IP stack.

    http://www.OpenRTOS.com - Real Time Engineers ltd license FreeRTOS to High
    Integrity Systems to sell under the OpenRTOS brand.  Low cost OpenRTOS
    licenses offer ticketed support, indemnification and middleware.

    http://www.SafeRTOS.com - High Integrity Systems also provide a safety
    engineered and independently SIL3 certified version for use in safety and
    mission critical applications that require provable dependability.

    1 tab == 4 spaces!
*/

/*-----------------------------------------------------------
 * Implementation of functions defined in portable.h for the NIOS2 port.
 *----------------------------------------------------------*/

/* Standard Includes. */
#include <stdio.h>
#include <string.h>
#include <errno.h>

/* Altera includes. */
#include "sys/alt_irq.h"
#include "priv/alt_irq_table.h"
#include "system.h"

/* Hardware includes. */
#include "itu.h"

/* Scheduler includes. */
#include "FreeRTOS.h"
#include "task.h"

/* Interrupts are enabled. */
#define portINITIAL_ESTATUS     ( StackType_t ) 0x01 

/*-----------------------------------------------------------*/

/* 
 * Setup the timer to generate the tick interrupts.
 */
static void prvSetupTimerInterrupt( void );

/*
 * Call back for the alarm function.
 */
void vPortSysTickHandler( void * context);

/*-----------------------------------------------------------*/

static void prvReadGp( uint32_t *ulValue )
{
	asm( "stw gp, (%0)" :: "r"(ulValue) );
}
/*-----------------------------------------------------------*/

/* 
 * See header file for description. 
 */
StackType_t *pxPortInitialiseStack( StackType_t *pxTopOfStack, TaskFunction_t pxCode, void *pvParameters )
{    
StackType_t *pxFramePointer = pxTopOfStack - 1;
StackType_t xGlobalPointer;

    prvReadGp( &xGlobalPointer ); 

    /* End of stack marker. */
    *pxTopOfStack = 0xdeadbeef;
    pxTopOfStack--;
    
    *pxTopOfStack = ( StackType_t ) pxFramePointer; 
    pxTopOfStack--;
    
    *pxTopOfStack = xGlobalPointer; 
    
    /* Space for R23 to R16. */
    pxTopOfStack -= 9;

    *pxTopOfStack = ( StackType_t ) pxCode; 
    pxTopOfStack--;

    *pxTopOfStack = portINITIAL_ESTATUS; 

    /* Space for R15 to R5. */    
    pxTopOfStack -= 12;
    
    *pxTopOfStack = ( StackType_t ) pvParameters; 

    /* Space for R3 to R1, muldiv and RA. */
    pxTopOfStack -= 5;
    
    return pxTopOfStack;
}
/*-----------------------------------------------------------*/

/* 
 * See header file for description. 
 */
BaseType_t xPortStartScheduler( void )
{
	/* Start the timer that generates the tick ISR.  Interrupts are disabled
	here already. */
	prvSetupTimerInterrupt();
	
	/* Start the first task. */
    asm volatile (  " movia r2, restore_sp_from_pxCurrentTCB        \n"
                    " jmp r2                                          " );

	/* Should not get here! */
	return 0;
}
/*-----------------------------------------------------------*/

void vPortEndScheduler( void )
{
	/* It is unlikely that the NIOS2 port will require this function as there
	is nothing to return to.  */
}

/*-----------------------------------------------------------*/
BaseType_t uart_irq() __attribute__ ((weak));
BaseType_t usb_irq() __attribute__ ((weak));
BaseType_t tape_recorder_irq() __attribute__ ((weak));
BaseType_t command_interface_irq() __attribute__ ((weak));

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


/*-----------------------------------------------------------*/

void vPortSysTickHandler(void * context)
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
            do_switch |= usb_irq();
        }
        if (pending & 0x02) {
            do_switch |= uart_irq();
        }
        if (pending & 0x01) {
            do_switch |= xTaskIncrementTick();
        }
        if (do_switch != pdFALSE) {
            ioWrite8(UART_DATA, 0x2e);
            vTaskSwitchContext();
        }
}
/*-----------------------------------------------------------*/



/** This function is a re-implementation of the Altera provided function.
 * The function is re-implemented to prevent it from enabling an interrupt
 * when it is registered. Interrupts should only be enabled after the FreeRTOS.org
 * kernel has its scheduler started so that contexts are saved and switched 
 * correctly.
*/

int alt_iic_isr_register(alt_u32 ic_id, alt_u32 irq, alt_isr_func isr, void *isr_context, void *flags)
{
	  int rc = -EINVAL;
	  int id = irq;             /* IRQ interpreted as the interrupt ID. */
	  alt_irq_context status;

	  if (id < ALT_NIRQ)
	  {
	    /*
	     * interrupts are disabled while the handler tables are updated to ensure
	     * that an interrupt doesn't occur while the tables are in an inconsistant
	     * state.
	     */

	    status = alt_irq_disable_all();

	    alt_irq[id].handler = isr;
	    alt_irq[id].context = isr_context;

	    rc = (isr) ? alt_ic_irq_enable(ic_id, id) : alt_ic_irq_disable(ic_id, id);

	    (void) status; // fixes compile warning
	  //  alt_irq_enable_all(status);
	  }

	  return rc;
}

int alt_irq_register( alt_u32 id, void* context, void (*handler) )
{
	int rc = -EINVAL;
	alt_irq_context status;

	if (id < ALT_NIRQ)
	{
		/*
		 * interrupts are disabled while the handler tables are updated to ensure
		 * that an interrupt doesn't occur while the tables are in an inconsistent
		 * state.
		 */

		status = alt_irq_disable_all ();

		alt_irq[id].handler = handler;
		alt_irq[id].context = context;

		rc = (handler) ? alt_ic_irq_enable (0, id): alt_ic_irq_disable (0, id);
		(void) status; // fixes compile warning
		/* alt_irq_enable_all(status); This line is removed to prevent the interrupt from being immediately enabled. */
	}

	return rc;
}

/*
 * Setup the systick timer to generate the tick interrupts at the required
 * frequency.
 */
void prvSetupTimerInterrupt( void )
{
	/* Try to register the interrupt handler. */
//	if ( -EINVAL == alt_irq_register( CPU_TIMER_IRQ_INTERRUPT_CONTROLLER_ID, 0x0, vPortSysTickHandler ) )
    if ( -EINVAL == alt_irq_register( 1, 0x0, vPortSysTickHandler ) )
	{
		/* Failed to install the Interrupt Handler. */
		asm( "break" );
	}
	else
	{
	    int timerDiv = (configCPU_CLOCK_HZ / configTICK_RATE_HZ) >> 8;
	    /* Configure SysTick to interrupt at the requested rate. */
	    ioWrite8(ITU_IRQ_TIMER_EN, 0);
	    ioWrite8(ITU_IRQ_DISABLE, 0xFF);
	    ioWrite8(ITU_IRQ_TIMER_HI, timerDiv >> 8);
	    ioWrite8(ITU_IRQ_TIMER_LO, timerDiv & 0xFF);
	    ioWrite8(ITU_IRQ_TIMER_EN, 1);
	    ioWrite8(ITU_IRQ_CLEAR, 0x01);
	    ioWrite8(ITU_IRQ_ENABLE, 0x01); // timer only : other modules shall enable their own interrupt
	    ioWrite8(UART_DATA, 0x35);
	}
}
