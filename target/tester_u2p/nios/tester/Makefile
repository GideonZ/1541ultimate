
CROSS	     = nios2-elf-
ELFTYPE      = elf32-littlenios2
ARCHITECTURE = nios2

OUTPUTDIRS =  flash
FLASH      =  flash
TESTER     =  tester

include ../../../common/environment.mk
PATH_DUTFPGA = ../../../fpga/testdut
SVFDUMP = ../../../../tools/svf_dump

PRJ      =  tester_main
FINAL    =  $(RESULT)/$(PRJ).elf $(RESULT)/$(PRJ).app 

SRCS_C   =	start_rtos.c \
			mdio.c \
			fix_fft.c \
			itu.c \
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
            time.c \
            alt_malloc_lock.c \
            alt_do_ctors.c \
            analog.c \
            audio_test.c \
            digital_io.c \
            jtag.c \
            ff.c \
            ffsystem.c \
            ffunicode.c
                        
SRCS_CC	 =  small_printf.cc \
            i2c_drv.cc \
            codec.cc \
			flash_switch.cc \
			flash.cc \
			w25q_flash.cc \
			product.cc \
			network_config.cc \
			network_interface.cc \
			network_test.cc \
			rmii_interface.cc \
			ethernet_test.cc \
			usb_base.cc \
			usb_hub.cc \
			usb_device.cc \
			usb_scsi.cc \
			path.cc \
			filemanager.cc \
			mystring.cc \
			filesystem_root.cc \
			file_device.cc \
			blockdev.cc \
			blockdev_ram.cc \
			disk.cc \
			diskio.cc \
			directory.cc \
			partition.cc \
			pattern.cc \
			file_partition.cc \
			size_str.cc \
			file_system.cc \
			filesystem_fat.cc \
			file.cc \
			rtc_only_i2c.cc \
			memory.cc \
			flash_disk_prep.cc \
			tester_main.cc
			

VPATH +=	$(PATH_SW)/FreeRTOS/Source/portable/nios
VPATH +=	$(PATH_SW)/application/tester
VPATH +=	$(PATH_SW)/portable/nios
VPATH +=	$(PATH_SW)/nios_tester_bsp
VPATH +=	$(PATH_SW)/nios_tester_bsp/HAL/inc
VPATH +=	$(PATH_SW)/nios_tester_bsp/drivers/inc
VPATH +=    $(PATH_DUTFPGA)/output_files
VPATH +=    $(PATH_SW)/../target/fpga/testexec/output_files
VPATH +=    ../dut/result
VPATH +=    ../testloader/result

SRCS_ASM = tester_binaries.s flash_binaries.s

SRCS_ASMS = port_asm.S

SRCS_6502 = 

SRCS_BIN =  chars.bin 1541.bin 1571.bin 1581.bin snds1541.bin snds1571.bin snds1581.bin

SRCS_IEC = 

SRCS_NANO = nano_minimal.nan

PATH_INC =  $(addprefix -I, $(VPATH))
OPTIONS  = -g -ffunction-sections -O0 -Wno-write-strings -mno-hw-div -mno-hw-mul -mno-hw-mulx -DOS=1 -DNIOS=1 -DDISABLE_USB_DEBUG=0
COPTIONS = $(OPTIONS) -std=gnu99
CPPOPT   = $(OPTIONS) -fno-exceptions -fno-rtti -fno-threadsafe-statics -fpermissive
BSP      = $(PATH_SW)/nios_tester_bsp
LINK 	 = $(BSP)/linker.x
LIBS3    = $(BSP)/libhal_bsp.a
LFLAGS   = --gc-sections -msys-crt0='$(BSP)/obj/HAL/src/crt0.o'  -msys-lib=hal_bsp -L $(BSP)

include ../../../common/rules.mk

../dut/result/dut.app::
	make -C ../dut
	
$(PATH_DUTFPGA)/output_files/dut.fpga: $(PATH_DUTFPGA)/output_files/testdut.svf
	@$(SVFDUMP) $< >/dev/null
	@mv dump.bin $@

tester_binaries.o: $(PATH_SW)/application/tester/waves_8_12.bin ../dut/result/dut.app $(PATH_DUTFPGA)/output_files/dut.fpga

flash_binaries.o: testexec.swp ../testloader/result/test_loader.app
