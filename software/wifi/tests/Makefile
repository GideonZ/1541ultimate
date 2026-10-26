#
# Host tests of the code of the ESP32 WiFi modules (raw_u64 = Ultimate 64, raw_c3 = Ultimate II+L,
# u64ctrl = Elite II / C64 Ultimate): their connector, dispatcher commands, event handler and AP
# selection, and the radio switch (wifi/common). The functions are cut out of each module's
# wifi_modem.c and rpc_dispatch.c as they are (software/io/wifi/tests/extract.awk) and run
# against the stand-ins of ESP-IDF, FreeRTOS and the UART in module_stubs.c. No hardware and no
# ESP-IDF needed.
#
#   make                       all modules, the radio switch, and then the -red runs
#   make test                  all modules and the radio switch
#   make MODULE=u64ctrl test   one module (raw_u64, raw_c3, u64ctrl)
#   make red                   the same cases against a model of the modules as they were before
#                              each fix (the LEGACY_* sed expressions below); every one must
#                              fail (project pattern: software/io/network/tests, eth-tx-frame-red)
#
# The cases are the C++ files picked up by name, so that a case file is added without touching
# this file:  module_*_test.cpp  run against each module,  radio_*test.cpp  against wifi/common.
#
PATH_SW  := ../..
EXTRACT  := awk -f $(PATH_SW)/io/wifi/tests/extract.awk
OUT      := output
ALL      := raw_u64 raw_c3 u64ctrl
MODULE   ?= $(ALL)
CXX      ?= g++
CC       ?= gcc

DIR_raw_u64 := $(PATH_SW)/wifi/raw_u64/main
DIR_raw_c3  := $(PATH_SW)/wifi/raw_c3/main
DIR_u64ctrl := $(PATH_SW)/u64ctrl/main
# The U64 and U2+L modules have the radio switch (radio_boot.h): only they see wifi/common
INC_raw_u64 := -I$(PATH_SW)/wifi/common -I$(PATH_SW)/wifi/tls
INC_raw_c3  := -I$(PATH_SW)/wifi/common
# u64ctrl's rpc_calls.h and wifi_modem.h include rpc_shared.h and radio_entropy.h from there. It has no radio
# switch: module_env.h does not count it as one, although radio_boot.h is on its path now
INC_u64ctrl := -I$(PATH_SW)/wifi/common

HOSTTEST := $(PATH_SW)/io/usb/tests
CASES    := $(wildcard module_*_test.cpp)
RADIO    := $(wildcard radio_*test.cpp)
# radio_entropy.h includes rpc_calls.h of raw_u64, which needs lwIP's headers; the wifi_random test stubs them out
RADIO_INC := -I$(PATH_SW)/wifi/common -I$(PATH_SW)/wifi/tls -I$(PATH_SW)/wifi/raw_u64/main -I$(PATH_SW)/test/wifi_random
COMMON   := -g -O0 -Wall -Wno-unused-parameter -D_FORTIFY_SOURCE=0
CFLAGS   := -std=gnu11 $(COMMON) -Wno-unused-variable -Wno-unused-function
CXXFLAGS := -std=c++14 $(COMMON) -I$(HOSTTEST) -I. -Istub
INCLUDES  = -I. -Istub -I$(DIR_$(M)) -I$(PATH_SW)/io/uart $(INC_$(M))

# What the extracted code of a module is made of ...
FUNCS_MODEM := send_event wifi_event_handler wifi_note_identify wifi_send_connected_event attempt_connect \
               wifi_connect_to_scanned enable_wifi_if_needed wifi_report_disabled handle_connect_command \
               connector_boot_state connect_thread
FUNCS_RPC   := cmd_identify cmd_wifi_connect cmd_wifi_endisable cmd_get_connection
# ... and what every module must have
NEEDED      := send_event wifi_event_handler attempt_connect wifi_connect_to_scanned enable_wifi_if_needed \
               handle_connect_command connect_thread cmd_identify cmd_wifi_connect cmd_wifi_endisable cmd_get_connection

# --- the -red runs: the same cases against a model of the modules as they were before a fix --
#
# One sed expression list per fix and module over the extracted code, which keeps what the fix
# changed and puts back what was there before. Every one has to make the suite FAIL (a failing
# check, not a build error); `make red` prints which checks failed. A fix that a module
# does not have, or whose cases are not in this commit, has no entry.
RED_FIXES := 1 2 4 5 6

# 1  The radio switch (U64, U2+L): no Disable is served, and the radio goes on at boot, as
#    on a module without the switch
LEGACY_1_raw_u64 := -e 's/case CMD_WIFI_DISABLE:/case 0x7F:/' -e 's/return Disabled;/return LastAP;/'
LEGACY_1_raw_c3  := $(LEGACY_1_raw_u64)

# 2  The control module: a Disable or Autoconnect taken in the AP walks is overwritten by the
#    line after the loop, an Enable does not spend an earlier Disconnect, and a Scan in Disabled
#    starts the radio. All three: a Connect only leaves Disabled for the retry, not Connected
LEGACY_CONNECT := -e '/case CMD_WIFI_CONNECT:/,/last_connect = \*cmd;/s/if (err == ESP_OK) {/if ((err == ESP_OK) \&\& (*state == Disabled)) {/'
LEGACY_2_raw_u64 := $(LEGACY_CONNECT)
LEGACY_2_raw_c3  := $(LEGACY_CONNECT)
LEGACY_2_u64ctrl := $(LEGACY_CONNECT) \
    -e 's/if (state == ScannedAPs) {/if (state != Connected) {/' -e 's/if (state == StoredAPs) {/if (state != Connected) {/' \
    -e '/^ *user_disconnected = false;$$/{' -e 'N' -e '/ESP_LOGI(TAG, "Starting WiFi.")/s/^.*\n//' -e '}' \
    -e '/if (\*state == Disabled) {$$/{' -e 'N' -e '/a scan does not turn it on/s/(\*state == Disabled)/(0)/' -e '}'

# 4  The first AP of the SSID that answers is joined, not the strongest
LEGACY_NOTSTRONGEST := -e '/sta.scan_method = WIFI_ALL_CHANNEL_SCAN;/d' -e '/sta.sort_method = WIFI_CONNECT_AP_BY_SIGNAL;/d'
LEGACY_4_raw_u64 := $(LEGACY_NOTSTRONGEST)
LEGACY_4_raw_c3  := $(LEGACY_NOTSTRONGEST)
LEGACY_4_u64ctrl := $(LEGACY_NOTSTRONGEST)

# 5  The AP picked from the list (BSSID, channel) is not honoured
LEGACY_NOPICK := -e 's/if (bssid \&\& memcmp(bssid, "\\0\\0\\0\\0\\0\\0", 6)) {/if (0) {/'
LEGACY_5_raw_u64 := $(LEGACY_NOPICK)
LEGACY_5_raw_c3  := $(LEGACY_NOPICK)
LEGACY_5_u64ctrl := $(LEGACY_NOPICK)

# 6  EVENT_DISCONNECTED does not carry the reason
LEGACY_NOREASON := -e 's/->reason = reason;/->reason = 0;/'
LEGACY_6_raw_u64 := $(LEGACY_NOREASON)
LEGACY_6_raw_c3  := $(LEGACY_NOREASON)
LEGACY_6_u64ctrl := $(LEGACY_NOREASON)

LEGACY = $(LEGACY_$(FIX)_$(M))

.PHONY: all test red clean radio

all: test red

test:
	@set -e; for m in $(MODULE); do $(MAKE) --no-print-directory M=$$m run; done
	@$(MAKE) --no-print-directory radio

# --- one module: M=<module> -------------------------------------------------------------

# The module's functions, verbatim, behind module_env.h, and the stubs at the end. The
# typedef of ConnectState_t is taken along with the awk of the three lines it needs.
# RED=<sed expressions> turns the code into a model of it as it was before a fix.
define EXTRACTED
	@mkdir -p $(1)
	@{ echo '#include "module_env.h"'; \
	   awk '/^typedef enum \{/ { t = 1; buf = "" } t { buf = buf $$0 "\n" } t && /\} ConnectState_t;/ { if (buf ~ /LastAP/) printf "%s", buf; t = 0 }' $(DIR_$(M))/wifi_modem.c; \
	   grep -E '^static const char \*states\[\]' $(DIR_$(M))/wifi_modem.c; \
	   for f in $(FUNCS_MODEM); do $(EXTRACT) -v f=$$f $(DIR_$(M))/wifi_modem.c; done; \
	   for f in $(FUNCS_RPC); do $(EXTRACT) -v f=$$f $(DIR_$(M))/rpc_dispatch.c; done; } > $(1)/module_code.in
	@for f in ConnectState_t $(NEEDED); do grep -q "$$f" $(1)/module_code.in || { echo "wifi tests: $$f not found in $(M)"; exit 1; }; done
	$(if $(LEGACY),@sed $(LEGACY) $(1)/module_code.in > $(1)/module_code.red && ! cmp -s $(1)/module_code.red $(1)/module_code.in || { echo "wifi tests: legacy model $(FIX) does not apply to $(M)"; exit 1; })
	$(if $(LEGACY),@mv $(1)/module_code.red $(1)/module_code.in)
	@{ cat $(1)/module_code.in; echo '#include "module_stubs.c"'; for f in $(wildcard module_*_stubs.c); do echo "#include \"$$f\""; done; } > $(1)/module_code.c
endef

define BUILD
	$(CC) $(CFLAGS) $(INCLUDES) -D$(DEF) -c -o $(1)/module_code.o $(1)/module_code.c
	$(CXX) $(CXXFLAGS) -I$(DIR_$(M)) -I$(PATH_SW)/io/uart $(INC_$(M)) -D$(DEF) -DMODULE_NAME='"$(M)"' \
	    -o $(1)/wifi_module_test $(CASES) $(HOSTTEST)/host_test_main.cpp $(1)/module_code.o -lpthread
endef

red:
	@set -e; for fix in $(RED_FIXES); do for m in $(MODULE); do $(MAKE) --no-print-directory M=$$m FIX=$$fix redrun; done; done

redrun: DEF = MODULE_$(shell echo $(M) | tr a-z A-Z)
redrun:
	@:
ifneq ($(LEGACY),)
	$(call EXTRACTED,$(OUT)/red$(FIX)/$(M))
	$(call BUILD,$(OUT)/red$(FIX)/$(M))
	@if $(OUT)/red$(FIX)/$(M)/wifi_module_test > $(OUT)/red$(FIX)/$(M)/log 2>&1; then \
	    echo "red $(FIX) $(M): the suite PASSED against the model before fix $(FIX): it no longer catches it"; exit 1; \
	 else \
	    echo "red $(FIX) $(M): $$(grep -c '^FAIL' $(OUT)/red$(FIX)/$(M)/log) check(s) fail against the model before fix $(FIX), as required:"; \
	    grep -E '^(FAIL|  check)' $(OUT)/red$(FIX)/$(M)/log | grep -v 'check_ok_' | cut -c1-150 | head -6 | sed 's/^/    /'; \
	 fi
endif

run: DEF = MODULE_$(shell echo $(M) | tr a-z A-Z)
run:
	$(call EXTRACTED,$(OUT)/$(M))
	$(call BUILD,$(OUT)/$(M))
	@echo "== $(M)"; $(OUT)/$(M)/wifi_module_test

# --- the radio switch: wifi_radio.c and radio_boot.c against a chip stub ---------------------

radio:
	@mkdir -p $(OUT)/radio
	$(CC) -std=c99 $(COMMON) $(RADIO_INC) -c -o $(OUT)/radio/wifi_radio.o $(PATH_SW)/wifi/common/wifi_radio.c
	$(CC) -std=c99 $(COMMON) -I$(PATH_SW)/wifi/common -c -o $(OUT)/radio/radio_boot.o $(PATH_SW)/wifi/common/radio_boot.c
	$(CXX) $(CXXFLAGS) $(RADIO_INC) -o $(OUT)/radio/wifi_radio_test $(RADIO) $(HOSTTEST)/host_test_main.cpp \
	    $(OUT)/radio/wifi_radio.o $(OUT)/radio/radio_boot.o -lpthread
	@echo "== radio"; $(OUT)/radio/wifi_radio_test

clean:
	@rm -rf $(OUT)
