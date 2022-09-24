
include ../common/environment.mk

PRJ        =  update
FINAL      =  $(RESULT)/$(PRJ).u2u
LINKMETHOD = ld

SRCS_C   =	mbstart_rtos.c \
			malloc_lock.c \
			itu.c \
			dump_hex.c \
			profiler.c \
			croutine.c \
            event_groups.c \
            list.c \
            queue.c \
            tasks.c \
            timers.c \
            port.c \
			assert.c \
            heap_3.c \
            ff.c \
            ffsystem.c \
            ffunicode.c \
            time.c

SRCS_CC	 =  memory.cc \
			small_printf.cc \
			flash.cc \
			at45_flash.cc \
			w25q_flash.cc \
            s25fl_flash.cc \
			pattern.cc \
			config.cc \
			filemanager.cc \
			file_device.cc \
			file_partition.cc \
			filesystem_root.cc \
			init_function.cc \
			c64.cc \
			screen.cc \
			screen_vt100.cc \
			keyboard_c64.cc \
			keyboard_vt100.cc \
			keyboard_usb.cc \
			mystring.cc \
			size_str.cc \
			rtc.cc \
			task_menu.cc \
			userinterface.cc \
			ui_elements.cc \
			stream.cc \
			host_stream.cc \
			stream_uart.cc \
			path.cc \
			blockdev.cc \
			blockdev_flash.cc \
			disk.cc \
			partition.cc \
			file_system.cc \
			diskio.cc \
			directory.cc \
			file.cc \
			filesystem_fat.cc \
			prog_flash.cc \
			$(PRJ).cc

SRCS_ASM =  mb_crt0.s \
            portasm.s

SRCS_6502 = 

SRCS_BIN =  ultimate.bin \
			mb_boot_700.bin \
			mb_boot2.bin \
			chars.bin \
			1541.bin \
			snds1541.bin

SRCS_IEC = 

SRCS_NANO = 

VPATH   += $(PATH_SW)/FreeRTOS/Source/portable/microblaze
VPATH   += $(PATH_SW)/application/mb_update
VPATH   += $(PATH_SW)/portable/microblaze
VPATH   += ../mb_ultimate/result
VPATH   += ../mb_boot_dd/result
VPATH   += ../mb_boot2/result

PATH_INC =  $(addprefix -I, $(VPATH) $(OUTPUT))
APP_OPTS = -DFIRST_WRITABLE_PAGE=0 -DOS=1 -DSAFEMODE=1 -DUPDATER -DRECOVERYAPP=1
COPTIONS = -g -ffunction-sections -Os -mcpu=v6.00.a -std=c99 $(APP_OPTS) -DRECOVERYAPP=1
CPPOPT   = -g -ffunction-sections -Os -mcpu=v6.00.a -fno-exceptions -fno-rtti -fno-threadsafe-statics $(APP_OPTS)
#OPTIONS  = -nostartfiles
LINK 	 = ./mb_update.lds
LFLAGS   = --gc-sections 

include ../common/rules.mk

update.o: update.cc $(INCLUDES) $(CHK_BIN)
	@echo "Compiling $(notdir $<)"
	@cat $(addprefix $(OUTPUT)/,$(CHK_BIN)) >$(OUTPUT)/checksums.h
	@$(CPP) $(CPPOPT) $(PATH_INC) -B. -c -Wa,-ahlms=$(OUTPUT)/$(@:.o=.lst) -o $(OUTPUT)/$(@F) $<

