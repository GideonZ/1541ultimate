CXX ?= g++
CXXFLAGS := -std=c++14 -g -I. -I.. -I../../components -I../../filesystem -I../../infra -I../../system -I../../userinterface -I../../io/usb/tests

all: temp-auto-cleanup

temp-auto-cleanup:
	$(CXX) $(CXXFLAGS) temp_auto_cleanup_test.cpp ../../io/usb/tests/host_test_main.cpp ../filemanager.cc ../path.cc ../../filesystem/file_system.cc ../../filesystem/filesystem_root.cc ../../filesystem/directory.cc ../../filesystem/partition.cc ../../components/pattern.cc ../../components/mystring.cc -lpthread -o tempAutoCleanupTest && ./tempAutoCleanupTest
