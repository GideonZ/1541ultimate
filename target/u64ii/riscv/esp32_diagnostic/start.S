.section .text.start,"ax",@progbits
.global _start
_start:
    csrci mstatus, 8
    li t0, 0x10000000
    sb zero, 0(t0)
    .option push
    .option norelax
    la sp, __stack_top
    .option pop
    la t0, __bss_start
    la t1, __bss_end
1:  bgeu t0, t1, 2f
    sw zero, 0(t0)
    addi t0, t0, 4
    j 1b
2:  call diagnostic_main
3:  j 3b
