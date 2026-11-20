# Host tests of the Ultimate's WiFi application (software/io/wifi). The code under test is
# cut out of wifi.cc, wifi_cmd.cc and network_esp32.cc as it is (extract.awk), and built
# with the real wifi.h, esp32.h, wifi_cmd.h and wifi_switch.h against the stand-ins of
# stubs/ and wifi_task_env.h. Every *_test.cpp here is a set of cases, all linked into one
# program per product. No hardware needed.
#
#   make          build and run, for U64=1, the U2+L (U64 not defined) and U64=2
#
CXX ?= g++
SW := ../../..
WIFI_DIR := ..
UART := ../../uart
HOST_TEST := ../../usb/tests

WIFI_CC := $(WIFI_DIR)/wifi.cc
NET_CC := ../../network/network_esp32.cc
NET_H := ../../network/network_esp32.h
CMD_CC := $(WIFI_DIR)/wifi_cmd.cc
EXTRACT := extract.awk

# 0 is the Ultimate II+L: it is built as the U2+L is, with U64 not defined at all
U64 ?= 1
VARIANTS := 1 0 2
OUT := output/u64_$(U64)

# Function names are given as written in the source (~ for a blank), so that the C++ method
# of a class is found.
WIFI_FUNCS := WiFi~::~WiFi WiFi~::~Init WiFi~::~Enable WiFi~::~Disable WiFi~::~RadioOn \
	WiFi~::~RadioOff WiFi~::~RunModeThread WiFi~::~sendEvent WiFi~::~ConnectStart WiFi~::~ConnectAnswered \
	WiFi~::~sendConnectPending WiFi~::getAccessPointItems BrowsableWifiAP~::~connect_ap
NET_FUNCS := NetworkLWIP_WiFi~::~disable NetworkLWIP_WiFi~::~enable NetworkLWIP_WiFi~::~ask_enable \
	NetworkLWIP_WiFi~::~cfg_show_aps NetworkLWIP_WiFi~::~cfg_enter_ap NetworkLWIP_WiFi~::~cfg_conn_last \
	NetworkLWIP_WiFi~::~effectuate_settings NetworkLWIP_WiFi~::~wifi_enable_change NetworkLWIP_WiFi~::~apply_enabled \
	NetworkLWIP_WiFi~::~set_enabled
CMD_FUNCS := wifi_wifi_connect

CASES := $(wildcard *_test.cpp)

CXXFLAGS := -std=c++14 -g -O0 -Wall -Wno-unused-parameter -Wno-unused-variable -Wno-unused-function -Wno-switch \
	-Wno-format -Wno-unknown-warning-option -Wno-mismatched-tags -Wno-deprecated-declarations \
	-fno-access-control -DRISCV=1 $(if $(filter 0,$(U64)),,-DU64=$(U64)) \
	-I$(OUT) -Istubs -I. -I$(WIFI_DIR) -I$(UART) -I$(SW)/wifi/raw_u64/main

# Prints the function $(1) of file $(2), and stops when it is not there
extract = out=$$(awk -v f="$(subst ~, ,$(1))" -f $(EXTRACT) $(2)); \
	[ -n "$$out" ] || { echo "wifi_task_test: $(subst ~, ,$(1)) not found in $(2)" >&2; exit 1; }; printf '%s\n' "$$out"

.PHONY: all test run clean

all: test

test:
	@for v in $(VARIANTS); do $(MAKE) --no-print-directory U64=$$v run || exit 1; done

# The default of "WiFi Enabled", from its row of the configuration (network_esp32.cc)
$(OUT)/wifi_enable_default.h: $(NET_CC) $(NET_H)
	@mkdir -p $(OUT)
	@tr -d '\r' < $< | sed -n 's/.*CFG_WIFI_ENABLE, *CFG_TYPE_ENUM.*, *\([0-9][0-9]*\) *},.*/#define WIFI_ENABLE_DEFAULT \1/p' > $@
	@tr -d '\r' < $(NET_H) | sed -n 's/^#define \(CFG_WIFI_ENABLE\) *\(0x[0-9A-Fa-f]*\).*/#define \1 \2/p' >> $@
	@grep -q CFG_WIFI_ENABLE $@ || { echo "wifi_task_test: CFG_WIFI_ENABLE not found in $(NET_H)" >&2; exit 1; }
	@grep -q WIFI_ENABLE_DEFAULT $@ || { echo "wifi_task_test: the CFG_WIFI_ENABLE row not found in $<" >&2; exit 1; }

# The code under test, in the order of its files
$(OUT)/task.cc: Makefile $(WIFI_CC) $(NET_CC) $(CMD_CC) $(EXTRACT) wifi_task_env.h $(OUT)/wifi_enable_default.h
	@mkdir -p $(OUT)
	@{ echo '#include "wifi_task_env.h"'; \
	   $(foreach f,$(WIFI_FUNCS),$(call extract,$(f),$(WIFI_CC));) \
	   $(foreach f,$(NET_FUNCS),$(call extract,$(f),$(NET_CC));) \
	   $(foreach f,$(CMD_FUNCS),$(call extract,$(f),$(CMD_CC));) } | tr -d '\r' > $(OUT)/task.orig
	@mv $(OUT)/task.orig $@

# The firmware's own printf (system/small_printf.cc), as the product builds it (FP_SUPPORT on
# the U64=2 products), with every function it defines renamed to fw_*, so that it replaces
# nothing of the host's C library. The code under test formats its messages with it: the
# host's snprintf knows formats the firmware's does not.
FW_PRINTF := $(SW)/system/small_printf.cc
FW_RENAME := -Dprintf=fw_printf -Dsprintf=fw_sprintf -Dsnprintf=fw_snprintf -Dvsprintf=fw_vsprintf \
	-Dvsnprintf=fw_vsnprintf -Dsscanf=fw_sscanf -D_vscanf=fw__vscanf -Dputs=fw_puts -Dputchar=fw_putchar
FW_FLAGS := -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=0 $(if $(filter 2,$(U64)),-DFP_SUPPORT=1)

$(OUT)/small_printf.o: $(FW_PRINTF) Makefile
	@mkdir -p $(OUT)
	$(CXX) -std=c++14 -g -O0 -w $(FW_FLAGS) $(FW_RENAME) -c -o $@ $<

# The code under test, with its snprintf and sprintf taken from the firmware's printf
$(OUT)/task.o: $(OUT)/task.cc $(OUT)/wifi_enable_default.h wifi_task_env.h Makefile
	$(CXX) $(CXXFLAGS) $(FW_FLAGS) -Dsnprintf=fw_snprintf -Dsprintf=fw_sprintf -c -o $@ $<

$(OUT)/wifi_task_test: $(OUT)/task.o $(OUT)/small_printf.o wifi_task_stubs.cpp $(CASES) \
		wifi_task_test.h wifi_task_env.h $(HOST_TEST)/host_test_main.cpp
	$(CXX) $(CXXFLAGS) -o $@ $(OUT)/task.o $(OUT)/small_printf.o wifi_task_stubs.cpp $(CASES) $(HOST_TEST)/host_test_main.cpp -lpthread

run: $(OUT)/wifi_task_test
	@echo "wifi_task_test ($(if $(filter 0,$(U64)),U2+L,U64=$(U64))):"; $(OUT)/wifi_task_test

clean:
	@rm -rf output
