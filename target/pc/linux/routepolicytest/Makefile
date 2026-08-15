PRJ      =  routepolicytest
FINAL    =  $(RESULT)/$(PRJ)

include ../../../common/environment.mk

VPATH +=	$(PATH_SW)/test/routepolicy
VPATH +=	$(PATH_SW)/network

CROSS     =
CC		  = $(CROSS)gcc
CPP		  = $(CROSS)g++
LD		  = $(CROSS)ld
OBJDUMP   = $(CROSS)objdump
OBJCOPY	  = $(CROSS)objcopy
SIZE	  = $(CROSS)size

.SUFFIXES:

# The decision only. lwip_route_hook.c is the part that reads a netif and
# needs the stack, and it is built with the stack; this is the part that
# decides, and it is built with nothing.
SRCS_C   =	route_policy.c \
			route_policy_test.c

SRCS_CC	 =
SRCS_ASM =
SRCS_6502 =
SRCS_BIN =
SRCS_IEC =
SRCS_NANO =

PATH_INC =  $(addprefix -I, $(VPATH))
OPTIONS  = -g -ffunction-sections -O0 -Wall -Wextra -DRUNS_ON_PC
COPTIONS = $(OPTIONS) -std=c99
CPPOPT   = $(OPTIONS)
LIBS     = -lgcc
LFLAGS   = --gc-sections

include ../../../common/rules.mk

$(RESULT)/$(PRJ): $(OBJS_C) $(OBJS_CC) $(OBJS_ASM) $(OBJS_6502) $(OBJS_BIN) $(OBJS_IEC) $(OBJS_NANO)
	@echo Linking...
	$(CC) $(ALL_OBJS) $(LLIB) -o $(RESULT)/$(PRJ) $(LIBS)

test: all
	@$(RESULT)/$(PRJ)
