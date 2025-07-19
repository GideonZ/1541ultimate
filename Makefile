
all: esp32 u2_rv u2plus u2pl u64 u64ii

esp32: esp32_raw_u64 esp32_raw_c3 esp32_u64ctrl

esp32_clean: esp32_raw_u64_clean esp32_raw_c3_clean esp32_u64ctrl_clean
esp32_raw_u64:
	@cd software/wifi/raw_u64 && idf.py build

esp32_raw_u64_clean:
	@cd software/wifi/raw_u64 && idf.py clean

esp32_raw_c3:
	@cd software/wifi/raw_c3 && idf.py build

esp32_raw_c3_clean:
	@cd software/wifi/raw_c3 && idf.py clean

esp32_u64ctrl:
	@cd software/u64ctrl && idf.py build

esp32_u64ctrl_clean:
	@cd software/u64ctrl && idf.py clean


u2:
	@$(MAKE) -C tools
	@$(MAKE) -C target/fpga/mb700
	@$(MAKE) -C target/fpga/mb700dd
	@$(MAKE) -C target/fpga/mb700gm
	@$(MAKE) -C target/u2/microblaze/mb_lwip
	@$(MAKE) -C target/u2/microblaze/mb_boot
	@$(MAKE) -C target/u2/microblaze/mb_boot_dd
	@$(MAKE) -C target/u2/microblaze/mb_boot_gm
	@$(MAKE) -C target/u2/microblaze/mb_boot2
	@$(MAKE) -C target/u2/microblaze/mb_ultimate
	@$(MAKE) -C target/u2/microblaze/mb_update
	@$(MAKE) -C target/u2/microblaze/mb_update_dd
	@$(MAKE) -C target/u2/microblaze/mb_update_gm
	@cp target/u2/microblaze/mb_update/result/update.u2u ./update_audio.u2u
	@cp target/u2/microblaze/mb_update_dd/result/update.u2u ./update_dual_drive_acia.u2u
	@cp target/u2/microblaze/mb_update_gm/result/update.u2u ./update_dual_drive_gmod2.u2u

u2_rv_loader:
	@$(MAKE) -C tools
	@$(MAKE) -C target/u2/riscv/loader
	@$(MAKE) -C target/fpga/rv700_loader

fpga_depends::
	@touch software/nios_solo_bsp/Makefile
	@touch software/nios_solo_bsp/public.mk
	@touch software/nios_appl_bsp/Makefile
	@touch software/nios_appl_bsp/public.mk
	@$(MAKE) -C tools
	@$(MAKE) -C target/u2/riscv/boot1
	@$(MAKE) -C target/u2/riscv/boot2
	@$(MAKE) -C software/nios_solo_bsp
	@$(MAKE) -C software/nios_appl_bsp
	@$(MAKE) -C target/u2plus/nios/boot_recovery
	@$(MAKE) -C target/u2plus/nios/boot_run
	@$(MAKE) -C target/u2plus_L/rvlite/bootloader
	@cd target/fpga/depends && ./make_depends.sh

esp_depends::
	@cd software && python3 esp_depends.py >esp_depends.txt

u2_rv:
	@$(MAKE) -C tools
	@$(MAKE) -C target/u2/riscv/boot1
	@$(MAKE) -C target/u2/riscv/boot2
	@$(MAKE) -C target/libs/riscv/lwip
	@$(MAKE) -C target/u2/riscv/ultimate
	@$(MAKE) -C target/fpga/rv700dd
	@$(MAKE) -C target/fpga/rv700au
	@$(MAKE) -C target/u2/riscv/updater
	@cp target/u2/riscv/updater/result/update.u2r ./update.u2r

u2_rv_swonly:
	@$(MAKE) -C tools
	@$(MAKE) -C target/libs/riscv/lwip
	@$(MAKE) -C target/u2/riscv/ultimate
	@$(MAKE) -C target/u2/riscv/updater
	@cp target/u2/riscv/updater/result/update.u2r ./update.u2r

u2_mb_to_rv:
	@$(MAKE) u2_rv
	@$(MAKE) -C target/u2/microblaze/mb_lwip
	@$(MAKE) -C target/u2/microblaze/mb_update_to_rv
	@cp target/u2/microblaze/mb_update_to_rv/result/update.u2u ./update_to_rv.u2u

u2_rv_revert:
	@$(MAKE) u2
	@$(MAKE) u2_rv
	@$(MAKE) -C target/u2/riscv/reverter
	@cp target/u2/riscv/reverter/result/revert.u2r ./revert_to_mb.u2r

mb:
	@$(MAKE) -C tools
	@$(MAKE) -C target/u2/microblaze/mb_lwip
	@$(MAKE) -C target/u2/microblaze/mb_boot
	@$(MAKE) -C target/u2/microblaze/mb_boot_dd
	@$(MAKE) -C target/u2/microblaze/mb_boot_gm
	@$(MAKE) -C target/u2/microblaze/mb_boot2
	@$(MAKE) -C target/u2/microblaze/mb_ultimate
	@$(MAKE) -C target/u2/microblaze/mb_update
	@$(MAKE) -C target/u2/microblaze/mb_update_dd
	@$(MAKE) -C target/u2/microblaze/mb_update_gm
	@cp target/u2/microblaze/mb_update/result/update.u2u ./update_audio.u2u
	@cp target/u2/microblaze/mb_update_dd/result/update.u2u ./update_dual_drive_acia.u2u
	@cp target/u2/microblaze/mb_update_gm/result/update.u2u ./update_dual_drive_gmod2.u2u

niosclean:
	@$(MAKE) -C target/libs/nios2/lwip clean
	@$(MAKE) -C target/u2plus/nios/boot_recovery clean
	@$(MAKE) -C target/u2plus/nios/boot_run clean
	@$(MAKE) -C target/u2plus/nios/ultimate clean
	@$(MAKE) -C target/u2plus/nios/recovery clean
	@$(MAKE) -C target/u2plus/nios/flash clean
	@$(MAKE) -C target/u2plus/nios/updater clean

niosboot:
	@$(MAKE) -C target/libs/nios2/lwip
	@$(MAKE) -C target/u2plus/nios/boot_recovery
	@$(MAKE) -C target/u2plus/nios/boot_run
	@$(MAKE) -C software/nios_solo_bsp
	@$(MAKE) -C software/nios_appl_bsp

u2plus:
	@touch software/nios_solo_bsp/Makefile
	@touch software/nios_solo_bsp/public.mk
	@touch software/nios_appl_bsp/Makefile
	@touch software/nios_appl_bsp/public.mk
	@$(MAKE) -C tools
	@$(MAKE) -C software/nios_solo_bsp
	@$(MAKE) -C software/nios_appl_bsp
	@$(MAKE) -C target/libs/nios2/lwip
	@$(MAKE) -C target/u2plus/nios/boot_recovery
	@$(MAKE) -C target/u2plus/nios/boot_run
	@$(MAKE) -C target/fpga/u2plus_recovery
	@$(MAKE) -C target/fpga/u2plus_run
	@$(MAKE) -C target/u2plus/nios/ultimate
	@$(MAKE) -C target/u2plus/nios/recovery
	@$(MAKE) -C target/u2plus/nios/updater
	@cp target/u2plus/nios/updater/result/update.app ./update.u2p

u2p_tester:
	@$(MAKE) -C target/libs/nios2/lwip
	@$(MAKE) -C target/tester_u2p/nios/testboot
	@$(MAKE) -C target/tester_u2p/nios/dutboot
	@$(MAKE) -C target/fpga/testdut
	@$(MAKE) -C target/fpga/testexec
	@touch software/nios_dut_bsp/Makefile
	@touch software/nios_dut_bsp/public.mk
	@touch software/nios_tester_bsp/Makefile
	@touch software/nios_tester_bsp/public.mk
	@$(MAKE) -C software/nios_dut_bsp
	@$(MAKE) -C software/nios_tester_bsp
	@$(MAKE) -C target/tester_u2p/nios/dut clean
	@$(MAKE) -C target/tester_u2p/nios/tester clean
	@$(MAKE) -C target/tester_u2p/nios/testloader clean
	@$(MAKE) -C target/tester_u2p/nios/testflasher clean
	@$(MAKE) -C target/tester_u2p/nios/dut
	@$(MAKE) -C target/tester_u2p/nios/testloader
	@$(MAKE) -C target/tester_u2p/nios/tester
	@$(MAKE) -C target/tester_u2p/nios/testflasher
	@$(MAKE) -C target/tester_package force
	@$(MAKE) -C target/tester_package force

u2p_tester_sw:
	@$(MAKE) -C target/libs/nios2/lwip
	@touch software/nios_dut_bsp/Makefile
	@touch software/nios_dut_bsp/public.mk
	@touch software/nios_tester_bsp/Makefile
	@touch software/nios_tester_bsp/public.mk
	@$(MAKE) -C software/nios_dut_bsp
	@$(MAKE) -C software/nios_tester_bsp
	@$(MAKE) -C target/tester_u2p/nios/dut clean
	@$(MAKE) -C target/tester_u2p/nios/tester clean
	@$(MAKE) -C target/tester_u2p/nios/testloader clean
	@$(MAKE) -C target/tester_u2p/nios/testflasher clean
	@$(MAKE) -C target/tester_u2p/nios/dut
	@$(MAKE) -C target/tester_u2p/nios/testloader
	@$(MAKE) -C target/tester_u2p/nios/tester
	@$(MAKE) -C target/tester_u2p/nios/testflasher
	@$(MAKE) -C target/tester_package force
	@$(MAKE) -C target/tester_package force

clean: esp32_clean
	@$(MAKE) -C tools clean
	@rm -f ./update*.u2*
	@rm -f ./update.u64
	@rm -rf target/fpga/mb700/work
	@rm -rf target/fpga/mb700dd/work
	@rm -rf target/fpga/mb700gm/work
	@rm -rf target/fpga/rv700dd/rv700dd
	@rm -rf target/fpga/rv700au/work
	@rm -rf target/fpga/rv700_loader/work
	@rm -rf target/fpga/_xm*
	@rm -rf target/fpga/x*
	@rm -rf target/fpga/*.x*
	@rm -rf `find target -name result`
	@rm -rf `find target -name output`

sw_clean:
	@rm -f ./update*.u2*
	@rm -f ./update.u64
	@rm -rf `find target -name result`
	@rm -rf `find target -name output`

mb_clean:
	@rm -f ./update*.u2u
	@rm -rf `find target/u2/microblaze/mb* -name result`
	@rm -rf `find target/u2/microblaze/mb* -name output`

u2plus_swonly:
	@touch software/nios_solo_bsp/Makefile
	@touch software/nios_solo_bsp/public.mk
	@touch software/nios_appl_bsp/Makefile
	@touch software/nios_appl_bsp/public.mk
	@$(MAKE) -C tools
	@$(MAKE) -C software/nios_solo_bsp
	@$(MAKE) -C software/nios_appl_bsp
	@$(MAKE) -C target/libs/nios2/lwip
	@$(MAKE) -C target/u2plus/nios/ultimate
	@$(MAKE) -C target/u2plus/nios/recovery
	@$(MAKE) -C target/u2plus/nios/updater
	@cp target/u2plus/nios/updater/result/update.app ./update.u2p

u2plus_swapply:
	@$(MAKE) -C tools
	@$(MAKE) -C software/nios_solo_bsp
	@$(MAKE) -C software/nios_appl_bsp
	@$(MAKE) -C target/libs/nios2/lwip
	@$(MAKE) -C target/u2plus/nios/ultimate
	nios2-download -g target/u2plus/nios/ultimate/result/ultimate.elf

u2_swonly:
	@$(MAKE) -C tools
	@$(MAKE) -C target/u2/microblaze/mb_lwip
	@$(MAKE) -C target/u2/microblaze/mb_boot
	@$(MAKE) -C target/u2/microblaze/mb_boot_dd
	@$(MAKE) -C target/u2/microblaze/mb_boot_gm
	@$(MAKE) -C target/u2/microblaze/mb_boot2
	@$(MAKE) -C target/u2/microblaze/mb_ultimate
	
nios_bsps:
	@touch software/nios_solo_bsp/Makefile
	@touch software/nios_solo_bsp/public.mk
	@touch software/nios_appl_bsp/Makefile
	@touch software/nios_appl_bsp/public.mk
	@$(MAKE) -C software/nios_solo_bsp
	@$(MAKE) -C software/nios_appl_bsp

u64: esp32_raw_u64
	@touch software/nios_solo_bsp/Makefile
	@touch software/nios_solo_bsp/public.mk
	@touch software/nios_appl_bsp/Makefile
	@touch software/nios_appl_bsp/public.mk
	@$(MAKE) -C tools
	@$(MAKE) -C software/nios_solo_bsp
	@$(MAKE) -C software/nios_appl_bsp
	@$(MAKE) -C target/libs/nios2/lwip
	@$(MAKE) -C target/u64/nios2/ultimate
	@$(MAKE) -C target/u64/nios2/updater
	@cp target/u64/nios2/updater/result/update.app ./update.u64

u64_clean:
	@$(MAKE) -C target/u64/nios2/ultimate clean
	@$(MAKE) -C target/u64/nios2/updater clean

u64ii: esp32_u64ctrl
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
	@cp target/u64ii/riscv/update/result/update.app ./update.ue2

u2pl: esp32_raw_c3
	@$(MAKE) -C tools
	@$(MAKE) -C target/libs/riscv/lwip
	@$(MAKE) -C target/u2plus_L/rvlite/bootloader
	@$(MAKE) -C target/fpga/u2plus_ecp5
	@$(MAKE) -C target/u2plus_L/riscv/ultimate
	@$(MAKE) -C target/u2plus_L/riscv/updater
	@cp target/u2plus_L/riscv/updater/result/update.app ./update.u2l

u2pl_swonly:
	@$(MAKE) -C tools
	@$(MAKE) -C target/libs/riscv/lwip
	@$(MAKE) -C target/u2plus_L/riscv/ultimate
	@$(MAKE) -C target/u2plus_L/riscv/updater
	@cp target/u2plus_L/riscv/updater/result/update.app ./update.u2l
