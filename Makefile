
all: esp32 u64ii

esp32: esp32_u64ctrl

esp32_clean: esp32_u64ctrl_clean

esp32_u64ctrl:
	@cd software/u64ctrl && idf.py build

esp32_u64ctrl_clean:
	@cd software/u64ctrl && idf.py clean

esp_depends::
	@cd software && python3 esp_depends.py >esp_depends.txt

clean: esp32_clean
	@$(MAKE) -C tools clean
	@rm -f ./update*.u2*
	@rm -f ./update.u64
	@rm -f ./update.ue2
	@rm -rf `find target -name result`
	@rm -rf `find target -name output`

u64ii:
	@mkdir -p u64ii
	@$(MAKE) -C tools
	@$(MAKE) -C target/libs/riscv/lwip
	@$(MAKE) -C target/u64ii/riscv/ultimate
	@$(MAKE) -C target/u64ii/riscv/factorytest
	@$(MAKE) -C target/u64ii/riscv/update
	@cp target/u64ii/riscv/ultimate/result/ultimate.app u64ii
	@cp target/u64ii/riscv/factorytest/result/factorytest.bin u64ii
	@cp software/u64ctrl/build/bootloader/bootloader.bin u64ii
	@cp software/u64ctrl/build/partition_table/partition-table.bin u64ii
	@cp software/u64ctrl/build/u64ctrl.bin u64ii
	@cp external/u64_mk2*.bit u64ii
	@cp target/u64ii/riscv/update/result/update.app ./update.ue2
