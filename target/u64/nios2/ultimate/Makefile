
CROSS	     = nios2-elf-
ELFTYPE      = elf32-littlenios2
ARCHITECTURE = nios2

include ../../../common/environment.mk

PRJ      =  ultimate
FINAL    =  $(RESULT)/$(PRJ).app $(RESULT)/$(PRJ).elf $(OUTPUT)/$(PRJ).sim

BSP      = $(PATH_SW)/nios_appl_bsp

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
            sid_coeff.c \
            nios_main.c \
            alt_malloc_lock.c \
            alt_do_ctors.c \
            cmd_buffer.c \
			server.c \
			http_protocol.c \
			middleware.c \
			multipart.c \
			url.c

SRCS_CC	 =  u2p_init.cc \
			usb_hwinit.cc \
			i2c_drv.cc \
			codec.cc \
			small_printf.cc \
			stream.cc \
			stream_uart.cc \
			memory.cc \
			init_function.cc \
			flash.cc \
			at45_flash.cc \
			w25q_flash.cc \
			s25fl_flash.cc \
			s25fl_l_flash.cc \
			prog_flash.cc \
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
			u64_config.cc \
			rtc_i2c.cc \
			c64.cc \
			c64_crt.cc \
			u64_machine.cc \
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
			filetype_d64.cc \
			filetype_g64.cc \
			filetype_t64.cc \
			filetype_prg.cc \
			filetype_u2p.cc \
			filetype_reu.cc \
			filetype_crt.cc \
			filetype_sid.cc \
			filetype_bin.cc \
			filetype_cfg.cc \
			filetype_vpl.cc \
			filetype_esp.cc \
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
			rmii_interface.cc \
			network_esp32.cc \
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
			color_timings.cc \
			hdmi_scan.cc \
			fpll.cc \
			dma_uart.cc \
			esp32.cc \
			wifi.cc \
			wifi_cmd.cc \
			acia.cc \
			modem.cc \
			listener_socket.cc \
			led_strip.cc \
			data_streamer.cc \
			configio.cc \
			sid_editor.cc \
			sid_device.cc \
			sid_device_fpgasid.cc \
			sid_device_swinsid.cc \
			sid_device_armsid.cc \
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
			route_streams.cc \
			assembly_search.cc \
			assembly.cc \
			filesystem_a64.cc \
			sntp_time.cc \
			$(PRJ).cc

SRCS_CC +=  iec_interface.cc 

SRCS_CC +=  iec_drive.cc \
			iec_channel.cc \
			iec_printer.cc \
			mps_printer.cc \
			mps_printer_cbm.cc \
			mps_printer_epson.cc \
			mps_printer_ibmgp.cc \
			mps_printer_ibmpp.cc \
			mps_chargen.cc \
			mps_charset.cc \
			mps_png.cc \
			iec_ulticopy.cc \
			softiec_target.cc

#			usb_em1010.cc \

VPATH +=	$(PATH_SW)/FreeRTOS/Source/portable/nios
VPATH +=	$(PATH_SW)/portable/nios
VPATH +=	$(PATH_SW)/u64
VPATH +=	$(PATH_SW)/6502/sidcrt/target
VPATH +=	$(BSP)
VPATH +=	$(BSP)/HAL/inc
VPATH +=	$(BSP)/drivers/inc
VPATH +=	$(PATH_SW)/6502/sidcrt/target
VPATH +=	$(PATH_SW)/wifi/raw_u64/main

#            sock_echo.c \
#			sys_calls.cc \
####			sampler.cc \
####			copper.cc \


SRCS_ASM =

SRCS_ASMS = port_asm.S

SRCS_6502 = bootcrt.tas cmd_test_rom.tas ulticopy.tas eapi.tas default_kernal.tas

SRCS_BIN =  basic.bin sidcrt.bin muscrt.bin musplayer.bin chars.bin module.bin default_chars.bin raminit.bin

SRCS_IEC = iec_code.iec

SRCS_NANO = nano_minimal.nan

PATH_INC =  $(addprefix -I, $(VPATH))
OPTIONS  = -g -ffunction-sections -Os -DOS -DDEVELOPER=0 -DNIOS=1 -DCLOCK_FREQ=66666667 -DU64=1 -Wno-write-strings -mno-hw-div -mno-hw-mul -mno-hw-mulx
COPTIONS = $(OPTIONS) -std=gnu99
CPPOPT   = $(OPTIONS) -std=gnu++11 -fno-exceptions -fno-rtti -fno-threadsafe-statics -fpermissive
LINK 	 = $(BSP)/linker.x
LIBS2    = $(BSP)/libhal_bsp.a ../../../libs/nios2/lwip/result/liblwip.a
LFLAGS   = --gc-sections -msys-crt0=$(BSP)/obj/HAL/src/crt0.o

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
