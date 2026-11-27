#
# Host test of the DHCP client handling in network/network_interface.cc: the member
# functions are cut out of the source as they are and run against a stand-in for the
# lwIP calls they make (software/test/dhcp/dhcp_restart_test.cc).
#
#   make          build
#   make test     build and run
#
PRJ     = dhcp
RESULT  = result
PATH_SW = ../../../../software
SRC     = $(PATH_SW)/io/network/network_interface.cc
EXTRACT = $(PATH_SW)/io/wifi/tests/extract.awk
FUNCS   = NetworkInterface@::@effectuate_settings NetworkInterface@::@link_up \
          NetworkInterface@::@link_down NetworkInterface@::@~NetworkInterface

CXX        = g++
CXXOPTIONS = -std=c++11 -g -O0 -Wall -Wno-unused-function -Wno-unused-variable

.PHONY: all test clean

all: $(RESULT)/$(PRJ)

# CR is dropped from the extract only: the source file has CRLF line ends
$(RESULT)/under_test.inc: $(SRC) $(EXTRACT)
	@mkdir -p $(RESULT)
	@rm -f $@; : > $@
	@for f in $(FUNCS); do \
	  out=$$(tr -d '\r' < $(SRC) | awk -v f="$$(echo $$f | tr '@' ' ')" -f $(EXTRACT)); \
	  [ -n "$$out" ] || { echo "dhcp: $$f not found in $(SRC)" >&2; exit 1; }; \
	  printf "%s\n" "$$out" >> $@ || exit 1; done

$(RESULT)/$(PRJ): $(PATH_SW)/test/dhcp/dhcp_restart_test.cc $(RESULT)/under_test.inc
	$(CXX) $(CXXOPTIONS) -DDHCP_UNDER_TEST='"under_test.inc"' -I$(RESULT) -o $@ $<

test: all
	@$(RESULT)/$(PRJ)

clean:
	@rm -rf $(RESULT)
