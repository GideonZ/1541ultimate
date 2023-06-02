CROSS	     = riscv32-unknown-elf-
ELFTYPE      = elf32-littleriscv
ARCHITECTURE = riscv
# CPU architecture and ABI
MARCH ?= rv32i
MABI  ?= ilp32

include ../common/environment.mk

PRJ        =  update
FINAL      =  $(RESULT)/$(PRJ).u2r
#LINKMETHOD = ld

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
            ff.c \
            ffsystem.c \
            ffunicode.c \
            time.c \
			riscv_main.c \
			syscalls.c \
			do_ctors.c \
			malloc_lock.c \

SRCS_CC	 =  memory.cc \
			small_printf.cc \
			flash.cc \
			at45_flash.cc \
			w25q_flash.cc \
			s25fl_flash.cc \
			pattern.cc \
			config.cc \
			filemanager.cc \
			file_device.cc \
			file_partition.cc \
			filesystem_root.cc \
			init_function.cc \
			c64.cc \
			screen.cc \
			screen_vt100.cc \
			keyboard_c64.cc \
			keyboard_usb.cc \
			keyboard_vt100.cc \
			mystring.cc \
			size_str.cc \
			rtc.cc \
			task_menu.cc \
			userinterface.cc \
			ui_elements.cc \
			stream.cc \
			host_stream.cc \
			prog_flash.cc \
			stream_uart.cc \
			path.cc \
			blockdev.cc \
			blockdev_flash.cc \
			disk.cc \
			partition.cc \
			file_system.cc \
			diskio.cc \
			directory.cc \
			file.cc \
			filesystem_fat.cc \
			$(PRJ).cc

SRCS_ASM =

SRCS_ASMS = crt0.S port_asm.S

SRCS_6502 = 

SRCS_BIN =  ultimate.bin \
			rv700dd.bin \
			boot2.bin \
			chars.bin \
			1541.bin \
			snds1541.bin

SRCS_IEC = 

SRCS_NANO = 

VPATH +=	$(PATH_SW)/FreeRTOS/Source/portable/risc-v
VPATH +=	$(PATH_SW)/FreeRTOS/Source/portable/risc-v/chip_specific_extensions/RV32I_CLINT_no_extensions
VPATH +=	$(PATH_SW)/portable/riscv
VPATH +=	$(PATH_SW)/../neorv32/sw/common
VPATH +=	$(PATH_SW)/../neorv32/sw/lib/include
VPATH +=	$(PATH_SW)/../neorv32/sw/lib/source
VPATH   += $(PATH_SW)/application/rv_update_u2
VPATH   += ../riscv32_u2_ultimate/result
VPATH   += ../riscv32_u2_boot2/result
VPATH   += ../../fpga/rv700dd/rv700dd

PATH_INC =  $(addprefix -I, $(VPATH) $(OUTPUT))
OPTIONS  = -march=$(MARCH) -mabi=$(MABI) -ffunction-sections -fdata-sections -nostartfiles -mno-fdiv
OPTIONS += -gdwarf-2 -Os -DRISCV -DFIRST_WRITABLE_PAGE=0 -DU2 -DOS=1 -DSAFEMODE=1 -DUPDATER -DRECOVERYAPP=1
OPTIONS += -DNO_IO_RESET=1 -DIOBASE=0x10000000 -DU2P_IO_BASE=0x10100000 -DCLOCK_FREQ=50000000 -Wno-write-strings
COPTIONS = $(OPTIONS) -std=gnu99
CPPOPT   = $(OPTIONS) -fno-exceptions -fno-rtti -fno-threadsafe-statics -fpermissive

LINK 	 = ./linker.x
LFLAGS   = --gc-sections 

include ../common/rules.mk

update.o: update.cc $(INCLUDES) $(CHK_BIN)
	@echo "Compiling $(notdir $<)"
	@cat $(addprefix $(OUTPUT)/,$(CHK_BIN)) >$(OUTPUT)/checksums.h
	@$(CPP) $(CPPOPT) $(PATH_INC) -B. -c -Wa,-ahlms=$(OUTPUT)/$(@:.o=.lst) -o $(OUTPUT)/$(@F) $<

$(RESULT)/$(PRJ).u2r: $(OUTPUT)/$(PRJ).out
	@echo Creating Updater Binary $@
	@$(OBJCOPY) -O binary $< $@
