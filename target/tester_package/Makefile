FLASH = flash
TESTER = tester
ROMS = ../../roms
SVFDUMP = ../../tools/svf_dump

all: $(FLASH) $(FLASH)/rompack.bin $(FLASH)/ultimate_run.swp $(FLASH)/ultimate_recovery.swp $(FLASH)/ultimate.app $(FLASH)/recovery.app \
	 $(TESTER) $(TESTER)/dut.fpga $(TESTER)/tester.app \
	 $(FLASH)/testexec.swp $(FLASH)/test_loader.app

force::
	@make -C ../u2plus/nios/flash 
	@make -C ../u2plus/nios/ultimate
	@make -C ../u2plus/nios/recovery
	@make -C ../tester_u2p/nios/dut
	@make -C ../tester_u2p/nios/tester
	@make all
	
$(FLASH)/rompack.bin: $(ROMS)/ar5pal.bin $(ROMS)/ar6pal.bin $(ROMS)/final3.bin $(ROMS)/rr38pal.bin $(ROMS)/rr38ntsc.bin $(ROMS)/tar_pal.bin \
	$(ROMS)/tar_ntsc.bin $(ROMS)/ss5pal.bin $(ROMS)/ss5ntsc.bin $(ROMS)/ar5ntsc.bin $(ROMS)/kcs.bin $(ROMS)/epyx.bin
	@echo "Creating ROM Pack."
	@cat $(ROMS)/ar5pal.bin $(ROMS)/ar6pal.bin $(ROMS)/final3.bin $(ROMS)/rr38pal.bin $(ROMS)/rr38ntsc.bin $(ROMS)/tar_pal.bin \
	$(ROMS)/tar_ntsc.bin $(ROMS)/ss5pal.bin $(ROMS)/ss5ntsc.bin $(ROMS)/ar5ntsc.bin $(ROMS)/kcs.bin $(ROMS)/epyx.bin >$@

$(FLASH)/ultimate_run.swp : ../u2plus/nios/flash/output/ultimate_run.swp
	@cp $< $@

$(FLASH)/ultimate_recovery.swp : ../u2plus/nios/flash/output/ultimate_recovery.swp
	@cp $< $@

$(FLASH)/testexec.swp : ../tester_u2p/nios/testflasher/output/testexec.swp
	@cp $< $@

$(FLASH)/test_loader.app : ../tester_u2p/nios/testloader/result/test_loader.app
	@cp $< $@

$(FLASH)/ultimate.app : ../u2plus/nios/ultimate/result/ultimate.app
	@cp $< $@

$(FLASH)/recovery.app : ../u2plus/nios/recovery/result/recovery.app
	@cp $< $@

$(TESTER)/dut.fpga: ../fpga/testdut/output_files/testdut.svf
	@$(SVFDUMP) $< >/dev/null
	@mv dump.bin $@

$(TESTER)/dut.app: ../tester_u2p/nios/dut/result/dut.app
	@cp $< $@

$(TESTER)/tester.app: ../tester_u2p/nios/tester/result/tester_main.app
	@cp $< $@

$(FLASH)::
	@mkdir -p $(FLASH)

$(TESTER)::
	@mkdir -p $(TESTER)

../u2plus/nios/flash/output/ultimate_run.swp:
	@make -C ../u2plus/nios/flash ultimate_run.swp

../u2plus/nios/flash/output/ultimate_recovery.swp:
	@make -C ../u2plus/nios/flash ultimate_recovery.swp

../tester_u2p/nios/testflasher/output/testexec.swp:
	@make -C ../tester_u2p/nios/testflasher testexec.swp

../tester_u2p/nios/testloader/result/test_loader.app:
	@make -C ../tester_u2p/nios/testloader

../u2plus/nios/ultimate/result/ultimate.app:
	@make -C ../u2plus/nios/ultimate

../u2plus/nios/recovery/result/recovery.app:
	@make -C ../u2plus/nios/recovery

 ../tester_u2p/nios/dut/result/dut.app:
	@make -C  ../tester_u2p/nios/dut

../tester_u2p/nios/tester/result/tester_main.app:
	@make -C ../tester_u2p/nios/tester
