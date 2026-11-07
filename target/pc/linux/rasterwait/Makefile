#
# Host model of the raster wait in C64::resume() (io/c64/c64.cc).
#
#   make          build
#   make test     build and run
#   make legacy   build and run the condition as it was before the fix; it fails
#   make legacy-d011  the fixed wait with $D011 cleared during it; it fails
#
RESULT  = result
PATH_SW = ../../../../software
CXX     = g++
CXXOPTIONS = -std=c++11 -g -O0 -Wall -I$(PATH_SW)/io/c64
SRC     = $(PATH_SW)/test/raster_wait/raster_wait_test.cc
DEPS    = $(SRC) $(PATH_SW)/io/c64/raster_wait.h

.PHONY: all test legacy legacy-d011 clean

all: $(RESULT)/rasterwait

$(RESULT)/rasterwait: $(DEPS)
	@mkdir -p $(RESULT)
	$(CXX) $(CXXOPTIONS) -o $@ $(SRC)

test: all
	@$(RESULT)/rasterwait

legacy: $(DEPS)
	@mkdir -p $(RESULT)
	$(CXX) $(CXXOPTIONS) -DRASTER_WAIT_LEGACY -o $(RESULT)/rasterwait_legacy $(SRC)
	@$(RESULT)/rasterwait_legacy

legacy-d011: $(DEPS)
	@mkdir -p $(RESULT)
	$(CXX) $(CXXOPTIONS) -DRASTER_WAIT_D011_LEGACY -o $(RESULT)/rasterwait_legacy_d011 $(SRC)
	@$(RESULT)/rasterwait_legacy_d011

clean:
	@rm -rf $(RESULT)
