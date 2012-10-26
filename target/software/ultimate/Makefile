# System tool locations

HERE = $(shell pwd)
TOOLS = ../../../tools
ZPU_TOOLCHAIN ?= /cygdrive/d/Proj/zpu/git/toolchain
#TOOLCHAIN = /home/gideon/Proj/zpu/toolchain/toolchain
BIN2HEX = $(TOOLS)/bin2hex
HEX2BIN = $(TOOLS)/hex2bin
MAKEAPPL = $(TOOLS)/makeappl
TASS = $(TOOLS)/64tass/64tass
PARSE_IEC = $(TOOLS)/parse_iec.py

BOOT = ../2nd_boot

# Configuration
SHELL     = bash
CROSS     = zpu-elf

# External inputs
ROMS = ../../../roms
BOOT_250_BIN = ../1st_boot/result/1st_boot_250.bin

# Outputs
RESULT    = $(shell pwd)/result
OUTPUT    = ./output
OUTPUT_FP = $(shell pwd)/output

PRJ      =  ultimate
PATH_SW  =  ../../../software

VPATH     = $(PATH_SW)/application \
			$(PATH_SW)/application/ultimate \
			$(PATH_SW)/filesystem \
			$(PATH_SW)/filemanager \
			$(PATH_SW)/system \
			$(PATH_SW)/io/flash \
			$(PATH_SW)/drive \
			$(PATH_SW)/components \
			$(PATH_SW)/io/stream \
			$(PATH_SW)/io/c64 \
			$(PATH_SW)/io/rtc \
			$(PATH_SW)/io/usb \
			$(PATH_SW)/io/tape \
			$(PATH_SW)/io/icap \
			$(PATH_SW)/io/sd_card \
			$(PATH_SW)/io/audio \
			$(PATH_SW)/io/overlay \
			$(PATH_SW)/io/userinterface \
			$(PATH_SW)/io/command_interface \
			$(PATH_SW)/io/copper \
			$(PATH_SW)/io/iec \
			$(PATH_SW)/6502 \
			$(ROMS)

INCLUDES =  $(wildcard $(addsuffix /*.h, $(VPATH)))


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

SRCS_CC	 =  stream.cc \
			memory.cc \
			flash.cc \
			at45_flash.cc \
			at49_flash.cc \
			w25q_flash.cc \
			config.cc \
			event.cc \
			main_loop.cc \
			filemanager.cc \
			file_device.cc \
			file_partition.cc \
			file_direntry.cc \
			filetype_d64.cc \
			filetype_g64.cc \
			filetype_t64.cc \
			filetype_prg.cc \
			filetype_tap.cc \
			filetype_sid.cc \
			filetype_reu.cc \
			filetype_iso.cc \
			filetype_crt.cc \
			filetype_bit.cc \
			rtc.cc \
			usb.cc \
			usb_hub.cc \
			usb_scsi.cc \
			usb_em1010.cc \
			c64.cc \
			screen.cc \
			keyboard.cc \
			disk_image.cc \
			c1541.cc \
			bam_header.cc \
			mystring.cc \
			path.cc \
			pattern.cc \
			blockdev.cc \
			blockdev_file.cc \
			disk.cc \
			partition.cc \
			file_system.cc \
			directory.cc \
			file.cc \
			fat_fs.cc \
			fat_dir.cc \
			fatfile.cc \
			ccsbcs.cc \
			iso9660.cc \
			sd_card.cc \
			sdio.cc \
			sdcard_manager.cc \
			tape_controller.cc \
			tape_recorder.cc \
			size_str.cc \
			userinterface.cc \
			editor.cc \
			ui_stream.cc \
			tree_browser.cc \
			tree_browser_state.cc \
			stream_menu.cc \
			config_menu.cc \
			context_menu.cc \
			task_menu.cc \
			audio_select.cc \
			iec.cc \
			$(PRJ).cc

####			sampler.cc \
####			copper.cc \
####			command_intf.cc \

SRCS_ASM =  crt0_appl.s

SRCS_6502 = sidcrt.tas bootcrt.tas cmd_test_rom.tas

SRCS_BIN =  chars.bin 1541.bin 1541c.bin 1541-ii.bin sounds.bin

SRCS_IEC = iec_code.txt

PATH_INC =  $(addprefix -I, $(VPATH))
COPTIONS = -Os -DGCC_ZPU -ffunction-sections
CPPOPT   = $(COPTIONS) -fno-exceptions -fno-rtti
LINK 	 = $(PATH_SW)/system/zpu.lds
LINK_V1	 = $(PATH_SW)/system/zpu_v1.lds
LLIB     =  -L $(ZPU_TOOLCHAIN)/install/lib/gcc/zpu-elf/3.4.2 \
			-L $(ZPU_TOOLCHAIN)/install/zpu-elf/lib
LIBS     = -lstdc++ -lsupc++ -lgcc --start-group -lc -lbcc --end-group -lgcc
#LIBS     = -lstdc++ -lsupc++ --start-group -lbcc --end-group
LFLAGS   = --relax --gc-sections 

VPATH   += $(OUTPUT) $(RESULT)

CC		  = $(CROSS)-gcc
CPP		  = $(CROSS)-g++
LD		  = $(CROSS)-ld
OBJDUMP   = $(CROSS)-objdump
OBJCOPY	  = $(CROSS)-objcopy

.SUFFIXES:

OBJS_ASM = $(notdir $(SRCS_ASM:%.s=%.o))
OBJS_C   = $(notdir $(SRCS_C:%.c=%.o))
OBJS_CC  = $(notdir $(SRCS_CC:%.cc=%.o))
OBJS_6502 = $(notdir $(SRCS_6502:%.tas=%.o))
OBJS_IEC = $(notdir $(SRCS_IEC:%.txt=%.o))
OBJS_BIN = $(notdir $(SRCS_BIN:%.bin=%.bo))

ALL_OBJS      = $(addprefix $(OUTPUT)/,$(OBJS_6502) $(OBJS_ASM) $(OBJS_C) $(OBJS_CC) $(OBJS_BIN) $(OBJS_IEC))
ALL_DEP_OBJS  = $(addprefix $(OUTPUT)/,$(OBJS_C) $(OBJS_CC))


.PHONY: hex clean all dep appl

all: $(OUTPUT) $(RESULT) $(RESULT)/$(PRJ).bin
#	$(MAKE) -C ../1st_boot

$(OUTPUT):
	@mkdir $(OUTPUT)

$(RESULT):
	@mkdir $(RESULT)
		
$(RESULT)/$(PRJ).bin: $(OUTPUT)/$(PRJ).out
	@echo "Creating Binary $@"
	@$(OBJCOPY) -O binary $< $@

$(RESULT)/$(PRJ)_V1.bin: $(OUTPUT)/$(PRJ)_V1.out
	@echo "Creating Binary $@"
	@$(OBJCOPY) -O binary $< $@

%.bo: %.bin
	@echo "Converting $(<F) binary to $(@F).."
	@cd $(dir $(shell find $(VPATH)  -maxdepth 1 -name $(<F))); $(OBJCOPY) -I binary -O elf32-zpu --binary-architecture zpu $(<F) $(OUTPUT_FP)/$(@F); cd $(HERE)

%.65: %.tas
	@echo "Assembling $<"
	$(TASS) $< --m6502 --nostart -o $(OUTPUT)/$(@F)

%.iec: %.txt
	@echo "Assembling IEC processor code $(<F)"
	@python $(PARSE_IEC) $< $(OUTPUT)/$(@F) >python_out

%.o: %.iec
	@echo "Converting $(<F) binary to .o"
	@cd $(OUTPUT); $(OBJCOPY) -I binary -O elf32-zpu --binary-architecture zpu $(<F) $@

%.o: %.65
	@echo "Converting .65 binary to .o"
	@cd $(OUTPUT); $(OBJCOPY) -I binary -O elf32-zpu --binary-architecture zpu $(<F) $@
    	
%.o: %.s
	@echo "Compiling $<"
	@$(CC) $(OPTIONS) $(PATH_INC) -B. -c -Wa,-ahlms=$(OUTPUT)/$(@:.o=.lst) -o $(OUTPUT)/$(@F) $<

%.o: %.c
	@echo "Compiling $<"
	@$(CC) $(COPTIONS) $(PATH_INC) -B. -c -Wa,-ahlms=$(OUTPUT)/$(@:.o=.lst) -o $(OUTPUT)/$(@F) $<

%.o: %.cc
	@echo "Compiling $<"
	@$(CPP) $(CPPOPT) $(PATH_INC) -B. -c -o $(OUTPUT)/$(@F) $<
	@$(CPP) -MM $(PATH_INC) $< >$(OUTPUT)/$(@F:.o=.d)

%.d: %.cc
	@$(CPP) -MM $(PATH_INC) $< >$(OUTPUT)/$(@F:.o=.d)

%.d: %.c
	@$(CC) -MM $(PATH_INC) $< >$(OUTPUT)/$(@F:.o=.d)

$(OUTPUT)/$(PRJ).out: $(LINK) $(OBJS_C) $(OBJS_CC) $(OBJS_ASM) $(OBJS_6502) $(OBJS_BIN) $(OBJS_IEC)
	@echo "Linking..."
	@$(LD) $(LLIB) $(LFLAGS) -T $(LINK) -Map=$(OUTPUT)/$(PRJ).map -o $(OUTPUT)/$(PRJ).out $(ALL_OBJS) $(LIBS)

$(OUTPUT)/$(PRJ)_V1.out: $(LINK_V1) $(OBJS_C) $(OBJS_CC) $(OBJS_ASM) $(OBJS_6502) $(OBJS_BIN) $(OBJS_IEC)
	@echo "Linking..."
	@$(LD) $(LLIB) $(LFLAGS) -T $(LINK_V1) -Map=$(OUTPUT)/$(PRJ).map -o $(OUTPUT)/$(PRJ)_V1.out $(ALL_OBJS) $(LIBS)

# pull in dependency info for *existing* .o files
-include $(ALL_DEP_OBJS:.o=.d)

clean:
	@-rm -rf $(OUTPUT)
	@-rm -rf $(RESULT)

dep:  $(OBJS_CC:.o=.d) $(OBJS_C:.o=.d)
    

# Version number should not be hard coded here, but be taken from versions.h (or vice versa: generate versions.h before compiling)
hex: $(OUTPUT) $(RESULT) $(RESULT)/$(PRJ).bin
	@$(MAKE) -C ../2nd_boot hex
	@$(BIN2HEX) -Z -o 0x108000 $(RESULT)/$(PRJ).bin $(RESULT)/$(PRJ).mcs
	@$(BIN2HEX) -Z -o 0x108000 -v "2.0RCA" -t $(RESULT)/$(PRJ).bin $(OUTPUT)/_flash.mcs
	@cat ../2nd_boot/result/flash_700.mcs         >>$(OUTPUT)/_flash.mcs
	@$(HEX2BIN) -o 0x000000 -s 0x138000 $(OUTPUT)/_flash.mcs $(RESULT)/flash_700.bin
	@$(BIN2HEX) -o 0x000000 $(RESULT)/flash_700.bin $(RESULT)/flash_700.mcs
	@rm $(OUTPUT)/_flash.mcs
	@$(BIN2HEX) -Z -o 0x108000 -v "2.0RCA" -t $(RESULT)/$(PRJ).bin $(OUTPUT)/_flash.mcs
	@cat ../2nd_boot/result/flash_400.mcs         >>$(OUTPUT)/_flash.mcs
	@$(HEX2BIN) -o 0x000000 -s 0x138000 $(OUTPUT)/_flash.mcs $(RESULT)/flash_400.bin
	@$(BIN2HEX) -o 0x000000 $(RESULT)/flash_400.bin $(RESULT)/flash_400.mcs
	@rm $(OUTPUT)/_flash.mcs
	@echo Done!

appl: $(OUTPUT) $(RESULT) $(RESULT)/$(PRJ)_V1.bin $(BOOT_250_BIN)
	@$(MAKEAPPL) $(RESULT)/appl.bin $(BOOT_250_BIN) 0 $(RESULT)/$(PRJ)_V1.bin 30000
