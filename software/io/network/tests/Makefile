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

.PHONY: all lwip-objs eth-tx-frame eth-tx-frame-red fw-hook-opts route-select route-select-red sys-arch sys-arch-red net-counters net-counters-red uart-isr uart-isr-red wifi-tx-nowait wifi-tx-nowait-red wifi-tx-sim wifi-tx-sim-red clean

all: eth-tx-frame eth-tx-frame-red route-select route-select-red sys-arch sys-arch-red net-counters net-counters-red uart-isr uart-isr-red wifi-tx-nowait wifi-tx-nowait-red wifi-tx-sim wifi-tx-sim-red

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

# wifi_tx_packet never waits, and the TX-done wake (wifi_tx_nowait_test.cpp). The code under test is cut
# out of io/wifi/wifi.cc as it is (extract.awk of io/wifi/tests) and built with the real DmaUART
# (dma_uart.cc, as for uart-isr) and cmd_buffer.c; the model of the pre-fix code is wifi_tx_legacy.inc,
# the others are sed models of the extracted code or of dma_uart.cc, each of which has to fail the suite.
WTX_WIFI_CC := ../../wifi/wifi.cc
WTX_WIFI_CMD_H := ../../wifi/wifi_cmd.h
WTX_EXTRACT := ../../wifi/tests/extract.awk
WTX := $(OUTPUT_DIR)/wtx

# The extracted code in $(WTX)/<dir>: wifi_tx_extract.inc (what the tests include before the packet
# function) and wifi_tx_packet.inc. The build stops if one is not in wifi.cc, if the firmware's reserve
# is not the one the tests assume, or if the request macro lost its wait.
wtx-extract:
	mkdir -p $(WTX)/base
	grep -h -e '^static volatile uint8_t wifi_tx_starved;$$' -e '^static struct tcpip_callback_msg \*wifi_txnow_msg;$$' $(WTX_WIFI_CC) > $(WTX)/base/wifi_tx_extract.inc
	test $$(wc -l < $(WTX)/base/wifi_tx_extract.inc) -eq 2
	for f in wifi_txnow_cb wifi_tx_free_isr wifi_tx_wake_init; do \
		awk -v f=$$f -f $(WTX_EXTRACT) $(WTX_WIFI_CC) > $(WTX)/base/$$f.tmp; \
		test -s $(WTX)/base/$$f.tmp || { echo "wifi_tx: $$f not found in wifi.cc"; exit 1; }; \
		cat $(WTX)/base/$$f.tmp >> $(WTX)/base/wifi_tx_extract.inc; \
	done
	awk -v f=wifi_tx_packet -f $(WTX_EXTRACT) $(WTX_WIFI_CC) > $(WTX)/base/wifi_tx_packet.inc
	test -s $(WTX)/base/wifi_tx_packet.inc
	grep -q '^#define WIFI_TX_RESERVE  *2$$' $(WTX_WIFI_CMD_H)
	grep -q '^#define WIFI_TX_WAKE_HYSTERESIS  *2$$' $(WTX_WIFI_CMD_H)
	grep -q 'GetBuffer(&buf, 1000)' $(WTX_WIFI_CMD_H)
	tr -d '\r' < ../../../network/config/arch/sys_arch.h | grep '^#define ERR_NEED_SCHED' > $(WTX)/base/fw_err_need_sched.h
	test -s $(WTX)/base/fw_err_need_sched.h

# usage: $(call wtx_build,<dir with dma_uart.cc>,<dir with the extracted code>,<test binary>,<defines>)
define wtx_build
	mkdir -p $(1)
	awk $(UART_REGS_AWK) $(UART_SRC)/dma_uart.h > $(1)/dma_uart.h
	grep -q dma_uart_regs.h $(1)/dma_uart.h
	$(CXX) -std=c++14 -g -w -I$(1) $(UART_INC) -c $(1)/dma_uart.cc -o $(1)/dma_uart.o
	$(CC) -g -w $(UART_INC) -c $(UART_SRC)/cmd_buffer.c -o $(1)/cmd_buffer.o
	$(CXX) $(CXXFLAGS) -Wno-unused-variable -Wno-unused-function -Wno-unused-parameter -I$(1) -I$(2) $(UART_INC) $(4) wifi_tx_nowait_test.cpp ../../usb/tests/host_test_main.cpp $(1)/dma_uart.o $(1)/cmd_buffer.o -lpthread -o $(3)
endef

wifi-tx-nowait: wtx-extract
	mkdir -p $(WTX)/uart
	cp $(UART_SRC)/dma_uart.cc $(WTX)/uart/dma_uart.cc
	$(call wtx_build,$(WTX)/uart,$(WTX)/base,$(WTX)/wifiTxNoWaitTest,)
	$(WTX)/wifiTxNoWaitTest

# The pre-fix wifi_tx_packet
wifi-tx-nowait-red-legacy: wtx-extract
	mkdir -p $(WTX)/uart
	cp $(UART_SRC)/dma_uart.cc $(WTX)/uart/dma_uart.cc
	$(call wtx_build,$(WTX)/uart,$(WTX)/base,$(WTX)/wifiTxNoWaitTest_legacy,-DWIFI_TX_LEGACY)
	@if $(WTX)/wifiTxNoWaitTest_legacy > $(WTX)/legacy.log 2>&1; then \
		echo "wifi-tx-nowait-red-legacy: the suite passed against the pre-fix wifi_tx_packet"; exit 1; \
	else \
		echo "wifi-tx-nowait-red-legacy: killed ($$(grep -c FAIL $(WTX)/legacy.log) checks failed), as required"; \
	fi

# Models of the new code with one part taken out. 'wifi' ones change the extracted wifi.cc code, 'uart'
# ones dma_uart.cc.
WTX_MUTANTS_WIFI := no-reserve no-flag no-recheck wake-without-flag wake-at-reserve no-hysteresis hysteresis-at-edge hysteresis-plus-1 no-clear clear-on-failure no-switch no-refused
WTX_MUTANTS_UART := no-hook-call count-not-reserve
mut_w_no-reserve := s/GetBufferKeeping(&buf, WIFI_TX_RESERVE)/GetBuffer(\&buf, 0)/g
mut_w_no-flag := s/^        wifi_tx_starved = 1;//
mut_w_no-recheck := s/^        gotbuf = esp32.uart->GetBufferKeeping(&buf, WIFI_TX_RESERVE);//
mut_w_wake-without-flag := s/if (wifi_tx_starved \&\& wifi_txnow_msg/if (wifi_txnow_msg/
mut_w_wake-at-reserve := s/> WIFI_TX_RESERVE + WIFI_TX_WAKE_HYSTERESIS) {/>= WIFI_TX_RESERVE) {/
mut_w_no-hysteresis := s/> WIFI_TX_RESERVE + WIFI_TX_WAKE_HYSTERESIS) {/> WIFI_TX_RESERVE) {/
mut_w_hysteresis-at-edge := s/> WIFI_TX_RESERVE + WIFI_TX_WAKE_HYSTERESIS) {/>= WIFI_TX_RESERVE + WIFI_TX_WAKE_HYSTERESIS) {/
mut_w_hysteresis-plus-1 := s/> WIFI_TX_RESERVE + WIFI_TX_WAKE_HYSTERESIS) {/> WIFI_TX_RESERVE + WIFI_TX_WAKE_HYSTERESIS + 1) {/
mut_w_no-clear := s/^            wifi_tx_starved = 0;$$//
mut_w_clear-on-failure := s/posted != ERR_MEM/1/
mut_w_no-switch := s/\*w = pdTRUE;//
mut_w_no-refused := s/net_counters.wifi_tx_refused++;//
mut_u_no-hook-call := s/u->isr_tx_free_callback(u->packets, \&HPTaskAwoken);//
mut_u_count-not-reserve := s/<= reserve) {/< reserve) {/

define WTX_MUTANT
wifi-tx-nowait-red-$(2): wtx-extract
	mkdir -p $$(WTX)/m_$(2)
	cp $$(WTX)/base/* $$(WTX)/m_$(2)/
	cp $$(UART_SRC)/dma_uart.cc $$(WTX)/m_$(2)/dma_uart.cc
	sed '$$(mut_$(1)_$(2))' $(4)/$(3) > $$(WTX)/m_$(2)/$(3).new
	@if cmp -s $(4)/$(3) $$(WTX)/m_$(2)/$(3).new; then echo "wifi-tx-nowait-red-$(2): the model changes nothing"; exit 1; fi
	mv $$(WTX)/m_$(2)/$(3).new $$(WTX)/m_$(2)/$(3)
	$$(call wtx_build,$$(WTX)/m_$(2),$$(WTX)/m_$(2),$$(WTX)/wifiTxNoWaitTest_$(2),)
	@if $$(WTX)/wifiTxNoWaitTest_$(2) > $$(WTX)/red-$(2).log 2>&1; then \
		echo "wifi-tx-nowait-red-$(2): the suite passed against the model"; exit 1; \
	else \
		echo "wifi-tx-nowait-red-$(2): killed ($$$$(grep -c FAIL $$(WTX)/red-$(2).log) checks failed), as required"; \
	fi
endef
$(foreach m,no-reserve no-flag no-recheck,$(eval $(call WTX_MUTANT,w,$(m),wifi_tx_packet.inc,$$(WTX)/base)))
$(foreach m,wake-without-flag wake-at-reserve no-hysteresis hysteresis-at-edge hysteresis-plus-1 no-clear clear-on-failure no-switch,$(eval $(call WTX_MUTANT,w,$(m),wifi_tx_extract.inc,$$(WTX)/base)))
$(eval $(call WTX_MUTANT,w,no-refused,wifi_tx_packet.inc,$$(WTX)/base))
$(eval $(call WTX_MUTANT,u,no-hook-call,dma_uart.cc,$$(UART_SRC)))
$(eval $(call WTX_MUTANT,u,count-not-reserve,dma_uart.cc,$$(UART_SRC)))
wifi-tx-nowait-red: wifi-tx-nowait-red-legacy $(addprefix wifi-tx-nowait-red-,$(WTX_MUTANTS_WIFI) $(WTX_MUTANTS_UART))

# The same code under load (wifi_tx_sim_test.cpp): lwIP's real TCP and UDP core, with the firmware's TCP
# options (fw_tcp_opts.h, taken from its lwipopts.h), over wifi_tx_packet and the DmaUART of the unit
# tests, on a simulated clock. Old against new on the same load; the models below break the new code
# and the suite has to notice.
SIM_LWIP_C := core/init.c core/def.c core/inet_chksum.c core/ip.c core/mem.c core/memp.c core/netif.c \
	core/pbuf.c core/stats.c core/sys.c core/timeouts.c core/tcp.c core/tcp_in.c core/tcp_out.c core/udp.c \
	core/ipv4/ip4.c core/ipv4/ip4_addr.c core/ipv4/ip4_frag.c core/ipv4/icmp.c
SIM_INC := -Ilwip_sim -I$(WTX)/sim -I../../../lwip/src/include

wtx-sim-lwip:
	mkdir -p $(WTX)/sim/lwip
	tr -d '\r' < $(FW_CONFIG)/lwipopts.h | grep -E '^#define (MEMP_NUM_TCP_SEG|TCP_WND|TCP_MAXRTX|TCP_SYNMAXRTX|TCP_QUEUE_OOSEQ|TCP_OOSEQ_MAX_PBUFS|TCP_MSS|TCP_CALCULATE_EFF_SEND_MSS|TCP_SND_BUF|TCP_SND_QUEUELEN|TCP_SNDLOWAT|LWIP_TCP_TIMESTAMPS|TCP_WND_UPDATE_THRESHOLD|LWIP_TCP_KEEPALIVE)[[:space:]]' > $(WTX)/sim/fw_tcp_opts.h
	test $$(wc -l < $(WTX)/sim/fw_tcp_opts.h) -eq 14
	for f in $(SIM_LWIP_C); do \
		$(CC) -g -w $(SIM_INC) -c $(LWIP_SRC)/$$f -o $(WTX)/sim/lwip/$$(basename $$f .c).o || exit 1; \
	done

# usage: $(call wtxsim_build,<dir with dma_uart.cc>,<dir with the extracted code>,<test binary>,<defines>)
define wtxsim_build
	mkdir -p $(1)
	awk $(UART_REGS_AWK) $(UART_SRC)/dma_uart.h > $(1)/dma_uart.h
	grep -q dma_uart_regs.h $(1)/dma_uart.h
	$(CXX) -std=c++14 -g -w -I$(1) $(UART_INC) -c $(1)/dma_uart.cc -o $(1)/dma_uart.o
	$(CC) -g -w $(UART_INC) -c $(UART_SRC)/cmd_buffer.c -o $(1)/cmd_buffer.o
	$(CXX) -std=c++14 -g -O1 -Wall -Wno-unused-variable -Wno-unused-function -Wno-unused-parameter -I. -I.. -I../../usb/tests $(SIM_INC) -I$(1) -I$(2) $(UART_INC) $(4) wifi_tx_sim_test.cpp ../../usb/tests/host_test_main.cpp $(1)/dma_uart.o $(1)/cmd_buffer.o $(WTX)/sim/lwip/*.o -lpthread -o $(3)
endef

wifi-tx-sim: wtx-extract wtx-sim-lwip
	mkdir -p $(WTX)/simuart
	cp $(UART_SRC)/dma_uart.cc $(WTX)/simuart/dma_uart.cc
	$(call wtxsim_build,$(WTX)/simuart,$(WTX)/base,$(WTX)/wifiTxSimTest,)
	$(WTX)/wifiTxSimTest

# Models: the old function in the place of the new (the stall and the throughput are measured on
# it), the wake not wired (dma_uart.cc), the flag never raised
WTXSIM_MUTANTS := new-is-legacy no-hook-call no-flag no-reserve no-hysteresis hysteresis-8
mut_s_no-hook-call := s/u->isr_tx_free_callback(u->packets, \&HPTaskAwoken);//
mut_s_no-flag := s/^        wifi_tx_starved = 1;//
mut_s_no-reserve := s/GetBufferKeeping(&buf, WIFI_TX_RESERVE)/GetBuffer(\&buf, 0)/g
# the wake without the hysteresis (the first design: free > reserve), and with one that is too large
mut_s_no-hysteresis := s/> WIFI_TX_RESERVE + WIFI_TX_WAKE_HYSTERESIS) {/> WIFI_TX_RESERVE) {/
mut_s_hysteresis-8 := s/> WIFI_TX_RESERVE + WIFI_TX_WAKE_HYSTERESIS) {/> WIFI_TX_RESERVE + 8) {/

define WTXSIM_MUTANT
wifi-tx-sim-red-$(1): wtx-extract wtx-sim-lwip
	mkdir -p $$(WTX)/sm_$(1)
	cp $$(WTX)/base/* $$(WTX)/sm_$(1)/
	cp $$(UART_SRC)/dma_uart.cc $$(WTX)/sm_$(1)/dma_uart.cc
	$(2)
	$$(call wtxsim_build,$$(WTX)/sm_$(1),$$(WTX)/sm_$(1),$$(WTX)/wifiTxSimTest_$(1),$(3))
	@if $$(WTX)/wifiTxSimTest_$(1) > $$(WTX)/sim-red-$(1).log 2>&1; then \
		echo "wifi-tx-sim-red-$(1): the suite passed against the model"; exit 1; \
	else \
		echo "wifi-tx-sim-red-$(1): killed ($$$$(grep -c FAIL $$(WTX)/sim-red-$(1).log) checks failed), as required"; \
	fi
endef
$(eval $(call WTXSIM_MUTANT,new-is-legacy,@true,-DWTX_NEW_IS_LEGACY))
$(eval $(call WTXSIM_MUTANT,no-hook-call,sed '$$(mut_s_no-hook-call)' $$(UART_SRC)/dma_uart.cc > $$(WTX)/sm_no-hook-call/dma_uart.cc.new && ! cmp -s $$(UART_SRC)/dma_uart.cc $$(WTX)/sm_no-hook-call/dma_uart.cc.new && mv $$(WTX)/sm_no-hook-call/dma_uart.cc.new $$(WTX)/sm_no-hook-call/dma_uart.cc,))
$(eval $(call WTXSIM_MUTANT,no-flag,sed '$$(mut_s_no-flag)' $$(WTX)/base/wifi_tx_packet.inc > $$(WTX)/sm_no-flag/wifi_tx_packet.inc.new && ! cmp -s $$(WTX)/base/wifi_tx_packet.inc $$(WTX)/sm_no-flag/wifi_tx_packet.inc.new && mv $$(WTX)/sm_no-flag/wifi_tx_packet.inc.new $$(WTX)/sm_no-flag/wifi_tx_packet.inc,))
$(eval $(call WTXSIM_MUTANT,no-reserve,sed '$$(mut_s_no-reserve)' $$(WTX)/base/wifi_tx_packet.inc > $$(WTX)/sm_no-reserve/wifi_tx_packet.inc.new && ! cmp -s $$(WTX)/base/wifi_tx_packet.inc $$(WTX)/sm_no-reserve/wifi_tx_packet.inc.new && mv $$(WTX)/sm_no-reserve/wifi_tx_packet.inc.new $$(WTX)/sm_no-reserve/wifi_tx_packet.inc,))
$(eval $(call WTXSIM_MUTANT,no-hysteresis,sed '$$(mut_s_no-hysteresis)' $$(WTX)/base/wifi_tx_extract.inc > $$(WTX)/sm_no-hysteresis/wifi_tx_extract.inc.new && ! cmp -s $$(WTX)/base/wifi_tx_extract.inc $$(WTX)/sm_no-hysteresis/wifi_tx_extract.inc.new && mv $$(WTX)/sm_no-hysteresis/wifi_tx_extract.inc.new $$(WTX)/sm_no-hysteresis/wifi_tx_extract.inc,))
$(eval $(call WTXSIM_MUTANT,hysteresis-8,sed '$$(mut_s_hysteresis-8)' $$(WTX)/base/wifi_tx_extract.inc > $$(WTX)/sm_hysteresis-8/wifi_tx_extract.inc.new && ! cmp -s $$(WTX)/base/wifi_tx_extract.inc $$(WTX)/sm_hysteresis-8/wifi_tx_extract.inc.new && mv $$(WTX)/sm_hysteresis-8/wifi_tx_extract.inc.new $$(WTX)/sm_hysteresis-8/wifi_tx_extract.inc,))
wifi-tx-sim-red: $(addprefix wifi-tx-sim-red-,$(WTXSIM_MUTANTS))

clean:
	rm -rf $(OUTPUT_DIR)
