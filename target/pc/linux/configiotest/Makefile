PRJ      =  configiotest
FINAL    =  $(RESULT)/$(PRJ)

include ../../../common/environment.mk

VPATH +=	$(PATH_SW)/test/configio
VPATH +=	$(PATH_SW)/chan_fat/ff14/source
VPATH :=    $(PATH_SW)/test/stubs $(PATH_SW)/test/configio/stubs $(PATH_SW)/chan_fat/pc $(VPATH)

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
            ffunicode.c \
            ffsystem.c

SRCS_CC	 =  small_printf.cc \
			mystring.cc \
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
			filesystem_d64.cc \
			filesystem_t64.cc \
			filesystem_root.cc \
			filesystem_iso9660.cc \
			filemanager.cc \
			embedded_d64.cc \
			size_str.cc \
			init_function.cc \
			config.cc \
			configio.cc \
			configio_test_stubs.cc \
			configio_test.cc

SRCS_ASM =
SRCS_6502 =
SRCS_BIN =
SRCS_IEC =
SRCS_NANO =

PATH_INC =  $(addprefix -I, $(VPATH))
OPTIONS  = -g -ffunction-sections -O0 -DRUNS_ON_PC -DSAFEMODE -DRECOVERYAPP -Wno-format -Wno-format-extra-args
COPTIONS = $(OPTIONS) -std=c99
CPPOPT   = $(OPTIONS) -fno-exceptions -fno-rtti -fno-threadsafe-statics -fpermissive
LIBS     = -lgcc
LFLAGS   = --gc-sections

include ../../../common/rules.mk

$(RESULT)/$(PRJ): $(OBJS_C) $(OBJS_CC) $(OBJS_ASM) $(OBJS_6502) $(OBJS_BIN) $(OBJS_IEC) $(OBJS_NANO)
	@echo Linking...
	$(CPP) $(ALL_OBJS) $(LLIB) -o $(RESULT)/$(PRJ) $(LIBS)

test: $(RESULT)/$(PRJ)
	@$(RESULT)/$(PRJ)
