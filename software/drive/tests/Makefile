CXX ?= g++
CXXFLAGS := -std=c++14 -g -Wall -Wextra -I. -I.. -I../../io/usb/tests
OUTPUT_DIR := output
GCR_TRACK_BOUNDS_TEST := $(OUTPUT_DIR)/gcrTrackBoundsTest

.PHONY: all gcr-track-bounds clean

all: gcr-track-bounds

gcr-track-bounds:
	mkdir -p $(OUTPUT_DIR)
	$(CXX) $(CXXFLAGS) gcr_track_bounds_test.cpp ../../io/usb/tests/host_test_main.cpp -lpthread -o $(GCR_TRACK_BOUNDS_TEST) && $(GCR_TRACK_BOUNDS_TEST)

clean:
	rm -rf $(OUTPUT_DIR)
