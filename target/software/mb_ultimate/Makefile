
include ../common/environment.mk

PRJ        =  ultimate
FINAL      =  $(RESULT)/$(PRJ).bin
LINKMETHOD = ld

SRCS_C   =	mbstart_rtos.c \
			itu.c \
			dump_hex.c \
			profiler.c \
			croutine.c \
            event_groups.c \
            list.c \
            queue.c \
            tasks.c \
            timers.c \
            port.c \
            heap_3.c \
            ff2.c \
            ccsbcs.c \
            ffsyscall.c \
            time.c \
            sid_coeff.c

SRCS_CC	 =  small_printf.cc \
            stream.cc \
			stream_uart.cc \
			memory_rtos.cc \
			init_function.cc \
			flash.cc \
			at45_flash.cc \
			w25q_flash.cc \
			s25fl_flash.cc \
			config.cc \
			filemanager.cc \
			file_device.cc \
			file_partition.cc \
			embedded_d64.cc \
			embedded_t64.cc \
			embedded_iso.cc \
			embedded_fat.cc \
			rtc.cc \
			c64.cc \
			c64_subsys.cc \
			screen.cc \
			keyboard_c64.cc \
			disk_image.cc \
			c1541.cc \
			bam_header.cc \
			mystring.cc \
			path.cc \
			pattern.cc \
			blockdev.cc \
			blockdev_file.cc \
			blockdev_ram.cc \
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
			stream_menu.cc \
			config_menu.cc \
			context_menu.cc \
			task_menu.cc \
			audio_select.cc \
			filetype_d64.cc \
			filetype_g64.cc \
			filetype_prg.cc \
			filetype_u2u.cc \
			filetype_reu.cc \
			filetype_crt.cc \
			filetype_sid.cc \
			filetype_bin.cc \
            host_stream.cc \
            keyboard_vt100.cc \
            screen_vt100.cc \
			usb_base.cc \
			usb_device.cc \
			usb_scsi.cc \
			usb_hub.cc \
			usb_ax88772.cc \
			usb_em1010.cc \
			network_interface.cc \
			network_lwip.cc \
			vfs.cc \
			ftpd.cc \
			tape_controller.cc \
			tape_recorder.cc \
			command_intf.cc \
			dos.cc \
			network_target.cc \
			uci_commands.cc \
			iec.cc \
			filetype_tap.cc \
            socket_stream.cc \
            socket_gui.cc \
			socket_test.cc \
			$(PRJ).cc

#            sock_echo.c \
#			sys_calls.cc \
####			sampler.cc \
####			copper.cc \

VPATH += $(PATH_SW)/FreeRTOS/Source/portable/microblaze
VPATH += $(PATH_SW)/portable/microblaze


SRCS_ASM =  mb_crt0.s portasm.s

SRCS_6502 = sidcrt.tas bootcrt.tas cmd_test_rom.tas ulticopy.tas

SRCS_BIN =  chars.bin 1541.bin 1541c.bin 1541-ii.bin sounds.bin module.bin

SRCS_IEC = iec_code.iec

SRCS_NANO = nano_minimal.nan

# TOOLCHAIN ?= $(XILINX_EDK)/gnu/microblaze/lin64

PATH_INC =  $(addprefix -I, $(VPATH))
OPTIONS  = -g -ffunction-sections -nostartfiles -Os -DOS -DDEVELOPER
COPTIONS = $(OPTIONS) -std=c99
CPPOPT   = $(OPTIONS) -fno-exceptions -fno-rtti -fno-threadsafe-statics
LINK 	 = ./mb_app.lds
LIBS     = ../mb_lwip/result/liblwip.a
LFLAGS   = --gc-sections 
	
include ../common/rules.mk
