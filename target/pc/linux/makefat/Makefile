PRJ      =  makefat
FINAL    =  $(RESULT)/$(PRJ)
LWIP     =  none

include ../../../common/environment.mk

VPATH +=	$(PATH_SW)/application/u64ii_prepare_fat
VPATH +=	$(PATH_SW)/chan_fat/ff14/source
VPATH +=	.
VPATH :=    $(PATH_SW)/chan_fat/pc $(VPATH)
VPATH +=    $(PATH_SW)/../html

INCLUDES =  $(wildcard $(addsuffix /*.h, $(VPATH)))

PATH_INC =  $(addprefix -I, $(VPATH))

CROSS        = 
ELFTYPE      = elf64-x86-64
ARCHITECTURE = i386:x86-64

CC		  = $(CROSS)gcc
CPP		  = $(CROSS)g++
LD		  = $(CROSS)ld
OBJDUMP   = $(CROSS)objdump
OBJCOPY	  = $(CROSS)objcopy
SIZE	  = $(CROSS)size

.SUFFIXES:


SRCS_C   =	dump_hex.c \
            ff.c \
            ffunicode.c \
            ffsystem.c \

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
			filesystem_fat.cc \
			filesystem_root.cc \
			size_str.cc \
			filemanager.cc \
			init_function.cc \
			flash_disk_prep.cc


SRCS_ASM =
SRCS_6502 = 
SRCS_BIN =  1541.bin 1571.bin 1581.bin snds1541.bin snds1571.bin snds1581.bin \
            characters.901225-01.bin kernal.901227-03.bin basic.901226-01.bin 
SRCS_IEC = 
SRCS_NANO = 
SRCS_HTML = index.html

PATH_INC =  $(addprefix -I, $(VPATH))
OPTIONS  = -g -DUSE_FILEMANAGER -DU64=2 -DMHS_PORT=1 -ffunction-sections -O0 -DRUNS_ON_PC -Wno-format -Wno-format-extra-args
COPTIONS = $(OPTIONS) 
CPPOPT   = $(OPTIONS) -fno-exceptions -fno-rtti -fno-threadsafe-statics
LINK 	 = 
LIBS     =  -lgcc 
LFLAGS   = --gc-sections 

include ../../../common/rules.mk

$(RESULT)/$(PRJ): $(OBJS_C) $(OBJS_CC) $(OBJS_ASM) $(OBJS_6502) $(OBJS_BIN) $(OBJS_IEC) $(OBJS_NANO) $(OBJS_HTML)
	@echo Linking...
	$(CPP) $(ALL_OBJS) $(LLIB) -o $(RESULT)/$(PRJ) $(LIBS)

