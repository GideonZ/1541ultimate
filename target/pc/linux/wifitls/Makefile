# Host build of the checks on the TLS calls to the WiFi module, on the channel they use (wifi_dedicated.cc)
# and on the network target's TLS commands; FreeRTOS, the UART, lwIP and the module are stand-ins.
#   make (build), make test (build and run)
RESULT  = result
OUTPUT  = output

PATH_SW = ../../../../software
TEST    = $(PATH_SW)/test/wifi_tls

VPATH   = $(TEST)/stub $(TEST) $(PATH_SW)/io/wifi $(PATH_SW)/io/uart $(PATH_SW)/wifi/raw_u64/main $(PATH_SW)/wifi/tls

CXX     = g++
CPPOPTIONS = -std=gnu++11 -g -O0 -Wall -Wextra -Wno-unused-parameter -DNIOS=1 -DU64=1 \
           $(addprefix -I, $(VPATH))

TLS_SRCS   = wifi_tls_test.cc world.cc wifi_tls.cc wifi_dedicated.cc
TLS_OBJS   = $(addprefix $(OUTPUT)/tls/, $(TLS_SRCS:%.cc=%.o))

# The network target's sources, which a red run can point at another tree.
NET_DIR  = $(PATH_SW)/io/network
NET_PATH = $(TEST)/netstub $(TEST)/stub $(NET_DIR) $(PATH_SW)/io/wifi $(PATH_SW)/io/command_interface
NET_OPTIONS = -std=gnu++11 -g -O0 -Wall -Wextra -Wno-unused-parameter -Wno-deprecated-declarations -DNIOS=1 -DU64=1 $(addprefix -I, $(NET_PATH))
NET_SRCS = net_tls_test.cc net_world.cc ip4addr_aton.cc
NET_OBJS = $(addprefix $(OUTPUT)/net/, $(NET_SRCS:%.cc=%.o)) $(OUTPUT)/net/network_target.o $(OUTPUT)/net/network_target_tls.o

.PHONY: all test clean

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

test: all
	@$(RESULT)/wifitls
	@$(RESULT)/nettls

clean:
	@rm -rf $(OUTPUT) $(RESULT)
