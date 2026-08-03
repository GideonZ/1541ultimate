CXX ?= g++
CXXFLAGS := -std=c++14 -g -I. -I.. -I../../io/usb/tests
OUTPUT_DIR := output
PATTERN_TEST := $(OUTPUT_DIR)/patternTest

.PHONY: all pattern clean

all: pattern

pattern:
	mkdir -p $(OUTPUT_DIR)
	$(CXX) $(CXXFLAGS) pattern_test.cpp ../pattern.cc ../mystring.cc ../../io/usb/tests/host_test_main.cpp -o $(PATTERN_TEST) && $(PATTERN_TEST)

clean:
	rm -rf $(OUTPUT_DIR)
