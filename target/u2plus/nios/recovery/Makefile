
CROSS	     = nios2-elf-
ELFTYPE      = elf32-littlenios2
ARCHITECTURE = nios2

include ../../../common/environment.mk

PRJ      =  recovery
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
            ffunicode.c \
            ffsystem.c \
            time.c \
            mdio.c \
            nios_main.c \
            alt_malloc_lock.c \
            alt_do_ctors.c
                        
SRCS_CC	 =  u2p_init.cc \
			usb_hwinit.cc \
            i2c_drv.cc \
            codec.cc \
			small_printf.cc \
            stream.cc \
			memory.cc \
			init_function.cc \
			flash.cc \
			w25q_flash.cc \
			product.cc \
			config.cc \
			subsys.cc \
			filemanager.cc \
			file_device.cc \
			file_partition.cc \
			rtc_i2c.cc \
			c64.cc \
			c64_crt.cc \
			c64_subsys.cc \
			screen.cc \
			keyboard_c64.cc \
			editor.cc \
			mystring.cc \
			path.cc \
			pattern.cc \
			blockdev.cc \
			disk.cc \
			partition.cc \
			file_system.cc \
			diskio.cc \
			directory.cc \
			file.cc \
            filesystem_root.cc \
			filesystem_fat.cc \
			size_str.cc \
			userinterface.cc \
			ui_elements.cc \
            user_file_interaction.cc \
			tree_browser.cc \
			tree_browser_state.cc \
			config_menu.cc \
			context_menu.cc \
			task_menu.cc \
			filetype_u2p.cc \
			usb_base.cc \
			usb_device.cc \
			usb_scsi.cc \
			usb_hub.cc \
			usb_hid.cc \
			keyboard_usb.cc \
			network_config.cc \
			network_interface.cc \
			rmii_interface.cc \
			$(PRJ).cc

VPATH +=	$(PATH_SW)/FreeRTOS/Source/portable/nios
VPATH +=	$(PATH_SW)/portable/nios
VPATH +=	$(PATH_SW)/nios_appl_bsp
VPATH +=	$(PATH_SW)/nios_appl_bsp/HAL/inc
VPATH +=	$(PATH_SW)/nios_appl_bsp/drivers/inc
VPATH +=    $(PATH_SW)/application/recovery

SRCS_ASM =

SRCS_ASMS = port_asm.S

SRCS_6502 = bootcrt.tas eapi.tas

SRCS_BIN =  chars.bin

SRCS_IEC = 

SRCS_NANO = nano_minimal.nan

PATH_INC =  $(addprefix -I, $(VPATH))
OPTIONS  = -g -ffunction-sections -O0 -DOS -DNIOS=1 -Wno-write-strings -mno-hw-div -mno-hw-mul -mno-hw-mulx -DRECOVERYAPP=1
COPTIONS = $(OPTIONS) -std=gnu99
CPPOPT   = $(OPTIONS) -std=gnu++11 -fno-exceptions -fno-rtti -fno-threadsafe-statics -fpermissive
BSP      = $(PATH_SW)/nios_appl_bsp
LINK 	 = $(BSP)/linker.x
LIBS2    = $(BSP)/libhal_bsp.a ../../../libs/nios2/lwip/result/liblwip.a
LFLAGS   = --gc-sections -msys-crt0=$(BSP)/obj/HAL/src/crt0.o

include ../../../common/rules.mk
