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
	.extern pxCurrentTCB
	.extern vTaskISRHandler
	.extern vTaskSwitchContext
	.extern uxCriticalNesting
	.extern pulISRStack
    .extern ulPortTempR31

	.global __FreeRTOS_interrupt_handler
	.global VPortYieldASM
	.global vStartFirstTask

.macro portSAVE_CONTEXT
    /* Store R31 to temporary location. This works, because interrupts are off */
    swi r31, r0, ulPortTempR31

    /* Load pointer to processor state block */
    lwi r31, r0, pxCurrentTCB
    lw  r31, r31, r0  /* redirect to allocated block */

    /* Now store everything */
    swi r30, r31, 120
    swi r29, r31, 116
    swi r28, r31, 112
    swi r27, r31, 108
    swi r26, r31, 104
    swi r25, r31, 100
    swi r24, r31, 96
    swi r23, r31, 92
    swi r22, r31, 88
    swi r21, r31, 84
    swi r20, r31, 80
    swi r19, r31, 76
    swi r18, r31, 72
    swi r17, r31, 68
    swi r16, r31, 64
    swi r15, r31, 60
/*    swi r14, r31, 56   (stored later!) */
    swi r13, r31, 52
    swi r12, r31, 48
    swi r11, r31, 44
    swi r10, r31, 40
    swi  r9, r31, 36
    swi  r8, r31, 32
    swi  r7, r31, 28
    swi  r6, r31, 24
    swi  r5, r31, 20
    swi  r4, r31, 16
    swi  r3, r31, 12
    swi  r2, r31, 8
    swi  r1, r31, 4

    /* The alpha and the omega */
    mfs  r4, rmsr
    swi  r4, r31, 0
    lwi  r3, r0, ulPortTempR31
    swi  r3, r31, 124
    lwi  r2, r0, uxCriticalNesting
    swi  r2, r31, 128
	.endm

.macro portRESTORE_CONTEXT
    /* Load pointer to processor state block */
    lwi r31, r0, pxCurrentTCB
    lw  r31, r31, r0  /* redirect to allocated block */

    /* Restore the critical nesting value. */
    lwi r3, r31, 128
    swi r3, r0, uxCriticalNesting

    /* Now reload everything */
    lwi r30, r31, 120
    lwi r29, r31, 116
    lwi r28, r31, 112
    lwi r27, r31, 108
    lwi r26, r31, 104
    lwi r25, r31, 100
    lwi r24, r31, 96
    lwi r23, r31, 92
    lwi r22, r31, 88
    lwi r21, r31, 84
    lwi r20, r31, 80
    lwi r19, r31, 76
    lwi r18, r31, 72
    lwi r17, r31, 68
    lwi r16, r31, 64
    lwi r15, r31, 60
    lwi r14, r31, 56
    lwi r13, r31, 52
    lwi r12, r31, 48
    lwi r11, r31, 44
    lwi r10, r31, 40
    lwi  r9, r31, 36
    lwi  r8, r31, 32
    lwi  r7, r31, 28
    lwi  r6, r31, 24
    lwi  r5, r31, 20
    lwi  r4, r31, 16
/* We need to use R3 still */
    lwi  r2, r31, 8
    lwi  r1, r31, 4

    /* Restore the MSR, except the I flag, RTID will set it. */
    lwi r3, r31, 0    /* 8 */
    andi r3, r3, ~2   /* 12 */
    mts rmsr, r3      /* 16 */
    /* Restore the remaining registers that we were using */
    lwi  r3, r31, 12  /* 20 */
    lwi  r31, r31, 124 /* 24 */
    rtid r14, 0       /* 28 Last instruction has IMM!  */
    nop               /* 32 */

	.endm

	.text
	.align  2


__FreeRTOS_interrupt_handler:
	portSAVE_CONTEXT
	/* Store the return address.  As we entered via an interrupt we do
	not need to modify the return address prior to storing R14. */
	swi r14, r31, 56

	/* Now switch to use the ISR stack. */
    lwi r1, r0, pulISRStack

	bralid r15, vTaskISRHandler
	or r0, r0, r0

	portRESTORE_CONTEXT


/* This is written in a way that the macro can just jump here, with link to R14 (bralid R14, VPortYieldASM..) */
VPortYieldASM:
    msrclr r0, 0x02 /* Turn interrupts off */
	portSAVE_CONTEXT

	/* Modify the return address so we return to the instruction after the
	exception. */
	addik r14, r14, 8
	swi   r14, r31, 56

	/* Now switch to use the ISR stack for the task switch. */
    lwi r1, r0, pulISRStack

    /* Call the scheduler */
	bralid r15, vTaskSwitchContext
	or r0, r0, r0

    /* Restore and continue */
	portRESTORE_CONTEXT

vStartFirstTask:
	portRESTORE_CONTEXT
	
