# Host build of the check on the WiFi module's dedicated channel and GET_RANDOM,
# its first user, against stand-ins. `make test` builds and runs it.
PRJ     = wifidedicated
PATH_SW = ../../../../software
TEST    = $(PATH_SW)/test/wifi_dedicated

# `make test` runs it against each module's headers: raw_u64 (the U64) and
# u64ctrl (the C64 Ultimate, U64=2). `make U64=2 run` is the second alone.
U64 ?= 1
ifeq ($(U64),2)
MODULE  = $(PATH_SW)/u64ctrl/main
RESULT  = result/u64ctrl
OUTPUT  = output/u64ctrl
else
MODULE  = $(PATH_SW)/wifi/raw_u64/main
RESULT  = result
OUTPUT  = output
endif

VPATH   = $(TEST)/stub $(TEST) $(PATH_SW)/test/wifi_random $(PATH_SW)/io/wifi $(PATH_SW)/io/uart \
          $(MODULE) $(PATH_SW)/wifi/common

CXX     = g++
CPPOPTIONS = -std=gnu++11 -g -O0 -Wall -Wextra -Wno-unused-parameter -DNIOS=1 -DU64=$(U64) \
           $(addprefix -I, $(VPATH))

SRCS_CC = wifi_dedicated_test.cc check.cc world.cc wifi_dedicated.cc wifi_random.cc
OBJS    = $(addprefix $(OUTPUT)/, $(SRCS_CC:%.cc=%.o))

.PHONY: all run test clean

all: $(RESULT)/$(PRJ)

$(OUTPUT)/%.o: %.cc
	@mkdir -p $(OUTPUT)
	$(CXX) $(CPPOPTIONS) -c -o $@ $<

# Compiled from a copy, so that its "wifi_cmd.h" is the stand-in rather than
# the real header next to it.
$(OUTPUT)/wifi_dedicated.o: $(PATH_SW)/io/wifi/wifi_dedicated.cc
	@mkdir -p $(OUTPUT)
	cp $< $(OUTPUT)/wifi_dedicated.cc
	$(CXX) $(CPPOPTIONS) -c -o $@ $(OUTPUT)/wifi_dedicated.cc

$(RESULT)/$(PRJ): $(OBJS)
	@mkdir -p $(RESULT)
	$(CXX) -o $@ $(OBJS)

run: all
	@$(RESULT)/$(PRJ)

test:
	@$(MAKE) --no-print-directory run
	@$(MAKE) --no-print-directory U64=2 run

clean:
	@rm -rf $(OUTPUT) $(RESULT)
