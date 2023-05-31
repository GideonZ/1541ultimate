
CROSS	     = riscv32-unknown-elf-
ELFTYPE      = elf32-littleriscv
ARCHITECTURE = riscv
# CPU architecture and ABI
MARCH ?= rv32i
MABI  ?= ilp32

include ../common/environment.mk

PRJ        =  rv_boot
DEST       =  $(PATH_SW)/../fpga/cpu_unit/rvlite/vhdl_source/bootrom_pkg_u2.vhd
FINAL      =  $(RESULT)/$(PRJ).bin $(DEST)
IMAGE_GEN  = $(PATH_SW)/../tools/bin2vhdl.py 
#LINKMETHOD = ld

PATH_SW  =  ../../../software

VPATH +=	$(PATH_SW)/application/mb_boot
VPATH +=	$(PATH_SW)/portable/riscv
VPATH +=	$(PATH_SW)/../neorv32/sw/common

SRCS_C   =	itu.c \

SRCS_CC	 =  mb_boot.cc \
            small_printf.cc

SRCS_ASMS =  crt0.S

SRCS_6502 = 

SRCS_BIN =  

VPATH +=	$(PATH_SW)/FreeRTOS/Source/portable/risc-v

PATH_INC =  $(addprefix -I, $(VPATH))
APPL_OPT = -DBOOTLOADER -D_FS_READONLY=1 -D_FS_MINIMIZE=3
OPTIONS  = $(APPL_OPT) -march=$(MARCH) -mabi=$(MABI) -ffunction-sections -fdata-sections -nostartfiles -mno-fdiv -Dmake_bootloader
OPTIONS += -g -Os -DOS -DNO_IO_RESET=1 -DIOBASE=0x10000000 -DU2P_IO_BASE=0x10100000 -DCLOCK_FREQ=50000000 -Wno-write-strings
COPTIONS = $(OPTIONS)
CPPOPT   = $(OPTIONS) -ffunction-sections -Os -fno-exceptions -fno-rtti
LINK 	 = linker.x
LFLAGS   = --gc-sections 

include ../common/rules.mk

$(DEST) : $(RESULT)/$(PRJ).bin
	$(IMAGE_GEN) $< $(DEST)
