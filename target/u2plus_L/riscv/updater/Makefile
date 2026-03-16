
CROSS	     = riscv32-unknown-elf-
ELFTYPE      = elf32-littleriscv
ARCHITECTURE = riscv
# CPU architecture and ABI
MARCH ?= rv32i_zicsr
MABI  ?= ilp32

include ../../../common/environment.mk

PRJ      =  update
FINAL    =  $(RESULT)/$(PRJ).app $(RESULT)/$(PRJ).elf

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
            mdio.c \
            cmd_buffer.c \
			riscv_main.c \
			do_ctors.c \
			malloc_lock.c \
                        
SRCS_CC	 =  memory.cc \
			u2pl_init.cc \
            i2c_drv.cc \
            codec.cc \
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
			rtc_i2c.cc \
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
			dma_uart.cc \
			esp32.cc \
			wifi_cmd.cc \
			$(PRJ).cc

VPATH +=	$(PATH_SW)/FreeRTOS/Source/portable/risc-v
VPATH +=	$(PATH_SW)/FreeRTOS/Source/portable/risc-v/chip_specific_extensions/RV32I_CLINT_no_extensions
VPATH +=	$(PATH_SW)/portable/riscv
VPATH +=	$(PATH_SW)/../neorv32/sw/common
VPATH +=	$(PATH_SW)/../neorv32/sw/lib/include
VPATH +=	$(PATH_SW)/../neorv32/sw/lib/source
VPATH +=    $(PATH_SW)/application/update_u2pl
VPATH +=    $(PATH_SW)/../html
VPATH +=	$(PATH_SW)/wifi/raw_c3/main
VPATH +=	$(PATH_SW)/wifi/raw_c3/build/bootloader
VPATH +=	$(PATH_SW)/wifi/raw_c3/build/partition_table
VPATH +=	$(PATH_SW)/wifi/raw_c3/build
VPATH +=    ../../../fpga/u2plus_ecp5/impl1
VPATH +=    ../ultimate/result

SRCS_ASM = update_binaries.s

SRCS_ASMS = crt0.S port_asm.S

SRCS_6502 = 

SRCS_BIN =  chars.bin 1541.bin 1571.bin 1581.bin snds1541.bin snds1571.bin snds1581.bin bootloader.bin partition-table.bin bridge.bin

SRCS_HTML = index.html

SRCS_IEC = 

SRCS_NANO = nano_minimal.nan

PATH_INC =  $(addprefix -I, $(VPATH))
OPTIONS  = -march=$(MARCH) -mabi=$(MABI) -ffunction-sections -fdata-sections -nostartfiles -mno-fdiv
OPTIONS += -gdwarf-2 -Os -DRISCV -DUSB2503 -DOS -DIOBASE=0x10000000 -DU2P_IO_BASE=0x10100000 -DCLOCK_FREQ=50000000 -Wno-write-strings -DRECOVERYAPP=1
COPTIONS = $(OPTIONS) -std=gnu99
CPPOPT   = $(OPTIONS) -fno-exceptions -fno-rtti -fno-threadsafe-statics -fpermissive
LINK 	 = ./linker.x
LFLAGS   = --gc-sections

include ../../../common/rules.mk

update_binaries.o: u2p_ecp5_impl1.bit ../ultimate/result/ultimate.app

