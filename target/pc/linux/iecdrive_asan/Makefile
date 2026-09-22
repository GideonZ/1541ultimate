# The drive suite of ../iecdrive under AddressSanitizer, which turns a use after free or a
# double free in the drive into an immediate report. Run it from this directory with
# ASAN_OPTIONS=detect_leaks=0, because the suite does not free what it set up at exit.
include ../iecdrive/Makefile

OPTIONS := $(OPTIONS) -fsanitize=address -fno-omit-frame-pointer
LIBS := $(LIBS) -fsanitize=address
