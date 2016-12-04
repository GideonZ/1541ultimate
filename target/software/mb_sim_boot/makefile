
include ../common/environment.mk

PRJ        =  mb_boot
FINAL      =  $(RESULT)/$(PRJ).bin $(OUTPUT)/$(PRJ).s00
LINKMETHOD = ld

#PATH_SW  =  ../../../software
VPATH   += $(PATH_SW)/portable/microblaze
 

SRCS_C   =
SRCS_CC	 =
SRCS_ASM =  mb_sim_crt0.s
SRCS_6502 = 
SRCS_BIN =  

PATH_INC =  $(addprefix -I, $(VPATH))
APPL_OPT = -DBOOTLOADER
COPTIONS = -ffunction-sections -Os -std=c99 $(APPL_OPT)
CPPOPT   = -ffunction-sections -Os -fno-exceptions -fno-rtti $(APPL_OPT)
LINK 	 = ./mb_boot.lds
LFLAGS   = --gc-sections

include ../common/rules.mk
