
include ../../../common/environment.mk

PRJ        =  update
FINAL      =  $(RESULT)/$(PRJ).u2u
LINKMETHOD = ld

SRCS_C   =	mbstart_rtos.c \
			malloc_lock.c \
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
			assert.c \
            heap_3.c \
            ff.c \
            ffsystem.c \
            ffunicode.c \
            time.c

SRCS_CC	 =  memory.cc \
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
			rtc.cc \
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
			$(PRJ).cc

SRCS_ASM =  mb_crt0.s \
            portasm.s

SRCS_6502 = 

SRCS_BIN =  ultimate.bin \
			boot2.bin \
			chars.bin \
			1541.bin \
			snds1541.bin

SRCS_HTML = index.html

SRCS_IEC = 

SRCS_NANO = 

VPATH   += $(PATH_SW)/FreeRTOS/Source/portable/microblaze
VPATH   += $(PATH_SW)/portable/microblaze
VPATH   += $(PATH_SW)/application/rv_update_u2
VPATH   += $(PATH_SW)/../html
VPATH   += ../../riscv/ultimate/result
VPATH   += ../../riscv/boot2/result
VPATH   += ../../../fpga/rv700dd/rv700dd
VPATH   += ../../../fpga/rv700au/work

PATH_INC =  $(addprefix -I, $(VPATH) $(OUTPUT))
APP_OPTS = -DFIRST_WRITABLE_PAGE=0 -DOS=1 -DSAFEMODE=1 -DUPDATER -DRECOVERYAPP=1
COPTIONS = -g -ffunction-sections -Os -mcpu=v6.00.a -std=c99 $(APP_OPTS) -DRECOVERYAPP=1
CPPOPT   = -g -ffunction-sections -Os -mcpu=v6.00.a -fno-exceptions -fno-rtti -fno-threadsafe-statics $(APP_OPTS)
#OPTIONS  = -nostartfiles
LINK 	 = ./mb_update.lds
LFLAGS   = --gc-sections 

include ../../../common/rules.mk

# The FPGA updater payloads are linked from .b images so the generated symbols
# match update.cc (_rv700dd_b_* / _rv700au_b_*). Append these after rules.mk,
# which initializes OBJS_BIN/CHK_BIN from SRCS_BIN.
OBJS_BIN += rv700dd.o rv700au.o
CHK_BIN += rv700dd.chk rv700au.chk

../../riscv/ultimate/result/ultimate.bin::
	@$(MAKE) -C ../../riscv/ultimate

../../riscv/boot2/result/boot2.bin::
	@$(MAKE) -C ../../riscv/boot2

ultimate.chk: ../../riscv/ultimate/result/ultimate.bin
	@echo Calculating checksum of $(<F) binary to $(@F)..
	@$(CHECKSUM) $< $(OUTPUT)/$(@F)

ultimate.o: ../../riscv/ultimate/result/ultimate.bin
	@echo Converting $(<F) binary to $(@F)..
	@$(eval was := _binary_$(subst .,_,$(subst /,_,$(subst -,_,$<))))
	@$(eval becomes := _ultimate_bin)
	@$(OBJCOPY) -I binary -O $(ELFTYPE) --binary-architecture $(ARCHITECTURE) $< $(OUTPUT)/$@ \
	--rename-section .data=.rodata,alloc,load,readonly,data,contents \
	--redefine-sym $(was)_start=$(becomes)_start \
	--redefine-sym $(was)_size=$(becomes)_size \
	--redefine-sym $(was)_end=$(becomes)_end

boot2.chk: ../../riscv/boot2/result/boot2.bin
	@echo Calculating checksum of $(<F) binary to $(@F)..
	@$(CHECKSUM) $< $(OUTPUT)/$(@F)

boot2.o: ../../riscv/boot2/result/boot2.bin
	@echo Converting $(<F) binary to $(@F)..
	@$(eval was := _binary_$(subst .,_,$(subst /,_,$(subst -,_,$<))))
	@$(eval becomes := _boot2_bin)
	@$(OBJCOPY) -I binary -O $(ELFTYPE) --binary-architecture $(ARCHITECTURE) $< $(OUTPUT)/$@ \
	--rename-section .data=.rodata,alloc,load,readonly,data,contents \
	--redefine-sym $(was)_start=$(becomes)_start \
	--redefine-sym $(was)_size=$(becomes)_size \
	--redefine-sym $(was)_end=$(becomes)_end

update.o: update.cc $(INCLUDES) $(CHK_BIN)
	@echo "Compiling $(notdir $<)"
	@cat $(addprefix $(OUTPUT)/,$(CHK_BIN)) >$(OUTPUT)/checksums.h
	@$(CPP) $(CPPOPT) $(PATH_INC) -B. -c -Wa,-ahlms=$(OUTPUT)/$(@:.o=.lst) -o $(OUTPUT)/$(@F) $<

