# Host build of the check on the WiFi module's dedicated channel and GET_RANDOM,
# its first user, against stand-ins. `make test` builds and runs it.
PRJ     = wifidedicated
RESULT  = result
OUTPUT  = output

PATH_SW = ../../../../software
TEST    = $(PATH_SW)/test/wifi_dedicated

VPATH   = $(TEST)/stub $(TEST) $(PATH_SW)/test/wifi_random $(PATH_SW)/io/wifi $(PATH_SW)/io/uart \
          $(PATH_SW)/wifi/raw_u64/main

CXX     = g++
CPPOPTIONS = -std=gnu++11 -g -O0 -Wall -Wextra -Wno-unused-parameter -DNIOS=1 -DU64=1 \
           $(addprefix -I, $(VPATH))

SRCS_CC = wifi_dedicated_test.cc check.cc world.cc wifi_dedicated.cc wifi_random.cc
OBJS    = $(addprefix $(OUTPUT)/, $(SRCS_CC:%.cc=%.o))

.PHONY: all test clean

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

test: all
	@$(RESULT)/$(PRJ)

clean:
	@rm -rf $(OUTPUT) $(RESULT)
