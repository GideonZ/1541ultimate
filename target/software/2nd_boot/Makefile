# System tool locations
TOOLS = ../../../tools
ZPU_TOOLCHAIN ?= /cygdrive/d/Proj/zpu/git/toolchain
BIN2HEX = $(TOOLS)/bin2hex
HEX2BIN = $(TOOLS)/hex2bin

# Configuration
CROSS     = zpu-elf

# External inputs
ROMS = ../../../roms

# Outputs
RESULT = ./result
OUTPUT = ./output

PRJ      =  2nd_boot
PATH_SW  =  ../../../software

VPATH     = $(PATH_SW)/application \
			$(PATH_SW)/application/2nd_boot \
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
			usb.cc \
			usb_scsi.cc \
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
			boot.cc
SRCS_ASM =  crt0_appl.s

PATH_INC =  $(addprefix -I, $(VPATH))
COPTIONS = -Os -DGCC_ZPU -DBOOTLOADER -D_FS_READONLY=1 -D_FS_MINIMIZE=3 -ffunction-sections
CPPOPT   = $(COPTIONS) -fno-exceptions -fno-rtti
LINK 	 = ./zpu_2nd_boot.lds
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


.PHONY: hex clean all

all: $(OUTPUT) $(RESULT) $(RESULT)/$(PRJ).bin
#	$(MAKE) -C ../1st_boot

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


../1st_boot/result/1st_boot_400.mcs::
	@$(MAKE) -C ../1st_boot

../1st_boot/result/1st_boot_700.mcs::
	@$(MAKE) -C ../1st_boot

$(RESULT)/flash_400.mcs: $(RESULT)/$(PRJ).bin ../1st_boot/result/1st_boot_400.mcs Makefile
	@echo Making Hex File for 400A
	@$(BIN2HEX) -Z -o 0x53CA0 -v "V1.1" -t  $(RESULT)/$(PRJ).bin $(OUTPUT)/_flash.mcs
	@$(BIN2HEX) -o 0x063000 -t -a $(ROMS)/ar5pal.bin   $(OUTPUT)/_flash.mcs
	@$(BIN2HEX) -o 0x06B000 -t -a $(ROMS)/ar6pal.bin   $(OUTPUT)/_flash.mcs
	@$(BIN2HEX) -o 0x073000 -t -a $(ROMS)/final3.bin   $(OUTPUT)/_flash.mcs
	@$(BIN2HEX) -o 0x083000 -t -a $(ROMS)/sounds.bin   $(OUTPUT)/_flash.mcs
	@$(BIN2HEX) -o 0x08A000 -t -a $(ROMS)/chars.bin    $(OUTPUT)/_flash.mcs
	@$(BIN2HEX) -o 0x08D000 -t -a $(ROMS)/epyx.bin     $(OUTPUT)/_flash.mcs
	@$(BIN2HEX) -o 0x08F000 -t -a $(ROMS)/1541.bin     $(OUTPUT)/_flash.mcs
	@$(BIN2HEX) -o 0x093000 -t -a $(ROMS)/rr38pal.bin  $(OUTPUT)/_flash.mcs
	@$(BIN2HEX) -o 0x0A3000 -t -a $(ROMS)/ss5pal.bin   $(OUTPUT)/_flash.mcs
	@$(BIN2HEX) -o 0x0B3000 -t -a $(ROMS)/ar5ntsc.bin  $(OUTPUT)/_flash.mcs
	@$(BIN2HEX) -o 0x0BB000 -t -a $(ROMS)/1541c.bin    $(OUTPUT)/_flash.mcs
	@$(BIN2HEX) -o 0x0BF000 -t -a $(ROMS)/1541-ii.bin  $(OUTPUT)/_flash.mcs
	@$(BIN2HEX) -o 0x0C3000 -t -a $(ROMS)/rr38ntsc.bin $(OUTPUT)/_flash.mcs
	@$(BIN2HEX) -o 0x0D3000 -t -a $(ROMS)/ss5ntsc.bin  $(OUTPUT)/_flash.mcs
	@$(BIN2HEX) -o 0x0E3000 -t -a $(ROMS)/tar_pal.bin  $(OUTPUT)/_flash.mcs
	@$(BIN2HEX) -o 0x0F3000 -t -a $(ROMS)/tar_ntsc.bin $(OUTPUT)/_flash.mcs
	@cat ../1st_boot/result/1st_boot_400.mcs         >>$(OUTPUT)/_flash.mcs
	@$(HEX2BIN) -o 0x000000 -s 0x103000 $(OUTPUT)/_flash.mcs $(RESULT)/flash.bin
	@$(BIN2HEX) -o 0x000000 $(RESULT)/flash.bin $(RESULT)/flash_400.mcs
	@rm $(OUTPUT)/_flash.mcs
	@echo Done!

$(RESULT)/flash_700.mcs: $(RESULT)/$(PRJ).bin ../1st_boot/result/1st_boot_700.mcs Makefile
	@echo Making Hex File for 700A
	@$(BIN2HEX) -Z -o 0x53CA0 -v "V1.1" -t  $(RESULT)/$(PRJ).bin $(OUTPUT)/_flash.mcs
	@$(BIN2HEX) -o 0x063000 -t -a $(ROMS)/ar5pal.bin   $(OUTPUT)/_flash.mcs
	@$(BIN2HEX) -o 0x06B000 -t -a $(ROMS)/ar6pal.bin   $(OUTPUT)/_flash.mcs
	@$(BIN2HEX) -o 0x073000 -t -a $(ROMS)/final3.bin   $(OUTPUT)/_flash.mcs
	@$(BIN2HEX) -o 0x083000 -t -a $(ROMS)/sounds.bin   $(OUTPUT)/_flash.mcs
	@$(BIN2HEX) -o 0x08A000 -t -a $(ROMS)/chars.bin    $(OUTPUT)/_flash.mcs
	@$(BIN2HEX) -o 0x08D000 -t -a $(ROMS)/epyx.bin     $(OUTPUT)/_flash.mcs
	@$(BIN2HEX) -o 0x08F000 -t -a $(ROMS)/1541.bin     $(OUTPUT)/_flash.mcs
	@$(BIN2HEX) -o 0x093000 -t -a $(ROMS)/rr38pal.bin  $(OUTPUT)/_flash.mcs
	@$(BIN2HEX) -o 0x0A3000 -t -a $(ROMS)/ss5pal.bin   $(OUTPUT)/_flash.mcs
	@$(BIN2HEX) -o 0x0B3000 -t -a $(ROMS)/ar5ntsc.bin  $(OUTPUT)/_flash.mcs
	@$(BIN2HEX) -o 0x0BB000 -t -a $(ROMS)/1541c.bin    $(OUTPUT)/_flash.mcs
	@$(BIN2HEX) -o 0x0BF000 -t -a $(ROMS)/1541-ii.bin  $(OUTPUT)/_flash.mcs
	@$(BIN2HEX) -o 0x0C3000 -t -a $(ROMS)/rr38ntsc.bin $(OUTPUT)/_flash.mcs
	@$(BIN2HEX) -o 0x0D3000 -t -a $(ROMS)/ss5ntsc.bin  $(OUTPUT)/_flash.mcs
	@$(BIN2HEX) -o 0x0E3000 -t -a $(ROMS)/tar_pal.bin  $(OUTPUT)/_flash.mcs
	@$(BIN2HEX) -o 0x0F3000 -t -a $(ROMS)/tar_ntsc.bin $(OUTPUT)/_flash.mcs
	@cat ../1st_boot/result/1st_boot_700.mcs         >>$(OUTPUT)/_flash.mcs
	@$(HEX2BIN) -o 0x000000 -s 0x103000 $(OUTPUT)/_flash.mcs $(RESULT)/flash.bin
	@$(BIN2HEX) -o 0x000000 $(RESULT)/flash.bin $(RESULT)/flash_700.mcs
	@rm $(OUTPUT)/_flash.mcs
	@echo Done!

clean:
	@-rm -rf $(OUTPUT)
	@-rm -rf $(RESULT)

#	@-rm -f *.o
#	@-rm -f *.out
#	@-rm -f *.bin
#	@-rm -f *.map
#	@-rm -f *.lst
#	@-rm -f *.mem
#	@-rm -f dis_*
#	@-rm -f *.mcs

hex: $(OUTPUT) $(RESULT) $(RESULT)/$(PRJ).bin $(RESULT)/flash_400.mcs $(RESULT)/flash_700.mcs


# Flash sector: 21000
# 400A: 3992C (447 pages, 2 sectors)
# 700A: 5364C (647 pages, 3 sectors)
# bootapp (max 60K, starting at page 650)
# bootapp: F360 starting at 53CA0 (118 pages max)
# Start of roms: 0x63000
# Configuration, reserved the last 2 blocks (16 pages) (~8K).
