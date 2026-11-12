#
# Host build of U64Machine's whole-RAM copies (u64/u64_machine.cc: clear_ram, get_all_memory)
# against a simulated C64 memory window, with the critical sections and scheduler suspends recorded.
#
# u64_machine.cc and u64_machine.h are copied into $(SHADOW) so that their includes of
# u64.h, c64.h, FreeRTOS.h and task.h resolve to the stubs through -I.
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

$(RESULT)/c64mem: $(SHADOW)/u64_machine.cc $(SHADOW)/u64_machine.h $(TESTDIR)/c64mem_test.cc $(wildcard $(TESTDIR)/stubs/*.h)
	@mkdir -p $(RESULT)
	$(CXX) $(CXXOPTIONS) -o $@ $(SRCS)

test: all
	@$(RESULT)/c64mem

clean:
	@rm -rf $(OUTPUT) $(RESULT)
