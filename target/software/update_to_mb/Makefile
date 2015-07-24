
CROSS     = zpu-elf

include ../common/environment.mk

PRJ      =  update
FINAL    =  $(RESULT)/$(PRJ).bin

SRCS_C   =	zpu.c \
			itu.c \
			dump_hex.c \
			small_printf.c

# the order of the files is important, because of the static constructors.
# main loop defines two important globals that define the event based polling mechanism:
# poll_list and the event queue. The sources following the main loop are the ones
# that register themselves in the main loop. The file manager is the second important
# root of the application. Devices can register themselves in the file system tree.
# Therefore, the file manager needs to exist.

SRCS_CC	 =  memory.cc \
			event.cc \
			poll.cc \
			flash.cc \
			at45_flash.cc \
			at49_flash.cc \
			w25q_flash.cc \
            s25fl_flash.cc \
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

SRCS_ASM =  crt0_appl.s

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

VPATH   += $(PATH_SW)/application/update_to_mb
VPATH   += ../mb_ultimate/result
VPATH   += ../mb_boot/result
VPATH   += ../mb_boot2/result

PATH_INC =  $(addprefix -I, $(VPATH))
COPTIONS = -Os -DGCC_ZPU -ffunction-sections -std=c99
CPPOPT   = -Os -DGCC_ZPU -ffunction-sections -fno-exceptions -fno-rtti -D_NO_FILE_ACCESS -DFIRST_WRITABLE_PAGE=0 -D_FPGA400=$(FPGA400)
LINK 	 = $(PATH_SW)/system/zpu.lds
LLIB     =  -L $(ZPU_TOOLCHAIN)/install/lib/gcc/zpu-elf/3.4.2 \
			-L $(ZPU_TOOLCHAIN)/install/zpu-elf/lib
LIBS     = -lstdc++ -lsupc++ -lgcc --start-group -lc -lbcc --end-group -lgcc
#LIBS     = -lstdc++ -lsupc++ --start-group -lbcc --end-group
LFLAGS   = --relax --gc-sections 

include ../common/rules_zpu.mk
 
update.o: update.cc $(INCLUDES) $(CHK_BIN)
	@echo "Compiling $(notdir $<)"
	@cat $(addprefix $(OUTPUT)/,$(CHK_BIN)) >$(OUTPUT)/checksums.h
	@$(CPP) $(CPPOPT) $(PATH_INC) -B. -c -Wa,-ahlms=$(OUTPUT)/$(@:.o=.lst) -o $(OUTPUT)/$(@F) $<

