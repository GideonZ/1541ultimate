
CROSS	     = nios2-elf-
ELFTYPE      = elf32-littlenios2
ARCHITECTURE = nios2

include ../common/environment.mk

PRJ      =  mb_runner
FINAL    =  $(RESULT)/$(PRJ).app $(RESULT)/$(PRJ).elf $(OUTPUT)/$(PRJ).sim

SRCS_C   =	itu.c \
			dump_hex.c \
            alt_do_ctors.c \
            small_printf.c \
            mb_runner.c
                        
SRCS_CC	 =  

VPATH +=	$(PATH_SW)/portable/nios
VPATH +=	$(PATH_SW)/nios_appl_bsp
VPATH +=	$(PATH_SW)/nios_appl_bsp/HAL/inc
VPATH +=	$(PATH_SW)/nios_appl_bsp/drivers/inc
VPATH +=	$(PATH_SW)/application/mb_runner
VPATH +=	$(PATH_SW)/../target/software/mb_ultimate/result
VPATH +=	$(PATH_SW)/../target/software/mb_sim_boot/result

SRCS_ASM = mb_binaries.s

SRCS_ASMS = 

SRCS_6502 = 

SRCS_BIN = 

SRCS_IEC = 

SRCS_NANO =

PATH_INC =  $(addprefix -I, $(VPATH))
OPTIONS  = -g -ffunction-sections -Os -DDEVELOPER=1 -DOS -DNIOS=1 -DCLOCK_FREQ=62500000 -Wno-write-strings -mno-hw-div -mno-hw-mul -mno-hw-mulx
# OPTIONS  = -g -ffunction-sections -O0 -DOS -DNIOS=1 -DCLOCK_FREQ=62500000 -Wno-write-strings -mno-hw-div -mno-hw-mul -mno-hw-mulx
COPTIONS = $(OPTIONS) -std=gnu99
CPPOPT   = $(OPTIONS) -fno-exceptions -fno-rtti -fno-threadsafe-statics -fpermissive
BSP      = $(PATH_SW)/nios_appl_bsp
LINK 	 = $(BSP)/linker.x
LIBS2    = $(BSP)/libhal_bsp.a ../nios2_elf_lwip/result/liblwip.a
LFLAGS   = --gc-sections -msys-crt0=$(BSP)/obj/HAL/src/crt0.o

include ../common/rules.mk
