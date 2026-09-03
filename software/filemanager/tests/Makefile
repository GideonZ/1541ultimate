CXX ?= g++
CXXFLAGS := -std=c++14 -g -I. -I.. -I../../components -I../../filesystem -I../../infra -I../../system -I../../userinterface -I../../io/usb/tests
OUTPUT_DIR := output
TEMP_AUTO_CLEANUP_TEST := $(OUTPUT_DIR)/tempAutoCleanupTest

.PHONY: all temp-auto-cleanup

all: temp-auto-cleanup

temp-auto-cleanup:
	mkdir -p $(OUTPUT_DIR)
	$(CXX) $(CXXFLAGS) temp_auto_cleanup_test.cpp ../../io/usb/tests/host_test_main.cpp ../filemanager.cc ../path.cc ../../filesystem/file_system.cc ../../filesystem/filesystem_root.cc ../../filesystem/directory.cc ../../filesystem/partition.cc ../../components/pattern.cc ../../components/mystring.cc -lpthread -o $(TEMP_AUTO_CLEANUP_TEST) && $(TEMP_AUTO_CLEANUP_TEST)
