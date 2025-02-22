
CROSS	     = nios2-elf-
ELFTYPE      = elf32-littlenios2
ARCHITECTURE = nios2

include ../../../common/environment.mk

PRJ      =  u64_loader
DEST     =  ../../../../ult64/target/u64_loader/onchip_mem.hex
BSP 	 =  ../../../../ult64/proglogic/nios_small/loader_bsp
FINAL    =  $(RESULT)/$(PRJ).hex $(RESULT)/$(PRJ).elf $(RESULT)/$(PRJ).app $(DEST)
HEXLAST  =  0x1FFFF
HEXBASE  =  0x30000000
HEXEND   =  0x3002FFFF

#VPATH	 :=  $(PATH_SW)/chan_fat/tiny_ro + $(VPATH)
VPATH    += $(PATH_SW)/application/tester

SRCS_C   =	start_rtos.c \
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
            alt_do_ctors_with_calibration.c \
            ddr2_calibrator_u64.c \
            ff.c \
            ffsystem.c \
            ffunicode.c \
            u64_tester.c \
                        
SRCS_CC	 =  small_printf.cc \
            codec.cc \
			i2c_drv.cc \
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
			fpll.cc \
			memory.cc \
			screen.cc \
			rtc_i2c.cc \
			product.cc \
			config.cc \
			prog_flash.cc \
			flash.cc \
			w25q_flash.cc \
            s25fl_flash.cc \
            s25fl_l_flash.cc \
            flash_disk_prep.cc \
			u64_programmer.cc


VPATH +=	$(PATH_SW)/FreeRTOS/Source/portable/nios
VPATH +=	$(PATH_SW)/application/programmer
VPATH +=	$(PATH_SW)/portable/nios
VPATH +=	$(BSP)
VPATH +=	$(BSP)/HAL/inc
VPATH +=	$(BSP)/drivers/inc
VPATH +=	$(PATH_SW)/u64

SRCS_ASM = 

SRCS_ASMS = port_asm.S

SRCS_6502 = 

SRCS_BIN =  

SRCS_IEC = 

SRCS_NANO = nano_minimal.nan

PATH_INC =  $(addprefix -I, $(VPATH))
OPTIONS  = -g -ffunction-sections -Os -Wno-write-strings -mno-hw-div -mno-hw-mul -mno-hw-mulx -DOS=1 -DNIOS=1
COPTIONS = $(OPTIONS) -std=gnu99
CPPOPT   = $(OPTIONS) -fno-exceptions -fno-rtti -fno-threadsafe-statics -fpermissive
LINK 	 = ./linker.x
LIBS3    = $(BSP)/libhal_bsp.a
LFLAGS   = --gc-sections -msys-crt0='$(BSP)/obj/HAL/src/crt0.o'  -msys-lib=hal_bsp -L $(BSP)

include ../../../common/rules.mk

ddr2_calibrator_u64.o: ddr2_calibrator_u64.c
	@echo Compiling $(<F) without optimization
	@$(CC) $(COPTIONS) -O0 $(PATH_INC) -B. -c -o $(OUTPUT)/$(@F) $<
	@$(CC) -MM $(PATH_INC) $< >$(OUTPUT)/$(@F:.o=.d)

$(DEST): $(RESULT)/$(PRJ).hex
	cp $(RESULT)/$(PRJ).hex $(DEST)
