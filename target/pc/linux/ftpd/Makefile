#
# Host build of the FTP server's data connection (network/ftpd.cc) against
# stubs of the RTOS and a recording file system, driven over loopback sockets.
#
#   make          build
#   make test     build and run
#
PRJ     = ftpd
RESULT  = result
OUTPUT  = output

PATH_SW = ../../../../software
STUBS   = $(PATH_SW)/test/ftpd/stubs

CXX     = g++
CXXOPTIONS = -std=gnu++11 -g -O0 -w -pthread -include $(STUBS)/ftpd_stubs.h \
             -I$(STUBS) -I$(PATH_SW)/network -I$(PATH_SW)/components

SRCS    = $(PATH_SW)/network/ftpd.cc $(PATH_SW)/components/pattern.cc $(PATH_SW)/components/mystring.cc \
          $(PATH_SW)/test/ftpd/ftpd_test.cc
DEPS    = $(PATH_SW)/network/ftpd.h $(PATH_SW)/network/vfs.h $(wildcard $(STUBS)/*.h)

.PHONY: all test clean

all: $(RESULT)/$(PRJ)

$(RESULT)/$(PRJ): $(SRCS) $(DEPS)
	@mkdir -p $(RESULT)
	$(CXX) $(CXXOPTIONS) -o $@ $(SRCS)

test: all
	@$(RESULT)/$(PRJ)

clean:
	@rm -rf $(OUTPUT) $(RESULT)
