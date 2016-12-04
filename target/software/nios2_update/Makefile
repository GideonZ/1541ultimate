
CROSS	     = nios2-elf-
ELFTYPE      = elf32-littlenios2
ARCHITECTURE = nios2

include ../common/environment.mk

PRJ      =  update
FINAL    =  $(RESULT)/$(PRJ).app $(RESULT)/$(PRJ).elf

SRCS_C   =	itu.c \
			dump_hex.c \
			codec.c \
			assert.c \
			profiler.c \
			croutine.c \
            event_groups.c \
            list.c \
            queue.c \
            tasks.c \
            timers.c \
            port.c \
            heap_3.c \
            ff2.c \
            ccsbcs.c \
            ffsyscall.c \
            time.c \
            i2c.c \
            mdio.c \
            nios_main.c \
            alt_malloc_lock.c \
            alt_do_ctors.c
                        
SRCS_CC	 =  memory.cc \
			small_printf.cc \
			flash.cc \
			at45_flash.cc \
			at49_flash.cc \
			w25q_flash.cc \
            s25fl_flash.cc \
			pattern.cc \
			config.cc \
			c64.cc \
			screen.cc \
			screen_vt100.cc \
			keyboard_c64.cc \
			keyboard_vt100.cc \
			mystring.cc \
			size_str.cc \
			rtc_i2c.cc \
			task_menu.cc \
			userinterface.cc \
			ui_elements.cc \
			stream.cc \
			host_stream.cc \
			prog_flash.cc \
			stream_uart.cc \
			$(PRJ).cc

VPATH +=	$(PATH_SW)/FreeRTOS/Source/portable/nios
VPATH +=	$(PATH_SW)/portable/nios
VPATH +=	$(PATH_SW)/nios_appl_bsp
VPATH +=	$(PATH_SW)/nios_appl_bsp/HAL/inc
VPATH +=	$(PATH_SW)/nios_appl_bsp/drivers/inc
VPATH +=    $(PATH_SW)/application/update_u2p
VPATH +=    $(PATH_SW)/../target/fpga/u2plus_run/output_files
VPATH +=    $(PATH_SW)/../target/software/nios2_ultimate/result

SRCS_ASM = update_binaries.s

SRCS_ASMS = port_asm.S

SRCS_6502 = 

SRCS_BIN =  chars.bin

SRCS_IEC = 

SRCS_NANO = nano_minimal.nan

PATH_INC =  $(addprefix -I, $(VPATH))
OPTIONS  = -g -ffunction-sections -O0 -DOS -DNIOS=1 -Wno-write-strings -mno-hw-div -mno-hw-mul -mno-hw-mulx -DRECOVERYAPP=1
COPTIONS = $(OPTIONS) -std=gnu99
CPPOPT   = $(OPTIONS) -fno-exceptions -fno-rtti -fno-threadsafe-statics -fpermissive
BSP      = $(PATH_SW)/nios_appl_bsp
LINK 	 = ./linker.x
LIBS2    = $(BSP)/libhal_bsp.a ../nios2_elf_lwip/result/liblwip.a
LFLAGS   = --gc-sections -msys-crt0=$(BSP)/obj/HAL/src/crt0.o

include ../common/rules.mk

update_binaries.o: ultimate_run.swp ../nios2_ultimate/result/ultimate.bin
