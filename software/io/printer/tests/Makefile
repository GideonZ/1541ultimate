CXX ?= g++
CXXFLAGS := -std=c++11 -g -Wall -Wextra -Werror -Ihost_rtos -I.. -fsanitize=address,undefined

all: spool

spool:
	$(CXX) $(CXXFLAGS) spool_test.cpp -lpthread -o spoolTest
	timeout 120 ./spoolTest

clean:
	rm -f spoolTest

.PHONY: all spool clean
