#
# Host build of the remote syslog forwarder (network/syslog.cc) against stubs of
# the RTOS and the config store, driven over loopback UDP sockets.
#
#   make          build
#   make test     build and run
#
PRJ     = syslog
RESULT  = result

PATH_SW = ../../../../software
STUBS   = $(PATH_SW)/test/syslog/stubs

CXX     = g++
CXXOPTIONS = -std=c++11 -g -O0 -Wall -pthread -I$(STUBS) -I$(PATH_SW)/network

SRCS    = $(PATH_SW)/network/syslog.cc $(PATH_SW)/test/syslog/syslog_test.cc
DEPS    = $(PATH_SW)/network/syslog.h $(PATH_SW)/network/network_config.h $(wildcard $(STUBS)/*.h)

.PHONY: all test clean

all: $(RESULT)/$(PRJ)

$(RESULT)/$(PRJ): $(SRCS) $(DEPS)
	@mkdir -p $(RESULT)
	$(CXX) $(CXXOPTIONS) -o $@ $(SRCS)

test: all
	@$(RESULT)/$(PRJ)
	@$(RESULT)/$(PRJ) boot
	@$(RESULT)/$(PRJ) boot-invalid

clean:
	@rm -rf $(RESULT)
