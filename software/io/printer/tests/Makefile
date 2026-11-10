CXX ?= g++
CXXFLAGS := -std=c++11 -g -Wall -Wextra -Werror -DPRINTER_SPOOL_HOST_TEST -Ihost_rtos -I.. $(EXTRA)

all: spool

# 'make EXTRA="-fsanitize=address,undefined"' for the sanitizers
spool:
	$(CXX) $(CXXFLAGS) spool_test.cpp -lpthread -o spoolTest
	timeout 120 ./spoolTest

clean:
	rm -rf spoolTest spoolTest.dSYM

.PHONY: all spool clean
