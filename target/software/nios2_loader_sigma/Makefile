
CROSS	     = nios2-elf-
ELFTYPE      = elf32-littlenios2
ARCHITECTURE = nios2

include ../common/environment.mk

DEST     =  ~/proj/ult64/target/u64/system_boot.hex
PRJ      =  primary
FINAL    =  $(RESULT)/$(PRJ).hex $(RESULT)/$(PRJ).elf $(DEST)
HEXLAST  =  0x1FFF
HEXBASE  =  0x20000000
HEXEND   =  0x20001FFF

SRCS_C   =	sigma_loader.c
                        
SRCS_CC	 =  

VPATH +=	$(PATH_SW)/application/sigma \
			$(PATH_SW)/nios_solo_bsp \
			$(PATH_SW)/nios_solo_bsp/HAL/inc

SRCS_ASM =

SRCS_ASMS = 

SRCS_6502 = 

SRCS_BIN =  

SRCS_IEC = 

SRCS_NANO = 

PATH_INC =  $(addprefix -I, $(VPATH))
OPTIONS  = -g -ffunction-sections -Os -Wno-write-strings -mno-hw-div -mno-hw-mul -mno-hw-mulx -DRECOVERY=0
COPTIONS = $(OPTIONS) -std=gnu99
CPPOPT   = $(OPTIONS) -fno-exceptions -fno-rtti -fno-threadsafe-statics -fpermissive
BSP      = $(PATH_SW)/nios_solo_bsp
LINK 	 = $(BSP)/linker.x
LIBS2    = $(BSP)/libhal_bsp.a
LFLAGS   = --gc-sections -msys-crt0=$(BSP)/obj/HAL/src/crt0.o

include ../common/rules.mk

$(DEST): $(RESULT)/$(PRJ).hex
	cp $(RESULT)/$(PRJ).hex $(DEST)
