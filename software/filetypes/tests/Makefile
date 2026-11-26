# Host test of the "Flash into ESP32" file action (software/filetypes/filetype_esp.cc).
# FileTypeESP :: execute is cut out of the file as it is (extract.awk) and built against the
# stand-ins of esp_env.h: a file in memory, a scripted ESP32, a user interface that logs.
# md5_test.cpp tests the MD5 and the reply comparison of software/io/wifi/esp_md5.h.
# esp32_verify_test.cpp runs Esp32 :: VerifyFlash and Esp32 :: Command, cut out of
# software/io/wifi/esp32.cc, against a UART that answers with scripted frames. No hardware
# needed.
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
# esp32.h as it is, with the UART of esp32stubs and the other stand-ins of the WiFi host tests
VERIFY_FLAGS := -fno-access-control -DRISCV=1 -Iesp32stubs -I$(SW)/io/wifi/tests/stubs -I$(SW)/io/uart
ESP32_CC := ../../io/wifi/esp32.cc

# Prints the function $(1) of $(SRC), and stops when it is not there
extract = out=$$(awk -v f="$(1)" -f $(EXTRACT) $(SRC) | tr -d '\r'); \
	[ -n "$$out" ] || { echo "esp_flash_test: $(1) not found in $(SRC)" >&2; exit 1; }; printf '%s\n' "$$out"

# Same for a function of $(ESP32_CC), given as written in the source (~ for a blank)
extract_esp32 = out=$$(awk -v f="$(subst ~, ,$(1))" -f $(EXTRACT) $(ESP32_CC) | tr -d '\r'); \
	[ -n "$$out" ] || { echo "esp_flash_test: $(subst ~, ,$(1)) not found in $(ESP32_CC)" >&2; exit 1; }; printf '%s\n' "$$out"

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

$(OUT)/esp32_funcs.cc: Makefile $(ESP32_CC) $(EXTRACT)
	@mkdir -p $(OUT)
	@{ $(call extract_esp32,Esp32::PackParams); $(call extract_esp32,Esp32::Command); \
	   $(call extract_esp32,Esp32~::~VerifyFlash); } > $@.tmp
	@mv $@.tmp $@

$(OUT)/esp32_verify_test: $(OUT)/esp32_funcs.cc esp32_verify_test.cpp esp32stubs/dma_uart.h \
		$(HOST_TEST)/host_test_main.cpp ../../io/wifi/esp32.h ../../io/wifi/esp_md5.h
	$(CXX) $(CXXFLAGS) $(VERIFY_FLAGS) -o $@ esp32_verify_test.cpp $(HOST_TEST)/host_test_main.cpp

run: $(OUT)/esp_flash_test $(OUT)/esp32_verify_test
	@echo "esp_flash_test:"; $(OUT)/esp_flash_test
	@echo "esp32_verify_test:"; $(OUT)/esp32_verify_test

clean:
	@rm -rf $(OUT)
