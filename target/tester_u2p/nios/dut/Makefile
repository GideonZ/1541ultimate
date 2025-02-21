
CROSS	     = nios2-elf-
ELFTYPE      = elf32-littlenios2
ARCHITECTURE = nios2

include ../../../common/environment.mk
PATH_DUTFPGA = ../../fpga/testdut

PRJ      =  dut
FINAL    =  $(RESULT)/$(PRJ).app $(RESULT)/$(PRJ).elf

SRCS_C   =	start_rtos.c \
			mdio.c \
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
            audio_test.c \
            fix_fft.c
                        
SRCS_CC	 =  dut_main.cc \
			usb_hwinit.cc \
            i2c_drv.cc \
            codec.cc \
			small_printf.cc \
			flash.cc \
			w25q_flash.cc \
			flash_switch.cc \
			product.cc \
			network_config.cc \
			network_interface.cc \
			network_test.cc \
			rmii_interface.cc \
			ethernet_test.cc \
			usb_base.cc \
			usb_device.cc \
			usb_hub.cc \
			usb_test.cc \
			flash_programmer.cc \
			rtc_only_i2c.cc

VPATH +=	$(PATH_SW)/FreeRTOS/Source/portable/nios
VPATH +=	$(PATH_SW)/application/tester
VPATH +=	$(PATH_SW)/portable/nios
VPATH +=	$(PATH_SW)/nios_dut_bsp
VPATH +=	$(PATH_SW)/nios_dut_bsp/HAL/inc
VPATH +=	$(PATH_SW)/nios_dut_bsp/drivers/inc
#VPATH +=	$(PATH_DUTFPGA)/output_files

SRCS_ASM = dut_binaries.s

SRCS_ASMS = port_asm.S

SRCS_6502 = 

SRCS_BIN = 

SRCS_IEC = 

SRCS_NANO = nano_phytest.nan nano_minimal.nan

PATH_INC =  $(addprefix -I, $(VPATH))
OPTIONS  = -g -ffunction-sections -O0 -Wno-write-strings -mno-hw-div -mno-hw-mul -mno-hw-mulx -DRECOVERY=0 -DNIOS=1 -DDISABLE_USB_DEBUG=1
COPTIONS = $(OPTIONS) -std=gnu99
CPPOPT   = $(OPTIONS) -fno-exceptions -fno-rtti -fno-threadsafe-statics -fpermissive
BSP      = $(PATH_SW)/nios_dut_bsp
LINK 	 = $(BSP)/linker.x
LIBS3    = $(BSP)/libhal_bsp.a
LFLAGS   = --gc-sections -msys-crt0='$(BSP)/obj/HAL/src/crt0.o'  -msys-lib=hal_bsp -L $(BSP)

include ../../../common/rules.mk

dut_binaries.o:   $(PATH_SW)/application/tester/wave_10.bin   $(PATH_SW)/application/tester/waves_11_7.bin
