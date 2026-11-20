# Host build of the checks on the TLS calls to the WiFi module, on the channel they use (wifi_dedicated.cc)
# and on the network target's TLS commands; FreeRTOS, the UART, lwIP and the module are stand-ins.
#   make (build), make test (build and run)
# U64=2 builds the same checks with the compile flags of the Ultimate 64 Elite II and the C64 Ultimate target (-DU64=2
# -DRISCV=1, no Nios) and against that product's module header, software/u64ctrl/main/rpc_calls.h (the U64=1 pass
# uses raw_u64's). It tests the NET_TLS gate and the network target above the module.
# NIOS=0 with U64=1 is the RISC-V Ultimate 64 target (-DU64=1 -DRISCV=1): it carries no WiFi module code, so only the
# gate is checked there, and it must say NET_TLS is off.
U64     ?= 1
NIOS    ?= 1
ifeq ($(U64)$(NIOS),11)
NIOS_OPT = -DNIOS=1
SUFFIX   =
MODULE   = wifi/raw_u64/main
EXPECT_TLS = 1
else
NIOS_OPT = -DRISCV=1
SUFFIX   = /u64-$(U64)-nios$(NIOS)
MODULE   = u64ctrl/main
EXPECT_TLS = $(if $(filter 2,$(U64)),1,0)
endif
# The other passes work under the ignored result/ and output/
RESULT  = result$(SUFFIX)
OUTPUT  = output$(SUFFIX)

PATH_SW = ../../../../software
TEST    = $(PATH_SW)/test/wifi_tls

VPATH   = $(TEST)/stub $(TEST) $(PATH_SW)/io/wifi $(PATH_SW)/io/uart $(PATH_SW)/$(MODULE) $(PATH_SW)/wifi/common $(PATH_SW)/wifi/tls

CXX     = g++
CPPOPTIONS = -std=gnu++11 -g -O0 -Wall -Wextra -Wno-unused-parameter $(NIOS_OPT) -DU64=$(U64) \
           $(addprefix -I, $(VPATH))

TLS_SRCS   = wifi_tls_test.cc world.cc wifi_tls.cc wifi_dedicated.cc wifi_random.cc
TLS_OBJS   = $(addprefix $(OUTPUT)/tls/, $(TLS_SRCS:%.cc=%.o))

# The network target's sources, which a red run can point at another tree.
NET_DIR  = $(PATH_SW)/io/network
NET_PATH = $(TEST)/netstub $(TEST)/stub $(NET_DIR) $(PATH_SW)/io/wifi $(PATH_SW)/io/command_interface $(PATH_SW)/network
NET_OPTIONS = -std=gnu++11 -g -O0 -Wall -Wextra -Wno-unused-parameter -Wno-deprecated-declarations $(NIOS_OPT) -DU64=$(U64) $(addprefix -I, $(NET_PATH))
NET_SRCS = net_tls_test.cc net_world.cc ip4addr_aton.cc
NET_OBJS = $(addprefix $(OUTPUT)/net/, $(NET_SRCS:%.cc=%.o)) $(OUTPUT)/net/network_target.o $(OUTPUT)/net/network_target_tls.o $(OUTPUT)/net/wifi_random.o

.PHONY: all test clean gate

all: $(RESULT)/wifitls $(RESULT)/nettls

$(OUTPUT)/tls/%.o: %.cc
	@mkdir -p $(OUTPUT)/tls
	$(CXX) $(CPPOPTIONS) -c -o $@ $<

# wifi_dedicated.cc includes "wifi_cmd.h", which the compiler looks for next to
# the source before anywhere else. A copy compiled from here finds the
# stand-in instead; it is the same file, byte for byte.
$(OUTPUT)/tls/wifi_dedicated.o: $(PATH_SW)/io/wifi/wifi_dedicated.cc
	@mkdir -p $(OUTPUT)/tls
	cp $< $(OUTPUT)/tls/wifi_dedicated.cc
	$(CXX) $(CPPOPTIONS) -c -o $@ $(OUTPUT)/tls/wifi_dedicated.cc

$(OUTPUT)/net/wifi_random.o: $(PATH_SW)/io/wifi/wifi_random.cc
	@mkdir -p $(OUTPUT)/net
	$(CXX) $(NET_OPTIONS) $(addprefix -I, $(PATH_SW)/$(MODULE) $(PATH_SW)/wifi/common $(PATH_SW)/wifi/tls $(PATH_SW)/io/uart) -c -o $@ $<

$(OUTPUT)/net/%.o: $(TEST)/%.cc
	@mkdir -p $(OUTPUT)/net
	$(CXX) $(NET_OPTIONS) -c -o $@ $<

# Compiled from a copy, so that its includes find the stand-ins rather than
# the headers next to it (network_interface.h).
$(OUTPUT)/net/network_target.o $(OUTPUT)/net/network_target_tls.o: $(OUTPUT)/net/%.o: $(NET_DIR)/%.cc $(NET_DIR)/network_target.h
	@mkdir -p $(OUTPUT)/net
	cp $< $(OUTPUT)/net/$*.cc
	$(CXX) $(NET_OPTIONS) -c -o $@ $(OUTPUT)/net/$*.cc

$(RESULT)/wifitls: $(TLS_OBJS)
	@mkdir -p $(RESULT)
	$(CXX) -o $@ $(TLS_OBJS)

$(RESULT)/nettls: $(NET_OBJS)
	@mkdir -p $(RESULT)
	$(CXX) -o $@ $(NET_OBJS)

# NET_TLS must be what this target has: on for the Nios II U64 and the U64-II, off for the RISC-V U64.
gate:
	$(CXX) $(NET_OPTIONS) -DEXPECT_NET_TLS=$(EXPECT_TLS) -fsyntax-only $(TEST)/net_gate_test.cc

test: all gate
	@$(RESULT)/wifitls
	@$(RESULT)/nettls
ifeq ($(U64)$(NIOS),11)
	@$(MAKE) U64=2 test
	@$(MAKE) U64=1 NIOS=0 gate
endif

clean:
	@rm -rf output result
