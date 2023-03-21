
CROSS	     = nios2-elf-
ELFTYPE      = elf32-littlenios2
ARCHITECTURE = nios2

include ../common/environment.mk

DEST     =  ../../../../ult64/target/u64_a4/onchip_mem.hex
PRJ      =  u64_boot
FINAL    =  $(RESULT)/$(PRJ).hex $(RESULT)/$(PRJ).elf $(DEST)
HEXLAST  =  0x0FFF
HEXBASE  =  0x30000000
HEXEND   =  0x30000FFF
BSP      = $(PATH_SW)/nios_solo_bsp

SRCS_C   =	u64_boot.c ddr2_calibrator_u64.c small_printf.c
                        
SRCS_CC	 =  

VPATH +=	$(PATH_SW)/system
VPATH +=	$(PATH_SW)/portable/nios

SRCS_ASM =

SRCS_ASMS = 

SRCS_6502 = 

SRCS_BIN =  

SRCS_IEC = 

SRCS_NANO = 

VPATH +=	$(PATH_SW)/FreeRTOS/Source/portable/nios
VPATH +=	$(BSP)
VPATH +=	$(BSP)/HAL/inc
VPATH +=	$(BSP)/drivers/inc

PATH_INC =  $(addprefix -I, $(VPATH))
OPTIONS  = -g -ffunction-sections -Os -Wno-write-strings -mno-hw-div -mno-hw-mul -mno-hw-mulx
COPTIONS = $(OPTIONS) -std=gnu99
CPPOPT   = $(OPTIONS) -fno-exceptions -fno-rtti -fno-threadsafe-statics -fpermissive
LINK 	 = ./linker.x
LIBS2    = $(BSP)/libhal_bsp.a
LFLAGS   = --gc-sections -msys-crt0=$(BSP)/obj/HAL/src/crt0.o

include ../common/rules.mk

$(DEST): $(RESULT)/$(PRJ).hex
	cp $(RESULT)/$(PRJ).hex $(DEST)
	