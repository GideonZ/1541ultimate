CXX ?= g++
CXXFLAGS := -std=c++14 -g -I. -I.. -I../../components -I../../filesystem -I../../infra -I../../system -I../../userinterface -I../../io/usb/tests
OUTPUT_DIR := output
TEMP_AUTO_CLEANUP_TEST := $(OUTPUT_DIR)/tempAutoCleanupTest
COPY_NAMING_TEST := $(OUTPUT_DIR)/copyNamingTest

# Firmware sources every filemanager host test links against.
SOURCES := ../../io/usb/tests/host_test_main.cpp ../filemanager.cc ../path.cc \
           ../../filesystem/file_system.cc ../../filesystem/filesystem_root.cc \
           ../../filesystem/directory.cc ../../filesystem/partition.cc \
           ../../components/pattern.cc ../../components/mystring.cc

.PHONY: all temp-auto-cleanup copy-naming clean

all: temp-auto-cleanup copy-naming

temp-auto-cleanup:
	mkdir -p $(OUTPUT_DIR)
	$(CXX) $(CXXFLAGS) temp_auto_cleanup_test.cpp $(SOURCES) -lpthread -o $(TEMP_AUTO_CLEANUP_TEST) && $(TEMP_AUTO_CLEANUP_TEST)

copy-naming:
	mkdir -p $(OUTPUT_DIR)
	$(CXX) $(CXXFLAGS) copy_naming_test.cpp $(SOURCES) -lpthread -o $(COPY_NAMING_TEST) && $(COPY_NAMING_TEST)

clean:
	rm -rf $(OUTPUT_DIR)
