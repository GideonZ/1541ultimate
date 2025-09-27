
CROSS	     = riscv32-unknown-elf-
ELFTYPE      = elf32-littleriscv
ARCHITECTURE = riscv
# CPU architecture and ABI
MARCH ?= rv32i
MABI  ?= ilp32

include ../../../common/environment.mk

FPGA_PATH ?= $(PATH_SW)/../external/rf_test

PRJ      =  rftest_updater
FINAL    =  $(PRJ).ue2 $(RESULT)/$(PRJ).app $(RESULT)/$(PRJ).elf $(OUTPUT)/$(PRJ).sim

SRCS_C   =	itu.c \
			dump_hex.c \
			codec.c \
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
             
SRCS_CC	 =  u64ii_init.cc \
			color_timings.cc \
			hdmi_scan.cc \
			usb_hwinit.cc \
			memory.cc \
			i2c_drv.cc \
			hw_i2c_drv.cc \
			nau8822.cc \
			small_printf.cc \
			flash.cc \
			w25q_flash.cc \
            s25fl_flash.cc \
            s25fl_l_flash.cc \
			pattern.cc \
			config.cc \
			filemanager.cc \
			file_device.cc \
			file_partition.cc \
			filesystem_root.cc \
			init_function.cc \
			c64.cc \
			u64_machine.cc \
			screen.cc \
			screen_vt100.cc \
			keyboard_c64.cc \
			keyboard_usb.cc \
			keyboard_vt100.cc \
			mystring.cc \
			size_str.cc \
			rtc_dummy.cc \
			task_menu.cc \
			userinterface.cc \
			ui_elements.cc \
			stream.cc \
			host_stream.cc \
			prog_flash.cc \
			product.cc \
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
			\
			usb_base.cc \
			usb_device.cc \
			usb_hub.cc \
			usb_hid.cc \
			\
			update_u64ii.cc

VPATH +=	.
VPATH +=	$(PATH_SW)/FreeRTOS/Source/portable/risc-v
VPATH +=	$(PATH_SW)/FreeRTOS/Source/portable/risc-v/chip_specific_extensions/RV32I_CLINT_no_extensions
VPATH +=	$(PATH_SW)/portable/riscv
VPATH +=	$(PATH_SW)/6502/sidcrt/target
VPATH +=	$(PATH_SW)/u64
VPATH +=    $(PATH_SW)/application/update_u2p
VPATH +=    $(PATH_SW)/../html
VPATH +=	$(PATH_SW)/u64ctrl/main
VPATH +=	$(PATH_SW)/u64ctrl/build/bootloader
VPATH +=	$(PATH_SW)/u64ctrl/build/partition_table
VPATH +=	$(PATH_SW)/u64ctrl/build
VPATH +=    $(FPGA_PATH)
VPATH +=    ../ultimate_rftest/result

SRCS_ASM = update_binaries_u64ii.s

SRCS_ASMS = crt0.S port_asm.S

SRCS_6502 = 

SRCS_BIN =  chars.bin 1541.bin 1571.bin 1581.bin snds1541.bin snds1571.bin snds1581.bin bootloader.bin partition-table.bin u64ctrl.bin

SRCS_HTML = index.html

SRCS_IEC = 

SRCS_NANO = nano_minimal.nan

PATH_INC =  $(addprefix -I, $(VPATH))
OPTIONS  = -g -ffunction-sections -DRISCV -Os -DOS -DUSB2513 -DRISC=1 -DSAFEMODE=1 -DIOBASE=0x10000000 -DU2P_IO_BASE=0x10100000
OPTIONS += -DCLOCK_FREQ=100000000 -DU64=2 -Wno-write-strings -fdata-sections -nostartfiles -mno-fdiv -DRECOVERYAPP=1 -DCOMMODORE=1
OPTIONS += -DDEVELOPER=1 -DSILENT=1 -DNO_ESP=1
COPTIONS = $(OPTIONS) -std=gnu99
CPPOPT   = $(OPTIONS) -std=gnu++11 -fno-exceptions -fno-rtti -fno-threadsafe-statics -fpermissive
LINK 	 = linker.x
LIBS     = #../../../libs/riscv/lwip/result/liblwip.a
#LFLAGS   = --gc-sections -msys-crt0=$(BSP)/obj/HAL/src/crt0.o
LFLAGS   = --gc-sections
LINKMETHOD = ld

include ../../../common/rules.mk


u64.swp: $(FPGA_PATH)/u64_mk2_artix.bit
	cp $< $(OUTPUT)/$(@F)

#$(SWAP) -h $< $(OUTPUT)/u64.swp 220000 
	 
u64.chk: u64.swp
	@echo Calculating checksum of $< binary to $(@F)..
	@$(CHECKSUM) $(OUTPUT)/$(<F) $(OUTPUT)/$(@F)

ultimate.chk: ../ultimate_rftest/result/ultimate.app
	@echo Calculating checksum of $< binary to $(@F)..
	@$(CHECKSUM) $< $(OUTPUT)/$(@F)

update_binaries_u64ii.o: u64.swp ../ultimate_rftest/result/ultimate.app

../ultimate_rftest/result/ultimate.app::
	make -C ../ultimate_rftest

update_u64ii.o: update_u64ii.cc $(INCLUDES) $(CHK_BIN) ultimate.chk u64.chk
	@echo "Compiling $(notdir $<)"
	@cat $(addprefix $(OUTPUT)/,$(CHK_BIN)) $(OUTPUT)/ultimate.chk $(OUTPUT)/u64.chk >$(OUTPUT)/checksums.h
	@$(CPP) $(CPPOPT) $(PATH_INC) -B. -c -Wa,-ahlms=$(OUTPUT)/$(@:.o=.lst) -o $(OUTPUT)/$(@F) $<

$(PRJ).ue2: $(RESULT)/$(PRJ).app
	@cp $(RESULT)/$(PRJ).app $(PRJ).ue2
