
CROSS	     = riscv32-unknown-elf-
ELFTYPE      = elf32-littleriscv
ARCHITECTURE = riscv
# CPU architecture and ABI
MARCH ?= rv32i
MABI  ?= ilp32

include ../../../common/environment.mk
# /home/gideon/proj/ultimate/target/u64ii/riscv/test/result/u64ii_test.elf
PRJ      =  u64ii_test
FINAL    =  $(RESULT)/$(PRJ).app $(RESULT)/$(PRJ).elf $(OUTPUT)/$(PRJ).sim

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
			sid_coeff.c \
			riscv_main.c \
			do_ctors.c \
			malloc_lock.c 

SRCS_CC	 =  u64ii_init.cc \
			usb_hwinit.cc \
			i2c_drv.cc \
			hw_i2c_drv.cc \
			nau8822.cc \
			small_printf.cc \
			memory.cc \
			init_function.cc \
			flash.cc \
			w25q_flash.cc \
			s25fl_flash.cc \
			prog_flash.cc \
			ult_syscalls.cc \
			color_timings.cc \
			hdmi_scan.cc \
			screen.cc \
			$(PRJ).cc

VPATH +=	$(PATH_SW)/FreeRTOS/Source/portable/risc-v
VPATH +=	$(PATH_SW)/FreeRTOS/Source/portable/risc-v/chip_specific_extensions/RV32I_CLINT_no_extensions
VPATH +=	$(PATH_SW)/portable/riscv
VPATH +=	$(PATH_SW)/u64
VPATH +=	$(PATH_SW)/../neorv32/sw/common
VPATH +=	$(PATH_SW)/../neorv32/sw/lib/include
VPATH +=	$(PATH_SW)/../neorv32/sw/lib/source


SRCS_ASM =

SRCS_ASMS = crt0.S port_asm.S

SRCS_6502 = 

SRCS_BIN =  characters.901225-01.bin basic.901226-01.bin kernal.901227-03.bin

SRCS_IEC = 

SRCS_NANO = nano_minimal.nan

PATH_INC =  $(addprefix -I, $(VPATH))
OPTIONS  = -march=$(MARCH) -mabi=$(MABI) $(EFFORT) -ffunction-sections -fdata-sections -nostartfiles -mno-fdiv
OPTIONS += -gdwarf-2 -O0 -DRISCV -DUSB2513 -DOS -DIOBASE=0x10000000 -DU2P_IO_BASE=0x10100000 -DCLOCK_FREQ=100000000 -Wno-write-strings -DFP_SUPPORT=1
COPTIONS = $(OPTIONS) -std=gnu99
CPPOPT   = $(OPTIONS) -std=gnu++11 -fno-exceptions -fno-rtti -fno-threadsafe-statics -fpermissive
LINK 	 = linker.x
LIBS    = ../../../libs/riscv/lwip/result/liblwip.a
#LFLAGS   = --gc-sections -msys-crt0=$(BSP)/obj/HAL/src/crt0.o
LFLAGS   = --gc-sections
LINKMETHOD = ld

include ../../../common/rules.mk
