# Host tests of the Ultimate's WiFi application (software/io/wifi). The code under test is
# cut out of wifi.cc, wifi_cmd.cc and network_esp32.cc as it is (extract.awk), and built
# with the real wifi.h, esp32.h, wifi_cmd.h and wifi_switch.h against the stand-ins of
# stubs/ and wifi_task_env.h. Every *_test.cpp here is a set of cases, all linked into one
# program per product. No hardware needed.
#
#   make          test and red
#   make test     build and run, for U64=1, the U2+L (U64 not defined) and U64=2
#   make red      build the same cases against a model of the code as it was before each fix
#                 (legacy/*.sed, applied to the extracted code); each has to fail
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
# set by `make red` to the legacy model to apply
LEG ?=
OUT := $(if $(LEG),output/red_$(basename $(notdir $(LEG)))_u64_$(U64),output/u64_$(U64))

# Function names are given as written in the source (~ for a blank), so that the C++ method
# of a class is found.
WIFI_FUNCS := WiFi~::~WiFi WiFi~::~Init WiFi~::~Enable WiFi~::~Disable WiFi~::~RadioOn \
	WiFi~::~RadioOff WiFi~::~RunModeThread WiFi~::~sendEvent WiFi~::~sendConnectPending WiFi~::getAccessPointItems \
	BrowsableWifiAP~::~connect_ap
NET_FUNCS := NetworkLWIP_WiFi~::~disable NetworkLWIP_WiFi~::~enable NetworkLWIP_WiFi~::~ask_enable \
	NetworkLWIP_WiFi~::~cfg_show_aps NetworkLWIP_WiFi~::~cfg_enter_ap NetworkLWIP_WiFi~::~cfg_conn_last
NET_FUNCS_SWITCH := NetworkLWIP_WiFi~::~wifi_enable_change NetworkLWIP_WiFi~::~set_enabled \
	NetworkLWIP_WiFi~::~cfg_enable NetworkLWIP_WiFi~::~cfg_disable
CMD_FUNCS := wifi_wifi_connect

CASES := $(wildcard *_test.cpp)
LEGACY := $(sort $(wildcard legacy/*.sed))

CXXFLAGS := -std=c++14 -g -O0 -Wall -Wno-unused-parameter -Wno-unused-variable -Wno-unused-function -Wno-switch \
	-Wno-format -Wno-unknown-warning-option -Wno-mismatched-tags -Wno-deprecated-declarations \
	-fno-access-control -DRISCV=1 $(if $(filter 0,$(U64)),,-DU64=$(U64)) \
	-I$(OUT) -Istubs -I. -I$(WIFI_DIR) -I$(UART) -I$(SW)/wifi/raw_u64/main

# Prints the function $(1) of file $(2), and stops when it is not there
extract = out=$$(awk -v f="$(subst ~, ,$(1))" -f $(EXTRACT) $(2)); \
	[ -n "$$out" ] || { echo "wifi_task_test: $(subst ~, ,$(1)) not found in $(2)" >&2; exit 1; }; printf '%s\n' "$$out"

.PHONY: all test red run redrun clean

all: test red

test:
	@for v in $(VARIANTS); do $(MAKE) --no-print-directory U64=$$v run || exit 1; done

red:
	@for l in $(LEGACY); do \
	  for v in $$(sed -n 's/^# variants: //p' $$l); do \
	    $(MAKE) --no-print-directory U64=$$v LEG=$$l redrun || exit 1; \
	  done; \
	done

# The default of "WiFi Enabled", from its row of the configuration (network_esp32.cc)
$(OUT)/wifi_enable_default.h: $(NET_CC) $(NET_H)
	@mkdir -p $(OUT)
	@tr -d '\r' < $< | sed -n 's/.*CFG_WIFI_ENABLE, *CFG_TYPE_ENUM.*, *\([0-9][0-9]*\) *},.*/#define WIFI_ENABLE_DEFAULT \1/p' > $@
	@tr -d '\r' < $(NET_H) | sed -n 's/^#define \(CFG_WIFI_ENABLE\) *\(0x[0-9A-Fa-f]*\).*/#define \1 \2/p' >> $@
	@grep -q CFG_WIFI_ENABLE $@ || { echo "wifi_task_test: CFG_WIFI_ENABLE not found in $(NET_H)" >&2; exit 1; }
	@grep -q WIFI_ENABLE_DEFAULT $@ || { echo "wifi_task_test: the CFG_WIFI_ENABLE row not found in $<" >&2; exit 1; }

# The code under test, in the order of its files; for a legacy model, with its sed applied
$(OUT)/task.cc: Makefile $(WIFI_CC) $(NET_CC) $(CMD_CC) $(EXTRACT) $(LEG) wifi_task_env.h $(OUT)/wifi_enable_default.h
	@mkdir -p $(OUT)
	@{ echo '#include "wifi_task_env.h"'; \
	   $(foreach f,$(WIFI_FUNCS),$(call extract,$(f),$(WIFI_CC));) \
	   $(foreach f,$(NET_FUNCS),$(call extract,$(f),$(NET_CC));) \
	   echo '#if U64 != 2'; \
	   $(foreach f,$(NET_FUNCS_SWITCH),$(call extract,$(f),$(NET_CC));) \
	   echo '#endif'; \
	   $(foreach f,$(CMD_FUNCS),$(call extract,$(f),$(CMD_CC));) } | tr -d '\r' > $(OUT)/task.orig
	@if [ -n "$(LEG)" ]; then \
	   sed -f $(LEG) $(OUT)/task.orig > $@; \
	   if cmp -s $@ $(OUT)/task.orig; then echo "wifi_task_test: $(LEG) changes nothing" >&2; exit 1; fi; \
	 else cp $(OUT)/task.orig $@; fi

$(OUT)/wifi_task_test: $(OUT)/task.cc $(OUT)/wifi_enable_default.h wifi_task_stubs.cpp $(CASES) \
		wifi_task_test.h wifi_task_env.h $(HOST_TEST)/host_test_main.cpp
	$(CXX) $(CXXFLAGS) -o $@ $(OUT)/task.cc wifi_task_stubs.cpp $(CASES) $(HOST_TEST)/host_test_main.cpp -lpthread

run: $(OUT)/wifi_task_test
	@echo "wifi_task_test ($(if $(filter 0,$(U64)),U2+L,U64=$(U64))):"; $(OUT)/wifi_task_test

# The same cases against the code as it was before a fix: the build has to work and the
# run has to fail, which says which checks
redrun: $(OUT)/wifi_task_test
	@if $(OUT)/wifi_task_test > $(OUT)/red.log 2>&1; then \
	  echo "red $(LEG) ($(if $(filter 0,$(U64)),U2+L,U64=$(U64))): the cases passed against the code before the fix -- they no longer catch it" >&2; exit 1; \
	else \
	  echo "red $(LEG) ($(if $(filter 0,$(U64)),U2+L,U64=$(U64))): $$(tail -n 1 $(OUT)/red.log) against the code before the fix, as required"; \
	  grep '^FAIL' $(OUT)/red.log | head -n 6 | sed 's/^/    /'; \
	fi

clean:
	@rm -rf output
