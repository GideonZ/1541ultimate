CXX ?= g++
CXXFLAGS := -std=c++14 -g -Wall -Wextra -I. -I.. -I../../usb/tests
OUTPUT_DIR := output
ETH_TX_FRAME_TEST := $(OUTPUT_DIR)/ethTxFrameTest

.PHONY: all eth-tx-frame clean

all: eth-tx-frame

eth-tx-frame:
	mkdir -p $(OUTPUT_DIR)
	$(CXX) $(CXXFLAGS) eth_tx_frame_test.cpp ../../usb/tests/host_test_main.cpp -lpthread -o $(ETH_TX_FRAME_TEST) && $(ETH_TX_FRAME_TEST)

clean:
	rm -rf $(OUTPUT_DIR)
