#
# Host test of "Use DHCP" as a configuration item: the real config.cc, with the change hook
# of network_interface.cc cut out of that file as it is (software/test/dhcp/confighook_test.cc).
#
#   make          build
#   make test     build and run
#
PRJ      =  confighook
FINAL    =  $(RESULT)/$(PRJ)

include ../../../common/environment.mk

VPATH +=	$(PATH_SW)/test/dhcp
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
			confighook_test.cc

SRCS_ASM =
SRCS_6502 =
SRCS_BIN =
SRCS_IEC =
SRCS_NANO =

PATH_INC =  $(addprefix -I, $(VPATH)) -I$(RESULT)
OPTIONS  = -g -ffunction-sections -O0 -DRUNS_ON_PC -DSAFEMODE -DRECOVERYAPP -Wno-format -Wno-format-extra-args
COPTIONS = $(OPTIONS) -std=c99
CPPOPT   = $(OPTIONS) -fno-exceptions -fno-rtti -fno-threadsafe-statics -fpermissive
LIBS     = -lgcc
LFLAGS   = --gc-sections

include ../../../common/rules.mk

# The change hook under test is cut out of network_interface.cc as it is. CR is dropped
# from the extract only: the source file has CRLF line ends.
NI_SRC  = $(PATH_SW)/io/network/network_interface.cc
EXTRACT = $(PATH_SW)/io/wifi/tests/extract.awk

$(RESULT)/under_test.inc: $(NI_SRC) $(EXTRACT) | $(RESULT)
	@out=$$(tr -d '\r' < $(NI_SRC) | awk -v f="NetworkInterface :: dhcp_change" -f $(EXTRACT)); \
	[ -n "$$out" ] || { echo "confighook: dhcp_change not found in $(NI_SRC)" >&2; exit 1; }; \
	printf "%s\n" "$$out" > $@

confighook_test.o: $(RESULT)/under_test.inc

$(RESULT)/$(PRJ): $(OBJS_C) $(OBJS_CC) $(OBJS_ASM) $(OBJS_6502) $(OBJS_BIN) $(OBJS_IEC) $(OBJS_NANO)
	@echo Linking...
	$(CPP) $(ALL_OBJS) $(LLIB) -o $(RESULT)/$(PRJ) $(LIBS)

test: all
	@$(RESULT)/$(PRJ)
