
include ../common/environment.mk

PRJ      =  free_rtos_demo
FINAL      =  $(RESULT)/$(PRJ).bin
LINKMETHOD = ld

VPATH += $(PATH_SW)/FreeRTOS/Source/portable/microblaze
VPATH += $(PATH_SW)/application/rtos
VPATH += $(PATH_SW)/portable/microblaze

SRCS_C   =	croutine.c \
            event_groups.c \
            list.c \
            queue.c \
            tasks.c \
            timers.c \
            port.c \
            mbstart_app.c \
			malloc_lock.c \
            small_printf.c \
            heap_3.c \
            itu.c \
            $(PRJ).c

SRCS_CC	 =  

SRCS_ASM =  mb_crt0.s portasm.s

PATH_INC =  $(addprefix -I, $(VPATH) $(OUTPUT))
APP_OPTS = -D_NO_FILE_ACCESS
COPTIONS = -g -ffunction-sections -O2 -std=c99 $(APP_OPTS)
CPPOPT   = -g -ffunction-sections -O2 -fno-exceptions -fno-rtti -fno-threadsafe-statics $(APP_OPTS)
#OPTIONS  = -nostartfiles
LINK 	 = ./mb_app.lds
LFLAGS   = --gc-sections 

include ../common/rules.mk

    
