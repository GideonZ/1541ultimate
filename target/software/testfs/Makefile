PRJ      =  testfs
FINAL    =  $(RESULT)/$(PRJ)

include ../common/environment.mk

VPATH +=	$(PATH_SW)/FreeRTOS/Source/portable/microblaze
VPATH +=	$(PATH_SW)/test/filesys

VPATH +=	$(PATH_SW)/chan_fat/ff14/source


INCLUDES =  $(wildcard $(addsuffix /*.h, $(VPATH)))

PATH_INC =  $(addprefix -I, $(VPATH))

CROSS     = 
CC		  = $(CROSS)gcc
CPP		  = $(CROSS)g++
LD		  = $(CROSS)ld
OBJDUMP   = $(CROSS)objdump
OBJCOPY	  = $(CROSS)objcopy
SIZE	  = $(CROSS)size

.SUFFIXES:


SRCS_C   =	dump_hex.c \
            ff.c \
            ffunicode.c

SRCS_CC	 =  mystring.cc \
			file_device.cc \
			file_partition.cc \
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
			test_d64.cc


SRCS_ASM =  
SRCS_6502 = 
SRCS_BIN =  
SRCS_IEC = 
SRCS_NANO = 

PATH_INC =  $(addprefix -I, $(VPATH))
OPTIONS  = -g -ffunction-sections -O0 -DRUNS_ON_PC -Wno-format -Wno-format-extra-args
COPTIONS = $(OPTIONS) -std=c99
CPPOPT   = $(OPTIONS) -fno-exceptions -fno-rtti -fno-threadsafe-statics
LINK 	 = ./mb_app.lds
LIBS     =  -lgcc 
LFLAGS   = --gc-sections 

include ../common/rules.mk

$(RESULT)/$(PRJ): $(OBJS_C) $(OBJS_CC) $(OBJS_ASM) $(OBJS_6502) $(OBJS_BIN) $(OBJS_IEC) $(OBJS_NANO)
	@echo Linking...
	$(CPP) $(ALL_OBJS) $(LLIB) -o $(RESULT)/$(PRJ) $(LIBS)

