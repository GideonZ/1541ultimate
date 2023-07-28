
CROSS	     = 
ELFTYPE      = 
ARCHITECTURE = 

CC = gcc
CPP = g++

PRJ      =  server
FINAL    =  $(RESULT)/$(PRJ)

SRCS_C   =	server.c \
			middleware.c \
			multipart.c \
			http_protocol.c \
			url.c \


SRCS_CC	 =  mystring.cc \
			small_printf.cc \
			json.cc \
			pattern.cc \
			routes.cc \
			server_run.cc

# Outputs
RESULT    ?= result
OUTPUT    ?= output

RESULT_FP = $(shell pwd)/$(RESULT)
OUTPUT_FP = $(shell pwd)/$(OUTPUT)

PATH_SW  =  ../../../../software

VPATH     = $(PATH_SW)/application \
			$(PATH_SW)/application/ultimate \
			$(PATH_SW)/filesystem \
			$(PATH_SW)/filemanager \
			$(PATH_SW)/filetypes \
			$(PATH_SW)/system \
			$(PATH_SW)/infra \
			$(PATH_SW)/components \
			$(PATH_SW)/userinterface \
			$(PATH_SW)/io/stream \
			$(PATH_SW)/network \
			$(PATH_SW)/api \
			$(PATH_SW)/test/server \
			$(PATH_SW)/httpd/c-version/lib \
			$(PATH_SW)/chan_fat/ff14/source \
			$(PATH_SW)/chan_fat/full

SRCS_ASM =


SRCS_ASMS = 

SRCS_6502 = 

SRCS_BIN =  

SRCS_IEC =

SRCS_NANO = 

PATH_INC =  $(addprefix -I, $(VPATH))
OPTIONS  = -ffunction-sections -fdata-sections
OPTIONS += -O0 -Wno-write-strings -D_PARSE_SIGNAL_ -D_PARSE_SIGNAL_INT_ -DENABLE_STATIC_FILE=2 -DMHS_PORT=8001 -DSTATIC_FILE_FOLDER=\"/home/gideon/html\"
COPTIONS = $(OPTIONS) -std=gnu99
CPPOPT   = $(OPTIONS) -fno-exceptions -fno-rtti -fno-threadsafe-statics -fpermissive
LIBS    = 
LFLAGS   = --gc-sections
LINKMETHOD = gcc

include ../../../common/rules.mk

$(RESULT)/$(PRJ): $(LINK) $(OBJS_C) $(OBJS_CC) $(OBJS_ASM) $(OBJS_ASMS) $(OBJS_6502) $(OBJS_BIN) $(OBJS_IEC) $(OBJS_NANO) $(OBJS_RBF) $(OBJS_APP) $(LWIPLIB) $(OBJS_RAW)
	@echo Linking using GCC...
	$(CPP) -o $@ $(ALL_OBJS)
	@$(SIZE) $@
