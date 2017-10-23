
# .PHONY: all mk1 mk2 mk3 mk3only prog special clean sw_clean loader

all: mk3 u2plus

mk3:
	@$(MAKE) -C tools
	@$(MAKE) -C target/fpga -f makefile_mb_700a
	@$(MAKE) -C target/fpga -f makefile_mb_700a_dd
	@$(MAKE) -C target/software/mb_lwip
	@$(MAKE) -C target/software/mb_boot
	@$(MAKE) -C target/software/mb_boot_dd
	@$(MAKE) -C target/software/mb_boot2
	@$(MAKE) -C target/software/mb_ultimate
	@$(MAKE) -C target/software/mb_update
	@$(MAKE) -C target/software/mb_update_dd
	@cp target/software/mb_update/result/update.u2u ./update_audio.u2u
	@cp target/software/mb_update_dd/result/update.u2u ./update_dual_drive.u2u

mb:
	@$(MAKE) -C tools
	@$(MAKE) -C target/software/mb_lwip
	@$(MAKE) -C target/software/mb_boot
	@$(MAKE) -C target/software/mb_boot2
	@$(MAKE) -C target/software/mb_ultimate
	@$(MAKE) -C target/software/mb_update

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
	@$(MAKE) -C target/software/nios2_flash
	@$(MAKE) -C target/software/nios2_update
	@cp target/software/nios2_update/result/update.app ./update.u2p

niosapps:
	@$(MAKE) -C target/software/nios2_ultimate
	@$(MAKE) -C target/software/nios2_recovery
	@$(MAKE) -C target/software/nios2_flash
	@$(MAKE) -C target/software/nios2_update
	@cp target/software/nios2_update/result/update.app ./update.u2p

u64apps:
	@$(MAKE) -C target/software/nios2_u64
	@$(MAKE) -C target/software/nios2_update_u64
	@$(MAKE) -C target/software/nios2_update_u64a4
	@cp target/software/nios2_update_u64/result/update.app ./update_5ceba2.u64
	@cp target/software/nios2_update_u64a4/result/update.app ./update_5ceba4.u64

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
	@$(MAKE) -C target/software/nios2_dut
	@$(MAKE) -C target/software/nios2_tester
	@$(MAKE) -C target/software/nios2_testloader
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
	@rm -rf target/fpga/_xm*
	@rm -rf target/fpga/x*
	@rm -rf target/fpga/*.x*
	@rm -rf `find target/software -name result`
	@rm -rf `find target/software -name output`

sw_clean:
	@rm -f ./update.bin
	@rm -f ./update.u2u
	@rm -f ./revert.u2u
	@rm -rf `find target/software -name result`
	@rm -rf `find target/software -name output`

mb_clean:
	@rm -f ./update.u2u
	@rm -f ./revert.u2u
	@rm -rf `find target/software/mb* -name result`
	@rm -rf `find target/software/mb* -name output`
	