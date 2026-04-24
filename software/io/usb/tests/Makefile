CXX ?= g++
CXXFLAGS := -std=c++14 -g -I. -I../../../components -I../../../system
KEYBOARD_TEST_FLAGS := $(CXXFLAGS) -I../../c64 -DNO_FILE_ACCESS
OUTPUT_DIR := output
MOUSE_TEST := $(OUTPUT_DIR)/usbHidMouseTest
KEYBOARD_TEST := $(OUTPUT_DIR)/usbHidKeyboardTest
KEYBOARD_QUEUE_TEST := $(OUTPUT_DIR)/usbKeyboardQueueTest
SELECTION_TEST := $(OUTPUT_DIR)/usbHidSelectionTest

.PHONY: all mouse keyboard keyboard-queue selection

all: mouse keyboard keyboard-queue selection

mouse:
	mkdir -p $(OUTPUT_DIR)
	$(CXX) $(CXXFLAGS) usb_hid_mouse_test.cpp host_test_main.cpp -lpthread -o $(MOUSE_TEST) && $(MOUSE_TEST)

keyboard:
	mkdir -p $(OUTPUT_DIR)
	$(CXX) $(CXXFLAGS) usb_hid_keyboard_test.cpp host_test_main.cpp -lpthread -o $(KEYBOARD_TEST) && $(KEYBOARD_TEST)

keyboard-queue:
	mkdir -p $(OUTPUT_DIR)
	$(CXX) $(KEYBOARD_TEST_FLAGS) usb_keyboard_queue_test.cpp ../keyboard_usb.cc host_test_main.cpp -lpthread -o $(KEYBOARD_QUEUE_TEST) && $(KEYBOARD_QUEUE_TEST)

selection:
	mkdir -p $(OUTPUT_DIR)
	$(CXX) $(CXXFLAGS) usb_hid_selection_test.cpp host_test_main.cpp -lpthread -o $(SELECTION_TEST) && $(SELECTION_TEST)
