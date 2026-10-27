CXX ?= g++
CXXFLAGS := -std=c++14 -g -Wall -I.. -I../../filesystem -I../../infra -I../../system -I../../io/usb/tests
OUTPUT_DIR := output
CBM_NAME_TEXT_TEST := $(OUTPUT_DIR)/cbmNameTextTest

.PHONY: all cbm-name-text clean

all: cbm-name-text

cbm-name-text:
	mkdir -p $(OUTPUT_DIR)
	$(CXX) $(CXXFLAGS) cbm_name_text_test.cpp ../../io/usb/tests/host_test_main.cpp ../pattern.cc ../mystring.cc -o $(CBM_NAME_TEXT_TEST) && $(CBM_NAME_TEXT_TEST)

clean:
	rm -rf $(OUTPUT_DIR)
