CXX ?= g++
CXXFLAGS := -std=c++14 -g -I. -I../../../components -I../../../system
KEYBOARD_TEST_FLAGS := $(CXXFLAGS) -I../../c64 -DNO_FILE_ACCESS

all: mouse keyboard keyboard-queue selection u64-config-source

mouse:
	$(CXX) $(CXXFLAGS) usb_hid_mouse_test.cpp host_test_main.cpp -lpthread -o usbHidMouseTest && ./usbHidMouseTest

keyboard:
	$(CXX) $(CXXFLAGS) usb_hid_keyboard_test.cpp host_test_main.cpp -lpthread -o usbHidKeyboardTest && ./usbHidKeyboardTest

keyboard-queue:
	$(CXX) $(KEYBOARD_TEST_FLAGS) usb_keyboard_queue_test.cpp ../keyboard_usb.cc host_test_main.cpp -lpthread -o usbKeyboardQueueTest && ./usbKeyboardQueueTest

selection:
	$(CXX) $(CXXFLAGS) usb_hid_selection_test.cpp host_test_main.cpp -lpthread -o usbHidSelectionTest && ./usbHidSelectionTest

u64-config-source:
	$(CXX) $(CXXFLAGS) u64_mouse_config_source_test.cpp host_test_main.cpp -lpthread -o usbHidU64MouseConfigSourceTest && ./usbHidU64MouseConfigSourceTest
