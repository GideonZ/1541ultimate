OUTPUT_FORMAT("elf32-microblaze", "elf32-microblaze",
	      "elf32-microblaze")
OUTPUT_ARCH(microblaze)
ENTRY(_start)
_HEAP_SIZE = DEFINED(_HEAP_SIZE) ? _HEAP_SIZE : 0x0;

MEMORY
{
    CACHE (rx)  : ORIGIN = 0x00000000, LENGTH = 2k
    DRAM (rwx)  : ORIGIN = 0x00004000, LENGTH = 0x00E7C000 /* thus until 00E80000 */
}

SECTIONS
{
    . = 0x0000;
    .vectors.reset : { *(.vectors.reset) }
    . = 0x0008;
    .vectors.sw_exception : { *(.vectors.sw_exception) }
    . = 0x0010;
    .vectors.interrupt : { *(.vectors.interrupt) }
    . = 0x0018;
    .vectors.breakpoint : { *(.vectors.breakpoint) }
    . = 0x0020;
    .vectors.hw_exception : { *(.vectors.hw_exception) }
    . = 0x0028;
  
  .startmeuk . :
  {
    *(.mysection)
  } > CACHE

  .init :
  {
    KEEP (*(.init))
  } > CACHE
  
  .text : {
    *(.text .stub .text.* .gnu.linkonce.t.*)
  } > CACHE
  .fini           :
  {
    KEEP (*(.fini))
  } > CACHE

  end_of_text = .;

  /* DATA */

  .rodata :
  {
    *(.rodata .rodata.* .gnu.linkonce.r.*)
    *(.rodata1)
  	
  } > CACHE

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
  } > CACHE

  . = 0x4000;
  
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

   . = ALIGN(4);
  .heap : {
     _heap = .;
     _heap_start = .;
     . += _HEAP_SIZE;
     _heap_end = .;
  }
  _stack = 0x00EBFFFC;
  _SDA_BASE_ = 0x00E80000;
  _SDA2_BASE_ = 0x00E90000;
}
