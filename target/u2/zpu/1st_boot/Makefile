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
FPGA_250  = ../../fpga/work250/ultimate_1541_250e
FPGA_400  = ../../fpga/work400/ultimate_1541_400a
FPGA_700  = ../../fpga/work700/ultimate_1541_700a
FPGA_700C = ../../fpga/work700c/ultimate2_cached
FPGA_1400 = ../../fpga/work1400/ultimate_1541_1400a

# Outputs
RESULT = ./result
OUTPUT = ./output

PRJ      =  1st_boot
PATH_SW  =  ../../../software

VPATH     = $(PATH_SW)/application/1st_boot \
			$(PATH_SW)/system \
			$(PATH_SW)/io/icap \
			$(PATH_SW)/io/flash

INCLUDES =  $(wildcard $(addsuffix /*.h, $(VPATH)))


SRCS_C   =	zpu_boot.c \
			itu.c
SRCS_CC	 =  boot1.cc
SRCS_ASM =  crt0_boot.s

PATH_INC =  $(addprefix -I, $(VPATH))
COPTIONS = -Os -DGCC_ZPU -DBOOTLOADER -ffunction-sections
CPPOPT   = $(COPTIONS) -fno-exceptions -fno-rtti
LINK 	 = ./zpu_1st_boot.lds
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

.SUFFIXES:

OBJS_ASM = $(notdir $(SRCS_ASM:%.s=%.o))
OBJS_C   = $(notdir $(SRCS_C:%.c=%.o))
OBJS_CC  = $(notdir $(SRCS_CC:%.cc=%.o))
ALL_OBJS = $(addprefix $(OUTPUT)/,$(OBJS_ASM) $(OBJS_C) $(OBJS_CC))


.PHONY: normal clean all test mk1 mk2 special cached

normal: $(OUTPUT) $(RESULT) $(RESULT)/$(PRJ)_700.mcs $(OUTPUT) $(RESULT) $(RESULT)/$(PRJ)_400.mcs $(RESULT)/$(PRJ)_250.bin

all: $(OUTPUT) $(RESULT) $(RESULT)/$(PRJ)_400.mcs $(RESULT)/$(PRJ)_700.mcs $(RESULT)/$(PRJ)_250.bin $(RESULT)/$(PRJ)_1400.mcs

special:  $(RESULT)/$(PRJ)_1400.mcs

mk1: $(OUTPUT) $(RESULT) $(RESULT)/$(PRJ)_250.bin 

mk2: $(OUTPUT) $(RESULT) $(RESULT)/$(PRJ)_700.mcs

cached: $(OUTPUT) $(RESULT) $(OUTPUT)/$(PRJ)_700_cached.bit

test:
	echo $(ALL_OBJS)
	echo $(OBJS_C)
	echo $(OBJS_CC)
	echo $(RESULT)/$(PRJ)_700_cached.bit
	
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

$(RESULT)/$(PRJ)_250.bin: $(OUTPUT)/$(PRJ)_250.bit
	@echo "(My) PromGen 250A..."
	@$(MYPROMGEN) $< $@

$(RESULT)/$(PRJ)_400.mcs: $(OUTPUT)/$(PRJ)_400.bit
	@echo "PromGen 400A..."
	@promgen -w -s 2048 -p mcs -spi -u 0 $< -o $@
	@$(HEX2BIN) -s 0x39990 $@ $(@:.mcs=.tmp)
	@$(BIN2HEX) -Z -v "FPGA U2 V17" $(@:.mcs=.tmp) $@
	@rm $(@:.mcs=.tmp)

$(RESULT)/$(PRJ)_700.mcs: $(OUTPUT)/$(PRJ)_700.bit
	@echo "PromGen 700A..."
	@promgen -w -s 2048 -p mcs -spi -u 0 $< -o $@
	@$(HEX2BIN) -s 0x53C90 $@ $(@:.mcs=.tmp)
	@$(BIN2HEX) -Z -v "FPGA U2 V9E" $(@:.mcs=.tmp) $@
	@rm $(@:.mcs=.tmp)
	
$(RESULT)/$(PRJ)_1400.mcs: $(OUTPUT)/$(PRJ)_1400.bit
	@echo "PromGen 1400A..."
	@promgen -w -s 2048 -p mcs -spi -u 0 $< -o $@
	@$(HEX2BIN) -s 0x91260 $@ $(@:.mcs=.tmp)
	@$(BIN2HEX) -Z -v "FPGA U2 V62" $(@:.mcs=.tmp) $@
	@rm $(@:.mcs=.tmp)

$(OUTPUT)/$(PRJ)_250.bit: $(OUTPUT)/$(PRJ).mem $(FPGA_250).bit
	@echo "Data2MEM 250E..."
	@data2mem -bm $(FPGA_250)_bd.bmm \
	         -bt $(FPGA_250).bit \
	         -bd $(OUTPUT)/$(PRJ).mem \
	         -o b $(OUTPUT)/$(PRJ)_250.bit

$(OUTPUT)/$(PRJ)_400.bit: $(OUTPUT)/$(PRJ).mem $(FPGA_400).bit
	@echo "Data2MEM 400A..."
	@data2mem -bm $(FPGA_400)_bd.bmm \
	         -bt $(FPGA_400).bit \
	         -bd $(OUTPUT)/$(PRJ).mem \
	         -o b $(OUTPUT)/$(PRJ)_400.bit

$(OUTPUT)/$(PRJ)_700.bit: $(OUTPUT)/$(PRJ).mem $(FPGA_700).bit
	@echo "Data2MEM 700A..."
	@data2mem -bm $(FPGA_700)_bd.bmm \
	         -bt $(FPGA_700).bit \
	         -bd $(OUTPUT)/$(PRJ).mem \
	         -o b $(OUTPUT)/$(PRJ)_700.bit

$(OUTPUT)/$(PRJ)_700_cached.bit: $(OUTPUT)/$(PRJ).mem $(FPGA_700C).bit
	@echo "Data2MEM 700A Cached..."
	@data2mem -bm $(FPGA_700C)_bd.bmm \
	         -bt $(FPGA_700C).bit \
	         -bd $(OUTPUT)/$(PRJ).mem \
	         -o b $(OUTPUT)/$(PRJ)_700_cached.bit

$(OUTPUT)/$(PRJ)_1400.bit: $(OUTPUT)/$(PRJ).mem $(FPGA_1400).bit
	@echo "Data2MEM 1400A..."
	@data2mem -bm $(FPGA_1400)_bd.bmm \
	         -bt $(FPGA_1400).bit \
	         -bd $(OUTPUT)/$(PRJ).mem \
	         -o b $(OUTPUT)/$(PRJ)_1400.bit

$(OUTPUT)/$(PRJ).mem: $(RESULT)/$(PRJ).bin
	@echo "Make mem..."
	@$(MAKEMEM) $< 4096 >$@

clean:
	@-rm -rf $(OUTPUT)
	@-rm -rf $(RESULT)
