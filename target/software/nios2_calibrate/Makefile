
CROSS	     = nios2-elf-
ELFTYPE      = elf32-littlenios2
ARCHITECTURE = nios2

include ../common/environment.mk

PRJ      =  calibrator
FINAL    =  $(RESULT)/$(PRJ).bin $(RESULT)/$(PRJ).elf $(OUTPUT)/$(PRJ).sim

SRCS_C   =	ddr2_calibrator.c
                        
SRCS_CC	 =  

VPATH +=	$(PATH_SW)/portable/nios

SRCS_ASM =

SRCS_ASMS = 

SRCS_6502 = 

SRCS_BIN =  

SRCS_IEC = 

SRCS_NANO = 

PATH_INC =  $(addprefix -I, $(VPATH))
OPTIONS  = -g -ffunction-sections -O0 -Wno-write-strings -mno-hw-div -mno-hw-mul -mno-hw-mulx
COPTIONS = $(OPTIONS) -std=gnu99
CPPOPT   = $(OPTIONS) -fno-exceptions -fno-rtti -fno-threadsafe-statics -fpermissive
LINK 	 = ./nios_solo_bram.lds
LLIB     =  -L $(PATH_SW)/nios_solo_bsp \
            -L $(NIOS_TOOLCHAIN)/nios2-elf/lib/mno-hw-mul \
            -L $(NIOS_TOOLCHAIN)/lib/gcc/nios2-elf/4.8.3/mno-hw-mul
# NOTE: The nios toolchain points to 
LIBS     =  -lc -lhal_bsp -lc -lgcc 
LFLAGS   = --gc-sections 

include ../common/rules.mk
