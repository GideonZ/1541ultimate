/*
 * linker.x - Linker script
 */

MEMORY
{
    memory : ORIGIN = 0x00030000, LENGTH = 0xE60000
}

OUTPUT_FORMAT( "elf32-littleriscv",
               "elf32-littleriscv",
               "elf32-littleriscv" )
OUTPUT_ARCH( riscv )
ENTRY( _start )

SECTIONS
{
  /* start section on WORD boundary */
  . = ALIGN(4);

  /* Actual instructions */
  .text :
  {
    PROVIDE(__text_start = .);
    PROVIDE(__textstart = .);

    *(.text.crt0)

    PROVIDE_HIDDEN (__rela_iplt_start = .);
    *(.rela.iplt)
    PROVIDE_HIDDEN (__rela_iplt_end = .);

    *(.rela.plt)

    KEEP(*(.text.boot)); /* keep start-up code at the beginning of rom */

    KEEP (*(SORT_NONE(.init)))

    *(.text.unlikely .text.*_unlikely .text.unlikely.*)
    *(.text.exit .text.exit.*)
    *(.text.startup .text.startup.*)
    *(.text.hot .text.hot.*)
    *(SORT(.text.sorted.*))
    *(.text .stub .text.* .gnu.linkonce.t.*)
    /* .gnu.warning sections are handled specially by elf.em.  */
    *(.gnu.warning)

    KEEP (*(SORT_NONE(.fini)))

    /* gcc uses crtbegin.o to find the start of
       the constructors, so we make sure it is
       first.  Because this is a wildcard, it
       doesn't matter if the user does not
       actually link against crtbegin.o; the
       linker won't look for a file to match a
       wildcard.  The wildcard also means that it
       doesn't matter which directory crtbegin.o
       is in.  */
    KEEP (*crtbegin.o(.ctors))
    KEEP (*crtbegin?.o(.ctors))
    /* We don't want to include the .ctor section from
       the crtend.o file until after the sorted ctors.
       The .ctor section from the crtend file contains the
       end of ctors marker and it must be last */
    KEEP (*(EXCLUDE_FILE (*crtend.o *crtend?.o ) .ctors))
    KEEP (*(SORT(.ctors.*)))
    KEEP (*(.ctors))

    KEEP (*crtbegin.o(.dtors))
    KEEP (*crtbegin?.o(.dtors))
    KEEP (*(EXCLUDE_FILE (*crtend.o *crtend?.o ) .dtors))
    KEEP (*(SORT(.dtors.*)))
    KEEP (*(.dtors))

    /* finish section on WORD boundary */
    . = ALIGN(4);

    PROVIDE (__etext = .);
    PROVIDE (_etext = .);
    PROVIDE (etext = .);
  } > memory


  /* read-only data, appended to .text */
  .rodata :
  {
    PROVIDE_HIDDEN (__init_array_start = .);
    KEEP (*(SORT_BY_INIT_PRIORITY(.init_array.*) SORT_BY_INIT_PRIORITY(.ctors.*)))
    KEEP (*(.init_array EXCLUDE_FILE (*crtbegin.o *crtbegin?.o *crtend.o *crtend?.o ) .ctors))
    PROVIDE_HIDDEN (__init_array_end = .);

    PROVIDE_HIDDEN (__fini_array_start = .);
    KEEP (*(SORT_BY_INIT_PRIORITY(.fini_array.*) SORT_BY_INIT_PRIORITY(.dtors.*)))
    KEEP (*(.fini_array EXCLUDE_FILE (*crtbegin.o *crtbegin?.o *crtend.o *crtend?.o ) .dtors))
    PROVIDE_HIDDEN (__fini_array_end = .);

    *(.rodata .rodata.* .gnu.linkonce.r.*)
    *(.rodata1)

    /* finish section on WORD boundary */
    . = ALIGN(4);
  } > memory


  /* initialized read/write data, accessed in RAM, placed in ROM, copied during boot */
  .data :
  {
    __DATA_BEGIN__ = .;
    __SDATA_BEGIN__ = .;
    *(.sdata2 .sdata2.* .gnu.linkonce.s2.*)
    *(.data1)
    *(.data .data.* .gnu.linkonce.d.*)
    SORT(CONSTRUCTORS)

    *(.data.rel.ro.local* .gnu.linkonce.d.rel.ro.local.*) *(.data.rel.ro .data.rel.ro.* .gnu.linkonce.d.rel.ro.*)
    *(.dynamic)

    /* We want the small data sections together, so single-instruction offsets
       can access them all, and initialized data all before uninitialized, so
       we can shorten the on-disk segment size.  */

    *(.srodata.cst16) *(.srodata.cst8) *(.srodata.cst4) *(.srodata.cst2) *(.srodata .srodata.*)
    *(.sdata .sdata.* .gnu.linkonce.s.*)

    PROVIDE_HIDDEN (__tdata_start = .);
    *(.tdata .tdata.* .gnu.linkonce.td.*)


    /* finish section on WORD boundary */
    . = ALIGN(4);

    _edata = .; PROVIDE (edata = .);
    . = .;
    __DATA_END__ = .;

  } > memory 

  /* zero/non-initialized read/write data placed in RAM */
  .bss (NOLOAD):
  {
    . = ALIGN(4);
    __BSS_START__ = .;
    __global_pointer$ = __BSS_START__ + 0x800;
    *(.dynsbss)
    *(.sbss .sbss.* .gnu.linkonce.sb.*)
    *(.sbss2 .sbss2.* .gnu.linkonce.sb2.*)
    *(.tbss .tbss.* .gnu.linkonce.tb.*) *(.tcommon)
    *(.scommon)
    *(.dynbss)
    *(.bss .bss.* .gnu.linkonce.b.*)

    PROVIDE_HIDDEN (__preinit_array_start = .);
    KEEP (*(.preinit_array))
    PROVIDE_HIDDEN (__preinit_array_end = .);

    *(COMMON)
    /* Align here to ensure that the .bss section occupies space up to
       _end.  Align after .bss to ensure correct alignment even if the
       .bss section disappears because there are no input sections.
       FIXME: Why do we need it? When there is no .bss section, we do not
       pad the .data section.  */
    . = ALIGN(. != 0 ? 32 / 8 : 1);

    . = ALIGN(4);
    __BSS_END__ = .;
    _end = .; PROVIDE (end = .);
  } > memory

  /* heap for dynamic memory allocation (use carefully!) */
  .heap :
  {
    PROVIDE(__heap_start = .);
    PROVIDE(__heap_limit = ORIGIN(memory) + LENGTH(memory));
	PROVIDE(__heap_end = ORIGIN(memory) + LENGTH(memory));
  } > memory


  /* Yet unused */
  .jcr                : { KEEP (*(.jcr)) }
  .got                : { *(.got.plt) *(.igot.plt) *(.got) *(.igot) }  .interp         : { *(.interp) }
  .note.gnu.build-id  : { *(.note.gnu.build-id) }
  .hash               : { *(.hash) }
  .gnu.hash           : { *(.gnu.hash) }
  .dynsym             : { *(.dynsym) }
  .dynstr             : { *(.dynstr) }
  .gnu.version        : { *(.gnu.version) }
  .gnu.version_d      : { *(.gnu.version_d) }
  .gnu.version_r      : { *(.gnu.version_r) }
  .rela.init          : { *(.rela.init) }
  .rela.text          : { *(.rela.text .rela.text.* .rela.gnu.linkonce.t.*) }
  .rela.fini          : { *(.rela.fini) }
  .rela.rodata        : { *(.rela.rodata .rela.rodata.* .rela.gnu.linkonce.r.*) }
  .rela.data.rel.ro   : { *(.rela.data.rel.ro .rela.data.rel.ro.* .rela.gnu.linkonce.d.rel.ro.*) }
  .rela.data          : { *(.rela.data .rela.data.* .rela.gnu.linkonce.d.*) }
  .rela.tdata         : { *(.rela.tdata .rela.tdata.* .rela.gnu.linkonce.td.*) }
  .rela.tbss          : { *(.rela.tbss .rela.tbss.* .rela.gnu.linkonce.tb.*) }
  .rela.ctors         : { *(.rela.ctors) }
  .rela.dtors         : { *(.rela.dtors) }
  .rela.got           : { *(.rela.got) }
  .rela.sdata         : { *(.rela.sdata .rela.sdata.* .rela.gnu.linkonce.s.*) }
  .rela.sbss          : { *(.rela.sbss .rela.sbss.* .rela.gnu.linkonce.sb.*) }
  .rela.sdata2        : { *(.rela.sdata2 .rela.sdata2.* .rela.gnu.linkonce.s2.*) }
  .rela.sbss2         : { *(.rela.sbss2 .rela.sbss2.* .rela.gnu.linkonce.sb2.*) }
  .rela.bss           : { *(.rela.bss .rela.bss.* .rela.gnu.linkonce.b.*) }


  /* Stabs debugging sections.  */
  .stab          0 : { *(.stab) }
  .stabstr       0 : { *(.stabstr) }
  .stab.excl     0 : { *(.stab.excl) }
  .stab.exclstr  0 : { *(.stab.exclstr) }
  .stab.index    0 : { *(.stab.index) }
  .stab.indexstr 0 : { *(.stab.indexstr) }
  .comment       0 : { *(.comment) }
  .gnu.build.attributes : { *(.gnu.build.attributes .gnu.build.attributes.*) }
  /* DWARF debug sections.
     Symbols in the DWARF debugging sections are relative to the beginning
     of the section so we begin them at 0.  */
  /* DWARF 1 */
  .debug          0 : { *(.debug) }
  .line           0 : { *(.line) }
  /* GNU DWARF 1 extensions */
  .debug_srcinfo  0 : { *(.debug_srcinfo) }
  .debug_sfnames  0 : { *(.debug_sfnames) }
  /* DWARF 1.1 and DWARF 2 */
  .debug_aranges  0 : { *(.debug_aranges) }
  .debug_pubnames 0 : { *(.debug_pubnames) }
  /* DWARF 2 */
  .debug_info     0 : { *(.debug_info .gnu.linkonce.wi.*) }
  .debug_abbrev   0 : { *(.debug_abbrev) }
  .debug_line     0 : { *(.debug_line .debug_line.* .debug_line_end) }
  .debug_frame    0 : { *(.debug_frame) }
  .debug_str      0 : { *(.debug_str) }
  .debug_loc      0 : { *(.debug_loc) }
  .debug_macinfo  0 : { *(.debug_macinfo) }
  /* SGI/MIPS DWARF 2 extensions */
  .debug_weaknames 0 : { *(.debug_weaknames) }
  .debug_funcnames 0 : { *(.debug_funcnames) }
  .debug_typenames 0 : { *(.debug_typenames) }
  .debug_varnames  0 : { *(.debug_varnames) }
  /* DWARF 3 */
  .debug_pubtypes 0 : { *(.debug_pubtypes) }
  .debug_ranges   0 : { *(.debug_ranges) }
  /* DWARF Extension.  */
  .debug_macro    0 : { *(.debug_macro) }
  .debug_addr     0 : { *(.debug_addr) }
  .gnu.attributes 0 : { KEEP (*(.gnu.attributes)) }
  /DISCARD/ : { *(.note.GNU-stack) *(.gnu_debuglink) *(.gnu.lto_*) }
}


  /* Provide symbols for neorv32 crt0 start-up code */
  PROVIDE(__ctr0_imem_begin          = ORIGIN(memory));
  PROVIDE(__ctr0_dmem_begin          = ORIGIN(memory));
  PROVIDE(__crt0_stack_begin         = (__heap_limit - 4));
  PROVIDE(__crt0_bss_start           = __BSS_START__);
  PROVIDE(__crt0_bss_end             = __BSS_END__);
  PROVIDE(__crt0_copy_data_src_begin = LOADADDR(.data));
  PROVIDE(__crt0_copy_data_dst_begin = ADDR(.data));
  PROVIDE(__crt0_copy_data_dst_end   = ADDR(.data) + SIZEOF(.data));
  PROVIDE(__crt0_io_space_begin      = ORIGIN(iodev));
  PROVIDE(__crt0_io_space_end        = ORIGIN(iodev) + LENGTH(iodev));


/* User defines
 * for REU and RAMDISK, Cartridge ROM, RAM, etc
 */
PROVIDE( __kernal_area   = 0x00EA8000 );
PROVIDE( __drive_b_sound = 0x00EB0000 );
PROVIDE( __drive_a_sound = 0x00EC0000 );
PROVIDE( __drive_b_area  = 0x00ED0000 );
PROVIDE( __drive_a_area  = 0x00EE0000 );

PROVIDE( __cart_ram_start = 0x00EF0000 );
PROVIDE( __cart_ram_limit = 0x00F00000 );

PROVIDE( __cart_rom_start = 0x00F00000 );
PROVIDE( __cart_rom_limit = 0x01000000 );

PROVIDE( __reu_ram_start = 0x01000000 );
PROVIDE( __reu_ram_limit = 0x02000000 );

PROVIDE( __ram_disk_start = 0x02000000 );
PROVIDE( __ram_disk_limit = 0x03000000 );

PROVIDE( __updater_start = 0x03000000 );
PROVIDE( __updater_limit = 0x04000000 );
