CXX ?= g++
RTOS := ../../command_interface/tests/host_rtos
# host_modem comes first: its config.h, socket.h and friends stand in for the device's.
FLAGS := -std=c++11 -g -Wall -fpermissive -fno-access-control -Ihost_modem -I$(RTOS) -I.. -I../../../components -I../../../system -I../../../infra \
	-I../../../io/stream -w -Wno-unused-variable -Wno-unused-but-set-variable

all: dial

dial:
	$(CXX) $(FLAGS) modem_dial_test.cpp -lpthread -o modemDialTest
	./modemDialTest

clean:
	rm -f modemDialTest

.PHONY: all dial clean
