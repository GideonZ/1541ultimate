
# .PHONY: all mk1 mk2 mk3 mk3only prog special clean sw_clean loader

all: mk3 u2plus u64

mk3:
	@$(MAKE) -C tools
	@$(MAKE) -C target/fpga -f makefile_mb_700a
	@$(MAKE) -C target/fpga -f makefile_mb_700a_dd
	@$(MAKE) -C target/fpga -f makefile_mb_700a_gm
	@$(MAKE) -C target/software/mb_lwip
	@$(MAKE) -C target/software/mb_boot
	@$(MAKE) -C target/software/mb_boot_dd
	@$(MAKE) -C target/software/mb_boot_gm
	@$(MAKE) -C target/software/mb_boot2
	@$(MAKE) -C target/software/mb_ultimate
	@$(MAKE) -C target/software/mb_update
	@$(MAKE) -C target/software/mb_update_dd
	@$(MAKE) -C target/software/mb_update_gm
	@cp target/software/mb_update/result/update.u2u ./update_audio.u2u
	@cp target/software/mb_update_dd/result/update.u2u ./update_dual_drive_acia.u2u
	@cp target/software/mb_update_gm/result/update.u2u ./update_dual_drive_gmod2.u2u

mb:
	@$(MAKE) -C tools
	@$(MAKE) -C target/software/mb_lwip
	@$(MAKE) -C target/software/mb_boot
	@$(MAKE) -C target/software/mb_boot_dd
	@$(MAKE) -C target/software/mb_boot_gm
	@$(MAKE) -C target/software/mb_boot2
	@$(MAKE) -C target/software/mb_ultimate
	@$(MAKE) -C target/software/mb_update
	@$(MAKE) -C target/software/mb_update_dd
	@$(MAKE) -C target/software/mb_update_gm
	@cp target/software/mb_update/result/update.u2u ./update_audio.u2u
	@cp target/software/mb_update_dd/result/update.u2u ./update_dual_drive_acia.u2u
	@cp target/software/mb_update_gm/result/update.u2u ./update_dual_drive_gmod2.u2u

niosclean:
	@$(MAKE) -C target/software/nios2_elf_lwip clean
	@$(MAKE) -C target/software/nios2_loader1 clean
	@$(MAKE) -C target/software/nios2_loader2 clean
	@$(MAKE) -C target/software/nios2_ultimate clean
	@$(MAKE) -C target/software/nios2_recovery clean
	@$(MAKE) -C target/software/nios2_flash clean
	@$(MAKE) -C target/software/nios2_update clean

niosboot:
	@$(MAKE) -C target/software/nios2_elf_lwip
	@$(MAKE) -C target/software/nios2_loader1
	@$(MAKE) -C target/software/nios2_loader2
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
	@$(MAKE) -C target/software/nios2_elf_lwip
	@$(MAKE) -C target/software/nios2_loader1
	@$(MAKE) -C target/software/nios2_loader2
	@$(MAKE) -C target/fpga/u2plus_recovery
	@$(MAKE) -C target/fpga/u2plus_run
	@$(MAKE) -C target/software/nios2_ultimate
	@$(MAKE) -C target/software/nios2_recovery
	@$(MAKE) -C target/software/nios2_update
	@cp target/software/nios2_update/result/update.app ./update.u2p

k1:
	@svn up
	@$(MAKE) -C tools
	@$(MAKE) -C target/fpga -f makefile_250e
	@$(MAKE) -C target/software/1st_boot mk1
	@$(MAKE) -C target/software/ultimate appl
	@cp target/software/ultimate/result/appl.bin .

prog:
	@$(MAKE) mk2
	@$(MAKE) -C target/fpga -f makefile_boot_700a
	@$(MAKE) -C target/software/programmer
	@cp target/software/programmer/result/*.bit .

special:
	@svn up
	@$(MAKE) -C target/fpga -f makefile_1400a
	@$(MAKE) -C target/fpga -f makefile_400a
	@$(MAKE) -C target/software/1st_boot special
	@$(MAKE) -C target/software/ultimate
	@echo "Bitfiles and application made.. No updater or such available."

loader:
	@$(MAKE) -C target/fpga -f makefile_boot_400a
	@$(MAKE) -C target/fpga -f makefile_boot_700a
	@$(MAKE) -C target/software/programmer
	@cp target/software/programmer/result/*.bit .

u2p_tester:
	@$(MAKE) -C target/software/nios2_elf_lwip
	@$(MAKE) -C target/software/nios2_testboot
	@$(MAKE) -C target/software/nios2_dutboot
	@$(MAKE) -C target/fpga/testdut
	@$(MAKE) -C target/fpga/testexec
	@touch software/nios_dut_bsp/Makefile
	@touch software/nios_dut_bsp/public.mk
	@touch software/nios_tester_bsp/Makefile
	@touch software/nios_tester_bsp/public.mk
	@$(MAKE) -C software/nios_dut_bsp
	@$(MAKE) -C software/nios_tester_bsp
	@$(MAKE) -C target/software/nios2_dut clean
	@$(MAKE) -C target/software/nios2_tester clean
	@$(MAKE) -C target/software/nios2_testloader clean
	@$(MAKE) -C target/software/nios2_testflasher clean
	@$(MAKE) -C target/software/nios2_dut
	@$(MAKE) -C target/software/nios2_testloader
	@$(MAKE) -C target/software/nios2_tester
	@$(MAKE) -C target/software/nios2_testflasher
	@$(MAKE) -C target/tester_package force
	@$(MAKE) -C target/tester_package force

u2p_tester_sw:
	@$(MAKE) -C target/software/nios2_elf_lwip
	@touch software/nios_dut_bsp/Makefile
	@touch software/nios_dut_bsp/public.mk
	@touch software/nios_tester_bsp/Makefile
	@touch software/nios_tester_bsp/public.mk
	@$(MAKE) -C software/nios_dut_bsp
	@$(MAKE) -C software/nios_tester_bsp
	@$(MAKE) -C target/software/nios2_dut clean
	@$(MAKE) -C target/software/nios2_tester clean
	@$(MAKE) -C target/software/nios2_testloader clean
	@$(MAKE) -C target/software/nios2_testflasher clean
	@$(MAKE) -C target/software/nios2_dut
	@$(MAKE) -C target/software/nios2_testloader
	@$(MAKE) -C target/software/nios2_tester
	@$(MAKE) -C target/software/nios2_testflasher
	@$(MAKE) -C target/tester_package force
	@$(MAKE) -C target/tester_package force

clean:
	@$(MAKE) -C tools clean
	@rm -f ./update.bin
	@rm -f ./update.u2u
	@rm -f ./revert.u2u
	@rm -f ./flash_700.mcs
	@rm -rf target/fpga/work1400
	@rm -rf target/fpga/work400
	@rm -rf target/fpga/work700
	@rm -rf target/fpga/boot_700
	@rm -rf target/fpga/work250
	@rm -rf target/fpga/mb700
	@rm -rf target/fpga/mb700dd
	@rm -rf target/fpga/_xm*
	@rm -rf target/fpga/x*
	@rm -rf target/fpga/*.x*
	@rm -rf `find target/software -name result`
	@rm -rf `find target/software -name output`

sw_clean:
	@rm -f ./update.bin
	@rm -f ./update_*.u2u
	@rm -f ./revert.u2u
	@rm -rf `find target/software -name result`
	@rm -rf `find target/software -name output`

mb_clean:
	@rm -f ./update_*.u2u
	@rm -f ./revert.u2u
	@rm -rf `find target/software/mb* -name result`
	@rm -rf `find target/software/mb* -name output`

u2plus_swonly:
	@touch software/nios_solo_bsp/Makefile
	@touch software/nios_solo_bsp/public.mk
	@touch software/nios_appl_bsp/Makefile
	@touch software/nios_appl_bsp/public.mk
	@$(MAKE) -C tools
	@$(MAKE) -C software/nios_solo_bsp
	@$(MAKE) -C software/nios_appl_bsp
	@$(MAKE) -C target/software/nios2_elf_lwip
	@$(MAKE) -C target/software/nios2_ultimate
	@$(MAKE) -C target/software/nios2_recovery
	@$(MAKE) -C target/software/nios2_update
	@cp target/software/nios2_update/result/update.app ./update.u2p

u2plus_swapply:
	@$(MAKE) -C tools
	@$(MAKE) -C software/nios_solo_bsp
	@$(MAKE) -C software/nios_appl_bsp
	@$(MAKE) -C target/software/nios2_elf_lwip
	@$(MAKE) -C target/software/nios2_ultimate
	@$(MAKE) -C target/software/nios2_recovery
	nios2-download -g target/software/nios2_ultimate/result/ultimate.elf

u2_swonly:
	@$(MAKE) -C tools
	@$(MAKE) -C target/software/mb_lwip
	@$(MAKE) -C target/software/mb_boot
	@$(MAKE) -C target/software/mb_boot_dd
	@$(MAKE) -C target/software/mb_boot2
	@$(MAKE) -C target/software/mb_ultimate
	
u64:
	@$(MAKE) -C tools
	@$(MAKE) -C software/nios_solo_bsp
	@$(MAKE) -C software/nios_appl_bsp
	@$(MAKE) -C target/software/nios2_elf_lwip
	@$(MAKE) -C target/software/nios2_u64
	@$(MAKE) -C target/software/nios2_update_u64a4
	@cp target/software/nios2_update_u64a4/result/update.app ./update.u64

u64dev:
	@$(MAKE) -C tools
	@$(MAKE) -C software/nios_solo_bsp
	@$(MAKE) -C software/nios_appl_bsp
	@$(MAKE) -C target/software/nios2_elf_lwip
	@$(MAKE) -C target/software/nios2_u64_dev
	@$(MAKE) -C target/software/nios2_update_u64dev
	@cp target/software/nios2_update_u64dev/result/update.app ./update_dev.u64
	
u64_clean:
	@$(MAKE) -C target/software/nios2_update_u64 clean
	@$(MAKE) -C target/software/nios2_u64_boot clean
	@$(MAKE) -C target/software/nios2_u64_dev clean
	@$(MAKE) -C target/software/nios2_u64_loader clean
	@$(MAKE) -C target/software/nios2_update_u64a4 clean
	@$(MAKE) -C target/software/nios2_update_u64dev clean
	@$(MAKE) -C target/software/nios2_u64 clean

u2pl:
        @$(MAKE) -C tools
        @$(MAKE) -C target/software/riscv32_unknown_elf_lwip
        @$(MAKE) -C target/software/riscv32_u2p_boot
        @$(MAKE) -C target/fpga/u2plus_ecp5
        @$(MAKE) -C target/software/riscv32_ultimate
        @$(MAKE) -C target/software/riscv32_update
        @cp target/software/riscv32_update/result/update.app ./update.u2l


