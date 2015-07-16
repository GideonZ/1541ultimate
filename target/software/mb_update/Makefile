# System tool locations

HERE = $(shell pwd)
TOOLS = $(shell pwd)/../../../tools
BIN2HEX = $(TOOLS)/bin2hex
HEX2BIN = $(TOOLS)/hex2bin
PROMGEN = $(TOOLS)/promgen
MAKEAPPL = $(TOOLS)/makeappl
MAKEMEM   = $(TOOLS)/make_mem
TASS = $(TOOLS)/64tass/64tass
PARSE_IEC = $(TOOLS)/parse_iec.py
PARSE_NANO = $(TOOLS)/parse_nano.py
1ST_BOOT = $(HERE)/../mb_boot
2ND_BOOT = $(HERE)/../mb_boot2
ULTIMATE = $(HERE)/../mb_ultimate
CHECKSUM = $(TOOLS)/checksum

# Configuration
SHELL     = bash
CROSS     = mb

# External inputs
ROMS = ../../../roms
LEGACY = ../../../legacy/2.6k

# Outputs
RESULT = ./result
OUTPUT = ./output

RESULT_FP = $(shell pwd)/result
OUTPUT_FP = $(shell pwd)/output

PRJ      =  update
PATH_SW  =  ../../../software

VPATH     = $(LEGACY)/software/application \
			$(PATH_SW)/application/mb_update \
			$(PATH_SW)/filesystem \
			$(PATH_SW)/filemanager \
			$(PATH_SW)/system \
			$(PATH_SW)/infra \
			$(PATH_SW)/io/flash \
			$(PATH_SW)/drive \
			$(PATH_SW)/components \
			$(PATH_SW)/io/stream \
			$(PATH_SW)/io/c64 \
			$(PATH_SW)/io/rtc \
			$(PATH_SW)/io/icap \
			$(PATH_SW)/io/sd_card \
			$(PATH_SW)/io/audio \
			$(PATH_SW)/io/overlay \
			$(PATH_SW)/io/command_interface \
			$(PATH_SW)/userinterface \
			$(PATH_SW)/FreeRTOS/Source \
			$(PATH_SW)/FreeRTOS/Source/include \
			$(PATH_SW)/FreeRTOS/Source/portable \
			$(PATH_SW)/FreeRTOS/Source/MemMang \
            $(ULTIMATE)/result \
            $(2ND_BOOT)/result \
			$(ROMS)

INCLUDES =  $(wildcard $(addsuffix /*.h, $(VPATH)))


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

# the order of the files is important, because of the static constructors.
# main loop defines two important globals that define the event based polling mechanism:
# poll_list and the event queue. The sources following the main loop are the ones
# that register themselves in the main loop. The file manager is the second important
# root of the application. Devices can register themselves in the file system tree.
# Therefore, the file manager needs to exist.

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
			keyboard_c64.cc \
			mystring.cc \
			size_str.cc \
			rtc.cc \
			task_menu.cc \
			userinterface.cc \
			ui_elements.cc \
			$(PRJ).cc

SRCS_ASM =  mb_crt0.s portasm.s

SRCS_6502 = 

SRCS_BIN =  ultimate.bin \
			fpga.bin \
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

TOOLCHAIN = C:/Xilinx/13.2/ISE_DS/EDK/gnu/microblaze/nt64

PATH_INC =  $(addprefix -I, $(VPATH))
APP_OPTS = -D_NO_FILE_ACCESS -DFIRST_WRITABLE_PAGE=0 
COPTIONS = -g -ffunction-sections -Os -std=c99 $(APP_OPTS)
CPPOPT   = -g -ffunction-sections -Os -fno-exceptions -fno-rtti -fno-threadsafe-statics $(APP_OPTS)
LINK 	 = ./mb_update.lds
LLIB     =  -L $(TOOLCHAIN)/microblaze-xilinx-elf/lib \
            -L $(TOOLCHAIN)/lib/gcc/microblaze-xilinx-elf/4.1.2
#LIBS     = -lstdc++ -lsupc++ -lgcc --start-group -lxil -lc -lm --end-group -lgcc
LIBS     =  -lc -lgcc 
LFLAGS   = --gc-sections 

VPATH   += $(OUTPUT) $(RESULT)

CC		  = $(CROSS)-gcc
CPP		  = $(CROSS)-g++
LD		  = $(CROSS)-ld
OBJDUMP   = $(CROSS)-objdump
OBJCOPY	  = $(CROSS)-objcopy
SIZE	  = $(CROSS)-size

.SUFFIXES:

OBJS_ASM = $(notdir $(SRCS_ASM:%.s=%.o))
OBJS_C   = $(notdir $(SRCS_C:%.c=%.o))
OBJS_CC  = $(notdir $(SRCS_CC:%.cc=%.o))
OBJS_6502 = $(notdir $(SRCS_6502:%.tas=%.o))
OBJS_IEC = $(notdir $(SRCS_IEC:%.iec=%.o))
OBJS_NANO = $(notdir $(SRCS_NANO:%.nan=%.o))
OBJS_BIN = $(notdir $(SRCS_BIN:%.bin=%.bo))
CHK_BIN  = $(notdir $(SRCS_BIN:%.bin=%.chk))

ALL_OBJS      = $(addprefix $(OUTPUT)/,$(OBJS_6502) $(OBJS_ASM) $(OBJS_C) $(OBJS_CC) $(OBJS_BIN) $(OBJS_IEC) $(OBJS_NANO))
ALL_DEP_OBJS  = $(addprefix $(OUTPUT)/,$(OBJS_C) $(OBJS_CC))


.PHONY: hex clean all dep

all: $(OUTPUT) $(RESULT) $(RESULT)/$(PRJ).u2u 

$(OUTPUT):
	@mkdir $(OUTPUT)

$(RESULT):
	@mkdir $(RESULT)
		
$(RESULT)/$(PRJ).u2u: $(OUTPUT)/$(PRJ).out
	@echo "Creating Updater Binary $@"
	$(OBJCOPY) -O binary $< $@

fpga.bin: $(1ST_BOOT)/output_mb/mb_boot_700.bit
	@echo "Converting $(<F) bitfile to $(@F)"
	@cd $(dir $<); $(PROMGEN) -r $(<F) $(OUTPUT_FP)/$(@F) >/dev/null; cd $(HERE)

%.65: %.tas
	@echo "Assembling $<"
	@$(TASS) $< --m6502 --nostart -o $(OUTPUT)/$(@F)

%.b: %.iec
	@echo "Assembling IEC processor code $(<F)"
	@python $(PARSE_IEC) $< $(OUTPUT)/$(@F) >python_out_iec

%.b: %.nan
	@echo "Assembling Nano processor code $(<F)"
	@python $(PARSE_NANO) $< $(OUTPUT)/$(@F) >python_out_nano

%.o: %.b
	@echo "Converting $(<F) binary to .o"
	@cd $(OUTPUT); $(OBJCOPY) -I binary -O elf32-microblaze --binary-architecture MicroBlaze $(<F) $@

%.o: %.65
	@echo "Converting .65 binary to .o"
	@cd $(OUTPUT); $(OBJCOPY) -I binary -O elf32-microblaze --binary-architecture MicroBlaze $(<F) $@
    	
%.bo: %.bin
	@echo "Converting $(<F) binary to $(@F).."
	@cd $(dir $(shell find $(VPATH)  -maxdepth 1 -name $(<F))); $(OBJCOPY) -I binary -O elf32-microblaze --binary-architecture MicroBlaze $(<F) $(@F); mv $(@F) $(OUTPUT_FP); cd $(HERE)

%.chk: %.bin
	@echo "Calculating checksum of $(<F) binary to $(@F).."
	@cd $(dir $(shell find $(VPATH)  -maxdepth 1 -name $(<F))); $(CHECKSUM) $(<F) $(OUTPUT_FP)/$(@F); cd $(HERE)

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

update.o: update.cc $(INCLUDES) $(CHK_BIN)
	@echo "Compiling $(notdir $<)"
	@cat $(addprefix $(OUTPUT)/,$(CHK_BIN)) >$(OUTPUT)/checksums.h
	@$(CPP) $(CPPOPT) $(PATH_INC) -B. -c -Wa,-ahlms=$(OUTPUT)/$(@:.o=.lst) -o $(OUTPUT)/$(@F) $<

%.d: %.cc
	@$(CPP) -MM $(PATH_INC) $< >$(OUTPUT)/$(@F:.o=.d)

%.d: %.c
	@$(CC) -MM $(PATH_INC) $< >$(OUTPUT)/$(@F:.o=.d)

$(OUTPUT)/$(PRJ).out: $(LINK) $(OBJS_C) $(OBJS_CC) $(OBJS_ASM) $(OBJS_6502) $(OBJS_BIN) $(OBJS_IEC) $(OBJS_NANO)
	@echo "Linking..."
	@$(LD) $(LLIB) $(LFLAGS) -T $(LINK) -Map=$(OUTPUT)/$(PRJ).map -o $(OUTPUT)/$(PRJ).out $(ALL_OBJS) $(LIBS)
	@$(SIZE) $(OUTPUT)/$(PRJ).out

# pull in dependency info for *existing* .o files
-include $(ALL_DEP_OBJS:.o=.d)

clean:
	@-rm -rf $(OUTPUT)
	@-rm -rf $(RESULT)

dep:  $(OBJS_CC:.o=.d) $(OBJS_C:.o=.d)
    
