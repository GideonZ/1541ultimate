CXX ?= g++
CXXFLAGS := -std=c++14 -g -I.. -I../../components -I../../system -I../../io/stream -I../../io/usb/tests
STATE_TEST_FLAGS := $(CXXFLAGS) -DNO_FILE_ACCESS -I../../io/c64

all: input-api input-api-state

input-api:
	$(CXX) $(CXXFLAGS) input_api_validation_test.cpp ../json.cc ../../components/mystring.cc ../../system/small_printf.cc ../../io/usb/tests/host_test_main.cpp -lpthread -o inputApiValidationTest && ./inputApiValidationTest

input-api-state:
	$(CXX) $(STATE_TEST_FLAGS) input_api_state_test.cpp ../../io/usb/keyboard_usb.cc ../../io/c64/joystick_output.cc ../../io/usb/tests/host_test_main.cpp -lpthread -o inputApiStateTest && ./inputApiStateTest
