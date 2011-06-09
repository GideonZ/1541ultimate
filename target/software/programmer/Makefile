# System tool locations
TOOLS = ../../../tools
ZPU_TOOLCHAIN ?= /cygdrive/d/Proj/zpu/git/toolchain
BIN2HEX   = $(TOOLS)/bin2hex
HEX2BIN   = $(TOOLS)/hex2bin
MAKEMEM   = $(TOOLS)/make_mem
MYPROMGEN = $(TOOLS)/promgen

# Configuration
CROSS     = zpu-elf

# External inputs
FPGA_400  = ../../fpga/boot400/boot_400a
FPGA_700  = ../../fpga/boot700/boot_700a
FPGA_1400 = ../../fpga/boot1400/boot_1400a
ROMS      = ../../../roms

# Outputs
RESULT = ./result
OUTPUT = ./output

PRJ      =  programmer
PATH_SW  =  ../../../software

VPATH     = $(PATH_SW)/application \
			$(PATH_SW)/application/programmer \
			$(PATH_SW)/filesystem \
			$(PATH_SW)/filemanager \
			$(PATH_SW)/system \
			$(PATH_SW)/io/flash \
			$(PATH_SW)/io/icap \
			$(PATH_SW)/drive \
			$(PATH_SW)/components \
			$(PATH_SW)/io/c64 \
			$(PATH_SW)/io/rtc \
			$(PATH_SW)/io/usb \
			$(PATH_SW)/io/sd_card \
			$(PATH_SW)/io/userinterface

INCLUDES =  $(wildcard $(addsuffix /*.h, $(VPATH)))


SRCS_C   =	zpu.c \
			itu.c \
			xmodem.c \
			crc16.c \
			dump_hex.c \
			small_printf.c
SRCS_CC	 =  blockdev.cc \
			disk.cc \
			partition.cc \
			file_system.cc \
			fat_fs.cc \
			fat_dir.cc \
			fatfile.cc \
			ccsbcs.cc \
			flash.cc \
			at45_flash.cc \
			at49_flash.cc \
			w25q_flash.cc \
			sd_card.cc \
			sdio.cc \
			memory.cc \
			prog.cc
SRCS_ASM =  crt0_appl.s

PATH_INC =  $(addprefix -I, $(VPATH))
COPTIONS = -Os -DGCC_ZPU -DBOOTLOADER -D_FS_READONLY=1 -D_FS_MINIMIZE=3 -ffunction-sections
CPPOPT   = $(COPTIONS) -fno-exceptions -fno-rtti
LINK 	 = ./zpu_32k_internal.lds
LLIB     =  -L $(ZPU_TOOLCHAIN)/install/lib/gcc/zpu-elf/3.4.2 \
			-L $(ZPU_TOOLCHAIN)/install/zpu-elf/lib
LIBS     = -lstdc++ -lsupc++ -lgcc --start-group -lc -lbcc --end-group -lgcc
LFLAGS   = --relax --gc-sections 

VPATH   += $(OUTPUT) $(RESULT)

CC		  = $(CROSS)-gcc
CPP		  = $(CROSS)-g++
LD		  = $(CROSS)-ld
OBJDUMP   = $(CROSS)-objdump
OBJCOPY	  = $(CROSS)-objcopy


.PHONY: all clean special

all: $(OUTPUT) $(RESULT) $(RESULT)/$(PRJ).bin $(OUTPUT)/$(PRJ)_700.bit

special: $(OUTPUT) $(RESULT) $(RESULT)/$(PRJ).bin $(OUTPUT)/$(PRJ)_400.bit $(OUTPUT)/$(PRJ)_1400.bit 

.SUFFIXES:

OBJS_ASM = $(notdir $(SRCS_ASM:%.s=%.o))
OBJS_C   = $(notdir $(SRCS_C:%.c=%.o))
OBJS_CC  = $(notdir $(SRCS_CC:%.cc=%.o))
ALL_OBJS = $(addprefix $(OUTPUT)/,$(OBJS_ASM) $(OBJS_C) $(OBJS_CC))

$(OUTPUT):
	@mkdir $(OUTPUT)

$(RESULT):
	@mkdir $(RESULT)
		
$(RESULT)/$(PRJ).bin: $(OUTPUT)/$(PRJ).out
	@echo "Creating Binary $@"
	@$(OBJCOPY) -O binary $< $@

%.o: %.s
	@echo "Compiling $<"
	@$(CC) $(OPTIONS) $(PATH_INC) -B. -c -Wa,-ahlms=$(OUTPUT)/$(@:.o=.lst) -o $(OUTPUT)/$(@F) $<

%.o: %.c $(INCLUDES)
	@echo "Compiling $<"
	@$(CC) $(COPTIONS) $(PATH_INC) -B. -c -Wa,-ahlms=$(OUTPUT)/$(@:.o=.lst) -o $(OUTPUT)/$(@F) $<

%.o: %.cc $(INCLUDES)
	@echo "Compiling $<"
	@$(CPP) $(CPPOPT) $(PATH_INC) -B. -c -Wa,-ahlms=$(OUTPUT)/$(@:.o=.lst) -o $(OUTPUT)/$(@F) $<

$(OUTPUT)/$(PRJ).out: $(LINK) $(OBJS_C) $(OBJS_CC) $(OBJS_ASM)
	@echo "Linking..."
	@$(LD) $(LLIB) $(LFLAGS) -T $(LINK) -Map=$(OUTPUT)/$(PRJ).map -o $(OUTPUT)/$(PRJ).out $(ALL_OBJS) $(LIBS)

$(OUTPUT)/$(PRJ).mem: $(RESULT)/$(PRJ).bin
	@echo "Make mem..."
	@$(MAKEMEM) $< 32768 >$@

$(OUTPUT)/$(PRJ)_400.bit: $(OUTPUT)/$(PRJ).mem $(FPGA_400).bit
	@echo "Data2MEM 400A..."
	@data2mem -bm $(FPGA_400)_bd.bmm \
	         -bt $(FPGA_400).bit \
	         -bd $(OUTPUT)/$(PRJ).mem \
	         -o b $(RESULT)/$(PRJ)_400.bit

$(OUTPUT)/$(PRJ)_700.bit: $(OUTPUT)/$(PRJ).mem $(FPGA_700).bit
	@echo "Data2MEM 700A..."
	@data2mem -bm $(FPGA_700)_bd.bmm \
	         -bt $(FPGA_700).bit \
	         -bd $(OUTPUT)/$(PRJ).mem \
	         -o b $(RESULT)/$(PRJ)_700.bit

$(OUTPUT)/$(PRJ)_1400.bit: $(OUTPUT)/$(PRJ).mem $(FPGA_1400).bit
	@echo "Data2MEM 1400A..."
	@data2mem -bm $(FPGA_1400)_bd.bmm \
	         -bt $(FPGA_1400).bit \
	         -bd $(OUTPUT)/$(PRJ).mem \
	         -o b $(RESULT)/$(PRJ)_1400.bit

clean:
	@-rm -rf $(OUTPUT)
	@-rm -rf $(RESULT)

