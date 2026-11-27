CXX ?= g++
CXXFLAGS := -std=c++14 -g -I. -I../../../components -I../../../system -I../../../io/stream
KEYBOARD_TEST_FLAGS := $(CXXFLAGS) -I../../c64 -DNO_FILE_ACCESS

VT100_TEST_FLAGS := $(CXXFLAGS) -I../../c64

all: mouse keyboard keyboard-queue keyboard-c64 selection parser-faults vt100-escape bot-policy scsi-exec

mouse:
	$(CXX) $(CXXFLAGS) usb_hid_mouse_test.cpp host_test_main.cpp -lpthread -o usbHidMouseTest && ./usbHidMouseTest

keyboard:
	$(CXX) $(CXXFLAGS) usb_hid_keyboard_test.cpp host_test_main.cpp -lpthread -o usbHidKeyboardTest && ./usbHidKeyboardTest

keyboard-queue:
	$(CXX) $(KEYBOARD_TEST_FLAGS) usb_keyboard_queue_test.cpp ../keyboard_usb.cc host_test_main.cpp -lpthread -o usbKeyboardQueueTest && ./usbKeyboardQueueTest

keyboard-c64:
	$(CXX) $(KEYBOARD_TEST_FLAGS) -I.. -I../../../infra keyboard_c64_clear_test.cpp ../../c64/keyboard_c64.cc ../keyboard_usb.cc host_test_main.cpp -lpthread -o keyboardC64ClearTest && ./keyboardC64ClearTest

selection:
	$(CXX) $(CXXFLAGS) usb_hid_selection_test.cpp host_test_main.cpp -lpthread -o usbHidSelectionTest && ./usbHidSelectionTest

parser-faults:
	$(CXX) $(CXXFLAGS) usb_hid_parser_faults_test.cpp host_test_main.cpp -lpthread -o usbHidParserFaultsTest && ./usbHidParserFaultsTest

vt100-escape:
	$(CXX) $(VT100_TEST_FLAGS) vt100_escape_test.cpp ../../stream/keyboard_vt100.cc host_test_main.cpp -lpthread -o vt100EscapeTest && ./vt100EscapeTest

bot-policy:
	$(CXX) $(CXXFLAGS) usb_bot_policy_test.cpp -o usbBotPolicyTest && ./usbBotPolicyTest

# The production usb_scsi.cc against a scripted USB host layer (scsi_sim.cpp). scsi_stubs comes first so that its
# FreeRTOS, itu.h and task headers win over the ones in system/ and in this directory.
SCSI_FLAGS := -std=c++14 -g -w -D_NO_FILE_ACCESS -Iscsi_stubs -I.. -I../../../components -I../../../system -I../../../filesystem -I../../../filemanager -I../../../portable/riscv -I../../../infra
SCSI_SRCS := usb_scsi_exec_test.cpp scsi_sim.cpp ../usb_scsi.cc ../../../filesystem/blockdev.cc ../../../components/mystring.cc ../../../components/pattern.cc

scsi-exec:
	$(CXX) $(SCSI_FLAGS) $(SCSI_SRCS) -o usbScsiExecTest && ./usbScsiExecTest
