# System tool locations

HERE = $(shell pwd)
TOOLS = ../../../tools
BIN2HEX = $(TOOLS)/bin2hex
HEX2BIN = $(TOOLS)/hex2bin
MAKEMEM  = $(TOOLS)/make_mem
MAKEAPPL = $(TOOLS)/makeappl
TASS = $(TOOLS)/64tass/64tass


# Configuration
SHELL     = bash

# External inputs
FPGA_700  = ../../../../Temp/cycle_chercker/cpu_checker_700a
BMM_FILE  = ../../../../private/fpga/debug/verification_cart/vhdl_source/rom_2k_bd.bmm

# Outputs
RESULT    = ./result
OUTPUT    = ./output
OUTPUT_FP = $(shell pwd)/output

PRJ      =  checker_rom
PATH_SW  =  ../../../software

VPATH     = $(PATH_SW)/6502

INCLUDES =  $(wildcard $(addsuffix /*.h, $(VPATH)))

SRCS_6502 = checker_rom.tas

PATH_INC =  $(addprefix -I, $(VPATH))

VPATH   += $(OUTPUT) $(RESULT)


.SUFFIXES:


.PHONY: clean all

all: $(OUTPUT) $(RESULT) $(RESULT)/$(PRJ).bit

$(FPGA_700).bit::

$(OUTPUT):
	@mkdir $(OUTPUT)

$(RESULT):
	@mkdir $(RESULT)
		
$(RESULT)/$(PRJ).bit: $(OUTPUT)/$(PRJ).mem $(FPGA_700).bit
	@echo "Data2MEM Checker 700A..."
	data2mem -bm $(BMM_FILE) \
	         -bt $(FPGA_700).bit \
	         -bd $(OUTPUT)/$(PRJ).mem \
	         -o b $(RESULT)/$(PRJ).bit

$(OUTPUT)/$(PRJ).65: $(SRCS_6502)
	@echo "Assembling $<"
	$(TASS) $< --m6502 --nostart -o $(OUTPUT)/$(@F)

$(OUTPUT)/$(PRJ).mem: $(OUTPUT)/$(PRJ).65
	@echo "Make mem..."
	@$(MAKEMEM) $< 2048 >$@

clean:
	@-rm -rf $(OUTPUT)
	@-rm -rf $(RESULT)

