#
# Host tests of the size checks of the ESP32 WiFi modules' dispatcher (rpc_request_too_short() in rpc_dispatch.c of raw_u64,
# raw_c3 and u64ctrl, cut out as it is) and of the Ultimate's event handler (wifi_event_too_short() in
# software/io/wifi/wifi_event_size.h). No hardware and no ESP-IDF needed.
#
#   make test                 all modules
#   make MODULE=u64ctrl test  one module
#   make red                  models of the code without a check or with a wrong one (the MUT_* sed expressions) must fail
#
PATH_SW  := ../..
EXTRACT  := awk -f extract.awk
OUT      := output
ALL      := raw_u64 raw_c3 u64ctrl
MODULE   ?= $(ALL)
CXX      ?= g++

DIR_raw_u64 := $(PATH_SW)/wifi/raw_u64/main
DIR_raw_c3  := $(PATH_SW)/wifi/raw_c3/main
DIR_u64ctrl := $(PATH_SW)/u64ctrl/main

HOSTTEST := $(PATH_SW)/io/usb/tests
CXXFLAGS := -std=c++14 -g -O0 -Wall -Wno-unused-parameter -D_FORTIFY_SOURCE=0 -I$(HOSTTEST) -Istub -I$(PATH_SW)/io/uart -I$(PATH_SW)/io/wifi

# MUT_<name> := sed expressions on the cut-out function (MUTF_<name> = module code) or on the header (MUTH_<name>)
MUTANTS := none no-check below-header setbaud-off-by-one setbaud-dropped connect-short send-length-unchecked send-head-unchecked time-dropped
MUTF_none := -e 's/^    int size = buf->size;/    return false; int size = buf->size;/'
MUTF_no-check := -e 's/if (size < (int)sizeof(rpc_header_t)) {/if (0) {/'
MUTF_below-header := -e 's/if (size < (int)sizeof(rpc_header_t)) {/if (size < 2) {/'
MUTF_setbaud-off-by-one := -e 's/return size < (int)sizeof(rpc_setbaud_req);/return size <= (int)sizeof(rpc_setbaud_req);/'
MUTF_setbaud-dropped := -e 's/return size < (int)sizeof(rpc_setbaud_req);/return false;/'
MUTF_connect-short := -e 's/return size < 98;/return size < 90;/'
MUTF_send-length-unchecked := -e 's/p->length > (uint32_t)(size - head)/0/'
MUTF_send-head-unchecked := -e 's/(size < head) ||//'
MUTF_time-dropped := -e 's/return size < (int)sizeof(rpc_get_time_req);/return false;/'
EVENT_MUTANTS := ev-connected-dropped ev-gotip-off-by-one ev-packet-length ev-packet-head ev-no-header
MUTH_ev-connected-dropped := -e 's/return size < (int)(offsetof(event_pkt_connected, ssid) + sizeof(((event_pkt_connected \*)0)->ssid));/return false;/'
MUTH_ev-gotip-off-by-one := -e 's/return size < (int)(offsetof(event_pkt_got_ip, changed) + sizeof(((event_pkt_got_ip \*)0)->changed));/return size < (int)(offsetof(event_pkt_got_ip, changed));/'
MUTH_ev-packet-length := -e 's/((const rpc_rx_pkt \*)data)->len > (uint32_t)(size - head)/0/'
MUTH_ev-packet-head := -e 's/(size < head) ||//'
MUTH_ev-no-header := -e 's/if (size < (int)sizeof(rpc_header_t)) {/if (0) {/'

.PHONY: all test red
all: test

# $(1) = output dir, $(2) = sed on the module function, $(3) = sed on wifi_event_size.h
define BUILD
	@mkdir -p $(1)
	@$(EXTRACT) -v f=rpc_request_too_short $(DIR_$(M))/rpc_dispatch.c > $(1)/rpc_code.inc
	@grep -q rpc_request_too_short $(1)/rpc_code.inc || { echo "rpc tests: the check of $(M) not found"; exit 1; }
	$(if $(2),@sed $(2) $(1)/rpc_code.inc > $(1)/rpc_code.new && ! cmp -s $(1)/rpc_code.new $(1)/rpc_code.inc || { echo "rpc tests: the model does not apply to $(M)"; exit 1; })
	$(if $(2),@mv $(1)/rpc_code.new $(1)/rpc_code.inc)
	@cp $(PATH_SW)/io/wifi/wifi_event_size.h $(1)/wifi_event_size.h
	$(if $(3),@sed $(3) $(1)/wifi_event_size.h > $(1)/wifi_event_size.new && ! cmp -s $(1)/wifi_event_size.new $(1)/wifi_event_size.h || { echo "rpc tests: the event model does not apply"; exit 1; })
	$(if $(3),@mv $(1)/wifi_event_size.new $(1)/wifi_event_size.h)
	$(CXX) -I$(1) $(CXXFLAGS) -I$(DIR_$(M)) -o $(1)/rpc_size_test rpc_size_test.cpp $(HOSTTEST)/host_test_main.cpp -lpthread
endef

test:
	@set -e; for m in $(MODULE); do $(MAKE) --no-print-directory M=$$m run; done

run:
	$(call BUILD,$(OUT)/$(M),,)
	@echo "== rpc size $(M)"; $(OUT)/$(M)/rpc_size_test

red:
	@set -e; for x in $(MUTANTS); do for m in $(MODULE); do $(MAKE) --no-print-directory M=$$m X=$$x redrun; done; done
	@set -e; for x in $(EVENT_MUTANTS); do $(MAKE) --no-print-directory M=u64ctrl X=$$x redevrun; done

redrun:
	$(call BUILD,$(OUT)/red_$(X)_$(M),$(MUTF_$(X)),)
	@if $(OUT)/red_$(X)_$(M)/rpc_size_test > $(OUT)/red_$(X)_$(M)/log 2>&1; then echo "red $(X) $(M): the cases PASSED against the model"; exit 1; \
	 else echo "red $(X) $(M): $$(grep -c '^FAIL' $(OUT)/red_$(X)_$(M)/log) check(s) fail, as required"; fi

redevrun:
	$(call BUILD,$(OUT)/red_$(X)_$(M),,$(MUTH_$(X)))
	@if $(OUT)/red_$(X)_$(M)/rpc_size_test > $(OUT)/red_$(X)_$(M)/log 2>&1; then echo "red $(X) $(M): the cases PASSED against the model"; exit 1; \
	 else echo "red $(X) $(M): $$(grep -c '^FAIL' $(OUT)/red_$(X)_$(M)/log) check(s) fail, as required"; fi
