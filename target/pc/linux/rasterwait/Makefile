#
# Host test of the VIC side of C64::resume() (io/c64/c64.cc). The test runs raster_resume()
# from io/c64/raster_wait.h, the function c64.cc calls, against a model VIC.
#
#   make          build
#   make test     build and run
#   make mutants  build the old or broken code, each from a copy of raster_wait.h with one
#                 edit; every one of them must fail
#
RESULT  = result
PATH_SW = ../../../../software
CXX     = g++
CXXOPTIONS = -std=c++11 -g -O0 -Wall
INC     = -I$(PATH_SW)/io/c64
SRC     = $(PATH_SW)/test/raster_wait/raster_wait_test.cc
HDR     = $(PATH_SW)/io/c64/raster_wait.h
DEPS    = $(SRC) $(HDR)

.PHONY: all test mutants clean

all: $(RESULT)/rasterwait

$(RESULT)/rasterwait: $(DEPS)
	@mkdir -p $(RESULT)
	$(CXX) $(CXXOPTIONS) $(INC) -o $@ $(SRC)

test: all
	@$(RESULT)/rasterwait

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
$(eval $(call MUTANT,den-write,s/d011_while_waiting(s.vic_d011, hi)/hi/))
$(eval $(call MUTANT,single-read,s/if (!((before ^ after) \& 0x80))/if (1)/))
$(eval $(call MUTANT,d012-reads-d011,s/lo = bus.read(RASTER_REG_D012);/lo = bus.read(RASTER_REG_D011);/))
$(eval $(call MUTANT,ms-inverted,s/if (bus.ms_elapsed())/if (!bus.ms_elapsed())/))
$(eval $(call MUTANT,hi-zero,s/uint8_t hi = (target \& 256) ? 0x80 : 0;/uint8_t hi = 0;/))
$(eval $(call MUTANT,no-minus-one,s/return line - 1;/return line;/))
$(eval $(call MUTANT,direct-minus-one,s/if (release_immediate || !line)/if (!line)/))
$(eval $(call MUTANT,target-is-raster-lo,s/int target = raster_wait_target(s.raster, s.raster_hi, release_immediate);/int target = s.raster;/))
$(eval $(call MUTANT,wrong-compare,s/bus.write(RASTER_REG_D012, lo);/bus.write(RASTER_REG_D012, 0);/))
$(eval $(call MUTANT,no-irq-clear,s/bus.write(RASTER_REG_IRQ, 0x81); \/\/ clear raster irq/(void)0;/))
$(eval $(call MUTANT,no-restore-d011,s/bus.write(RASTER_REG_D011, s.vic_d011);/(void)0;/))
$(eval $(call MUTANT,no-irq-en-restore,s/bus.write(RASTER_REG_IRQ_EN, s.vic_irq_en);/(void)0;/))

mutants:
	-@echo "== legacy"; $(MAKE) --no-print-directory legacy 2>&1 | tail -2
	-@echo "== legacy-d011"; $(MAKE) --no-print-directory legacy-d011 2>&1 | tail -2
	-@echo "== den-write"; $(MAKE) --no-print-directory den-write 2>&1 | tail -2
	-@echo "== single-read"; $(MAKE) --no-print-directory single-read 2>&1 | tail -2
	-@echo "== d012-reads-d011"; $(MAKE) --no-print-directory d012-reads-d011 2>&1 | tail -2
	-@echo "== ms-inverted"; $(MAKE) --no-print-directory ms-inverted 2>&1 | tail -2
	-@echo "== hi-zero"; $(MAKE) --no-print-directory hi-zero 2>&1 | tail -2
	-@echo "== no-minus-one"; $(MAKE) --no-print-directory no-minus-one 2>&1 | tail -2
	-@echo "== direct-minus-one"; $(MAKE) --no-print-directory direct-minus-one 2>&1 | tail -2
	-@echo "== target-is-raster-lo"; $(MAKE) --no-print-directory target-is-raster-lo 2>&1 | tail -2
	-@echo "== wrong-compare"; $(MAKE) --no-print-directory wrong-compare 2>&1 | tail -2
	-@echo "== no-irq-clear"; $(MAKE) --no-print-directory no-irq-clear 2>&1 | tail -2
	-@echo "== no-restore-d011"; $(MAKE) --no-print-directory no-restore-d011 2>&1 | tail -2
	-@echo "== no-irq-en-restore"; $(MAKE) --no-print-directory no-irq-en-restore 2>&1 | tail -2

clean:
	@rm -rf $(RESULT)
