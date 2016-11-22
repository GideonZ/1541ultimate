
CROSS	     = nios2-elf-
ELFTYPE      = elf32-littlenios2
ARCHITECTURE = nios2

OUTPUTDIRS =  flash
FLASH      =  flash
TESTER     =  tester

include ../common/environment.mk
PATH_DUTFPGA = ../../fpga/testdut

PRJ      =  tester_main
FINAL    =  $(FLASH) $(TESTER) $(RESULT)/$(PRJ).elf $(TESTER)/tester.app $(FLASH)/rompack.bin $(TESTER)/dut.fpga $(TESTER)/dut.app \
			$(FLASH)/ultimate_run.swp $(FLASH)/ultimate_recovery.swp $(FLASH)/ultimate.app $(FLASH)/recovery.app

SRCS_C   =	start_rtos.c \
			i2c.c \
			mdio.c \
			codec.c \
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
            ff2.c \
            ffsyscall.c \
            ccsbcs.c
                        
SRCS_CC	 =  small_printf.cc \
			flash_switch.cc \
			flash.cc \
			w25q_flash.cc \
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
			tester_main.cc
			
			

VPATH +=	$(PATH_SW)/FreeRTOS/Source/portable/nios
VPATH +=	$(PATH_SW)/application/tester
VPATH +=	$(PATH_SW)/portable/nios
VPATH +=	$(PATH_SW)/nios_tester_bsp
VPATH +=	$(PATH_SW)/nios_tester_bsp/HAL/inc
VPATH +=	$(PATH_SW)/nios_tester_bsp/drivers/inc
VPATH +=    ../../fpga/testdut/output_files

SRCS_ASM = tester_binaries.s

SRCS_ASMS = port_asm.S

SRCS_6502 = 

SRCS_BIN = 

SRCS_IEC = 

SRCS_NANO = nano_minimal.nan

PATH_INC =  $(addprefix -I, $(VPATH))
OPTIONS  = -g -ffunction-sections -O0 -Wno-write-strings -mno-hw-div -mno-hw-mul -mno-hw-mulx -DOS=1 -DNIOS=1
COPTIONS = $(OPTIONS) -std=gnu99
CPPOPT   = $(OPTIONS) -fno-exceptions -fno-rtti -fno-threadsafe-statics -fpermissive
BSP      = $(PATH_SW)/nios_tester_bsp
LINK 	 = $(BSP)/linker.x
LIBS3    = $(BSP)/libhal_bsp.a
LFLAGS   = --gc-sections -msys-crt0='$(BSP)/obj/HAL/src/crt0.o'  -msys-lib=hal_bsp -L $(BSP)

include ../common/rules.mk

tester_binaries.o: $(PATH_SW)/application/tester/waves_8_12.bin 

$(FLASH)/rompack.bin: $(ROMS)/ar5pal.bin $(ROMS)/ar6pal.bin $(ROMS)/final3.bin $(ROMS)/rr38pal.bin $(ROMS)/rr38ntsc.bin $(ROMS)/tar_pal.bin \
	$(ROMS)/tar_ntsc.bin $(ROMS)/ss5pal.bin $(ROMS)/ss5ntsc.bin $(ROMS)/ar5ntsc.bin $(ROMS)/kcs.bin $(ROMS)/epyx.bin
	@echo "Creating ROM Pack."
	@cat $(ROMS)/ar5pal.bin $(ROMS)/ar6pal.bin $(ROMS)/final3.bin $(ROMS)/rr38pal.bin $(ROMS)/rr38ntsc.bin $(ROMS)/tar_pal.bin \
	$(ROMS)/tar_ntsc.bin $(ROMS)/ss5pal.bin $(ROMS)/ss5ntsc.bin $(ROMS)/ar5ntsc.bin $(ROMS)/kcs.bin $(ROMS)/epyx.bin >$@

$(FLASH)/ultimate_run.swp : #../nios2_flash/output/ultimate_run.swp
	@make -C ../nios2_flash
	@cp $< $@

$(FLASH)/ultimate_recovery.swp : #../nios2_flash/output/ultimate_recovery.swp
	@make -C ../nios2_flash
	@cp $< $@

$(FLASH)/ultimate.app : #../nios2_ultimate/result/ultimate.app
	@make -C ../nios2_ultimate
	@cp $< $@

$(FLASH)/recovery.app : #../nios2_recovery/result/recovery.app
	@make -C ../nios2_recovery
	@cp $< $@

$(TESTER)/dut.fpga: testdut.b
	@cp $(OUTPUT)/$(<F) $@

$(TESTER)/dut.app::
	@make -C ../nios2_dut
	@cp ../nios2_dut/result/dut.app $@

$(TESTER)/dut.app::
	@make -C ../nios2_dut
	@cp ../nios2_dut/result/dut.app $@

$(TESTER)/tester.app: $(RESULT)/$(PRJ).app
	@cp $< $@

$(FLASH)::
	@mkdir -p $(FLASH)

$(TESTER)::
	@mkdir -p $(TESTER)
