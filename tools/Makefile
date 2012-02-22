#
# Makefile for 1541 Ultimate Project
#
# This Makefile requires GNU make
#
# --------------------------------------------------------------------------
SHELL = /bin/bash
MAKEFILE = Makefile

# Core information
CC  = gcc



# --------------------------------------------------------------------------
# Compiling

%: %.c
	@echo
	@echo $@
	@$(CC) $(CFLAGS) $^ -o $(basename $@)

# --------------------------------------------------------------------------
# Binaries

.PHONY:	all clean
all:   	bin2hex hex2bin make_array make_mem makeappl promgen checksum dump_vcd
ifneq ($(TERM), cygwin)
	@$(MAKE) -C 64tass
endif

clean:
	@rm -f bin2hex
	@rm -f hex2bin
	@rm -f make_array
	@rm -f make_mem
	@rm -f makeappl
	@rm -f promgen
	@rm -f checksum
	@rm -f bin2hex.exe
	@rm -f hex2bin.exe
	@rm -f make_array.exe
	@rm -f make_mem.exe
	@rm -f makeappl.exe
	@rm -f promgen.exe
	@rm -f checksum.exe
	@rm -f dump_vcd.exe
	@rm -f 64tass/64tass
	@rm -f 64tass/*.o
	