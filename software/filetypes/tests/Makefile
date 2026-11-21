# Host test of the "Flash into ESP32" file action (software/filetypes/filetype_esp.cc).
# FileTypeESP :: execute is cut out of the file as it is (extract.awk) and built against the
# stand-ins of esp_env.h: a file in memory, a scripted ESP32, a user interface that logs.
# md5_test.cpp tests the MD5 and the reply comparison of software/io/wifi/esp_md5.h. No
# hardware needed.
#
#   make          build and run
#
CXX ?= g++
SW := ../..
EXTRACT := ../../io/wifi/tests/extract.awk
SRC := ../filetype_esp.cc
HOST_TEST := ../../io/usb/tests
OUT := output

CXXFLAGS := -std=c++14 -g -O0 -Wall -Wno-unused-parameter -Wno-unused-variable -Wno-unused-function \
	-Wno-format -Wno-unknown-warning-option -I. -I.. -I$(SW)/io/wifi

# Prints the function $(1) of $(SRC), and stops when it is not there
extract = out=$$(awk -v f="$(1)" -f $(EXTRACT) $(SRC) | tr -d '\r'); \
	[ -n "$$out" ] || { echo "esp_flash_test: $(1) not found in $(SRC)" >&2; exit 1; }; printf '%s\n' "$$out"

.PHONY: all run clean
all: run

$(OUT)/execute.cc: Makefile $(SRC) $(EXTRACT) esp_env.h
	@mkdir -p $(OUT)
	@{ echo '#include "esp_env.h"'; echo '#include "esp_image.h"'; \
	   if grep -q 'esp_read_at(' $(SRC); then $(call extract,esp_read_at); fi; \
	   $(call extract,status_callback); \
	   $(call extract,FileTypeESP::execute); } > $@.tmp
	@mv $@.tmp $@

$(OUT)/esp_flash_test: $(OUT)/execute.cc esp_env.cpp esp_flash_test.cpp md5_test.cpp esp_env.h \
		$(HOST_TEST)/host_test_main.cpp ../esp_image.h ../../io/wifi/esp_md5.h
	$(CXX) $(CXXFLAGS) -o $@ $(OUT)/execute.cc esp_env.cpp esp_flash_test.cpp md5_test.cpp $(HOST_TEST)/host_test_main.cpp

run: $(OUT)/esp_flash_test
	@echo "esp_flash_test:"; $(OUT)/esp_flash_test

clean:
	@rm -rf $(OUT)
