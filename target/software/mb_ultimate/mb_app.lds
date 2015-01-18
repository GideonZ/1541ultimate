OUTPUT_FORMAT("elf32-microblaze", "elf32-microblaze",
	      "elf32-microblaze")
OUTPUT_ARCH(microblaze)
ENTRY(_premain)
_HEAP_SIZE = DEFINED(_HEAP_SIZE) ? _HEAP_SIZE : 0x0;

MEMORY
{
    DRAM (rwx)  : ORIGIN = 0x00010000, LENGTH = 0x00E70000 /* thus until 00E80000 */
}

SECTIONS
{
  .startmeuk 0x10000 :
  {
    *(.mysection)
  } > DRAM

  .init :
  {
    KEEP (*(.init))
  } > DRAM
  
  .text : {
    *(.text .stub .text.* .gnu.linkonce.t.*)
  } > DRAM
  .fini           :
  {
    KEEP (*(.fini))
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
  _heap_end = 0x00380000;

  _stack = 0x003BFFFC;
  _SDA_BASE_ = 0x00380000;
  _SDA2_BASE_ = 0x00390000;
}
