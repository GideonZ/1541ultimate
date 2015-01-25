# System tool locations

HERE = $(shell pwd)
TOOLS = ../../../tools
BIN2HEX   = $(TOOLS)/bin2hex
HEX2BIN   = $(TOOLS)/hex2bin
MAKEMEM   = $(TOOLS)/make_mem
MYPROMGEN = $(TOOLS)/promgen
MAKEAPPL  = $(TOOLS)/makeappl
TASS      = $(TOOLS)/64tass/64tass

# Configuration
SHELL     = bash
CROSS     = mb

# External inputs
ROMS = ../../../roms

# Outputs
RESULT    = ./result_mb
OUTPUT    = ./output_mb
OUTPUT_FP = $(shell pwd)/output_mb

PRJ      =  irq_test
PATH_SW  =  ../../../software

VPATH     = $(PATH_SW)/application \
			$(PATH_SW)/application/mb_hello \
			$(PATH_SW)/benchmark \
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
			$(PATH_SW)/6502 \
			$(ROMS)

INCLUDES =  $(wildcard $(addsuffix /*.h, $(VPATH)))

SRCS_C   =	mbstart_app.c \
			itu.c \
			irq_test.c \
			small_printf.c

# the order of the files is important, because of the static constructors.
# main loop defines two important globals that define the event based polling mechanism:
# poll_list and the event queue. The sources following the main loop are the ones
# that register themselves in the main loop. The file manager is the second important
# root of the application. Devices can register themselves in the file system tree.
# Therefore, the file manager needs to exist.

SRCS_CC	 =  

#
SRCS_ASM =  

SRCS_6502 = 

SRCS_BIN =  

CC		  = $(CROSS)-gcc
CPP		  = $(CROSS)-g++
LD		  = $(CROSS)-ld
OBJDUMP   = $(CROSS)-objdump
OBJCOPY	  = $(CROSS)-objcopy
SIZE	  = $(CROSS)-size

TOOLCHAIN = C:/Xilinx/13.2/ISE_DS/EDK/gnu/microblaze/nt64

PATH_INC =  $(addprefix -I, $(VPATH))
OPTIONS  = -ffunction-sections -fdata-sections -O2 
COPTIONS = $(OPTIONS) -std=c99
CPPOPT   = $(OPTIONS) -fno-exceptions -fno-rtti
LINK 	 = ./mb_app.lds
LLIB     =  -L $(TOOLCHAIN)/microblaze-xilinx-elf/lib \
            -L $(TOOLCHAIN)/lib/gcc/microblaze-xilinx-elf/4.1.2
#LIBS     = -lstdc++ -lsupc++ -lgcc --start-group -lxil -lc -lm --end-group -lgcc
LIBS     =  -lc -lgcc
LFLAGS   = --gc-sections 

VPATH   += $(OUTPUT) $(RESULT)


.SUFFIXES:

OBJS_ASM = $(notdir $(SRCS_ASM:%.s=%.o))
OBJS_C   = $(notdir $(SRCS_C:%.c=%.o))
OBJS_CC  = $(notdir $(SRCS_CC:%.cc=%.o))
OBJS_6502 = $(notdir $(SRCS_6502:%.tas=%.o))
OBJS_BIN = $(notdir $(SRCS_BIN:%.bin=%.bo))

ALL_OBJS      = $(addprefix $(OUTPUT)/,$(OBJS_6502) $(OBJS_ASM) $(OBJS_C) $(OBJS_CC) $(OBJS_BIN))
ALL_DEP_OBJS  = $(addprefix $(OUTPUT)/,$(OBJS_C) $(OBJS_CC))


.PHONY: clean all dep

all: $(OUTPUT) $(RESULT) $(RESULT)/$(PRJ).bin $(OUTPUT)/$(PRJ).mem $(OUTPUT)/$(PRJ).m8

$(OUTPUT):
	@mkdir $(OUTPUT)

$(RESULT):
	@mkdir $(RESULT)
		
$(RESULT)/$(PRJ).bin: $(OUTPUT)/$(PRJ).out
	@echo "Creating Binary $(@F)"
	$(OBJCOPY) -O binary $< $(RESULT)/$(@F)

%.bo: %.bin
	@echo "Converting $(<F) binary to $(@F).."
	cd $(dir $(shell find $(VPATH)  -maxdepth 1 -name $(<F))); $(OBJCOPY) -I binary -O elf32-microblaze --binary-architecture microblaze $(<F) $(@F) ; mv $(@F) $(OUTPUT_FP)/$(@F) ; cd $(HERE)

%.65: %.tas
	@echo "Assembling $<"
	$(TASS) $< --m6502 --nostart -o $(OUTPUT)/$(@F)

%.o: %.65
	@echo "Converting .65 binary to .o"
	@cd $(OUTPUT); $(OBJCOPY) -I binary -O elf32-microblaze --binary-architecture microblaze $(<F) $@
    	
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

$(OUTPUT)/$(PRJ).out: $(LINK) $(OBJS_C) $(OBJS_CC) $(OBJS_ASM) $(OBJS_6502) $(OBJS_BIN)
	@echo "Linking..."
	@$(LD) $(LLIB) $(LFLAGS) -T $(LINK) -Map=$(OUTPUT)/$(PRJ).map -o $(OUTPUT)/$(PRJ).out $(ALL_OBJS) $(LIBS)
	@$(SIZE) $(OUTPUT)/$(PRJ).out

$(OUTPUT)/$(PRJ).mem: $(RESULT)/$(PRJ).bin
	@echo "Make mem..."
	@$(MAKEMEM) -w $< 65536 65536 >$@

$(OUTPUT)/$(PRJ).m8: $(RESULT)/$(PRJ).bin
	@echo "Make mem..."
	@$(MAKEMEM) $< 65536 65536 >$@

# pull in dependency info for *existing* .o files
-include $(ALL_DEP_OBJS:.o=.d)

clean:
	@-rm -rf $(OUTPUT)
	@-rm -rf $(RESULT)

dep:  $(OBJS_CC:.o=.d) $(OBJS_C:.o=.d)
