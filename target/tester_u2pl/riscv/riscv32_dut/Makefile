
CROSS	     = riscv32-unknown-elf-
ELFTYPE      = elf32-littleriscv
ARCHITECTURE = riscv
# CPU architecture and ABI
MARCH ?= rv32i
MABI  ?= ilp32

include ../../../common/environment.mk

PRJ      =  dut
FINAL    =  $(RESULT)/$(PRJ).app $(RESULT)/$(PRJ).elf $(RESULT)/$(PRJ).bin

SRCS_C   =	itu.c \
			dump_hex.c \
			assert.c \
			profiler.c \
			croutine.c \
			event_groups.c \
			list.c \
			queue.c \
			tasks.c \
			timers.c \
			port.c \
			heap_3.c \
			mdio.c \
			riscv_main.c \
			syscalls.c \
			do_ctors.c \
			malloc_lock.c \
                        
SRCS_CC	 =  memory.cc \
			small_printf.cc \
			dut_main.cc \
			i2c_drv.cc \
			flash.cc \
			w25q_flash.cc \
			codec.cc \
			network_config.cc \
			network_interface.cc \
			network_test.cc \
			rmii_interface.cc \
			ethernet_test.cc \
			usb_base.cc \
			usb_device.cc \
			usb_hub.cc \
			usb_test.cc \
			rtc_only_i2c.cc


VPATH +=	$(PATH_SW)/FreeRTOS/Source/portable/risc-v
VPATH +=	$(PATH_SW)/FreeRTOS/Source/portable/risc-v/chip_specific_extensions/RV32I_CLINT_no_extensions
VPATH +=	$(PATH_SW)/portable/riscv
VPATH +=	$(PATH_SW)/../neorv32/sw/common
VPATH +=	$(PATH_SW)/../neorv32/sw/lib/include
VPATH +=	$(PATH_SW)/../neorv32/sw/lib/source
VPATH +=    $(PATH_SW)/application/u2pl_tester

SRCS_ASM =

SRCS_ASMS = crt0.S port_asm.S

SRCS_6502 = 

SRCS_BIN =  

SRCS_IEC = 

SRCS_NANO = nano_phytest.nan nano_minimal.nan

VPATH +=	$(PATH_SW)/FreeRTOS/Source/portable/risc-v

PATH_INC =  $(addprefix -I, $(VPATH))
OPTIONS  = -march=$(MARCH) -mabi=$(MABI) $(EFFORT) -ffunction-sections -fdata-sections -nostartfiles -mno-fdiv
OPTIONS += -g -Os -DOS -DNO_IO_RESET=1 -DVERBOSE=1 -DNO_BOOT=0 -DUSB2503=1 -DIOBASE=0x10000000 -DU2P_IO_BASE=0x10100000 -DCLOCK_FREQ=50000000 -Wno-write-strings
COPTIONS = $(OPTIONS) -std=gnu99
CPPOPT   = $(OPTIONS) -fno-exceptions -fno-rtti -fno-threadsafe-statics -fpermissive
LINK 	 = linker.x
LFLAGS   = --gc-sections 
#LINKMETHOD = ld

include ../../../common/rules.mk
