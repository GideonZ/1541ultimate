###################################-*-asm*- 
# 
# Copyright (c) 2001 Xilinx, Inc.  All rights reserved. 
# 
# Xilinx, Inc. 
#
# XILINX IS PROVIDING THIS DESIGN, CODE, OR INFORMATION "AS IS" AS A 
# COURTESY TO YOU.  BY PROVIDING THIS DESIGN, CODE, OR INFORMATION AS
# ONE POSSIBLE   IMPLEMENTATION OF THIS FEATURE, APPLICATION OR 
# STANDARD, XILINX IS MAKING NO REPRESENTATION THAT THIS IMPLEMENTATION
# IS FREE FROM ANY CLAIMS OF INFRINGEMENT, AND YOU ARE RESPONSIBLE 
# FOR OBTAINING ANY RIGHTS YOU MAY REQUIRE FOR YOUR IMPLEMENTATION.  
# XILINX EXPRESSLY DISCLAIMS ANY WARRANTY WHATSOEVER WITH RESPECT TO 
# THE ADEQUACY OF THE IMPLEMENTATION, INCLUDING BUT NOT LIMITED TO 
# ANY WARRANTIES OR REPRESENTATIONS THAT THIS IMPLEMENTATION IS FREE 
# FROM CLAIMS OF INFRINGEMENT, IMPLIED WARRANTIES OF MERCHANTABILITY 
# AND FITNESS FOR A PARTICULAR PURPOSE.
# 
# crt0.s 
# 
# Default C run-time initialization for MicroBlaze standalone 
# executables (compiled with -xl-mode-executable or no switches)
#    
# $Id: crt0.s,v 1.1.2.1 2009/06/16 21:29:06 vasanth Exp $
# 
#######################################

/*
        
        MicroBlaze Vector Map for standalone executables

         Address                Vector type                 Label
         -------                -----------                 ------    

    	# 0x00 #		(-- IMM --)
	# 0x04 #                Reset                       _start1
    
	# 0x08 #		(-- IMM --)
	# 0x0c #		Software Exception          _exception_handler
    
	# 0x10 #		(-- IMM --)
	# 0x14 #		Hardware Interrupt          _interrupt_handler
    
        # 0x18 #                (-- IMM --)
        # 0x1C #                Breakpoint Exception        (-- Don't Care --)
    
        # 0x20 #                (-- IMM --) 
        # 0x24 #                Hardware Exception          _hw_exception_handler

*/      
	.globl _start
        .section .vectors.reset, "ax"
	.align 3
        .ent _start
        .type _start, @function
_start:
        brai    _start1
		nop
        brai    restart
        .end _start
    
/*
        .section .vectors.interrupt, "ax"
        .align 3
_vector_interrupt:      
        brai    _interrupt_handler

        .section .vectors.hw_exception, "ax"
        .align 3
_vector_hw_exception:       
        brai    _hw_exception_handler
*/

        .section .text
        .globl _start1
        .align 4
        .ent _start1
        .type _start1, @function   
_start1:
	nop
	la	r13, r0, _SDA_BASE_         /* Set the Small Data Anchors and the stack pointer */
	la	r2, r0, _SDA2_BASE_
	la	r1, r0, _stack-16           /* 16 bytes (4 words are needed by crtinit for args and link reg */

/*
__test_io:
    la  r3, r0, 0x12345678
    la  r5, r0, 0x04000000

    lbu r6, r5, r0
    sb  r3, r5, r0
    addi r5, r5, 1
    lbu r6, r5, r0
    sb  r3, r5, r0
    addi r5, r5, 1
    lbu r6, r5, r0
    sb  r3, r5, r0
    addi r5, r5, 1
    lbu r6, r5, r0
    sb  r3, r5, r0
    addi r5, r5, 1
    lhu r6, r5, r0
    sh  r3, r5, r0
    addi r5, r5, 1
    lhu r6, r5, r0
    sh  r3, r5, r0
    addi r5, r5, 1
    lhu r6, r5, r0
    sh  r3, r5, r0
    addi r5, r5, 2
    lw  r6, r5, r0
    sw  r3, r5, r0
__end_test_io:
*/

/* Test for simulation, jump directly over init and xmodem download */
/*
    bralid  r15, 0x10000
    nop
*/
    addi r3, r0, 2044  /* set R3 to top cache location */
__loop:
    lw    r4, r3, r0   /* load word from memory (cache hit) */
    sw    r4, r3, r0   /* store word to memory (write through) */
    beqi  r3, __done   /* if reached 0, we are done */
    addi  r3, r3, -4   /* decrement address */
    bri   __loop
__done:
	brlid	r15, _premain               /* Initialize BSS and run program */
	nop

    brlid   r15, _exit                   /* Call exit with the return value of main */
    addik   r5, r3, 0

    /* Control does not reach here */
    .end _start1
    

/* 
    _exit
    Our simple _exit
    .globl _exit
    .align 2
    .ent _exit
    .type _exit, @function
_exit:
    bri     0
	.end _exit
*/

