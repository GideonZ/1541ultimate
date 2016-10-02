
.PHONY: all mk1 mk2 mk3 mk3only prog special clean sw_clean loader

all:
	@svn up
	@$(MAKE) -C tools
	@$(MAKE) -C target/fpga -f makefile_700a
	@$(MAKE) -C target/fpga -f makefile_400a
	@$(MAKE) -C target/fpga -f makefile_250e
	@$(MAKE) -C target/software/1st_boot
	@$(MAKE) -C target/software/2nd_boot
	@$(MAKE) -C target/software/ultimate
	@$(MAKE) -C target/software/ultimate appl
	@$(MAKE) -C target/software/update FPGA400=1
	@cp target/software/update/result/update.bin .
	@cp target/software/ultimate/result/appl.bin .

mk2:
	@svn up
	@$(MAKE) -C legacy/2.6k/tools
	@$(MAKE) -C legacy/2.6k/target/fpga -f makefile_700a
	@$(MAKE) -C legacy/2.6k/target/software/1st_boot mk2
	@$(MAKE) -C legacy/2.6k/target/software/2nd_boot
	@$(MAKE) -C legacy/2.6k/target/software/ultimate
	@$(MAKE) -C legacy/2.6k/target/software/update FPGA400=0

mk3:
	@$(MAKE) -C tools
	@$(MAKE) -C target/fpga -f makefile_mb_700a
	@$(MAKE) -C target/software/mb_lwip
	@$(MAKE) -C target/software/mb_boot
	@$(MAKE) -C target/software/mb_boot2
	@$(MAKE) -C target/software/mb_ultimate
	@$(MAKE) -C target/software/mb_revert
	@$(MAKE) -C target/software/mb_update
	@$(MAKE) -C target/software/update_to_mb
	@cp target/software/update_to_mb/result/update.bin .
	@cp target/software/mb_revert/result/revert.u2u .
	@cp target/software/mb_update/result/update.u2u .

mk3only:
	@$(MAKE) -C tools
	@$(MAKE) -C target/fpga -f makefile_mb_700a
	@$(MAKE) -C target/software/mb_lwip
	@$(MAKE) -C target/software/mb_boot
	@$(MAKE) -C target/software/mb_boot2
	@$(MAKE) -C target/software/mb_ultimate
	@$(MAKE) -C target/software/mb_update
	@cp target/software/mb_update/result/update.u2u .

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
