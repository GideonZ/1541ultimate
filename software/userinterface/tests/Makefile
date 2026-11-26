# Host test of UserInterface::postMessage / getMessage (userinterface.cc). The two
# functions are cut out of userinterface.cc as they are (io/wifi/tests/extract.awk) and
# built against a real bounded queue (message_queue_env.h), because the stand-ins of the
# other host suites ignore xQueueSend's result and so hide a lost message.
#
#   make          build and run
#   make leak     the same under AddressSanitizer with leak detection
CXX ?= g++
SW := ../..
EXTRACT := $(SW)/io/wifi/tests/extract.awk
CXXFLAGS := -std=c++14 -g -O0 -I. -I$(SW)/components -I$(SW)/io/usb/tests

all: output/post_message.cc
	$(CXX) $(CXXFLAGS) message_queue_test.cpp $(SW)/components/mystring.cc $(SW)/io/usb/tests/host_test_main.cpp -lpthread -o output/messageQueueTest && ./output/messageQueueTest

leak: output/post_message.cc
	$(CXX) $(CXXFLAGS) -fsanitize=address -fno-omit-frame-pointer message_queue_test.cpp $(SW)/components/mystring.cc $(SW)/io/usb/tests/host_test_main.cpp -lpthread -o output/messageQueueLeakTest && ASAN_OPTIONS=detect_leaks=1 ./output/messageQueueLeakTest

output/post_message.cc: $(SW)/userinterface/userinterface.cc $(EXTRACT)
	@mkdir -p output
	{ for f in postMessage getMessage; do awk -v f="$$f" -f $(EXTRACT) $(SW)/userinterface/userinterface.cc; done; } | tr -d '\r' > $@
	@test `grep -c "UserInterface" $@` -eq 2 || { echo "extraction failed"; exit 1; }

clean:
	rm -rf output
