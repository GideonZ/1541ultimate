#
# Host build of the UCI DOS target (filemanager/dos.cc), the UCI control target
# (io/command_interface/control_target.cc) and the REU preloader
# (io/c64/reu_preloader.cc) against fakes of the file manager, the subsystem
# dispatcher, the C64, the drives and the command interface registers.
#
# The three production files are compiled UNMODIFIED. A quoted #include first
# searches the including file's own directory, so stubs could not shadow
# headers that sit next to the .cc. The Makefile therefore copies each .cc and
# its own .h into $(SHADOW) and compiles the copies; every other include is
# then resolved through -I, stubs first. See software/test/uci_targets/uci_fakes.h
# for the test-control API.
#
#   make          build
#   make test     build and run
#
PRJ     = ucitargets
RESULT  = result
OUTPUT  = output
SHADOW  = $(OUTPUT)/shadow

PATH_SW = ../../../../software
TESTDIR = $(PATH_SW)/test/uci_targets
STUBS   = $(TESTDIR)/stubs

CXX     = g++
CXXOPTIONS = -std=gnu++11 -g -O0 -w -pthread -include stdint.h '-DSLICED_TRANSFER_YIELD()=uci_fake_yield()' \
             -I$(STUBS) -I$(TESTDIR) -I$(SHADOW) \
             -I$(PATH_SW)/system -I$(PATH_SW)/components -I$(PATH_SW)/infra \
             -I$(PATH_SW)/io/command_interface -I$(PATH_SW)/filesystem \
             -I$(PATH_SW)/portable/riscv

# production sources, copied: <source dir>/<name>
PROD    = filemanager/dos io/command_interface/control_target io/c64/reu_preloader
SHADOW_SRCS = $(addprefix $(SHADOW)/,$(addsuffix .cc,$(notdir $(PROD))))
SHADOW_HDRS = $(addprefix $(SHADOW)/,$(addsuffix .h,$(notdir $(PROD))))

# every software/test/uci_targets/*_test.cc is its own program, linked with the
# production files and the fakes; `make test` runs them all
TESTS   = $(notdir $(basename $(wildcard $(TESTDIR)/*_test.cc)))
COMMON  = $(SHADOW_SRCS) $(TESTDIR)/uci_fakes.cc \
          $(PATH_SW)/components/mystring.cc $(PATH_SW)/components/pattern.cc
COMMON_OBJS = $(addprefix $(OUTPUT)/,$(notdir $(COMMON:.cc=.o)))
DEPS    = $(SHADOW_HDRS) $(TESTDIR)/uci_fakes.h $(TESTDIR)/uci_test_util.h $(wildcard $(STUBS)/*.h) \
          $(wildcard $(PATH_SW)/components/*.h $(PATH_SW)/io/command_interface/*.h)

# software/test/uci_targets/u64/*_test.cc: control_target.cc built with U64 (stubs_u64 first),
# for the U64-only commands; linked with the same fakes
U64TESTS = $(notdir $(basename $(wildcard $(TESTDIR)/u64/*_test.cc)))
U64OPTIONS = -I$(TESTDIR)/stubs_u64 $(CXXOPTIONS) -DU64
U64_OBJS = $(OUTPUT)/u64/control_target.o $(filter-out $(OUTPUT)/control_target.o,$(COMMON_OBJS))

.PHONY: all test clean

all: $(addprefix $(RESULT)/,$(TESTS)) $(addprefix $(RESULT)/u64_,$(U64TESTS))

# copy rules: the copy is rebuilt whenever the production file changes
define COPY_RULE
$(SHADOW)/$(notdir $(1)).cc: $(PATH_SW)/$(1).cc
	@mkdir -p $(SHADOW)
	cp $$< $$@
$(SHADOW)/$(notdir $(1)).h: $(PATH_SW)/$(1).h
	@mkdir -p $(SHADOW)
	cp $$< $$@
endef
$(foreach p,$(PROD),$(eval $(call COPY_RULE,$(p))))

vpath %.cc $(SHADOW) $(TESTDIR) $(PATH_SW)/components

$(OUTPUT)/%.o: %.cc $(DEPS)
	@mkdir -p $(OUTPUT)
	$(CXX) $(CXXOPTIONS) -c -o $@ $<

$(RESULT)/%: $(OUTPUT)/%.o $(COMMON_OBJS)
	@mkdir -p $(RESULT)
	$(CXX) $(CXXOPTIONS) -o $@ $^

$(OUTPUT)/u64/control_target.o: $(SHADOW)/control_target.cc $(DEPS) $(wildcard $(TESTDIR)/stubs_u64/*.h)
	@mkdir -p $(OUTPUT)/u64
	$(CXX) $(U64OPTIONS) -c -o $@ $<

$(OUTPUT)/u64/%_test.o: $(TESTDIR)/u64/%_test.cc $(DEPS) $(wildcard $(TESTDIR)/stubs_u64/*.h)
	@mkdir -p $(OUTPUT)/u64
	$(CXX) $(U64OPTIONS) -c -o $@ $<

$(RESULT)/u64_%: $(OUTPUT)/u64/%.o $(U64_OBJS)
	@mkdir -p $(RESULT)
	$(CXX) $(U64OPTIONS) -o $@ $^

test: all
	@rc=0; for t in $(TESTS) $(addprefix u64_,$(U64TESTS)); do $(RESULT)/$$t || rc=1; done; exit $$rc

clean:
	@rm -rf $(OUTPUT) $(RESULT)
