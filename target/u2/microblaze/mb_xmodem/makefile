
include ../../../common/environment.mk

PRJ      =  mb_xmodem
FINAL    =  $(RESULT)/$(PRJ).bin $(RESULT)/$(PRJ)_700.bit

FPGA_700      = ../../fpga/mb700/ultimate_mb_700a.bit
FPGA_700_BMM  = ../../fpga/mb700/ultimate_mb_700a_bd.bmm


VPATH    += $(PATH_SW)/application/mb_boot

SRCS_C   =	mbstart.c \
			itu.c \
			xmodem.c \
			mb_xmodem.c \
			small_printf.c

SRCS_CC	 =  

SRCS_ASM =  mb_crt0.s

SRCS_6502 = 

SRCS_BIN =  

PATH_INC =  $(addprefix -I, $(VPATH))
APPL_OPT = -DBOOTLOADER -D_FS_READONLY=1 -D_FS_MINIMIZE=3
COPTIONS = -ffunction-sections -Os -std=c99 $(APPL_OPT)
CPPOPT   = -ffunction-sections -Os -fno-exceptions -fno-rtti $(APPL_OPT)
LINK 	 = ./mb_boot.lds
LLIB     =  -L $(TOOLCHAIN)/lib/gcc/microblaze-xilinx-elf/4.1.2 \
			-L $(TOOLCHAIN)/microblaze-xilinx-elf/lib
LIBS     = -lstdc++ -lsupc++ -lgcc --start-group -lxil -lc -lm --end-group -lgcc
LFLAGS   = --relax --gc-sections 

include ../../../common/rules.mk

$(RESULT)/$(PRJ)_700.bit: $(OUTPUT)/$(PRJ).mem $(FPGA_700) $(FPGA_700_BMM)
	@echo "Data2MEM 700A..."
	@$(XILINXBIN)/data2mem -bm $(FPGA_700_BMM) \
	         -bt $(FPGA_700) \
	         -bd $(OUTPUT)/$(PRJ).mem tag icache \
	         -bd $(OUTPUT)/$(PRJ).mem tag dcache \
	         -o b $(RESULT)/$(PRJ)_700.bit \
	         -bx $(OUTPUT)
