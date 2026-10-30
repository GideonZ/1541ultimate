CROSS ?= riscv-none-elf-
ROOT := ../../../..
SRC := $(ROOT)/software/application/esp32_diagnostic
FLAGS := -march=rv32i_zicsr -mabi=ilp32 -Os -ffreestanding -fno-builtin -fno-exceptions -fno-rtti -fno-threadsafe-statics -fno-stack-protector -msmall-data-limit=0 -mno-relax -ffunction-sections -fdata-sections -Wall -Wextra -Werror
all: result/esp32-identity.ue2
result:
	mkdir -p result
result/diagnostic.elf: start.S linker.x $(SRC)/main.cc $(SRC)/runtime.cc $(SRC)/platform.h $(ROOT)/software/io/wifi/controller_probe.h $(ROOT)/software/io/wifi/controller_identity.h | result
	$(CROSS)g++ $(FLAGS) -std=c++11 -nostdlib -Wl,--gc-sections,-Map,result/diagnostic.map,-T,linker.x -I$(ROOT)/software/io/wifi -I$(SRC) start.S $(SRC)/main.cc $(SRC)/runtime.cc -o $@
	$(CROSS)objdump -d $@ > result/diagnostic.dis
	$(CROSS)nm -n $@ > result/diagnostic.symbols
result/diagnostic.bin: result/diagnostic.elf
	$(CROSS)objcopy -O binary $< $@
result/esp32-identity.ue2: result/diagnostic.bin
	python3 package.py $< $@
.PHONY: all
