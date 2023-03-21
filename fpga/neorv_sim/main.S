
.file "main.S"
.section .text.crt0
.balign 4
.global _start


_start:
.cfi_startproc
.cfi_undefined ra

// ************************************************************************************************
// We need to ensure interrupts are globally disabled at start. This is is required if this code
// is part of a program uploaded by the on-chip debugger (potentionally taking control from the
// bootloader). We setup a new stack pointer here and WE DO NOT WANT TO trap to an outdated trap
// handler with a modified stack pointer.
// ************************************************************************************************
  csrrci zero, mstatus, (1<<3) // disable global interrupt flag


// ************************************************************************************************
// Setup pointers using linker script symbols
// ************************************************************************************************
__crt0_pointer_init:
  .option push
  .option norelax

  la sp, __crt0_stack_begin // stack pointer
  la gp, __global_pointer$  // global pointer

  .option pop


// ************************************************************************************************
// Setup CPU core CSRs (some of them DO NOT have a dedicated
// reset and need to be explicitly initialized)
// ************************************************************************************************
__crt0_cpu_csr_init:

  la   x10,   trap_handler              // configure early-boot trap handler
  csrw mtvec, x10
  csrw mepc,  x10                       // just to init mepc

  csrw mstatus, zero                    // clear all control flags, also disable global IRQ
  csrw mie,     zero                    // absolutely no interrupt sources, thanks
  csrw mip,     zero                    // clear all pending interrupts

  csrw 0x320,      zero                 // stop all counters; use "mcountinhibit" literal address for lagacy toolchain compatibility
  csrw mcounteren, zero                 // no access from less-privileged modes to counter CSRs

  csrw mcycle,    zero                  // reset cycle counter
  csrw mcycleh,   zero
  csrw minstret,  zero                  // reset instructions-retired counter
  csrw minstreth, zero


// ************************************************************************************************
// Initialize integer register file (lower half)
// ************************************************************************************************
__crt0_reg_file_clear:
//addi  x0, x0, 0 // hardwired to zero
  addi  x1, x0, 0
//addi  x2, x0, 0 // stack pointer sp
//addi  x3, x0, 0 // global pointer gp
  addi  x4, x0, 0
  addi  x5, x0, 0
  addi  x6, x0, 0
  addi  x7, x0, 0
//addi  x8, x0, 0 // implicitly initialized within crt0
//addi  x9, x0, 0 // implicitly initialized within crt0
//addi x10, x0, 0 // implicitly initialized within crt0
//addi x11, x0, 0 // implicitly initialized within crt0
//addi x12, x0, 0 // implicitly initialized within crt0
//addi x13, x0, 0 // implicitly initialized within crt0
//addi x14, x0, 0 // implicitly initialized within crt0
//addi x15, x0, 0 // implicitly initialized within crt0


// ************************************************************************************************
// Initialize integer register file (upper half, if no E extension)
// ************************************************************************************************
#ifndef __riscv_32e
// do not do this if compiling bootloader (to save some program space)
#ifndef make_bootloader
  addi x16, x0, 0
  addi x17, x0, 0
  addi x18, x0, 0
  addi x19, x0, 0
  addi x20, x0, 0
  addi x21, x0, 0
  addi x22, x0, 0
  addi x23, x0, 0
  addi x24, x0, 0
  addi x25, x0, 0
  addi x26, x0, 0
  addi x27, x0, 0
  addi x28, x0, 0
  addi x29, x0, 0
  addi x30, x0, 0
  addi x31, x0, 0
#endif
#endif


// ************************************************************************************************
// Clear .bss section (word-wise) using linker script symbols
// ************************************************************************************************
__crt0_clear_bss:
  la   x11,  __crt0_bss_start
  la   x12,  __crt0_bss_end

__crt0_clear_bss_loop:
  bge  x11,  x12, __crt0_clear_bss_loop_end
  sw   zero, 0(x11)
  addi x11,  x11, 4
  j    __crt0_clear_bss_loop

__crt0_clear_bss_loop_end:


// ************************************************************************************************
// Copy initialized .data section from ROM to RAM (word-wise) using linker script symbols
// ************************************************************************************************
__crt0_copy_data:
  la   x11, __crt0_copy_data_src_begin        // start of data area (copy source)
  la   x12, __crt0_copy_data_dst_begin        // start of data area (copy destination)
  beq  x11, x12, __crt0_copy_data_loop_end    // Nothing to do, if source and destination address are the same
  la   x13, __crt0_copy_data_dst_end          // last address of destination data area

__crt0_copy_data_loop:
  bge  x12, x13,  __crt0_copy_data_loop_end
  lw   x14, 0(x11)
  sw   x14, 0(x12)
  addi x11, x11, 4
  addi x12, x12, 4
  j    __crt0_copy_data_loop

__crt0_copy_data_loop_end:

// ************************************************************************************************
// Setup arguments and call main function
// ************************************************************************************************
__crt0_main_entry:
  addi a0,  zero, 0                  // a0 = argc = 0
  addi a1,  zero, 0                  // a1 = argv = 0
  jal  ra,  main                     // call actual app's main function, this "should" not return

__crt0_main_exit:                    // main's "return" and "exit" will arrive here
  csrw mscratch, a0                  // backup main's return code to mscratch (for debugger)


// ************************************************************************************************
// call "after main" handler (if there is any) if main really returns
// ************************************************************************************************
#ifndef make_bootloader              // after_main handler not supported for bootloader

__crt0_main_aftermath:
  .weak __neorv32_crt0_after_main
  la   ra, __neorv32_crt0_after_main
  beqz ra, __crt0_main_aftermath_end // check if an aftermath handler has been specified
  jalr ra                            // execute handler with main's return code still in a0

__crt0_main_aftermath_end:

#endif

// ************************************************************************************************
// go to endless sleep mode
// ************************************************************************************************
__crt0_shutdown:
  csrci mstatus, 8                   // disable global IRQs (clear mstatus.mie)
  wfi                                // go to sleep mode
  j .                                // endless loop


// ************************************************************************************************
// trap for ecall and irqs
// ************************************************************************************************
.balign 4
trap_handler:
  csrr  a0,   mcause
  blt   a0,   zero, __irq_handler

__ecall_handler:
  csrr  a0,   mepc
  addi  a0, a0, 4
  csrw  mepc, a0
  addi  a5, a5, 1
  mret

__irq_handler:
  sb x0, -1(s0)  // IO write to ack IRQ and count them in test bench IO addr 3E
  addi  a4, a4, 1
  mret

// ************************************************************************************************
// main function
// ************************************************************************************************
.macro ecall_wrap
    addi a2, a2, 1
    ecall
    addi a3, a3, 1
    .endm

.macro ten_ecall_wraps
    ecall_wrap
    ecall_wrap
    ecall_wrap
    ecall_wrap
    ecall_wrap
    ecall_wrap
    ecall_wrap
    ecall_wrap
    ecall_wrap
    ecall_wrap
    .endm

main:
    li a2, 0
    li a3, 0
    li a4, 0
    li a5, 0

    li s1, 0x800
    csrs mie, s1 // enable external interrupts

    csrs mstatus, 8 // enable global interrupt flag
    li s0, 0x1000003f
    nop
    nop
    ten_ecall_wraps
    ten_ecall_wraps
    ten_ecall_wraps
    ten_ecall_wraps
    ten_ecall_wraps
    nop
    nop
    sb a2, 0(s0)
    sb a3, 0(s0)
    sb a4, 0(s0)
    sb a5, 0(s0)
    nop
    nop
    nop
    nop
    nop
    jal .-4

.cfi_endproc
.end
