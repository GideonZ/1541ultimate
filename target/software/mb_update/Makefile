
include ../common/environment.mk

PRJ      =  update
FINAL    =  $(RESULT)/$(PRJ).u2u

SRCS_C   =	mbstart_rtos.c \
			itu.c \
			dump_hex.c \
			small_printf.c \
			profiler.c \
			croutine.c \
            event_groups.c \
            list.c \
            queue.c \
            tasks.c \
            timers.c \
            port.c \
            heap_3.c

SRCS_CC	 =  memory_rtos.cc \
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
			rtc.cc \
			task_menu.cc \
			userinterface.cc \
			ui_elements.cc \
			stream.cc \
			host_stream.cc \
			stream_uart.cc \
			$(PRJ).cc

SRCS_ASM =  mb_crt0.s \
            portasm.s

SRCS_6502 = 

SRCS_BIN =  ultimate.bin \
			mb_boot_700.bin \
			mb_boot2.bin \
			chars.bin \
			1541-ii.bin \
			1541.bin \
			1541c.bin \
			ar5ntsc.bin \
			ar5pal.bin \
			ar6pal.bin \
			epyx.bin \
			final3.bin \
			rr38ntsc.bin \
			rr38pal.bin \
			sounds.bin \
			ss5ntsc.bin \
			ss5pal.bin \
			tar_ntsc.bin \
			tar_pal.bin 

SRCS_IEC = 

SRCS_NANO = 

VPATH   += $(PATH_SW)/application/mb_update
VPATH   += ../mb_ultimate/result
VPATH   += ../mb_boot/result
VPATH   += ../mb_boot2/result

PATH_INC =  $(addprefix -I, $(VPATH) $(OUTPUT))
APP_OPTS = -D_NO_FILE_ACCESS -DFIRST_WRITABLE_PAGE=0 -DUPDATER
COPTIONS = -g -ffunction-sections -Os -std=c99 $(APP_OPTS)
CPPOPT   = -g -ffunction-sections -Os -fno-exceptions -fno-rtti -fno-threadsafe-statics $(APP_OPTS)
LINK 	 = ./mb_update.lds
LLIB     =  -L $(TOOLCHAIN)/microblaze-xilinx-elf/lib \
            -L $(TOOLCHAIN)/lib/gcc/microblaze-xilinx-elf/4.1.2
LIBS     =  -lc -lgcc 
LFLAGS   = --gc-sections 


include ../common/rules.mk
 

update.o: update.cc $(INCLUDES) $(CHK_BIN)
	@echo "Compiling $(notdir $<)"
	@cat $(addprefix $(OUTPUT)/,$(CHK_BIN)) >$(OUTPUT)/checksums.h
	@$(CPP) $(CPPOPT) $(PATH_INC) -B. -c -Wa,-ahlms=$(OUTPUT)/$(@:.o=.lst) -o $(OUTPUT)/$(@F) $<

