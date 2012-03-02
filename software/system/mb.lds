OUTPUT_FORMAT("elf32-microblaze", "elf32-microblaze",
	      "elf32-microblaze")
OUTPUT_ARCH(microblaze)
ENTRY(_start)
_HEAP_SIZE = DEFINED(_HEAP_SIZE) ? _HEAP_SIZE : 0x0;

MEMORY
{
    BRAM (rx)  : ORIGIN = 0x00000000, LENGTH = 8k
    DRAM (rwx) : ORIGIN = 0x00010000, LENGTH = 0x00EB0000
}

SECTIONS
{
  .fixed_vectors (NOLOAD) :
  {
	. = .;
    KEEP (*(.fixed_vectors))
  } = 0

  .startmeuk 0x20000:
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

  .rodata      ALIGN(4)    :
  {
    *(.rodata .rodata.* .gnu.linkonce.r.*)
    *(.rodata1)
  	. = ALIGN(4); /* Enforce alignment of .data below */
  } > DRAM

    /* Now we want the data to load, but not to live in that
       area of the ram. So we load the initialized data here,
       but we try to let it live behind our loaded image. */

    end_of_text = .;

    __ram_start = .;
    
/*  .data 0x10000 : AT (end_of_text) */
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
  }

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
  _stack = 0x0ebfffc;
  
/*
  .stack 0xff0000:
  {
    *(.stack)
  }  
*/
}
