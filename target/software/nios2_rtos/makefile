
CROSS	     = nios2-elf-
ELFTYPE      = elf32-littlenios2
ARCHITECTURE = nios2

include ../common/environment.mk

PRJ      =  free_rtos_demo
FINAL    =  $(RESULT)/$(PRJ).bin $(RESULT)/$(PRJ).elf $(OUTPUT)/$(PRJ).sim


VPATH +=	$(PATH_SW)/FreeRTOS/Source/portable/nios_sigma
VPATH +=	$(PATH_SW)/niosbsp
VPATH +=	$(PATH_SW)/niosbsp/HAL/inc
VPATH +=	$(PATH_SW)/niosbsp/drivers/inc
VPATH +=	$(PATH_SW)/application/rtos

INCLUDES =  $(wildcard $(addsuffix /*.h, $(VPATH)))

SRCS_C   =	croutine.c \
            event_groups.c \
            list.c \
            queue.c \
            tasks.c \
            timers.c \
            port.c \
            itu.c \
            small_printf.c \
            dump_hex.c \
            heap_3.c \
            $(PRJ).c

SRCS_CC	 =  

SRCS_ASM =

SRCS_ASMS = port_asm.S

OPTIONS  = -g -ffunction-sections -O0 -DOS  -Wno-write-strings -mno-hw-div -mno-hw-mul -mno-hw-mulx
COPTIONS = $(OPTIONS) -std=gnu99
CPPOPT   = $(OPTIONS) -fno-exceptions -fno-rtti -fno-threadsafe-statics
LINK 	 = ./nios_app.lds
LLIB     =  -L $(PATH_SW)/niosbsp \
			-L $(NIOS_TOOLCHAIN)/nios2-elf/lib \
            -L $(NIOS_TOOLCHAIN)/lib/gcc/nios2-elf/4.8.3
LIBS     =  -lc -lhal_bsp -lc -lgcc
LFLAGS   = --gc-sections 

include ../common/rules.mk
