
CROSS	     = nios2-elf-
ELFTYPE      = elf32-littlenios2
ARCHITECTURE = nios2

include ../common/environment.mk

PRJ      =  flash_fpga
FINAL    =  $(RESULT)/$(PRJ).bin $(RESULT)/$(PRJ).elf 

SRCS_C   =	itu.c \
			dump_hex.c \
            alt_do_ctors.c
                        
SRCS_CC	 =	memory.cc \
			prog_flash.cc \
			small_printf.cc \
            stream.cc \
			stream_uart.cc \
			flash.cc \
			at45_flash.cc \
			w25q_flash.cc \
			s25fl_flash.cc \
			config.cc \
			rtc.cc \
			c64.cc \
			screen.cc \
			screen_vt100.cc \
			keyboard_c64.cc \
			keyboard_vt100.cc \
			mystring.cc \
            host_stream.cc \
			$(PRJ).cc

VPATH +=	$(PATH_SW)/system/altera
VPATH +=	$(PATH_SW)/niosbsp
VPATH +=	$(PATH_SW)/niosbsp/HAL/inc
VPATH +=	$(PATH_SW)/niosbsp/drivers/inc
VPATH +=	$(PATH_SW)/application/flasher
VPATH +=    $(PATH_SW)/../target/fpga/u2c5/output_files

SRCS_ASM =

SRCS_ASMS = 

SRCS_6502 = 

SRCS_RBF =  ultimate_altera.srec

SRCS_BIN = chars.bin

SRCS_IEC = 

SRCS_NANO =

PATH_INC =  $(addprefix -I, $(VPATH))
OPTIONS  = -g -ffunction-sections -O0 -DNIOS -D_NO_FILE_ACCESS -Wno-write-strings -mno-hw-div -mno-hw-mul -mno-hw-mulx
COPTIONS = $(OPTIONS) -std=gnu99
CPPOPT   = $(OPTIONS) -fno-exceptions -fno-rtti -fno-threadsafe-statics -fpermissive
LINK 	 = ./mb_app.lds
LLIB     =  -L $(PATH_SW)/niosbsp \
			-L ../nios2_elf_lwip/result \
            -L $(NIOS_TOOLCHAIN)/nios2-elf/lib/mno-hw-mul \
            -L $(NIOS_TOOLCHAIN)/lib/gcc/nios2-elf/4.8.3/mno-hw-mul
# NOTE: The nios toolchain points to 
LIBS     =  -lc -lhal_bsp -llwip -lc -lgcc 
LFLAGS   = --gc-sections 

include ../common/rules.mk
