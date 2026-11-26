#
# Host build of the socket DMA service (network/socket_dma.cc) against stubs of
# the RTOS, the C64 and the file manager, driven over loopback sockets.
#
#   make          build
#   make test     build and run
#
PRJ     = socketdma
RESULT  = result
OUTPUT  = output

PATH_SW = ../../../../software
STUBS   = $(PATH_SW)/test/socket_dma/stubs

CXX     = g++
CXXOPTIONS = -std=c++11 -g -O0 -w -pthread -DSOCKET_DMA_STALL_MS=3000 \
             -I$(STUBS) -I$(PATH_SW)/network

SRCS    = $(PATH_SW)/network/socket_dma.cc $(PATH_SW)/test/socket_dma/socket_dma_test.cc
DEPS    = $(PATH_SW)/network/socket_dma.h $(PATH_SW)/network/socket_keepalive.h $(wildcard $(STUBS)/*.h)

.PHONY: all test clean

all: $(RESULT)/$(PRJ)

$(RESULT)/$(PRJ): $(SRCS) $(DEPS)
	@mkdir -p $(RESULT)
	$(CXX) $(CXXOPTIONS) -o $@ $(SRCS)

test: all
	@$(RESULT)/$(PRJ)

clean:
	@rm -rf $(OUTPUT) $(RESULT)
