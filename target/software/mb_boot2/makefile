
include ../common/environment.mk

PRJ        =  mb_boot2
FINAL      =  $(RESULT)/$(PRJ).bin
LINKMETHOD = ld

SRCS_C   =	mbstart_app.c \
			itu.c \
			xmodem.c \
			dump_hex.c \
			ff2.c \
			diskio.c \
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
SRCS_ASM =  mb_crt0.s

SRCS_6502 = 

SRCS_BIN =  

# make sure that the path we use finds tiny_ro first.
VPATH     := $(PATH_SW)/chan_fat/tiny_ro  $(VPATH) 
VPATH     += $(PATH_SW)/portable/microblaze
VPATH     += $(PATH_SW)/application/2nd_boot


PATH_INC =  $(addprefix -I, $(VPATH))
OPTIONS  = -g -ffunction-sections -nostartfiles -O1 -DBOOTLOADER -D_FS_READONLY=1 -D_FS_MINIMIZE=3
COPTIONS = $(OPTIONS) -std=c99
CPPOPT   = $(OPTIONS) -fno-exceptions -fno-rtti -fno-threadsafe-statics
LINK 	 = ./mb_boot2.lds
LFLAGS   = --gc-sections 
	
include ../common/rules.mk
