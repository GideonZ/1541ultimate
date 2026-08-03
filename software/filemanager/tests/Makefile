CXX ?= g++
CXXFLAGS := -std=c++14 -g -I. -I.. -I../../components -I../../filesystem -I../../infra -I../../system -I../../userinterface -I../../io/usb/tests
OUTPUT_DIR := output
TEMP_AUTO_CLEANUP_TEST := $(OUTPUT_DIR)/tempAutoCleanupTest
DOS_COMMAND_TEST := $(OUTPUT_DIR)/dosCommandTest

SOURCES := ../../io/usb/tests/host_test_main.cpp ../filemanager.cc ../path.cc \
           ../../filesystem/file_system.cc ../../filesystem/filesystem_root.cc \
           ../../filesystem/directory.cc ../../filesystem/partition.cc \
           ../../components/pattern.cc ../../components/mystring.cc

# dos.cc reaches into the drive, user interface, RTOS and network layers, none
# of which build on the host. The stubs in ../../test/hoststubs replace exactly
# those headers and must therefore be searched before the real ones.
DOS_CXXFLAGS := -std=c++14 -g -I../../test/hoststubs -I. -I.. -I../../components \
                -I../../filesystem -I../../infra -I../../system -I../../portable/riscv \
                -I../../io/command_interface -I../../io/flash -I../../io/rtc \
                -I../../io/stream -I../../userinterface \
                -I../../httpd/c-version/lib -I../../io/usb/tests
DOS_SOURCES := ../dos.cc ../../test/hoststubs/command_intf_globals.cc \
               ../../test/hoststubs/firmware_globals.cc

.PHONY: all temp-auto-cleanup dos-command

all: temp-auto-cleanup dos-command

temp-auto-cleanup:
	mkdir -p $(OUTPUT_DIR)
	$(CXX) $(CXXFLAGS) temp_auto_cleanup_test.cpp $(SOURCES) -lpthread -o $(TEMP_AUTO_CLEANUP_TEST) && $(TEMP_AUTO_CLEANUP_TEST)

dos-command:
	mkdir -p $(OUTPUT_DIR)
	$(CXX) $(DOS_CXXFLAGS) dos_command_test.cpp $(SOURCES) $(DOS_SOURCES) -lpthread -o $(DOS_COMMAND_TEST) && $(DOS_COMMAND_TEST)
