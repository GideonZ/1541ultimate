# System tool locations
HERE = $(shell pwd)
TOOLS = $(shell pwd)/../../../tools
ZPU_TOOLCHAIN ?= /cygdrive/d/Proj/zpu/git/toolchain
#TOOLCHAIN = /home/gideon/Proj/zpu/toolchain/toolchain
BIN2HEX = $(TOOLS)/bin2hex
HEX2BIN = $(TOOLS)/hex2bin
PROMGEN = $(TOOLS)/promgen
CHECKSUM = $(TOOLS)/checksum

1ST_BOOT = $(HERE)/../1st_boot
2ND_BOOT = $(HERE)/../2nd_boot
ULTIMATE = $(HERE)/../ultimate

# Configuration
CROSS     = zpu-elf

# External inputs
ROMS     = ../../../roms
FPGA_400 = ../../fpga/work400/ultimate_1541_400a
FPGA_700 = ../../fpga/work700/ultimate_1541_700a


# Outputs
RESULT = $(shell pwd)/result
OUTPUT = $(shell pwd)/output

PRJ      =  update
PATH_SW  =  $(HERE)/../../../software

VPATH     = $(PATH_SW)/application \
			$(PATH_SW)/application/update \
			$(PATH_SW)/filesystem \
			$(PATH_SW)/filemanager \
			$(PATH_SW)/system \
			$(PATH_SW)/io/flash \
			$(PATH_SW)/drive \
			$(PATH_SW)/components \
			$(PATH_SW)/io/c64 \
			$(PATH_SW)/io/rtc \
			$(PATH_SW)/io/usb \
			$(PATH_SW)/io/audio \
			$(PATH_SW)/io/overlay \
			$(PATH_SW)/io/icap \
			$(PATH_SW)/io/sd_card \
			$(PATH_SW)/io/userinterface \
			$(ROMS) \
			$(ULTIMATE)/result \
			$(2ND_BOOT)/result

INCLUDES =  $(wildcard $(addsuffix /*.h, $(VPATH)))

FPGA400 ?= 0

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
			main_loop.cc \
			flash.cc \
			at45_flash.cc \
			at49_flash.cc \
			w25q_flash.cc \
			path.cc \
			pattern.cc \
			config.cc \
			c64.cc \
			screen.cc \
			keyboard.cc \
			mystring.cc \
			size_str.cc \
			rtc.cc \
			task_menu.cc \
			userinterface.cc \
			$(PRJ).cc

SRCS_ASM =  crt0_appl.s

SRCS_BIN =  ultimate.bin \
			1st_boot_700.bin \
			chars.bin \
			2nd_boot.bin \
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

ifeq	($(FPGA400), 1)
	SRCS_BIN += 1st_boot_400.bin
endif
    
PATH_INC =  $(addprefix -I, $(VPATH))
COPTIONS = -Os -DGCC_ZPU -ffunction-sections
CPPOPT   = $(COPTIONS) -fno-exceptions -fno-rtti -D_NO_FILE_ACCESS -DFIRST_WRITABLE_PAGE=0 -D_FPGA400=$(FPGA400)
LINK 	 = $(PATH_SW)/system/zpu.lds
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
OBJS_BIN = $(notdir $(SRCS_BIN:%.bin=%.bo))
CHK_BIN  = $(notdir $(SRCS_BIN:%.bin=%.chk))
ALL_OBJS = $(addprefix $(OUTPUT)/,$(OBJS_ASM) $(OBJS_C) $(OBJS_CC) $(OBJS_BIN))


.PHONY: clean all

	
all: $(OUTPUT) $(RESULT) $(RESULT)/$(PRJ).bin
#	$(MAKE) -C ../1st_boot

$(OUTPUT):
	@mkdir $(OUTPUT)

$(RESULT):
	@mkdir $(RESULT)
		
$(RESULT)/$(PRJ).bin: $(OUTPUT)/$(PRJ).out
	@echo "Creating Binary $@"
	@$(OBJCOPY) -O binary $< $@

1st_boot_700.bin: $(1ST_BOOT)/output/1st_boot_700.bit
	@echo "Converting $(<F) bitfile to $(@F)"
	@cd $(dir $<); $(PROMGEN) -r $(<F) $(OUTPUT)/$(@F) >/dev/null; cd $(HERE)
	
1st_boot_400.bin: $(1ST_BOOT)/output/1st_boot_400.bit
	@echo "Converting $(<F) bitfile to $(@F)"
	@cd $(dir $<); $(PROMGEN) -r $(<F) $(OUTPUT)/$(@F) >/dev/null; cd $(HERE)

%.bo: %.bin
	@echo "Converting $(<F) binary to $(@F).."
	@cd $(dir $(shell find $(VPATH)  -maxdepth 1 -name $(<F))); $(OBJCOPY) -I binary -O elf32-zpu --binary-architecture zpu $(<F) $(OUTPUT)/$(@F); cd $(HERE)

%.chk: %.bin
	@echo "Calculating checksum of $(<F) binary to $(@F).."
	@cd $(dir $(shell find $(VPATH)  -maxdepth 1 -name $(<F))); $(CHECKSUM) $(<F) $(OUTPUT)/$(@F); cd $(HERE)

%.o: %.s
	@echo "Compiling $(notdir $<)"
	@$(CC) $(OPTIONS) $(PATH_INC) -B. -c -Wa,-ahlms=$(OUTPUT)/$(@:.o=.lst) -o $(OUTPUT)/$(@F) $<

%.o: %.c $(INCLUDES)
	@echo "Compiling $(notdir $<)"
	@$(CC) $(COPTIONS) $(PATH_INC) -B. -c -Wa,-ahlms=$(OUTPUT)/$(@:.o=.lst) -o $(OUTPUT)/$(@F) $<

%.o: %.cc $(INCLUDES)
	@echo "Compiling $(notdir $<)"
	@$(CPP) $(CPPOPT) $(PATH_INC) -B. -c -Wa,-ahlms=$(OUTPUT)/$(@:.o=.lst) -o $(OUTPUT)/$(@F) $<

update.o: update.cc $(INCLUDES) $(CHK_BIN)
	@echo "Compiling $(notdir $<)"
	@cat $(addprefix $(OUTPUT)/,$(CHK_BIN)) >$(OUTPUT)/checksums.h
	@$(CPP) $(CPPOPT) $(PATH_INC) -B. -c -Wa,-ahlms=$(OUTPUT)/$(@:.o=.lst) -o $(OUTPUT)/$(@F) $<

$(OUTPUT)/$(PRJ).out: $(LINK) $(OBJS_C) $(OBJS_CC) $(OBJS_ASM) $(OBJS_BIN)
	@echo "Linking..."
	@$(LD) $(LLIB) $(LFLAGS) -T $(LINK) -Map=$(OUTPUT)/$(PRJ).map -o $(OUTPUT)/$(PRJ).out $(ALL_OBJS) $(LIBS)


clean:
	@-rm -rf $(OUTPUT)
	@-rm -rf $(RESULT)
