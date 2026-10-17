# Host build of the check the Nios applies to the WiFi module's answer to
# GET_RANDOM, with lwIP's headers stubbed out. `make test` builds and runs it.
PRJ     = wifirandom
RESULT  = result
OUTPUT  = output

PATH_SW = ../../../../software

VPATH   = $(PATH_SW)/io/wifi $(PATH_SW)/wifi/raw_u64/main $(PATH_SW)/test/wifi_random

CXX     = g++
CPPOPTIONS = -std=gnu++11 -g -O0 -Wall -Wextra -Wno-unused-parameter \
           $(addprefix -I, $(VPATH))

SRCS_CC = wifi_random.cc wifi_random_test.cc
OBJS    = $(addprefix $(OUTPUT)/, $(SRCS_CC:%.cc=%.o))

.PHONY: all test clean

all: $(RESULT)/$(PRJ)

$(OUTPUT)/%.o: %.cc
	@mkdir -p $(OUTPUT)
	$(CXX) $(CPPOPTIONS) -c -o $@ $<

$(RESULT)/$(PRJ): $(OBJS)
	@mkdir -p $(RESULT)
	$(CXX) -o $@ $(OBJS)

test: all
	@$(RESULT)/$(PRJ)

clean:
	@rm -rf $(OUTPUT) $(RESULT)
