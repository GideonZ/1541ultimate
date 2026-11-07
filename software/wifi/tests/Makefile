#
# Host tests of the UART interrupt handler of the ESP32 WiFi modules (raw_u64 = Ultimate 64, raw_c3 = Ultimate II+L,
# u64ctrl = Elite II / C64 Ultimate): my_uart.c, whose handler is cut out of each module as it is and run against a
# modelled UART and the real buffer queues (software/io/uart/cmd_buffer.c). No hardware and no ESP-IDF needed.
#
#   make isr                    all modules
#   make MODULE=u64ctrl isr     one module
#   make isr-red                the models of the code before each fix (the LEGACY_ISR_* sed expressions) must fail the cases
#
# The cases are the C++ files isr_test.cpp and isr_*_test.cpp, picked up by name, so that a fix adds a file.
#
PATH_SW  := ../..
EXTRACT  := awk -f extract.awk
OUT      := output
ALL      := raw_u64 raw_c3 u64ctrl
MODULE   ?= $(ALL)
CXX      ?= g++
CC       ?= gcc

DIR_raw_u64 := $(PATH_SW)/wifi/raw_u64/main
DIR_raw_c3  := $(PATH_SW)/wifi/raw_c3/main
DIR_u64ctrl := $(PATH_SW)/u64ctrl/main

HOSTTEST := $(PATH_SW)/io/usb/tests
CASES    := isr_test.cpp $(wildcard isr_*_test.cpp)
COMMON   := -g -O0 -Wall -Wno-unused-parameter -D_FORTIFY_SOURCE=0
CFLAGS   := -std=gnu11 $(COMMON) -Wno-unused-variable -Wno-unused-function
CXXFLAGS := -std=c++14 $(COMMON) -I$(HOSTTEST) -I.
CMDBUF_SRC := $(PATH_SW)/io/uart/cmd_buffer.c

# The functions of my_uart.c the cases run (a function a tree does not have is left out)
ISR_FUNCS := my_uart_tx_fill my_uart_intr_handler my_uart_transmit_packet my_uart_get_buffer my_uart_free_buffer
ISR_RED_FIXES := ovf ovfr ovfr2 ovfr3 ovfr4
# LEGACY_ISR_<fix> := sed expressions that turn a module's handler into a model of it before the fix
#   ovf    the overflow interrupt is not enabled: it is never seen (the handler before the fix)
#   ovfr   an overflow does not mark the open frame as damaged (no skip)
#   ovfr2  the skip does not end at the first 0xC0
#   ovfr3  the FIFO is read after the loss (not reset, but 'nothing to read' is forgotten)
#   ovfr4  only the FIFO reset is missing
LEGACY_ISR_ovf    := -e 's/ | UART_INTR_RXFIFO_OVF)/)/'
LEGACY_ISR_ovfr   := -e 's/p_uart->rx_resync = 1;/(void)0;/'
LEGACY_ISR_ovfr2  := -e 's/^\( *\)p_uart->rx_resync = 0;/\1(void)0;/'
LEGACY_ISR_ovfr3  := -e 's/uart_hal_rxfifo_rst(/(void)(/' -e 's/uart_intr_status \&= ~(UART_INTR_RXFIFO_OVF | UART_INTR_RXFIFO_TOUT | UART_INTR_RXFIFO_FULL);/(void)0;/'
LEGACY_ISR_ovfr4  := -e 's/uart_hal_rxfifo_rst(/(void)(/'

.PHONY: all isr isr-red
all: isr

# isr_code.in: isr_env.h, the handler's types from the module's my_uart.c, the interrupt mask it enables, the handler as it is
# (the sed expressions model it before a fix), then isr_stubs.c
define ISR_BUILD
	@mkdir -p $(1)
	@{ echo '#include "isr_env.h"'; \
	   awk '/^typedef struct \{/ { t = 1; buf = "" } t { buf = buf $$0 "\n" } t && /^\} (my_uart_obj_t|uart_context_t);/ { printf "%s", buf; t = 0 } t && /^\}/ { t = 0 }' $(DIR_$(M))/my_uart.c; \
	   echo 'static my_uart_obj_t *p_uart_obj[UART_NUM_MAX]; static uart_context_t uart_context[UART_NUM_MAX];'; \
	   echo "#define MODULE_INTR_ENABLE_MASK ($$(sed -n 's/^ *\.intr_enable_mask *= *\(.*\),$$/\1/p' $(DIR_$(M))/my_uart.c | head -1))"; \
	   for f in $(ISR_FUNCS); do $(EXTRACT) -v f=$$f $(DIR_$(M))/my_uart.c; done; } > $(1)/isr_code.in
	@grep -q 'my_uart_obj_t;' $(1)/isr_code.in && grep -q 'uart_intr_status' $(1)/isr_code.in || { echo "wifi tests: the ISR of $(M) not found"; exit 1; }
	$(if $(2),@sed $(2) $(1)/isr_code.in > $(1)/isr_code.red && ! cmp -s $(1)/isr_code.red $(1)/isr_code.in || { echo "wifi tests: ISR model does not apply to $(M)"; exit 1; })
	$(if $(2),@mv $(1)/isr_code.red $(1)/isr_code.in)
	@{ cat $(1)/isr_code.in; echo '#include "isr_stubs.c"'; } > $(1)/isr_code.c
	$(CC) $(CFLAGS) -I. -Istub -I$(PATH_SW)/io/uart -I$(DIR_$(M)) -c -o $(1)/isr_code.o $(1)/isr_code.c
	$(CC) $(CFLAGS) -I. -Istub -I$(PATH_SW)/io/uart -c -o $(1)/cmd_buffer.o $(CMDBUF_SRC)
	$(CC) $(CFLAGS) -I. -Istub -c -o $(1)/cmdbuf_queues.o cmdbuf_queues.c
	$(CXX) $(CXXFLAGS) -I$(PATH_SW)/io/uart -I$(DIR_$(M)) -o $(1)/isr_test $(CASES) $(HOSTTEST)/host_test_main.cpp \
	    $(1)/isr_code.o $(1)/cmd_buffer.o $(1)/cmdbuf_queues.o -lpthread
endef

isr:
	@set -e; for m in $(MODULE); do $(MAKE) --no-print-directory M=$$m isrrun; done

isrrun:
	$(call ISR_BUILD,$(OUT)/isr/$(M),)
	@echo "== isr $(M)"; $(OUT)/isr/$(M)/isr_test

isr-red:
	@set -e; for fix in $(ISR_RED_FIXES); do for m in $(MODULE); do $(MAKE) --no-print-directory M=$$m FIX=$$fix isrredrun; done; done

isrredrun:
	$(call ISR_BUILD,$(OUT)/redisr$(FIX)/$(M),$(LEGACY_ISR_$(FIX)))
	@if $(OUT)/redisr$(FIX)/$(M)/isr_test > $(OUT)/redisr$(FIX)/$(M)/log 2>&1; then \
	    echo "red isr $(FIX) $(M): the ISR cases PASSED against the model before $(FIX): they no longer catch it"; exit 1; \
	 else \
	    echo "red isr $(FIX) $(M): $$(grep -c '^FAIL' $(OUT)/redisr$(FIX)/$(M)/log) check(s) fail against the model before $(FIX), as required"; \
	 fi
