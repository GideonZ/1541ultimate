CXX ?= g++
CXXFLAGS := -std=c++11 -Wall -Wextra -Werror -I..
# The UCI Server's own source, against host stand-ins for the hardware and FreeRTOS.
UCI_FLAGS := -std=c++11 -g -Wall -I../../../infra -Ihost_uci -Ihost_rtos -I.. -DHOST_RTOS_NO_TASKS -Wno-format -fsanitize=address,undefined

all: palette uci-deferral

palette:
	$(CXX) $(CXXFLAGS) palette_command_test.cpp -o paletteCommandTest
	./paletteCommandTest

# CI preloads a library; the sanitizer runtime has to be the first one loaded
uci-deferral:
	$(CXX) $(UCI_FLAGS) uci_deferral_test.cpp ../command_intf.cc -lpthread -o uciDeferralTest
	env -u LD_PRELOAD ./uciDeferralTest

clean:
	rm -f paletteCommandTest uciDeferralTest

.PHONY: all palette uci-deferral clean
