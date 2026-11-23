CXX ?= g++
CXXFLAGS := -std=c++14 -g -I.. -I../../components -I../../system -I../../io/stream -I../../io/usb/tests
STATE_TEST_FLAGS := $(CXXFLAGS) -DNO_FILE_ACCESS -I../../io/c64 -I../../io/usb -I../../infra

all: input-api input-api-state json-multiroot

input-api:
	$(CXX) $(CXXFLAGS) input_api_validation_test.cpp ../json.cc ../../components/mystring.cc ../../components/pattern.cc ../../system/small_printf.cc ../../io/usb/tests/host_test_main.cpp -lpthread -o inputApiValidationTest && ./inputApiValidationTest

input-api-state:
	$(CXX) $(STATE_TEST_FLAGS) input_api_state_test.cpp ../../io/usb/keyboard_usb.cc ../../io/c64/joystick_output.cc ../../io/c64/keyboard_c64.cc ../../io/usb/tests/host_test_main.cpp -lpthread -o inputApiStateTest && ./inputApiStateTest

json-multiroot:
	$(CXX) $(CXXFLAGS) json_multiroot_test.cpp ../json.cc ../../components/mystring.cc ../../components/pattern.cc ../../system/small_printf.cc ../../io/usb/tests/host_test_main.cpp -lpthread -o jsonMultiRootTest && ./jsonMultiRootTest

# Same test under AddressSanitizer with leak detection, which is what catches
# the objects convert() used to drop. Wants running in CI as well.
json-leak:
	$(CXX) $(CXXFLAGS) -fsanitize=address -fno-omit-frame-pointer json_multiroot_test.cpp ../json.cc ../../components/mystring.cc ../../components/pattern.cc ../../system/small_printf.cc ../../io/usb/tests/host_test_main.cpp -lpthread -o jsonMultiRootLeakTest && ASAN_OPTIONS=detect_leaks=1 ./jsonMultiRootLeakTest
