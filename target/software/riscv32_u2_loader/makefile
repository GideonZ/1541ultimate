CROSS	     = riscv32-unknown-elf-
ELFTYPE      = elf32-littleriscv
ARCHITECTURE = riscv
# CPU architecture and ABI
MARCH ?= rv32i
MABI  ?= ilp32

include ../common/environment.mk

PRJ        =  boot2
DEST       =  $(PATH_SW)/../fpga/cpu_unit/rvlite/vhdl_source/bootrom_pkg_u2_loader.vhd
FINAL      =  $(RESULT)/$(PRJ).bin $(DEST)
#LINKMETHOD = ld
IMAGE_GEN  =  $(PATH_SW)/../tools/bin2vhdl.py 

SRCS_C   =	itu.c \
			xmodem.c \
			dump_hex.c \
			ff.c \
			diskio.c \
			syscalls.c \
			small_printf.c

SRCS_CC	 =  blockdev.cc \
			disk.cc \
			partition.cc \
			flash.cc \
			at45_flash.cc \
			w25q_flash.cc \
			s25fl_flash.cc \
			sd_card.cc \
			sdio.cc \
			memory.cc \
			boot.cc
#
SRCS_ASMS =  crt0.S

SRCS_6502 = 

SRCS_BIN =  

# make sure that the path we use finds tiny_ro first.
VPATH     := $(PATH_SW)/chan_fat/tiny_ro  $(VPATH) 
VPATH     += $(PATH_SW)/portable/riscv
VPATH     += $(PATH_SW)/application/2nd_boot
VPATH     += $(PATH_SW)/../neorv32/sw/common
VPATH     += $(PATH_SW)/../neorv32/sw/lib/include
VPATH     += $(PATH_SW)/../neorv32/sw/lib/source
VPATH     += $(PATH_SW)/FreeRTOS/Source/portable/risc-v

#OPTIONS += -g -Os -DOS 

PATH_INC =  $(addprefix -I, $(VPATH))
OPTIONS  = -g -Os -ffunction-sections -fdata-sections -nostartfiles -Wno-write-strings
OPTIONS += -march=$(MARCH) -mabi=$(MABI) -DU2 -DRISCV -D_FS_READONLY=1 -D_FS_MINIMIZE=3 -DBOOTLOADER
OPTIONS += -DNO_IO_RESET=1 -DIOBASE=0x10000000 -DU2P_IO_BASE=0x10100000 -DCLOCK_FREQ=50000000
COPTIONS = $(OPTIONS) -std=gnu99
CPPOPT   = $(OPTIONS) -fno-exceptions -fno-rtti -fno-threadsafe-statics
LINK 	 = ./linker.x
LFLAGS   = --gc-sections 
	
include ../common/rules.mk

$(DEST) : $(RESULT)/$(PRJ).bin 
	$(IMAGE_GEN) $< $(DEST) 32768
