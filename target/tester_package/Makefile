FLASH = flash
TESTER = tester
ROMS = ../../roms
SVFDUMP = ../../tools/svf_dump

all: $(FLASH) $(FLASH)/rompack.bin $(FLASH)/ultimate_run.swp $(FLASH)/ultimate_recovery.swp $(FLASH)/ultimate.app $(FLASH)/recovery.app \
	 $(TESTER) $(TESTER)/dut.fpga $(TESTER)/dut.app $(TESTER)/tester.app

force::
	@make -C ../software/nios2_flash ultimate_run.swp
	@make -C ../software/nios2_flash ultimate_recovery.swp
	@make -C ../software/nios2_ultimate
	@make -C ../software/nios2_recovery
	@make -C ../software/nios2_dut
	@make -C ../software/nios2_tester
	@make all
	
$(FLASH)/rompack.bin: $(ROMS)/ar5pal.bin $(ROMS)/ar6pal.bin $(ROMS)/final3.bin $(ROMS)/rr38pal.bin $(ROMS)/rr38ntsc.bin $(ROMS)/tar_pal.bin \
	$(ROMS)/tar_ntsc.bin $(ROMS)/ss5pal.bin $(ROMS)/ss5ntsc.bin $(ROMS)/ar5ntsc.bin $(ROMS)/kcs.bin $(ROMS)/epyx.bin
	@echo "Creating ROM Pack."
	@cat $(ROMS)/ar5pal.bin $(ROMS)/ar6pal.bin $(ROMS)/final3.bin $(ROMS)/rr38pal.bin $(ROMS)/rr38ntsc.bin $(ROMS)/tar_pal.bin \
	$(ROMS)/tar_ntsc.bin $(ROMS)/ss5pal.bin $(ROMS)/ss5ntsc.bin $(ROMS)/ar5ntsc.bin $(ROMS)/kcs.bin $(ROMS)/epyx.bin >$@

$(FLASH)/ultimate_run.swp : ../software/nios2_flash/output/ultimate_run.swp
	@cp $< $@

$(FLASH)/ultimate_recovery.swp : ../software/nios2_flash/output/ultimate_recovery.swp
	@cp $< $@

$(FLASH)/ultimate.app : ../software/nios2_ultimate/result/ultimate.app
	@cp $< $@

$(FLASH)/recovery.app : ../software/nios2_recovery/result/recovery.app
	@cp $< $@

$(TESTER)/dut.fpga: ../fpga/testdut/output_files/testdut.svf
	@$(SVFDUMP) $< >/dev/null
	@mv dump.bin $@

$(TESTER)/dut.app: ../software/nios2_dut/result/dut.app
	@cp $< $@

$(TESTER)/tester.app: ../software/nios2_tester/result/tester_main.app
	@cp $< $@

$(FLASH)::
	@mkdir -p $(FLASH)

$(TESTER)::
	@mkdir -p $(TESTER)

../software/nios2_flash/output/ultimate_run.swp:
	@make -C ../software/nios2_flash ultimate_run.swp

../software/nios2_flash/output/ultimate_recovery.swp:
	@make -C ../software/nios2_flash ultimate_recovery.swp

../software/nios2_ultimate/result/ultimate.app:
	@make -C ../software/nios2_ultimate

../software/nios2_recovery/result/recovery.app:
	@make -C ../software/nios2_recovery

 ../software/nios2_dut/result/dut.app:
	@make -C ../software/nios2_dut

../software/nios2_tester/result/tester_main.app:
	@make -C ../software/nios2_tester
