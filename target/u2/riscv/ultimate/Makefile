
CROSS	     = riscv32-unknown-elf-
ELFTYPE      = elf32-littleriscv
ARCHITECTURE = riscv
# CPU architecture and ABI
MARCH ?= rv32i
MABI  ?= ilp32

include ../../../common/environment.mk

PRJ      =  ultimate
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
			ff.c \
			ffsystem.c \
			ffunicode.c \
			time.c \
			sid_coeff.c \
			riscv_main.c \
			do_ctors.c \
			malloc_lock.c \
            server.c \
            http_protocol.c \
            middleware.c \
            multipart.c \
            url.c \

SRCS_CC	 =	small_printf.cc \
			stream.cc \
			stream_uart.cc \
			memory.cc \
			init_function.cc \
			flash.cc \
			at45_flash.cc \
			w25q_flash.cc \
			s25fl_flash.cc \
			product.cc \
			config.cc \
			subsys.cc \
			filemanager.cc \
			file_device.cc \
			file_partition.cc \
			embedded_d64.cc \
			embedded_t64.cc \
			embedded_iso.cc \
			embedded_fat.cc \
			rtc.cc \
			c64.cc \
			c64_crt.cc \
			c64_subsys.cc \
			screen.cc \
			keyboard_c64.cc \
			disk_image.cc \
			c1541.cc \
			wd177x.cc \
			mfmdisk.cc \
			mystring.cc \
			path.cc \
			pattern.cc \
			blockdev.cc \
			blockdev_file.cc \
			blockdev_ram.cc \
			blockdev_flash.cc \
			disk.cc \
			partition.cc \
			file_system.cc \
			diskio.cc \
			directory.cc \
			file.cc \
			filesystem_iso9660.cc \
			filesystem_root.cc \
			filesystem_d64.cc \
			filesystem_t64.cc \
			filesystem_fat.cc \
			sd_card.cc \
			sdio.cc \
			sdcard_manager.cc \
			size_str.cc \
			userinterface.cc \
			ui_elements.cc \
			user_file_interaction.cc \
			editor.cc \
			tree_browser.cc \
			tree_browser_state.cc \
			config_menu.cc \
			context_menu.cc \
			task_menu.cc \
			audio_select.cc \
			filetype_d64.cc \
			filetype_g64.cc \
			filetype_t64.cc \
			filetype_prg.cc \
			filetype_u2u.cc \
			filetype_reu.cc \
			filetype_crt.cc \
			filetype_sid.cc \
			filetype_bin.cc \
			filetype_cfg.cc \
			host_stream.cc \
			keyboard_vt100.cc \
			screen_vt100.cc \
			usb_base.cc \
			usb_device.cc \
			usb_scsi.cc \
			usb_hub.cc \
			usb_ax88772.cc \
			usb_hid.cc \
			usb_ms_cbi.cc \
			keyboard_usb.cc \
			network_config.cc \
			network_interface.cc \
			vfs.cc \
			ftpd.cc \
			tape_controller.cc \
			tape_recorder.cc \
			command_intf.cc \
			dos.cc \
			network_target.cc \
			control_target.cc \
			filetype_tap.cc \
			socket_stream.cc \
			socket_gui.cc \
			socket_dma.cc \
			syslog.cc \
			home_directory.cc \
			reu_preloader.cc \
			configio.cc \
			acia.cc \
			modem.cc \
			listener_socket.cc \
			ramdisk.cc \
			system_info.cc \
			ult_syscalls.cc \
			httpd.cc \
			json.cc \
			routes.cc \
			route_files.cc \
			route_drives.cc \
			route_runners.cc \
			route_configs.cc \
			route_machine.cc \
			assembly_search.cc \
			assembly.cc \
			filesystem_a64.cc \
			sntp_time.cc \
			$(PRJ).cc

SRCS_CC +=  iec_interface.cc \
			iec_drive.cc \
			iec_channel.cc \
			iec_ulticopy.cc \
			softiec_target.cc

VPATH +=	$(PATH_SW)/FreeRTOS/Source/portable/risc-v
VPATH +=	$(PATH_SW)/FreeRTOS/Source/portable/risc-v/chip_specific_extensions/RV32I_CLINT_no_extensions
VPATH +=	$(PATH_SW)/portable/riscv
VPATH +=	$(PATH_SW)/6502/sidcrt/target
VPATH +=	$(PATH_SW)/../neorv32/sw/common
VPATH +=	$(PATH_SW)/../neorv32/sw/lib/include
VPATH +=	$(PATH_SW)/../neorv32/sw/lib/source


SRCS_ASM =

SRCS_ASMS = crt0.S port_asm.S

SRCS_6502 = bootcrt.tas cmd_test_rom.tas ulticopy.tas eapi.tas

SRCS_BIN =  basic.bin chars.bin module.bin sidcrt.bin muscrt.bin musplayer.bin

SRCS_IEC = iec_code.iec

SRCS_NANO = nano_minimal.nan

PATH_INC =  $(addprefix -I, $(VPATH))
OPTIONS  = -march=$(MARCH) -mabi=$(MABI) $(EFFORT) -ffunction-sections -fdata-sections -nostartfiles -mno-fdiv
OPTIONS += -gdwarf-2 -Os -DRISCV -DU2 -DOS -DIOBASE=0x10000000 -DU2P_IO_BASE=0x10100000 -DCLOCK_FREQ=50000000 -Wno-write-strings
COPTIONS = $(OPTIONS) -std=gnu99
CPPOPT   = $(OPTIONS) -std=gnu++11 -fno-exceptions -fno-rtti -fno-threadsafe-statics -fpermissive
LINK 	 = linker.x
LIBS    = ../../../libs/riscv/lwip/result/liblwip.a
#LFLAGS   = --gc-sections -msys-crt0=$(BSP)/obj/HAL/src/crt0.o
LFLAGS   = --gc-sections
LINKMETHOD = ld

include ../../../common/rules.mk

mps_png.o: mps_png.cc
	@echo Compiling $(<F) optimized for speed
	@$(CPP) $(CPPOPT) -O3 $(PATH_INC) -B. -c -o $(OUTPUT)/$(@F) $<
	@$(CPP) -MM $(PATH_INC) $< >$(OUTPUT)/$(@F:.o=.d)

$(PATH_SW)/6502/sidcrt/target/sidcrt.bin::
	make -C $(PATH_SW)/6502/sidcrt

$(PATH_SW)/6502/sidcrt/target/muscrt.bin::
	make -C $(PATH_SW)/6502/sidcrt

$(PATH_SW)/6502/sidcrt/target/musplayer.bin::
	make -C $(PATH_SW)/6502/sidcrt
