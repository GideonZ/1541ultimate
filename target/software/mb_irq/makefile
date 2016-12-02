
include ../common/environment.mk

PRJ        =  irq_test
FINAL      =  $(RESULT)/$(PRJ).bin
LINKMETHOD = ld

VPATH      += $(PATH_SW)/application/mb_hello \
			$(PATH_SW)/portable/microblaze \
			$(ROMS)

SRCS_C   =	mbstart_app.c \
			itu.c \
			irq_test.c \
			small_printf.c


SRCS_CC	 =  

SRCS_ASM =  

SRCS_6502 = 

SRCS_BIN =  

PATH_INC =  $(addprefix -I, $(VPATH) $(OUTPUT))
APP_OPTS = -D_NO_FILE_ACCESS
COPTIONS = -g -ffunction-sections -Os -std=c99 $(APP_OPTS)
CPPOPT   = -g -ffunction-sections -Os -fno-exceptions -fno-rtti -fno-threadsafe-statics $(APP_OPTS)
#OPTIONS  = -nostartfiles
LINK 	 = ./mb_app.lds
LFLAGS   = --gc-sections 

include ../common/rules.mk

