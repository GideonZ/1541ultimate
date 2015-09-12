RESULT    = .
OUTPUT    = output

PATH_SW  =  ../../../software

VPATH     = $(PATH_SW)/test/filesys \
            $(PATH_SW)/chan_fat \
			$(PATH_SW)/chan_fat/option \
			$(PATH_SW)/chan_fat/full \
			$(PATH_SW)/filesystem \
			$(PATH_SW)/filemanager \
			$(PATH_SW)/components \
			$(PATH_SW)/infra \
			$(PATH_SW)/system \
			$(PATH_SW)/FreeRTOS/Source \
			$(PATH_SW)/FreeRTOS/Source/include \
			$(PATH_SW)/FreeRTOS/Source/portable \
			$(PATH_SW)/FreeRTOS/Source/MemMang \

INCLUDES =  $(wildcard $(addsuffix /*.h, $(VPATH)))

# XILINX ?= C:/Xilinx/13.2
# TOOLCHAIN = $(XILINX)/ISE_DS/EDK/gnu/microblaze/nt
# XILINXBIN = $(XILINX)/ISE_DS/ISE/bin/nt

PATH_INC =  $(addprefix -I, $(VPATH))
# VPATH   += $(OUTPUT) $(RESULT)

CROSS     = 
CC		  = $(CROSS)gcc
CPP		  = $(CROSS)g++
LD		  = $(CROSS)ld
OBJDUMP   = $(CROSS)objdump
OBJCOPY	  = $(CROSS)objcopy
SIZE	  = $(CROSS)size

.SUFFIXES:

PRJ      =  cyg_filesystems
FINAL    =  $(RESULT)/$(PRJ).exe

SRCS_C   =	ff2.c \
            ccsbcs.c \
            ffsyscall.c \
            dump_hex.c

SRCS_CC	 =  mystring.cc \
            filemanager.cc \
			file_device.cc \
			file_partition.cc \
			embedded_d64.cc \
			embedded_t64.cc \
			embedded_iso.cc \
			embedded_fat.cc \
			path.cc \
			pattern.cc \
			blockdev.cc \
            blockdev_emul.cc \
			blockdev_file.cc \
			blockdev_ram.cc \
			disk.cc \
			partition.cc \
			file_system.cc \
			diskio.cc \
			directory.cc \
			file.cc \
            filesystem_root.cc \
			filesystem_fat.cc \
			filesystem_d64.cc \
			filesystem_t64.cc \
			filesystem_iso9660.cc \
			size_str.cc \
			test.cc


SRCS_ASM =  
SRCS_6502 = 
SRCS_BIN =  
SRCS_IEC = 
SRCS_NANO = 

PATH_INC =  $(addprefix -I, $(VPATH))
OPTIONS  = -g -ffunction-sections -O0 -DRUNS_ON_PC 
COPTIONS = $(OPTIONS) -std=c99
CPPOPT   = $(OPTIONS) -fno-exceptions -fno-rtti -fno-threadsafe-statics
LINK 	 = ./mb_app.lds
LIBS     =  -lgcc 
LFLAGS   = --gc-sections 

include ../common/rules.mk

$(RESULT)/$(PRJ).exe: $(OBJS_C) $(OBJS_CC) $(OBJS_ASM) $(OBJS_6502) $(OBJS_BIN) $(OBJS_IEC) $(OBJS_NANO)
	@echo Linking...
	$(CPP) $(ALL_OBJS) $(LLIB) -o $(RESULT)/$(PRJ).exe  $(LIBS)

