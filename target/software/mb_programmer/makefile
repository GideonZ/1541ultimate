# System tool locations

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
RESULT    = ./result
OUTPUT    = ./output

PRJ      =  mb_programmer
PATH_SW  =  ../../../software
FINAL    =  $(RESULT)/$(PRJ).bin

VPATH     = $(PATH_SW)/application \
			$(PATH_SW)/application/programmer \
			$(PATH_SW)/chan_fat/option \
			$(PATH_SW)/chan_fat/tiny_ro \
			$(PATH_SW)/chan_fat \
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
			dump_hex.c \
			ff2.c \
			diskio.c \
			small_printf.c

# the order of the files is important, because of the static constructors.
# main loop defines two important globals that define the event based polling mechanism:
# poll_list and the event queue. The sources following the main loop are the ones
# that register themselves in the main loop. The file manager is the second important
# root of the application. Devices can register themselves in the file system tree.
# Therefore, the file manager needs to exist.

SRCS_CC	 =  blockdev.cc \
			disk.cc \
			partition.cc \
			sd_card.cc \
			sdio.cc \
			memory.cc \
			prog.cc
#
SRCS_ASM =  mb_crt0.s

SRCS_6502 = 

SRCS_BIN =  

CC		  = $(CROSS)-gcc
CPP		  = $(CROSS)-g++
LD		  = $(CROSS)-ld
OBJDUMP   = $(CROSS)-objdump
OBJCOPY	  = $(CROSS)-objcopy
SIZE	  = $(CROSS)-size

TOOLCHAIN = C:/Xilinx/13.2/ISE_DS/EDK/gnu/microblaze/nt

PATH_INC =  $(addprefix -I, $(VPATH))
APPL_OPT = -DBOOTLOADER -D_FS_READONLY=1 -D_FS_MINIMIZE=3 -DSKIP_OLD_FLASHES
COPTIONS = -ffunction-sections -Os -std=c99 $(APPL_OPT)
CPPOPT   = -ffunction-sections -Os -fno-exceptions -fno-rtti $(APPL_OPT)
LINK 	 = ./mb_prog.lds
LLIB     =  -L $(TOOLCHAIN)/lib/gcc/microblaze-xilinx-elf/4.1.2 \
			-L $(TOOLCHAIN)/microblaze-xilinx-elf/lib
LIBS     = -lstdc++ -lsupc++ -lgcc --start-group -lxil -lc -lm --end-group -lgcc
LFLAGS   = --gc-sections 

VPATH   += $(OUTPUT)

include ../common/rules.mk

