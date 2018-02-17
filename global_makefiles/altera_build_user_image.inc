################################################################################
##
## (C) COPYRIGHT 2014 TECHNOLUTION BV, GOUDA NL
## | =======          I                   ==          I    =
## |    I             I                    I          I
## |    I   ===   === I ===  I ===   ===   I  I    I ====  I   ===  I ===
## |    I  /   \ I    I/   I I/   I I   I  I  I    I  I    I  I   I I/   I
## |    I  ===== I    I    I I    I I   I  I  I    I  I    I  I   I I    I
## |    I  \     I    I    I I    I I   I  I  I   /I  \    I  I   I I    I
## |    I   ===   === I    I I    I  ===  ===  === I   ==  I   ===  I    I
## |                 +---------------------------------------------------+
## +----+            |  +++++++++++++++++++++++++++++++++++++++++++++++++|
##      |            |             ++++++++++++++++++++++++++++++++++++++|
##      +------------+                          +++++++++++++++++++++++++|
##                                                         ++++++++++++++|
##                                                                  +++++|
##
################################################################################
## This is the file to create a user image for the Altera Arria GX Starter kit
################################################################################

NIOS_PATH=$(QUARTUS_ROOTDIR)/../nios2eds/bin
NIOS_GCC_PATH=$(NIOS_PATH)/gnu/H-i686-pc-linux-gnu/bin

.PHONY: build_flash_file $(TARGET).flash

$(TARGET).flash: $(TARGET).sof
	@${TL_ENV} $(ENVIRONMENTS) && $(MAKE) sub_build_flash_file

# use a sub make routine to be able to use the variables set by TL_ENV
sub_build_flash_file:
	@rm -f $(WORK_DIR)/$(TARGET).flash
	PATH=$(PATH):$(NIOS_GCC_PATH) && java -jar $(NIOS_PATH)/sof2flash.jar --input=$(WORK_DIR)/$(TARGET).sof --output=$(WORK_DIR)/$(TARGET).flash --offset=0x01640000 --pfl --optionbit=0x00018000 --programmingmode=FPP