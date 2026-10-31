OUTPUT_ARCH(riscv)
ENTRY(_start)
MEMORY { ram (rwx) : ORIGIN = 0x03000000, LENGTH = 0x00100000 }
SECTIONS {
    .text : { KEEP(*(.text.start)) *(.text*) } > ram
    .rodata : { *(.rodata*) *(.srodata*) } > ram
    .data : { *(.data*) *(.sdata*) } > ram
    .bss (NOLOAD) : {
        . = ALIGN(4); __bss_start = .;
        *(.bss*) *(.sbss*) *(COMMON)
        . = ALIGN(4); __bss_end = .;
    } > ram
    .stack (NOLOAD) : { . = ALIGN(16); . += 4096; __stack_top = .; } > ram
    /DISCARD/ : { *(.comment) *(.eh_frame*) *(.riscv.attributes) }
    ASSERT(__stack_top < 0x03100000, "diagnostic RAM overflow")
}
