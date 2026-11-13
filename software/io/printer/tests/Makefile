CXX ?= g++
CXXFLAGS := -std=c++11 -g -Wall -Wextra -Werror -DPRINTER_SPOOL_HOST_TEST -Ihost_rtos -I.. $(EXTRA)
# iec_printer.cc itself, against stand-ins for the rest of the firmware (a private copy: the real
# mps_printer.h sits next to it). The copy needs one change for a 64 bit host: a string used as an int.
CMDFLAGS := -std=c++14 -g -fpermissive -w -DPRINTER_SPOOL_HOST_TEST -DIEC_PRINTER_FLUSH_WAIT_MS=300 -Ihost_printer -Ihost_rtos -Ibuild -I. $(EXTRA)

all: spool printer-cmd

# 'make EXTRA="-fsanitize=address,undefined"' for the sanitizers
spool:
	$(CXX) $(CXXFLAGS) spool_test.cpp -lpthread -o spoolTest
	timeout 120 ./spoolTest

printer-cmd:
	mkdir -p build
	cp ../iec_printer.cc ../iec_printer.h ../printer_spool.h build/
	sed 's|(int) FS_ROOT "printer"|0|' ../iec_printer.cc > build/iec_printer.cc
	$(CXX) $(CMDFLAGS) printer_cmd_test.cpp build/iec_printer.cc -lpthread -o cmdTest
	timeout 120 ./cmdTest

clean:
	rm -rf spoolTest spoolTest.dSYM cmdTest cmdTest.dSYM build

.PHONY: all spool printer-cmd clean
