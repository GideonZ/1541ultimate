#
# Host build of the SD card driver (io/sd_card/sd_card.cc and sdio.cc, with OS) against a
# simulated card behind the SDIO registers and recorded RTOS calls.
#
# sd_card.h and the two .cc files are copied into $(SHADOW): sdio.h sits next to them in
# io/sd_card, and the copies make every include resolve through -I, stubs first.
#
#   make test     build and run
#
RESULT  = result
OUTPUT  = output
SHADOW  = $(OUTPUT)/shadow
PATH_SW = ../../../../software
TESTDIR = $(PATH_SW)/test/sd_card
CXX     = g++
CXXOPTIONS = -std=gnu++11 -g -O0 -w -fpermissive -DOS -I$(TESTDIR)/stubs -I$(SHADOW) -I$(PATH_SW)/filesystem -I$(PATH_SW)/components -I$(PATH_SW)/system
SRCS    = $(SHADOW)/sd_card.cc $(SHADOW)/sdio.cc $(PATH_SW)/filesystem/blockdev.cc $(TESTDIR)/sd_card_test.cc

.PHONY: all test clean

all: $(RESULT)/sdcard

$(SHADOW)/%: $(PATH_SW)/io/sd_card/%
	@mkdir -p $(SHADOW)
	cp $< $@

# sdio.cc casts the buffer pointer to uint32_t to test its alignment. On a 64-bit host that
# truncates (clang refuses it outright), so the shadow copy, and only it, casts via uintptr_t.
$(SHADOW)/sdio.cc: $(PATH_SW)/io/sd_card/sdio.cc
	@mkdir -p $(SHADOW)
	sed 's/(uint32_t)buf/(uint32_t)(uintptr_t)buf/' $< > $@
	@test $$(grep -c '(uint32_t)(uintptr_t)buf' $@) -eq 2 || { echo "sdio.cc: expected 2 pointer casts to patch"; rm -f $@; exit 1; }

$(RESULT)/sdcard: $(SHADOW)/sd_card.cc $(SHADOW)/sdio.cc $(SHADOW)/sd_card.h $(TESTDIR)/sd_card_test.cc $(wildcard $(TESTDIR)/stubs/*.h)
	@mkdir -p $(RESULT)
	$(CXX) $(CXXOPTIONS) -o $@ $(SRCS)

test: all
	@$(RESULT)/sdcard

clean:
	@rm -rf $(OUTPUT) $(RESULT)
