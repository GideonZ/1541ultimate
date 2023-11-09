
CROSS	     = riscv32-unknown-elf-
ELFTYPE      = elf32-littleriscv
ARCHITECTURE = riscv
# CPU architecture and ABI
MARCH ?= rv32i_zicsr
MABI  ?= ilp32

include ../../../common/environment.mk

DEST     =  $(PATH_SW)/../fpga/cpu_unit/vhdl_source/bootloader_u2p_lattice.vhd
PRJ      =  bootloader
FINAL    =  $(RESULT)/$(PRJ).elf $(RESULT)/$(PRJ).bin $(RESULT)/$(PRJ).vhd $(DEST)
IMAGE_GEN = $(PATH_SW)/../neorv32/sw/image_gen/image_gen 

SRCS_C   =	bootloader.c ddr2_calibrator_lattice.c small_printf.c itu.c
                        
SRCS_CC	 =  

VPATH +=	$(PATH_SW)/portable/riscv
VPATH +=	$(PATH_SW)/../neorv32/sw/common

SRCS_ASM =

SRCS_ASMS = crt0.S

SRCS_6502 = 

SRCS_BIN =  

SRCS_IEC = 

SRCS_NANO = 

VPATH +=	$(PATH_SW)/FreeRTOS/Source/portable/risc-v

PATH_INC =  $(addprefix -I, $(VPATH))
OPTIONS  = -march=$(MARCH) -mabi=$(MABI) $(EFFORT) -ffunction-sections -fdata-sections -nostartfiles -mno-fdiv -Dmake_bootloader
OPTIONS += -g -Os -DOS -DNO_IO_RESET=1 -DVERBOSE=1 -DNO_BOOT=0 -DIOBASE=0x10000000 -DU2P_IO_BASE=0x10100000 -DCLOCK_FREQ=50000000 -Wno-write-strings
COPTIONS = $(OPTIONS) -std=gnu99
CPPOPT   = $(OPTIONS) -fno-exceptions -fno-rtti -fno-threadsafe-statics -fpermissive
LINK 	 = linker.x
LFLAGS   = --gc-sections 

include ../../../common/rules.mk

$(RESULT)/$(PRJ).vhd : $(RESULT)/$(PRJ).bin
	$(IMAGE_GEN) -bld_img $< $(RESULT)/$(PRJ).vhd $(shell basename $(CURDIR))

$(DEST): $(RESULT)/$(PRJ).vhd
	cp $(RESULT)/$(PRJ).vhd $(DEST)
