# Host tests of the tape code: the recorder (io/tape/tape_recorder.cc) and the TAP file type
# (filetypes/filetype_tap.cc). The code under test is compiled as it is in the tree and runs
# against the stand-ins of stubs/ and tape_env.cc: a model of the recorder registers (taken from
# fpga/io/c2n_record/vhdl_source/c2n_record.vhd), a fake FileManager that counts opens and
# closes, a model of the subsystem mutex, a scripted RTOS task and delay, and global allocation
# counters. No hardware needed.
#
# Every file *_test.cpp in this directory is one test program. What it tests is named below,
# one line for each program, so that adding a program adds one line there and nothing else
# changes.
#
# The recorder's four RECORD_* register macros (raw stores and loads at a fixed bus address)
# are the only thing changed for the build, and it is done mechanically: tape_recorder.h is
# copied to output/gen with them replaced by an #include of tape_regs.h, and tape_recorder.cc
# is copied there unchanged, so that it finds that header first. The rest of tape_recorder.h
# and the whole of tape_recorder.cc are the tree's.
#
#   make              all the tests
#   make asan         the same under AddressSanitizer and UBSan
#   make asan-native  the same with the sanitizer's own leak and delete checks (no allocation counters)
#   make tsan         the same under ThreadSanitizer
#
CXX ?= g++
SW := ../../..
TAPE := $(SW)/io/tape
GEN := output/gen
CXXFLAGS := -std=c++14 -g -DRUNS_ON_PC -Wno-format \
	-Istubs -I. -I$(GEN) -I$(SW)/io/usb/tests -I$(SW)/components -I$(SW)/filesystem -I$(SW)/filemanager \
	-I$(SW)/infra -I$(SW)/system -I$(SW)/userinterface -I$(SW)/filetypes -I$(SW)/portable/riscv \
	-I$(SW)/httpd/c-version/lib -I$(SW)/io/stream
SAN := -fsanitize=address,undefined -fno-omit-frame-pointer
TSAN := -fsanitize=thread -fno-omit-frame-pointer

SUITES := $(patsubst %.cpp,%,$(wildcard *_test.cpp))
COMMON := tape_env.cc $(SW)/io/usb/tests/host_test_main.cpp $(SW)/components/mystring.cc $(SW)/components/pattern.cc
HEADERS := $(wildcard stubs/*.h *.h)

# the code each program tests
REAL_tape_recorder_test := $(GEN)/tape_recorder.cc
REAL_tape_recorder_race_test := $(GEN)/tape_recorder.cc
REAL_tape_file_test := $(SW)/filetypes/filetype_tap.cc

.PHONY: all asan asan-native tsan
.SECONDEXPANSION:

all: $(addprefix output/,$(SUITES))
	@set -e; for t in $^; do ./$$t; done

asan: $(addprefix output/,$(addsuffix _asan,$(SUITES)))
	@set -e; for t in $^; do env -u LD_PRELOAD ASAN_OPTIONS=detect_leaks=0 ./$$t; done

# The sanitizer's own view: no counting new/delete (the counters then read zero), so that
# AddressSanitizer reports mismatched deletes and LeakSanitizer reports what is never freed.
# Run with ASAN_OPTIONS as needed, for example detect_leaks=1:alloc_dealloc_mismatch=0.
asan-native: $(addprefix output/,$(addsuffix _asan_native,$(SUITES)))
	@set -e; for t in $^; do env -u LD_PRELOAD ./$$t; done

tsan: $(addprefix output/,$(addsuffix _tsan,$(SUITES)))
	@set -e; for t in $^; do env -u LD_PRELOAD ./$$t; done

$(GEN)/tape_recorder.h: $(TAPE)/tape_recorder.h
	mkdir -p $(GEN)
	sed -e 's/^#define RECORD_STATUS .*/#include "tape_regs.h"/' -e '/^#define RECORD_\(CONTROL\|DATA\|DATA32\) /d' $< > $@
	@grep -q 'tape_regs.h' $@
	@if grep -q '^#define RECORD_' $@; then echo "register macros left in $@"; exit 1; fi

$(GEN)/tape_recorder.cc: $(TAPE)/tape_recorder.cc $(GEN)/tape_recorder.h
	cp $< $@

output/%: %.cpp $$(REAL_$$*) $(COMMON) $(HEADERS)
	mkdir -p output
	$(CXX) $(CXXFLAGS) $< $(REAL_$*) $(COMMON) -lpthread -o $@

output/%_asan: %.cpp $$(REAL_$$*) $(COMMON) $(HEADERS)
	mkdir -p output
	$(CXX) $(CXXFLAGS) $(SAN) $< $(REAL_$*) $(COMMON) -lpthread -o $@

output/%_asan_native: %.cpp $$(REAL_$$*) $(COMMON) $(HEADERS)
	mkdir -p output
	$(CXX) $(CXXFLAGS) $(SAN) -DTAPE_NO_ALLOC_COUNT $< $(REAL_$*) $(COMMON) -lpthread -o $@

output/%_tsan: %.cpp $$(REAL_$$*) $(COMMON) $(HEADERS)
	mkdir -p output
	$(CXX) $(CXXFLAGS) $(TSAN) -DTAPE_NO_ALLOC_COUNT $< $(REAL_$*) $(COMMON) -lpthread -o $@
