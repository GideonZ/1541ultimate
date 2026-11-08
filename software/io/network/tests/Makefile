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

.PHONY: all lwip-objs eth-tx-frame eth-tx-frame-red fw-hook-opts route-select route-select-red sys-arch sys-arch-red net-counters net-counters-red uart-isr uart-isr-red clean

all: eth-tx-frame eth-tx-frame-red route-select route-select-red sys-arch sys-arch-red net-counters net-counters-red uart-isr uart-isr-red

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

# The real io/uart/dma_uart.cc and cmd_buffer.c, with the register block of dma_uart.h replaced by the
# proxies of uart_host/dma_uart_regs.h (see dma_uart_isr_test.cpp). The generated header has to sit
# beside the copy of dma_uart.cc, so that the quoted include finds it before the real one.
UART_SRC := ../../uart
UART_GEN := $(OUTPUT_DIR)/uart
UART_ISR_TEST := $(OUTPUT_DIR)/dmaUartIsrTest
UART_INC := -DNIOS=1 -Iuart_host -I$(UART_SRC) -I../../../network
UART_REGS_AWK := '/^typedef struct _dma_uart_t/ { print "\#include \"dma_uart_regs.h\""; skip = 1 } !skip { print } /^} dma_uart_t;/ { skip = 0 }'

# usage: $(call uart_isr_build,<dir holding dma_uart.cc>,<test binary>)
define uart_isr_build
	awk $(UART_REGS_AWK) $(UART_SRC)/dma_uart.h > $(1)/dma_uart.h
	grep -q dma_uart_regs.h $(1)/dma_uart.h
	$(CXX) -std=c++14 -g -w -I$(1) $(UART_INC) -c $(1)/dma_uart.cc -o $(1)/dma_uart.o
	$(CC) -g -w $(UART_INC) -c $(UART_SRC)/cmd_buffer.c -o $(1)/cmd_buffer.o
	$(CXX) $(CXXFLAGS) -Wno-unused-variable -I$(1) $(UART_INC) dma_uart_isr_test.cpp ../../usb/tests/host_test_main.cpp $(1)/dma_uart.o $(1)/cmd_buffer.o -lpthread -o $(2)
endef

uart-isr:
	mkdir -p $(UART_GEN)
	cp $(UART_SRC)/dma_uart.cc $(UART_GEN)/dma_uart.cc
	$(call uart_isr_build,$(UART_GEN),$(UART_ISR_TEST))
	$(UART_ISR_TEST)

# dma_uart.cc with one of the counters removed, as the models of the red run: each has to fail the suite.
UART_ISR_MUTANTS := no-long-count no-overflow-count no-overflow-clear
mut_u_no-long-count := s/net_counters.uart_rx_long++;//
mut_u_no-overflow-count := s/net_counters.uart_rx_overflow++;//
mut_u_no-overflow-clear := s/u->uart->ictrl = DMAUART_CLR_OVERFLOW;//

define UART_ISR_MUTANT
uart-isr-red-$(1):
	mkdir -p $$(OUTPUT_DIR)/uart_$(1)
	sed '$$(mut_u_$(1))' $$(UART_SRC)/dma_uart.cc > $$(OUTPUT_DIR)/uart_$(1)/dma_uart.cc
	@if cmp -s $$(UART_SRC)/dma_uart.cc $$(OUTPUT_DIR)/uart_$(1)/dma_uart.cc; then echo "uart-isr-red-$(1): the model changes nothing"; exit 1; fi
	$$(call uart_isr_build,$$(OUTPUT_DIR)/uart_$(1),$$(OUTPUT_DIR)/dmaUartIsrTest_$(1))
	@if $$(OUTPUT_DIR)/dmaUartIsrTest_$(1) > $$(OUTPUT_DIR)/uart-isr-red-$(1).log 2>&1; then \
		echo "uart-isr-red-$(1): the suite passed against the model"; exit 1; \
	else \
		echo "uart-isr-red-$(1): killed ($$$$(grep -c FAIL $$(OUTPUT_DIR)/uart-isr-red-$(1).log) checks failed), as required"; \
	fi
endef
$(foreach m,$(UART_ISR_MUTANTS),$(eval $(call UART_ISR_MUTANT,$(m))))
uart-isr-red: $(addprefix uart-isr-red-,$(UART_ISR_MUTANTS))

NET_COUNTERS_TEST := $(OUTPUT_DIR)/netCountersTest
NET_COUNTERS_H := ../../../network/net_counters.h

net-counters:
	mkdir -p $(OUTPUT_DIR)
	$(CXX) $(CXXFLAGS) -I../../../network net_counters_test.cpp ../../usb/tests/host_test_main.cpp -lpthread -o $(NET_COUNTERS_TEST) && $(NET_COUNTERS_TEST)

# The histogram helpers with one boundary or update changed; each model has to fail the suite.
NET_COUNTERS_MUTANTS := bound-10 bound-100 bound-1 timeout-bucket no-max
mut_h_bound-10 := s/ticks < 10)/ticks <= 10)/
mut_h_bound-100 := s/ticks < 100)/ticks <= 100)/
mut_h_bound-1 := s/ticks < 2)/ticks < 1)/
mut_h_timeout-bucket := s/: NET_WTX_TIMEOUT\]/: 4]/
mut_h_no-max := s/net_counters.wifi_tx_wait_max = ticks;//

define NET_COUNTERS_MUTANT
net-counters-red-$(1):
	mkdir -p $$(OUTPUT_DIR)/nc_$(1)
	sed '$$(mut_h_$(1))' $$(NET_COUNTERS_H) > $$(OUTPUT_DIR)/nc_$(1)/net_counters.h
	@if cmp -s $$(NET_COUNTERS_H) $$(OUTPUT_DIR)/nc_$(1)/net_counters.h; then echo "net-counters-red-$(1): the model changes nothing"; exit 1; fi
	$$(CXX) $$(CXXFLAGS) -I$$(OUTPUT_DIR)/nc_$(1) net_counters_test.cpp ../../usb/tests/host_test_main.cpp -lpthread -o $$(OUTPUT_DIR)/netCountersTest_$(1)
	@if $$(OUTPUT_DIR)/netCountersTest_$(1) > $$(OUTPUT_DIR)/nc_$(1).log 2>&1; then \
		echo "net-counters-red-$(1): the suite passed against the model"; exit 1; \
	else \
		echo "net-counters-red-$(1): killed ($$$$(grep -c FAIL $$(OUTPUT_DIR)/nc_$(1).log) checks failed), as required"; \
	fi
endef
$(foreach m,$(NET_COUNTERS_MUTANTS),$(eval $(call NET_COUNTERS_MUTANT,$(m))))
net-counters-red: $(addprefix net-counters-red-,$(NET_COUNTERS_MUTANTS))

# The NETCTR reporter of network_interface.cc (the real text from the "Bench builds only" comment to the InitFunction line,
# CRs dropped) against FreeRTOS/lwIP stand-ins: net_report_test.cpp as it is is the production
# configuration (the default of net_counters.h: no reporter task, nothing printed), with
# -DNET_COUNTERS_REPORT=1 it is the bench configuration (one task, at most one line per 5 s round).
NET_REPORT_DIR := $(OUTPUT_DIR)/net_report
NET_REPORT_SRC := ../network_interface.cc
define net_report_run
	mkdir -p $(NET_REPORT_DIR)
	tr -d '\r' < $(1) | awk '/^\/\/ Bench builds only \(NET_COUNTERS_REPORT/ {on=1} /^InitFunction lwIP_initializer/ {on=0} on' > $(NET_REPORT_DIR)/$(2).inc
	test -s $(NET_REPORT_DIR)/$(2).inc
	$(CXX) $(CXXFLAGS) $(3) -I../../../network -DNETWORK_INTERFACE_REPORT_INC='"$(2).inc"' -I$(NET_REPORT_DIR) -Wno-unused-function -Wno-unused-parameter net_report_test.cpp ../../usb/tests/host_test_main.cpp -lpthread -o $(NET_REPORT_DIR)/$(2)Test
	$(NET_REPORT_DIR)/$(2)Test
endef

net-report:
	$(call net_report_run,$(NET_REPORT_SRC),prod,-DNET_REPORT_EXPECT_DEFAULT_OFF)
	$(call net_report_run,$(NET_REPORT_SRC),bench,-DNET_COUNTERS_REPORT=1)

# Red: the same suite against network_interface.cc without the #if NET_COUNTERS_REPORT lines (and their #endif) (that is how the
# reporter was built before this change), and against a default of 1 in the header. Each has to fail the production run.
net-report-red-no-if:
	mkdir -p $(NET_REPORT_DIR)
	tr -d '\r' < $(NET_REPORT_SRC) | awk '/^#if NET_COUNTERS_REPORT/ {d=1; next} d && /^#if/ {d++} d && /^#endif/ {d--; if (d==0) next} 1' > $(NET_REPORT_DIR)/ni_no-if.cc
	@if cmp -s $(NET_REPORT_SRC) $(NET_REPORT_DIR)/ni_no-if.cc; then echo "net-report-red-no-if: the model changes nothing"; exit 1; fi
	@if $(MAKE) --no-print-directory -s net-report-run-prod SRC=$(NET_REPORT_DIR)/ni_no-if.cc > $(NET_REPORT_DIR)/red-no-if.log 2>&1; then \
		echo "net-report-red-no-if: the suite passed against the model"; exit 1; \
	else \
		echo "net-report-red-no-if: killed ($$(grep -c FAIL $(NET_REPORT_DIR)/red-no-if.log) checks failed), as required"; \
	fi
net-report-red-default-on:
	mkdir -p $(NET_REPORT_DIR)/hdr
	sed 's/define NET_COUNTERS_REPORT 0 /define NET_COUNTERS_REPORT 1 /' $(NET_COUNTERS_H) > $(NET_REPORT_DIR)/hdr/net_counters.h
	@if cmp -s $(NET_COUNTERS_H) $(NET_REPORT_DIR)/hdr/net_counters.h; then echo "net-report-red-default-on: the model changes nothing"; exit 1; fi
	@if $(MAKE) --no-print-directory -s net-report-run-prod SRC=$(NET_REPORT_SRC) HDR=$(NET_REPORT_DIR)/hdr > $(NET_REPORT_DIR)/red-default-on.log 2>&1; then \
		echo "net-report-red-default-on: the suite passed against the model"; exit 1; \
	else \
		echo "net-report-red-default-on: killed ($$(grep -c FAIL $(NET_REPORT_DIR)/red-default-on.log) checks failed), as required"; \
	fi
net-report-run-prod:
	$(call net_report_run,$(SRC),prod_red,-DNET_REPORT_EXPECT_DEFAULT_OFF $(if $(HDR),-I$(HDR),))
net-report-red: net-report-red-no-if net-report-red-default-on
.PHONY: net-report net-report-red net-report-red-no-if net-report-red-default-on net-report-run-prod
all: net-report net-report-red

sys-arch:
	mkdir -p $(OUTPUT_DIR)
	$(CC) -g -w $(SYS_ARCH_INC) -c $(SYS_ARCH_C) -o $(OUTPUT_DIR)/sys_arch.o
	$(CXX) $(CXXFLAGS) $(SYS_ARCH_INC) -I$(OUTPUT_DIR) sys_arch_test.cpp ../../usb/tests/host_test_main.cpp $(OUTPUT_DIR)/sys_arch.o -lpthread -o $(SYS_ARCH_TEST) && $(SYS_ARCH_TEST)

# The same cases against sys_arch.c with the waits truncated to whole ticks, as they were.
# It has to fail, as for eth-tx-frame-red.
sys-arch-red:
	mkdir -p $(OUTPUT_DIR)
	sed 's/ms_to_ticks( \([A-Za-z]*\) )/\1 \/ portTICK_PERIOD_MS/' $(SYS_ARCH_C) > $(OUTPUT_DIR)/sys_arch_legacy.c
	@if cmp -s $(SYS_ARCH_C) $(OUTPUT_DIR)/sys_arch_legacy.c; then echo "sys-arch-red: the model changes nothing"; exit 1; fi
	$(CC) -g -w $(SYS_ARCH_INC) -c $(OUTPUT_DIR)/sys_arch_legacy.c -o $(OUTPUT_DIR)/sys_arch_legacy.o
	$(CXX) $(CXXFLAGS) $(SYS_ARCH_INC) -I$(OUTPUT_DIR) sys_arch_test.cpp ../../usb/tests/host_test_main.cpp $(OUTPUT_DIR)/sys_arch_legacy.o -lpthread -o $(SYS_ARCH_RED)
	@if $(SYS_ARCH_RED) > $(OUTPUT_DIR)/sys-arch-red.log 2>&1; then \
		echo "sys-arch-red: the suite passed against the pre-fix behaviour -- it no longer catches the defect"; \
		exit 1; \
	else \
		echo "sys-arch-red: $$(tail -1 $(OUTPUT_DIR)/sys-arch-red.log) against the pre-fix behaviour, as required"; \
	fi

clean:
	rm -rf $(OUTPUT_DIR)
