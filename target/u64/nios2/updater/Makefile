
CROSS	     = nios2-elf-
ELFTYPE      = elf32-littlenios2
ARCHITECTURE = nios2

include ../../../common/environment.mk

FPGA_PATH ?= ../../../../external

PRJ      =  update
FINAL    =  $(RESULT)/$(PRJ).app $(RESULT)/$(PRJ).elf

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
            nios_main.c \
            alt_malloc_lock.c \
            alt_do_ctors.c \
            alt_load.c
             
SRCS_CC	 =  memory.cc \
            i2c_drv.cc \
			small_printf.cc \
			flash.cc \
			at45_flash.cc \
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
			rtc_i2c.cc \
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
			u2p_init.cc \
			usb_hwinit.cc \
			usb_base.cc \
			usb_device.cc \
			usb_hub.cc \
			usb_hid.cc \
			\
			update_u64.cc

VPATH +=	.
VPATH +=	$(PATH_SW)/FreeRTOS/Source/portable/nios
VPATH +=	$(PATH_SW)/portable/nios
VPATH +=	$(PATH_SW)/nios_appl_bsp
VPATH +=	$(PATH_SW)/nios_appl_bsp/HAL/inc
VPATH +=	$(PATH_SW)/nios_appl_bsp/drivers/inc
VPATH +=    $(PATH_SW)/application/update_u2p
VPATH +=    $(PATH_SW)/u64
VPATH +=    $(PATH_SW)/../html
VPATH +=	$(PATH_SW)/wifi/raw_u64/main
VPATH +=	$(PATH_SW)/wifi/raw_u64/build/bootloader
VPATH +=	$(PATH_SW)/wifi/raw_u64/build/partition_table
VPATH +=	$(PATH_SW)/wifi/raw_u64/build
VPATH +=    $(FPGA_PATH)
VPATH +=    ../ultimate/result

SRCS_ASM = update_binaries_u64.s

SRCS_ASMS = port_asm.S

SRCS_6502 = 

SRCS_BIN =  chars.bin 1541.bin 1571.bin 1581.bin snds1541.bin snds1571.bin snds1581.bin bootloader.bin partition-table.bin bridge.bin

SRCS_HTML = index.html

SRCS_IEC = 

SRCS_NANO = nano_minimal.nan

PATH_INC =  $(addprefix -I, $(VPATH))
OPTIONS  = -g -ffunction-sections -O0 -DOS -DNIOS=1 -DSAFEMODE=1 -DCLOCK_FREQ=66666667 -DU64=1 -Wno-write-strings -mno-hw-div -mno-hw-mul -mno-hw-mulx -DRECOVERYAPP=1
COPTIONS = $(OPTIONS) -std=gnu99
CPPOPT   = $(OPTIONS) -fno-exceptions -fno-rtti -fno-threadsafe-statics -fpermissive
BSP      = $(PATH_SW)/nios_appl_bsp
LINK 	 = ./linker.x
LIBS2    = $(BSP)/libhal_bsp.a
LFLAGS   = --gc-sections -msys-crt0=$(BSP)/obj/HAL/src/crt0.o

include ../../../common/rules.mk

$(OUTPUT)/u64.pof: u64.sof u64.cof
	quartus_cpf -c u64.cof
	
u64.swp: $(OUTPUT)/u64.pof
	$(SWAP) -h $< $(OUTPUT)/$@ 290000 
	 
u64.chk: u64.swp
	@echo Calculating checksum of $< binary to $(@F)..
	@$(CHECKSUM) $(OUTPUT)/$(<F) $(OUTPUT)/$(@F)

ultimate.chk: ../ultimate/result/ultimate.app
	@echo Calculating checksum of $< binary to $(@F)..
	@$(CHECKSUM) $< $(OUTPUT)/$(@F)

update_binaries_u64.o: u64.swp ../ultimate/result/ultimate.app

../ultimate/result/ultimate.app::
	make -C ../ultimate

update_u64.o: update_u64.cc $(INCLUDES) $(CHK_BIN) ultimate.chk u64.chk
	@echo "Compiling $(notdir $<)"
	@cat $(addprefix $(OUTPUT)/,$(CHK_BIN)) $(OUTPUT)/ultimate.chk $(OUTPUT)/u64.chk >$(OUTPUT)/checksums.h
	@$(CPP) $(CPPOPT) $(PATH_INC) -B. -c -Wa,-ahlms=$(OUTPUT)/$(@:.o=.lst) -o $(OUTPUT)/$(@F) $<
