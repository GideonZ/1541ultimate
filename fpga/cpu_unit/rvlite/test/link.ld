SECTIONS
{
    . = 0x00000000;         /* Set your start address here */
    .text : { *(.text) }
    .data : { *(.data) }
    .bss  : { *(.bss)  }
}
