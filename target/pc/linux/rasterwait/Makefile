#
# Host test of the raster wait in C64::resume() (io/c64/c64.cc). The test runs
# raster_wait_for_line() from io/c64/raster_wait.h, the function c64.cc calls.
#
#   make          build
#   make test     build, run, and check that c64.cc calls the function
#   make legacy   the condition as it was before the fix; it fails
#   make legacy-d011  the fixed wait with $D011 cleared during it; it fails
#   make legacy-order $D012 read before $D011; it fails
#
# The legacy targets compile a copy of raster_wait.h with one line edited back to the old
# code, so a pass or fail comes from the code c64.cc runs and not from a second copy of it.
#
RESULT  = result
PATH_SW = ../../../../software
CXX     = g++
CXXOPTIONS = -std=c++11 -g -O0 -Wall
INC     = -I$(PATH_SW)/io/c64
SRC     = $(PATH_SW)/test/raster_wait/raster_wait_test.cc
HDR     = $(PATH_SW)/io/c64/raster_wait.h
DEPS    = $(SRC) $(HDR)

.PHONY: all test callsite legacy legacy-d011 legacy-order clean

all: $(RESULT)/rasterwait

$(RESULT)/rasterwait: $(DEPS)
	@mkdir -p $(RESULT)
	$(CXX) $(CXXOPTIONS) $(INC) -o $@ $(SRC)

test: all callsite
	@$(RESULT)/rasterwait

# c64.cc must run the shared wait and keep no loop of its own
callsite:
	@test "`grep -c 'raster_wait_for_line(bus' $(PATH_SW)/io/c64/c64.cc`" = 2 || { echo "c64.cc does not call raster_wait_for_line twice"; exit 1; }
	@! grep -n 'VIC_REG(18) != rast_lo\|raster_at_line(' $(PATH_SW)/io/c64/c64.cc || { echo "c64.cc has its own raster wait"; exit 1; }

# $(1) = target name, $(2) = sed script applied to a copy of raster_wait.h
define MUTANT
$(RESULT)/$(1)/raster_wait.h: $(HDR)
	@mkdir -p $(RESULT)/$(1)
	sed -e '$(2)' $(HDR) > $$@
	@! cmp -s $(HDR) $$@ || { echo "mutation $(1) did not change raster_wait.h"; exit 1; }
$(1): $(SRC) $(RESULT)/$(1)/raster_wait.h
	$(CXX) $(CXXOPTIONS) -I$(RESULT)/$(1) -o $(RESULT)/rasterwait_$(1) $(SRC)
	@$(RESULT)/rasterwait_$(1)
endef

$(eval $(call MUTANT,legacy,s/return (d012 == lo) \&\& ((d011 \& 0x80) == hi);/return !((d012 != lo) \&\& ((d011 \& 0x80) != hi));/))
$(eval $(call MUTANT,legacy-d011,s/return (uint8_t)((d011 \& 0x7F) | compare_hi);/return compare_hi;/))
$(eval $(call MUTANT,legacy-order,s/uint8_t d011 = bus.d011();/uint8_t d012 = bus.d012();/;s/raster_at_line(bus.d012(), d011, lo, hi)/raster_at_line(d012, bus.d011(), lo, hi)/))

clean:
	@rm -rf $(RESULT)
