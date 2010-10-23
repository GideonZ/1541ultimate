
.PHONY: all clean

all:
	@$(MAKE) -C tools
	@$(MAKE) -C target/fpga -f makefile_700a
	@$(MAKE) -C target/fpga -f makefile_400a
	@$(MAKE) -C target/fpga -f makefile_250e
	@$(MAKE) -C target/software/1st_boot
	@$(MAKE) -C target/software/2nd_boot
	@$(MAKE) -C target/software/ultimate hex
	@$(MAKE) -C target/software/ultimate appl
	@$(MAKE) -C target/software/update
	@cp target/software/update/result/update.bin .
	@cp target/software/ultimate/result/flash_700.mcs .
	@cp target/software/update/result/appl.bin .

clean:
	@$(MAKE) -C tools clean
	@rm -f ./update.bin
	@rm -f ./flash_700.mcs
	@rm -rf target/fpga/work400
	@rm -rf target/fpga/work700
	@rm -rf target/fpga/work250
	@rm -rf target/fpga/_xm*
	@rm -rf target/fpga/x*
	@rm -rf target/fpga/*.x*
	@rm -rf target/software/1st_boot/result
	@rm -rf target/software/1st_boot/output
	@rm -rf target/software/2nd_boot/result
	@rm -rf target/software/2nd_boot/output
	@rm -rf target/software/ultimate/result
	@rm -rf target/software/ultimate/output
	@rm -rf target/software/update/result
	@rm -rf target/software/update/output
