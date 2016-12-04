
include ../common/environment.mk

PRJ        =  irq_test
FINAL      =  $(RESULT)/$(PRJ).bin $(OUTPUT)/$(PRJ).sim
LINKMETHOD = ld

VPATH      += $(PATH_SW)/application/mb_hello \
			$(PATH_SW)/portable/microblaze \
			$(ROMS)

SRCS_C   =	mbstart_app.c \
			itu.c \
			irq_test.c \
			dump_hex.c \
			small_printf.c


SRCS_CC	 =  

SRCS_ASM = mb_crt0.s 

SRCS_6502 = 

SRCS_BIN =  

PATH_INC =  $(addprefix -I, $(VPATH) $(OUTPUT))
APP_OPTS = -D_NO_FILE_ACCESS
COPTIONS = -g -ffunction-sections -O2 -std=c99 $(APP_OPTS)
CPPOPT   = -g -ffunction-sections -O2 -fno-exceptions -fno-rtti -fno-threadsafe-statics $(APP_OPTS)
#OPTIONS  = -nostartfiles
LINK 	 = ./mb_app.lds
LFLAGS   = --gc-sections 

include ../common/rules.mk

