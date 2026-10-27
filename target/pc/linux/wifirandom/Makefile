# Host build of the check the Nios applies to the WiFi module's answer to
# GET_RANDOM, with lwIP's headers stubbed out. `make test` builds and runs it
# against each module's headers: raw_u64 (the U64) and u64ctrl (the C64 Ultimate,
# U64=2). `make U64=2 run` is the second alone.
PRJ     = wifirandom
PATH_SW = ../../../../software

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

VPATH   = $(PATH_SW)/io/wifi $(MODULE) $(PATH_SW)/wifi/common $(PATH_SW)/test/wifi_random

CXX     = g++
CPPOPTIONS = -std=gnu++11 -g -O0 -Wall -Wextra -Wno-unused-parameter -DU64=$(U64) \
           $(addprefix -I, $(VPATH))

SRCS_CC = wifi_random.cc wifi_random_test.cc
OBJS    = $(addprefix $(OUTPUT)/, $(SRCS_CC:%.cc=%.o))

.PHONY: all run test clean

all: $(RESULT)/$(PRJ)

$(OUTPUT)/%.o: %.cc
	@mkdir -p $(OUTPUT)
	$(CXX) $(CPPOPTIONS) -c -o $@ $<

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
