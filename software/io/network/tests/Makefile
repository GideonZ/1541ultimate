CXX ?= g++
CXXFLAGS := -std=c++14 -g -Wall -Wextra -I. -I.. -I../../usb/tests
LWIP_INC := -Ilwip_host -I../../../lwip/src/include
OUTPUT_DIR := output
FW_CONFIG := ../../../network/config
ROUTE_INC := -I$(OUTPUT_DIR) -I$(FW_CONFIG)
ETH_TX_FRAME_TEST := $(OUTPUT_DIR)/ethTxFrameTest
ETH_TX_FRAME_RED := $(OUTPUT_DIR)/ethTxFrameTestLegacy
ROUTE_SELECT_TEST := $(OUTPUT_DIR)/routeSelectTest
ROUTE_SELECT_RED := $(OUTPUT_DIR)/routeSelectTestLegacy
REF_POOL_TEST := $(OUTPUT_DIR)/refPoolTest
REF_POOL_RED := $(OUTPUT_DIR)/refPoolTestLegacy
LWIP_SRC := ../../../lwip/src
# The lwIP core the route tests run against: ip4.c (the routing and the hook dispatch),
# udp.c (the source checks on send), netif.c, ip4_addr.c and what they pull in.
LWIP_C := core/init.c core/def.c core/inet_chksum.c core/ip.c core/mem.c core/memp.c \
	core/netif.c core/pbuf.c core/stats.c core/sys.c core/timeouts.c core/udp.c \
	core/ipv4/ip4.c core/ipv4/ip4_addr.c core/ipv4/ip4_frag.c core/ipv4/icmp.c
LWIP_CFLAGS := -g -w $(LWIP_INC) $(ROUTE_INC)
# The pool test: lwIP's pbuf.c and memp.c with static pools, as on the device
POOL_INC := -Ilwip_refpool -I../../../lwip/src/include -I$(OUTPUT_DIR)
POOL_C := core/init.c core/def.c core/inet_chksum.c core/ip.c core/mem.c core/memp.c \
	core/netif.c core/pbuf.c core/stats.c core/sys.c core/timeouts.c core/udp.c \
	core/ipv4/ip4.c core/ipv4/ip4_addr.c core/ipv4/ip4_frag.c

.PHONY: all lwip-objs eth-tx-frame eth-tx-frame-red fw-hook-opts route-select route-select-red fw-pool-opts ref-pool ref-pool-red clean

all: eth-tx-frame eth-tx-frame-red route-select route-select-red ref-pool ref-pool-red

eth-tx-frame:
	mkdir -p $(OUTPUT_DIR)
	$(CXX) $(CXXFLAGS) eth_tx_frame_test.cpp ../../usb/tests/host_test_main.cpp -lpthread -o $(ETH_TX_FRAME_TEST) && $(ETH_TX_FRAME_TEST)

# The same suite against a model of the two drivers as they were before the fix.
# It has to fail: this target succeeds only when it does, so a later change that
# guts the checks is caught here rather than passing quietly.
eth-tx-frame-red:
	mkdir -p $(OUTPUT_DIR)
	$(CXX) $(CXXFLAGS) -DETH_TX_FRAME_LEGACY eth_tx_frame_test.cpp ../../usb/tests/host_test_main.cpp -lpthread -o $(ETH_TX_FRAME_RED)
	@if $(ETH_TX_FRAME_RED) > $(OUTPUT_DIR)/red.log 2>&1; then \
		echo "eth-tx-frame-red: the suite passed against the pre-fix behaviour -- it no longer catches the defect"; \
		exit 1; \
	else \
		echo "eth-tx-frame-red: $$(tail -1 $(OUTPUT_DIR)/red.log) against the pre-fix behaviour, as required"; \
	fi

# The hook lines of the firmware's lwipopts.h (which cannot be included on the host), so
# that the tests stop building when the firmware stops configuring the hook.
fw-hook-opts:
	mkdir -p $(OUTPUT_DIR)
	tr -d '\r' < $(FW_CONFIG)/lwipopts.h | grep '^#define LWIP_HOOK' > $(OUTPUT_DIR)/fw_hook_opts.h || true

# lwIP's own sources, with the firmware's hook, and without it for the pre-fix build.
lwip-objs: fw-hook-opts
	mkdir -p $(OUTPUT_DIR)/lwip $(OUTPUT_DIR)/lwip-legacy
	for f in $(LWIP_C); do \
		$(CC) $(LWIP_CFLAGS) -c $(LWIP_SRC)/$$f -o $(OUTPUT_DIR)/lwip/$$(basename $$f .c).o || exit 1; \
		$(CC) $(LWIP_CFLAGS) -DROUTE_SELECT_LEGACY -c $(LWIP_SRC)/$$f -o $(OUTPUT_DIR)/lwip-legacy/$$(basename $$f .c).o || exit 1; \
	done

route-select: lwip-objs
	$(CXX) $(CXXFLAGS) $(LWIP_INC) $(ROUTE_INC) route_select_test.cpp ../../usb/tests/host_test_main.cpp $(OUTPUT_DIR)/lwip/*.o -lpthread -o $(ROUTE_SELECT_TEST) && $(ROUTE_SELECT_TEST)

# The same cases without the hook, which is lwIP's lookup as it was. It has to fail,
# as for eth-tx-frame-red.
route-select-red: lwip-objs
	$(CXX) $(CXXFLAGS) $(LWIP_INC) $(ROUTE_INC) -DROUTE_SELECT_LEGACY route_select_test.cpp ../../usb/tests/host_test_main.cpp $(OUTPUT_DIR)/lwip-legacy/*.o -lpthread -o $(ROUTE_SELECT_RED)
	@if $(ROUTE_SELECT_RED) > $(OUTPUT_DIR)/route-red.log 2>&1; then \
		echo "route-select-red: the suite passed against the pre-fix behaviour -- it no longer catches the defect"; \
		exit 1; \
	else \
		echo "route-select-red: $$(tail -1 $(OUTPUT_DIR)/route-red.log) against the pre-fix behaviour, as required"; \
	fi

# The firmware's MEMP_NUM_PBUF, for the pool test; the build stops if the file no longer
# sets it
fw-pool-opts:
	mkdir -p $(OUTPUT_DIR)
	tr -d '\r' < $(FW_CONFIG)/lwipopts.h | grep '^#define MEMP_NUM_PBUF ' > $(OUTPUT_DIR)/fw_pool_opts.h

ref-pool: fw-pool-opts
	mkdir -p $(OUTPUT_DIR)/lwip-pool
	for f in $(POOL_C); do \
		$(CC) -g -w $(POOL_INC) -c $(LWIP_SRC)/$$f -o $(OUTPUT_DIR)/lwip-pool/$$(basename $$f .c).o || exit 1; \
	done
	$(CXX) $(CXXFLAGS) $(POOL_INC) refpool_test.cpp ../../usb/tests/host_test_main.cpp $(OUTPUT_DIR)/lwip-pool/*.o -lpthread -o $(REF_POOL_TEST) && $(REF_POOL_TEST)

# The same cases with the single entry the firmware had. It has to fail, as for
# eth-tx-frame-red.
ref-pool-red:
	mkdir -p $(OUTPUT_DIR)/lwip-pool-legacy
	for f in $(POOL_C); do \
		$(CC) -g -w -DREF_POOL_LEGACY $(POOL_INC) -c $(LWIP_SRC)/$$f -o $(OUTPUT_DIR)/lwip-pool-legacy/$$(basename $$f .c).o || exit 1; \
	done
	$(CXX) $(CXXFLAGS) -DREF_POOL_LEGACY $(POOL_INC) refpool_test.cpp ../../usb/tests/host_test_main.cpp $(OUTPUT_DIR)/lwip-pool-legacy/*.o -lpthread -o $(REF_POOL_RED)
	@if $(REF_POOL_RED) > $(OUTPUT_DIR)/pool-red.log 2>&1; then \
		echo "ref-pool-red: the suite passed with a single reference pbuf -- it no longer catches the defect"; \
		exit 1; \
	else \
		echo "ref-pool-red: $$(tail -1 $(OUTPUT_DIR)/pool-red.log) with a single reference pbuf, as required"; \
	fi

clean:
	rm -rf $(OUTPUT_DIR)
