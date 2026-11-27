CXX ?= g++
CXXFLAGS := -std=c++14 -g -I. -I.. -I../../components -I../../filesystem -I../../infra -I../../system -I../../userinterface -I../../io/usb/tests
OUTPUT_DIR := output
TEMP_AUTO_CLEANUP_TEST := $(OUTPUT_DIR)/tempAutoCleanupTest

.PHONY: all temp-auto-cleanup asan fat-media-swap fat-media-swap-asan

all: temp-auto-cleanup fat-media-swap

temp-auto-cleanup:
	mkdir -p $(OUTPUT_DIR)
	$(CXX) $(CXXFLAGS) temp_auto_cleanup_test.cpp ../../io/usb/tests/host_test_main.cpp ../filemanager.cc ../path.cc ../../filesystem/file_system.cc ../../filesystem/filesystem_root.cc ../../filesystem/directory.cc ../../filesystem/partition.cc ../../components/pattern.cc ../../components/mystring.cc -lpthread -o $(TEMP_AUTO_CLEANUP_TEST) && $(TEMP_AUTO_CLEANUP_TEST)

# The same suite under AddressSanitizer, which turns a use after free into an immediate report.
# Run on its own: make asan. It does not free everything it sets up at exit, so leak detection is off,
# as for the image mount tests in .github/workflows/build.yml.
asan:
	mkdir -p $(OUTPUT_DIR)
	$(CXX) $(CXXFLAGS) -fsanitize=address -fno-omit-frame-pointer temp_auto_cleanup_test.cpp ../../io/usb/tests/host_test_main.cpp ../filemanager.cc ../path.cc ../../filesystem/file_system.cc ../../filesystem/filesystem_root.cc ../../filesystem/directory.cc ../../filesystem/partition.cc ../../components/pattern.cc ../../components/mystring.cc -lpthread -o $(OUTPUT_DIR)/tempAutoCleanupTestAsan && env -u LD_PRELOAD ASAN_OPTIONS=detect_leaks=0 $(OUTPUT_DIR)/tempAutoCleanupTestAsan

# Media swaps on a FAT file device: the real FileDevice, Disk, Partition, FileSystemFAT and FatFs on a RAM block device.
FAT_MEDIA_SWAP_TEST := $(OUTPUT_DIR)/fatMediaSwapTest
FAT_INC := -I../../chan_fat/pc -I../../chan_fat/ff14/source -I../../filesystem -I../../system
FAT_SRCS := fat_media_swap_test.cpp ../../io/usb/tests/host_test_main.cpp ../file_device.cc ../file_partition.cc ../path.cc \
	../../filesystem/disk.cc ../../filesystem/partition.cc ../../filesystem/blockdev.cc ../../filesystem/blockdev_ram.cc \
	../../filesystem/diskio.cc ../../filesystem/file_system.cc ../../filesystem/filesystem_fat.cc ../../filesystem/directory.cc \
	../../components/pattern.cc ../../components/mystring.cc ../../components/size_str.cc
FAT_C_SRCS := ../../chan_fat/ff14/source/ff.c ../../chan_fat/ff14/source/ffunicode.c ../../chan_fat/ff14/source/ffsystem.c

fat-media-swap:
	mkdir -p $(OUTPUT_DIR)
	$(CC) -g -w $(FAT_INC) -c $(FAT_C_SRCS) && mv ff.o ffunicode.o ffsystem.o $(OUTPUT_DIR)/
	$(CXX) $(CXXFLAGS) $(FAT_INC) $(FAT_SRCS) $(OUTPUT_DIR)/ff.o $(OUTPUT_DIR)/ffunicode.o $(OUTPUT_DIR)/ffsystem.o -o $(FAT_MEDIA_SWAP_TEST) && $(FAT_MEDIA_SWAP_TEST)

# The same under AddressSanitizer, which turns a use after free into an immediate report.
fat-media-swap-asan:
	mkdir -p $(OUTPUT_DIR)/asan
	$(CC) -g -w -fsanitize=address $(FAT_INC) -c $(FAT_C_SRCS) && mv ff.o ffunicode.o ffsystem.o $(OUTPUT_DIR)/asan/
	$(CXX) $(CXXFLAGS) $(FAT_INC) -fsanitize=address -fno-omit-frame-pointer $(FAT_SRCS) $(OUTPUT_DIR)/asan/ff.o $(OUTPUT_DIR)/asan/ffunicode.o $(OUTPUT_DIR)/asan/ffsystem.o -o $(OUTPUT_DIR)/fatMediaSwapTestAsan && env -u LD_PRELOAD ASAN_OPTIONS=detect_leaks=0 $(OUTPUT_DIR)/fatMediaSwapTestAsan
