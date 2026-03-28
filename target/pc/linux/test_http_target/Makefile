PRJ      =  test_http
FINAL    =  $(RESULT)/$(PRJ)
LWIP     =  nolwip

include ../../../common/environment.mk

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
			small_printf.c \
			server.c \
			http_protocol.c \
			middleware.c \
			multipart.c \
			url.c \
			dummy_api.c

SRCS_CC	 =  mystring.cc \
			json.cc \
			http_target.cc \
			pattern.cc \
			http_request.cc \
			test_http_target.cc


SRCS_ASM =  
SRCS_6502 = 
SRCS_BIN =  
SRCS_IEC = 
SRCS_NANO = 

PATH_INC =  $(addprefix -I, $(VPATH))
OPTIONS  = -g -ffunction-sections -O0 -DRUNS_ON_PC -Wno-format -Wno-format-extra-args -DENABLE_STATIC_FILE=1 -DMHS_PORT=8001 -DTEMP_FILE_PATH=\"./\"
COPTIONS = $(OPTIONS) -std=c99
CPPOPT   = $(OPTIONS) -fno-exceptions -fno-rtti -fno-threadsafe-statics
LINK 	 = ./mb_app.lds
LIBS     =  -lgcc 
LFLAGS   = --gc-sections 

include ../../../common/rules.mk

$(RESULT)/$(PRJ): $(OBJS_C) $(OBJS_CC) $(OBJS_ASM) $(OBJS_6502) $(OBJS_BIN) $(OBJS_IEC) $(OBJS_NANO)
	@echo Linking...
	$(CPP) $(ALL_OBJS) $(LLIB) -o $(RESULT)/$(PRJ) $(LIBS)

