OUTPUT_FORMAT("elf32-microblaze", "elf32-microblaze",
	      "elf32-microblaze")
OUTPUT_ARCH(microblaze)
ENTRY(_premain)

MEMORY
{
    PROG (rx)   : ORIGIN = 0x00010000, LENGTH = 0x000F0000 /* thus until 00100000 */
    DRAM (rw)   : ORIGIN = 0x00100000, LENGTH = 0x00D70000 /* thus until 00E80000 */
}

SECTIONS
{
  .text 0x10000 : {
    *(.text.start)
    *(.text .stub .text.* .gnu.linkonce.t.*)
  } > PROG
  .fini           :
  {
    KEEP (*(.fini))
  } > PROG

  .init :
  {
    KEEP (*(.init))
  } > PROG
  
  end_of_text = .;

  /* DATA */

  .rodata :
  {
    *(.rodata .rodata.* .gnu.linkonce.r.*)
    *(.rodata1)
  	
  } > PROG

  .ctors ALIGN(4):
  {
    __constructor_list = .;
    KEEP (*(.ctors))
    __end_of_constructors = .;
    
    __destructor_list = .;
    KEEP (*(.dtors))
    __end_of_destructors = .;
  } > PROG

  .data :
  {
    __data_start = . ;
    *(.data .data.* .gnu.linkonce.d.* .gcc_except_table)

    KEEP (*(.jcr))
/*     SORT(CONSTRUCTORS)  */
    __data_end = .;
  } > DRAM AT>PROG

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
  __data_load = LOADADDR(.data);

  .heap ALIGN(32) : {
     _heap = .;
     _heap_start = .;
  } 
  _heap_end = 0x00E70000;
  _stack = 0x00E7FFFC;
  _SDA_BASE_ = 0x00E80000;
  _SDA2_BASE_ = 0x00E90000;
}

/* User defines
 * for REU and RAMDISK, Cartridge ROM, RAM, etc
 */
PROVIDE( __kernal_area   = 0x0EA8000 );
PROVIDE( __drive_b_sound = 0x0EB0000 );
PROVIDE( __drive_a_sound = 0x0EC0000 );
PROVIDE( __drive_b_area  = 0x0ED0000 );
PROVIDE( __drive_a_area  = 0x0EE0000 );

PROVIDE( __cart_ram_start = 0x0EF0000 );
PROVIDE( __cart_ram_limit = 0x0F00000 );

PROVIDE( __cart_rom_start = 0x0F00000 );
PROVIDE( __cart_rom_limit = 0x1000000 );

PROVIDE( __reu_ram_start = 0x1000000 );
PROVIDE( __reu_ram_limit = 0x2000000 );

