OUTPUT_FORMAT("elf32-microblaze", "elf32-microblaze",
	      "elf32-microblaze")
OUTPUT_ARCH(microblaze)
ENTRY(_premain)

MEMORY
{
    DRAM (rwx)  : ORIGIN = 0x00001000, LENGTH = 0x0000E000 /* thus until 000F000; stay in the first 64K! */
}

SECTIONS
{
  .text 0x1000 : {
    *(.text.start)
    *(.text .stub .text.* .gnu.linkonce.t.*)
  } > DRAM
  .fini           :
  {
    KEEP (*(.fini))
  } > DRAM

  .init :
  {
    KEEP (*(.init))
  } > DRAM
  
  end_of_text = .;

  /* DATA */

  .rodata :
  {
    *(.rodata .rodata.* .gnu.linkonce.r.*)
    *(.rodata1)
  	
  } > DRAM

  .data ALIGN(4) :
  {
    __data_start = . ;
    *(.data .data.* .gnu.linkonce.d.* .gcc_except_table)
    __constructor_list = .;
    KEEP (*(.ctors))
    __end_of_constructors = .;
    
    __destructor_list = .;
    KEEP (*(.dtors))
    __end_of_destructors = .;

    KEEP (*(.jcr))
/*     SORT(CONSTRUCTORS)  */
    __data_end = .;
  } > DRAM

  __bss_start = .;
  __bss_start__ = .;
  .bss ALIGN(4) (NOLOAD) :
  {
   *(.dynbss)
   *(.bss .bss.* .gnu.linkonce.b.*)
   *(COMMON)
   /* Align here to ensure that the .bss section occupies space up to
      _end.  Align after .bss to ensure correct alignment even if the
      .bss section disappears because there are no input sections.  */
   . = ALIGN(32 / 8);
  } > DRAM
  . = ALIGN(32 / 8);
  _end = .;
  _bss_end__ = . ; __bss_end__ = . ; __end__ = . ;

  PROVIDE (end = .);

  .heap ALIGN(32) : {
     _heap = .;
     _heap_start = .;
  } 
  _heap_end = 0x0000E000;
  _stack = 0x0000EFFC;
  _SDA_BASE_ = 0x00E80000;
  _SDA2_BASE_ = 0x00E90000;
}
