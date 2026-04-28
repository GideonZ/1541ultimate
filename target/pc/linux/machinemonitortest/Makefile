PRJ      =  machinemonitortest
FINAL    =  $(RESULT)/$(PRJ)

include ../../../common/environment.mk

VPATH += $(PATH_SW)/test/user_interface/src

CROSS     =
CC        = $(CROSS)gcc
CPP       = $(CROSS)g++
LD        = $(CROSS)ld
OBJDUMP   = $(CROSS)objdump
OBJCOPY   = $(CROSS)objcopy
SIZE      = $(CROSS)size

.SUFFIXES:

SRCS_C   = dump_hex.c

SRCS_CC  = small_printf.cc \
			mystring.cc \
			screen.cc \
			ui_elements.cc \
			machine_monitor.cc \
			task_actions.cc \
			disassembler_6502.cc \
			machine_monitor_test.cc

SRCS_ASM =
SRCS_6502 =
SRCS_BIN =
SRCS_IEC =
SRCS_NANO =

PATH_INC = $(addprefix -I, $(VPATH))
OPTIONS  = -g -ffunction-sections -O0 -DRUNS_ON_PC -Wno-format -Wno-format-extra-args
COPTIONS = $(OPTIONS) -std=c99
CPPOPT   = $(OPTIONS) -fno-exceptions -fno-rtti -fno-threadsafe-statics
LIBS     = -lgcc
LFLAGS   = --gc-sections

include ../../../common/rules.mk

$(RESULT)/$(PRJ): $(OBJS_C) $(OBJS_CC) $(OBJS_ASM) $(OBJS_6502) $(OBJS_BIN) $(OBJS_IEC) $(OBJS_NANO)
	@echo Linking...
	$(CPP) $(ALL_OBJS) $(LLIB) -o $(RESULT)/$(PRJ) $(LIBS)