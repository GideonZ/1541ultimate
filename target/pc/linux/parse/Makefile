PRJ      =  parse
FINAL    =  $(RESULT)/$(PRJ)

include ../../../common/environment.mk

VPATH +=	$(PATH_SW)/FreeRTOS/Source/portable/microblaze
VPATH +=	$(PATH_SW)/test/iecdrive
VPATH +=	$(PATH_SW)/chan_fat/ff14/source
VPATH :=    $(PATH_SW)/test/stubs $(PATH_SW)/chan_fat/pc $(VPATH)

CROSS     = 
CC		  = $(CROSS)gcc
CPP		  = $(CROSS)g++
LD		  = $(CROSS)ld
OBJDUMP   = $(CROSS)objdump
OBJCOPY	  = $(CROSS)objcopy
SIZE	  = $(CROSS)size

.SUFFIXES:


SRCS_C   =	dump_hex.c
SRCS_CC	 =  mystring.cc \
			pattern.cc \
			cbmdos_parser.cc \
			cbmdos_parser_test.cc

SRCS_ASM =  
SRCS_6502 = 
SRCS_BIN =  
SRCS_IEC = 
SRCS_NANO = 

PATH_INC =  $(addprefix -I, $(VPATH))
OPTIONS  = -g -ffunction-sections -O0 -DRUNS_ON_PC -DSAFEMODE -Wno-format -Wno-format-extra-args
COPTIONS = $(OPTIONS) -std=c99
CPPOPT   = $(OPTIONS) -fno-exceptions -fno-rtti -fno-threadsafe-statics -fpermissive
#LIBS     =  -lgcc -lcurses
LIBS     =  -lgcc
LFLAGS   = --gc-sections 

include ../../../common/rules.mk

$(RESULT)/$(PRJ): $(OBJS_C) $(OBJS_CC) $(OBJS_ASM) $(OBJS_6502) $(OBJS_BIN) $(OBJS_IEC) $(OBJS_NANO)
	@echo Linking...
	$(CPP) $(ALL_OBJS) $(LLIB) -o $(RESULT)/$(PRJ) $(LIBS)

