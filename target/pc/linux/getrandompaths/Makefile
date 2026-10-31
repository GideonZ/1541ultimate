#
# Host tests of the paths GET_RANDOM takes on the Ultimate and the control module that no
# other host test compiles. Each is cut out of its file as it is and built against
# stand-ins (software/test/get_random_paths):
#   net_target  NetworkTarget::get_random(), the GET_RANDOM case of parse_command() and the
#               status messages, with wifi_random.cc
#   isr         wifi_rx_isr() from wifi_cmd.cc and DmaUART::ClearRxBuffer() from dma_uart.cc
#   voltages    cmd_get_voltages() from the u64ctrl module's rpc_dispatch.c
#
#   make test               build and run all three
#   make SRC=<dir> test     take the files from a copy of software/ (a mutant)
#
PATH_SW = ../../../../software
SRC    ?= $(PATH_SW)
TESTDIR = $(PATH_SW)/test/get_random_paths
OUTPUT  = output
RESULT  = result

CXX = g++
CC  = gcc
WARN = -Wall -Wno-unused-parameter -Wno-unused-variable -Wno-unused-function -Wno-format -Wno-unknown-warning-option

# A function from its header line (its start, matched as text) to the first line that is "}"
FN = awk -v f="$(1)" 'index($$0, f "(") == 1 && $$0 !~ /;[[:space:]\r]*$$/ { p = 1 } p { print } p && /^\}/ { p = 0 }' $(2)

.PHONY: all test clean

all: $(RESULT)/net_target $(RESULT)/isr $(RESULT)/voltages

$(OUTPUT)/net.cc: $(SRC)/io/network/network_target.cc $(SRC)/io/command_interface/command_intf.cc
	@mkdir -p $(OUTPUT)
	@{ echo '#include "net_env.h"'; \
	   grep -E '^Message c_(message_empty|status_ok) ' $(SRC)/io/command_interface/command_intf.cc; \
	   grep -E '^Message c_status_' $(SRC)/io/network/network_target.cc; \
	   $(call FN,void NetworkTarget :: get_random,$(SRC)/io/network/network_target.cc); \
	   echo 'void NetworkTarget :: parse_command(Message *command, Message **reply, Message **status)'; \
	   echo '{'; echo '    switch (command->message[1]) {'; \
	   awk '/case NET_CMD_GET_RANDOM:/ { p = 1 } p { print } p && /^            break;\r?$$/ { p = 0 }' $(SRC)/io/network/network_target.cc; \
	   echo '    default:'; echo '        break;'; echo '    }'; echo '}'; } > $@
	@grep -q 'NetworkTarget :: get_random' $@ || { echo "getrandompaths: get_random not found"; exit 1; }
	@grep -q 'case NET_CMD_GET_RANDOM:' $@ || { echo "getrandompaths: the GET_RANDOM case of parse_command not found"; exit 1; }

$(RESULT)/net_target: $(OUTPUT)/net.cc $(TESTDIR)/net_target_test.cc
	@mkdir -p $(RESULT)
	$(CXX) -std=gnu++11 -g -O0 $(WARN) -DU64=1 -I$(TESTDIR)/stub -I$(TESTDIR) -I$(SRC)/io/network -I$(SRC)/io/wifi \
	    -I$(PATH_SW)/test/wifi_random -I$(SRC)/wifi/raw_u64/main -I$(SRC)/wifi/common -I$(SRC)/wifi/tls -I$(SRC)/io/uart \
	    -o $@ $(OUTPUT)/net.cc $(TESTDIR)/net_target_test.cc $(SRC)/io/wifi/wifi_random.cc

$(OUTPUT)/isr.cc: $(SRC)/io/wifi/wifi_cmd.cc $(SRC)/io/uart/dma_uart.cc
	@mkdir -p $(OUTPUT)
	@{ echo '#include "isr_env.h"'; \
	   $(call FN,BaseType_t wifi_rx_isr,$(SRC)/io/wifi/wifi_cmd.cc); \
	   $(call FN,void DmaUART::ClearRxBuffer,$(SRC)/io/uart/dma_uart.cc); } > $@
	@grep -q 'wifi_rx_isr' $@ && grep -q 'ClearRxBuffer' $@ || { echo "getrandompaths: isr functions not found"; exit 1; }

$(RESULT)/isr: $(OUTPUT)/isr.cc $(TESTDIR)/isr_test.cc
	@mkdir -p $(RESULT)
	$(CXX) -std=gnu++11 -g -O0 $(WARN) -DU64=1 -I$(TESTDIR)/stub -I$(TESTDIR) -I$(SRC)/io/wifi -I$(SRC)/wifi/raw_u64/main -I$(SRC)/wifi/common -I$(SRC)/wifi/tls -I$(SRC)/io/uart \
	    -o $@ $(OUTPUT)/isr.cc $(TESTDIR)/isr_test.cc

$(OUTPUT)/voltages_extract.c: $(SRC)/u64ctrl/main/rpc_dispatch.c
	@mkdir -p $(OUTPUT)
	@{ grep -E '^#define UART_CHAN' $<; $(call FN,void cmd_get_voltages,$<); } > $@
	@grep -q 'cmd_get_voltages' $@ || { echo "getrandompaths: cmd_get_voltages not found"; exit 1; }

$(RESULT)/voltages: $(OUTPUT)/voltages_extract.c $(TESTDIR)/voltages_test.c
	@mkdir -p $(RESULT)
	$(CC) -std=gnu11 -g -O0 $(WARN) -I$(OUTPUT) -I$(TESTDIR)/stub -I$(SRC)/u64ctrl/main -I$(SRC)/wifi/common \
	    -o $@ $(TESTDIR)/voltages_test.c

test: all
	@$(RESULT)/net_target && $(RESULT)/isr && $(RESULT)/voltages

clean:
	@rm -rf $(OUTPUT) $(RESULT)
