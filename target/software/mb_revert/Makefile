
include ../common/environment.mk

PRJ        =  revert
FINAL      =  $(RESULT)/$(PRJ).u2u
LINKMETHOD = ld

LEGACY = ../../../legacy/2.6k
1ST_BOOT = $(LEGACY)/target/software/1st_boot
2ND_BOOT = $(LEGACY)/target/software/2nd_boot
ULTIMATE = $(LEGACY)/target/software/ultimate

SRCS_C   =	mbstart_rtos.c \
			malloc_lock.c \
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

SRCS_CC	 =  memory.cc \
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
			1st_boot_700.bin \
			2nd_boot.bin \
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

VPATH   += $(PATH_SW)/FreeRTOS/Source/portable/microblaze
VPATH   += $(PATH_SW)/portable/microblaze
VPATH   += $(PATH_SW)/application/mb_update
VPATH   += $(2ND_BOOT)/result
VPATH   += $(ULTIMATE)/result

PATH_INC =  $(addprefix -I, $(LEGACY)/software/application $(VPATH) $(OUTPUT))
APP_OPTS = -D_NO_FILE_ACCESS -DFIRST_WRITABLE_PAGE=0 
COPTIONS = -g -ffunction-sections -Os -std=c99 $(APP_OPTS)
CPPOPT   = -g -ffunction-sections -Os -fno-exceptions -fno-rtti -fno-threadsafe-statics $(APP_OPTS)
OPTIONS  = -nostartfiles
LINK 	 = ./mb_update.lds
LFLAGS   = --gc-sections 


include ../common/rules.mk

$(OUTPUT)/1st_boot_700.bin : $(1ST_BOOT)/output/1st_boot_700.bit
	@echo "Converting $(<F) bitfile to $(@F)"
	@$(PROMGEN) -r $< $(OUTPUT)/$(@F) 

revert.o: revert.cc $(INCLUDES) $(CHK_BIN)
	@echo "Compiling $(notdir $<)"
	@cat $(addprefix $(OUTPUT)/,$(CHK_BIN)) >$(OUTPUT)/checksums.h
	@$(CPP) $(CPPOPT) $(PATH_INC) -B. -c -Wa,-ahlms=$(OUTPUT)/$(@:.o=.lst) -o $(OUTPUT)/$(@F) $<

