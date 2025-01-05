#
# This is a project Makefile. It is assumed the directory this Makefile resides in is a
# project subdirectory.
#

PROJECT_NAME := bridge

# include $(IDF_PATH)/make/project.mk

$(PROJECT_NAME).esp: build/$(PROJECT_NAME).bin build/bootloader/bootloader.bin build/partition_table/partition-table.bin
	@../../../tools/makeappl $(PROJECT_NAME).esp \
	build/bootloader/bootloader.bin 0x1000 \
	build/partition_table/partition-table.bin 0x8000 \
	build/$(PROJECT_NAME).bin 0x10000
