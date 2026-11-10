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
SEND_ALL_TEST := $(OUTPUT_DIR)/sendAllTest
SEND_ALL_RED := $(OUTPUT_DIR)/sendAllTestLegacy
NETWORK_TARGET_TEST := $(OUTPUT_DIR)/networkTargetTest
# The network target with the host's sockets for lwIP's, and the UCI host stand-ins.
NETWORK_TARGET_FLAGS := -std=c++11 -g -Wall -DRUNS_ON_PC -include target3_host/network_interface.h -Itarget3_host -I../../command_interface/tests/host_uci \
	-I../../command_interface/tests/host_rtos -I.. -I../../command_interface -I../../../network \
	-fsanitize=address,undefined
REF_POOL_TEST := $(OUTPUT_DIR)/refPoolTest
REF_POOL_RED := $(OUTPUT_DIR)/refPoolTestLegacy
SYS_ARCH_TEST := $(OUTPUT_DIR)/sysArchTest
SYS_ARCH_RED := $(OUTPUT_DIR)/sysArchTestLegacy
# network/sys_arch.c as it is, against the FreeRTOS stand-ins of sys_arch_host/
SYS_ARCH_C := ../../../network/sys_arch.c
SYS_ARCH_INC := -Isys_arch_host -I../../../lwip/src/include -I../../../network/config
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

.PHONY: all lwip-objs eth-tx-frame eth-tx-frame-red fw-hook-opts route-select route-select-red fw-pool-opts ref-pool ref-pool-red sys-arch sys-arch-red send-all send-all-red network-target clean

all: eth-tx-frame eth-tx-frame-red route-select route-select-red ref-pool ref-pool-red sys-arch sys-arch-red send-all send-all-red network-target

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

sys-arch:
	mkdir -p $(OUTPUT_DIR)
	$(CC) -g -w $(SYS_ARCH_INC) -c $(SYS_ARCH_C) -o $(OUTPUT_DIR)/sys_arch.o
	$(CXX) $(CXXFLAGS) $(SYS_ARCH_INC) sys_arch_test.cpp ../../usb/tests/host_test_main.cpp $(OUTPUT_DIR)/sys_arch.o -lpthread -o $(SYS_ARCH_TEST) && $(SYS_ARCH_TEST)

# The same cases against sys_arch.c with the waits truncated to whole ticks, as they were.
# It has to fail, as for eth-tx-frame-red.
sys-arch-red:
	mkdir -p $(OUTPUT_DIR)
	sed 's/ms_to_ticks( \([A-Za-z]*\) )/\1 \/ portTICK_PERIOD_MS/' $(SYS_ARCH_C) > $(OUTPUT_DIR)/sys_arch_legacy.c
	@if cmp -s $(SYS_ARCH_C) $(OUTPUT_DIR)/sys_arch_legacy.c; then echo "sys-arch-red: the model changes nothing"; exit 1; fi
	$(CC) -g -w $(SYS_ARCH_INC) -c $(OUTPUT_DIR)/sys_arch_legacy.c -o $(OUTPUT_DIR)/sys_arch_legacy.o
	$(CXX) $(CXXFLAGS) $(SYS_ARCH_INC) sys_arch_test.cpp ../../usb/tests/host_test_main.cpp $(OUTPUT_DIR)/sys_arch_legacy.o -lpthread -o $(SYS_ARCH_RED)
	@if $(SYS_ARCH_RED) > $(OUTPUT_DIR)/sys-arch-red.log 2>&1; then \
		echo "sys-arch-red: the suite passed against the pre-fix behaviour -- it no longer catches the defect"; \
		exit 1; \
	else \
		echo "sys-arch-red: $$(tail -1 $(OUTPUT_DIR)/sys-arch-red.log) against the pre-fix behaviour, as required"; \
	fi

send-all:
	mkdir -p $(OUTPUT_DIR)
	$(CXX) $(CXXFLAGS) send_all_test.cpp ../../usb/tests/host_test_main.cpp -lpthread -o $(SEND_ALL_TEST) && $(SEND_ALL_TEST)

# The same suite against the send loop as it was. It has to fail, as for eth-tx-frame-red.
send-all-red:
	mkdir -p $(OUTPUT_DIR)
	$(CXX) $(CXXFLAGS) -DSEND_ALL_LEGACY send_all_test.cpp ../../usb/tests/host_test_main.cpp -lpthread -o $(SEND_ALL_RED)
	@if $(SEND_ALL_RED) > $(OUTPUT_DIR)/send-all-red.log 2>&1; then \
		echo "send-all-red: the suite passed against the pre-fix behaviour -- it no longer catches the defect"; \
		exit 1; \
	else \
		echo "send-all-red: $$(tail -1 $(OUTPUT_DIR)/send-all-red.log) against the pre-fix behaviour, as required"; \
	fi

# Target 3 on its worker task, against loopback peers.
network-target:
	mkdir -p $(OUTPUT_DIR)
	$(CXX) $(NETWORK_TARGET_FLAGS) network_target_test.cpp ../network_target.cc -lpthread -o $(NETWORK_TARGET_TEST) \
	    -Wl,--wrap=gethostbyname_r -Wl,--wrap=send
	env -u LD_PRELOAD timeout 60s $(NETWORK_TARGET_TEST)

clean:
	rm -rf $(OUTPUT_DIR)
