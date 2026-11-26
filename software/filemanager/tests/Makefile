CXX ?= g++
CXXFLAGS := -std=c++14 -g -I. -I.. -I../../components -I../../filesystem -I../../infra -I../../system -I../../userinterface -I../../io/usb/tests
OUTPUT_DIR := output
TEMP_AUTO_CLEANUP_TEST := $(OUTPUT_DIR)/tempAutoCleanupTest

.PHONY: all temp-auto-cleanup asan

all: temp-auto-cleanup

temp-auto-cleanup:
	mkdir -p $(OUTPUT_DIR)
	$(CXX) $(CXXFLAGS) temp_auto_cleanup_test.cpp ../../io/usb/tests/host_test_main.cpp ../filemanager.cc ../path.cc ../../filesystem/file_system.cc ../../filesystem/filesystem_root.cc ../../filesystem/directory.cc ../../filesystem/partition.cc ../../components/pattern.cc ../../components/mystring.cc -lpthread -o $(TEMP_AUTO_CLEANUP_TEST) && $(TEMP_AUTO_CLEANUP_TEST)

# The same suite under AddressSanitizer, which turns a use after free into an immediate report.
# Run on its own: make asan. It does not free everything it sets up at exit, so leak detection is off,
# as for the image mount tests in .github/workflows/build.yml.
asan:
	mkdir -p $(OUTPUT_DIR)
	$(CXX) $(CXXFLAGS) -fsanitize=address -fno-omit-frame-pointer temp_auto_cleanup_test.cpp ../../io/usb/tests/host_test_main.cpp ../filemanager.cc ../path.cc ../../filesystem/file_system.cc ../../filesystem/filesystem_root.cc ../../filesystem/directory.cc ../../filesystem/partition.cc ../../components/pattern.cc ../../components/mystring.cc -lpthread -o $(OUTPUT_DIR)/tempAutoCleanupTestAsan && env -u LD_PRELOAD ASAN_OPTIONS=detect_leaks=0 $(OUTPUT_DIR)/tempAutoCleanupTestAsan
