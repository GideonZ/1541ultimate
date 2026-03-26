PRJ      =  test_http
FINAL    =  $(RESULT)/$(PRJ)

include ../../../common/environment.mk

#VPATH +=	$(PATH_SW)/FreeRTOS/Source/portable/microblaze
#VPATH +=	$(PATH_SW)/test/filesys
#VPATH +=	$(PATH_SW)/chan_fat/ff14/source
#VPATH :=    $(PATH_SW)/chan_fat/pc $(VPATH)

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
			small_printf.c

SRCS_CC	 =  mystring.cc \
			json.cc \
			http_target.cc \
			pattern.cc \
			test_http_target.cc


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

include ../../../common/rules.mk

$(RESULT)/$(PRJ): $(OBJS_C) $(OBJS_CC) $(OBJS_ASM) $(OBJS_6502) $(OBJS_BIN) $(OBJS_IEC) $(OBJS_NANO)
	@echo Linking...
	$(CPP) $(ALL_OBJS) $(LLIB) -o $(RESULT)/$(PRJ) $(LIBS)

