#
# Host build of U64Machine's whole-RAM copies (u64/u64_machine.cc: clear_ram, get_all_memory)
# and of u64/c64_window.h against a simulated C64 memory window, with the critical sections
# and scheduler suspends recorded.
#
# u64_machine.cc and u64_machine.h are copied into $(SHADOW) so that their includes of
# u64.h, c64.h, FreeRTOS.h and task.h resolve to the stubs through -I. c64_window.h is used
# in place. The last check is on the source: unlock_irq must poke through c64_window.h.
#
#   make test     build and run
#
RESULT  = result
OUTPUT  = output
SHADOW  = $(OUTPUT)/shadow
PATH_SW = ../../../../software
TESTDIR = $(PATH_SW)/test/c64mem
CXX     = g++
CXXOPTIONS = -std=gnu++11 -g -O0 -Wall -Wno-unused-function -I$(TESTDIR)/stubs -I$(SHADOW)
SRCS    = $(SHADOW)/u64_machine.cc $(TESTDIR)/c64mem_test.cc

.PHONY: all test clean

all: $(RESULT)/c64mem

$(SHADOW)/%: $(PATH_SW)/u64/%
	@mkdir -p $(SHADOW)
	cp $< $@

$(SHADOW)/c64_window.h: $(PATH_SW)/u64/c64_window.h
	@mkdir -p $(SHADOW)
	cp $< $@

$(RESULT)/c64mem: $(SHADOW)/u64_machine.cc $(SHADOW)/u64_machine.h $(SHADOW)/c64_window.h $(TESTDIR)/c64mem_test.cc $(wildcard $(TESTDIR)/stubs/*.h)
	@mkdir -p $(RESULT)
	$(CXX) $(CXXOPTIONS) -o $@ $(SRCS)

test: all
	@rc=0; $(RESULT)/c64mem || rc=1; \
	if tr -d '\r' < $(PATH_SW)/u64/u64_config.cc | awk '/U64Config :: unlock_irq/,/^}/' | grep -q 'c64_isr_poke_io(0xD038, 0)' && \
	   ! tr -d '\r' < $(PATH_SW)/u64/u64_config.cc | awk '/U64Config :: unlock_irq/,/^}/' | grep -v '^ *//' | grep -q 'C64_POKE'; then \
	    echo "PASS unlock_irq_pokes_through_c64_isr_poke_io (source)"; \
	else echo "FAIL unlock_irq_pokes_through_c64_isr_poke_io (source)"; rc=1; fi; exit $$rc

clean:
	@rm -rf $(OUTPUT) $(RESULT)
