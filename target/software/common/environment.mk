# System tool locations

TOOLS = ../../../tools
HERE = $(shell pwd)
BIN2HEX = $(TOOLS)/bin2hex
HEX2BIN = $(TOOLS)/hex2bin
PROMGEN = $(TOOLS)/promgen
MAKEAPPL = $(TOOLS)/makeappl
MAKEMEM = $(TOOLS)/make_mem
TASS = $(TOOLS)/64tass/64tass
PARSE_IEC = $(TOOLS)/parse_iec.py
PARSE_NANO = $(TOOLS)/parse_nano.py
CHECKSUM = $(TOOLS)/checksum
SWAP = $(TOOLS)/swap
SVFDUMP = $(TOOLS)/svf_dump

# Configuration
CROSS        ?= mb-
ELFTYPE      ?= elf32-microblaze
ARCHITECTURE ?= MicroBlaze

BOOT = ../2nd_boot
LWIPLIB = ../$(subst -,_,$(CROSS))lwip/result/liblwip.a


# External inputs
ROMS = ../../../roms

# Outputs
RESULT    ?= result
OUTPUT    ?= output

RESULT_FP = $(shell pwd)/$(RESULT)
OUTPUT_FP = $(shell pwd)/$(OUTPUT)

PATH_SW  =  ../../../software

LWIP ?= lwip-1.4.1

VPATH     = $(PATH_SW)/application \
			$(PATH_SW)/application/ultimate \
			$(PATH_SW)/filesystem \
			$(PATH_SW)/filemanager \
			$(PATH_SW)/filetypes \
			$(PATH_SW)/system \
			$(PATH_SW)/infra \
			$(PATH_SW)/io/flash \
			$(PATH_SW)/drive \
			$(PATH_SW)/components \
			$(PATH_SW)/network \
			$(PATH_SW)/userinterface \
			$(PATH_SW)/io/stream \
			$(PATH_SW)/io/c64 \
			$(PATH_SW)/io/rtc \
			$(PATH_SW)/io/usb \
			$(PATH_SW)/io/i2c \
			$(PATH_SW)/io/mdio \
			$(PATH_SW)/io/tape \
			$(PATH_SW)/io/icap \
			$(PATH_SW)/io/sd_card \
			$(PATH_SW)/io/audio \
			$(PATH_SW)/io/overlay \
			$(PATH_SW)/io/command_interface \
			$(PATH_SW)/io/copper \
			$(PATH_SW)/io/network \
			$(PATH_SW)/io/uart \
			$(PATH_SW)/io/wifi \
			$(PATH_SW)/io/acia \
			$(PATH_SW)/network/config \
			$(PATH_SW)/io/iec \
			$(PATH_SW)/6502 \
			$(PATH_SW)/ModPlayer_16k \
			$(PATH_SW)/chan_fat \
			$(PATH_SW)/chan_fat/option \
			$(PATH_SW)/chan_fat/full \
			$(PATH_SW)/FreeRTOS/Source \
			$(PATH_SW)/FreeRTOS/Source/include \
			$(PATH_SW)/FreeRTOS/Source/MemMang \
			$(PATH_SW)/$(LWIP)/src/include \
			$(PATH_SW)/$(LWIP)/src/include/ipv4 \
			$(PATH_SW)/$(LWIP)/src/include/posix/sys \
			$(PATH_SW)/$(LWIP)/src/include/posix \
			$(ROMS)

INCLUDES =  $(wildcard $(addsuffix /*.h, $(VPATH)))

XILINX ?= C:/Xilinx/13.2
PLATFORM ?= nt
TOOLCHAIN = $(XILINX_EDK)/gnu/microblaze/$(PLATFORM)
XILINXBIN = $(XILINX)/ISE_DS/ISE/bin/$(PLATFORM)

PATH_INC =  $(addprefix -I, $(VPATH))
# VPATH   += $(OUTPUT) $(RESULT)

CC		  = $(CROSS)gcc
CPP		  = $(CROSS)g++
LD		  = $(CROSS)ld
OBJDUMP   = $(CROSS)objdump
OBJCOPY	  = $(CROSS)objcopy
SIZE	  = $(CROSS)size

.SUFFIXES:

