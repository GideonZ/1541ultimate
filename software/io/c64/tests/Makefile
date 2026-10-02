CXX ?= g++
BUILD := build
SRC := ../../..
CXXFLAGS := -std=c++14 -g -Wall -Wno-format -Wno-unused-variable -I.. -I$(BUILD) -I. \
	-I$(SRC)/components -I$(SRC)/infra -I$(SRC)/system -I$(SRC)/filemanager -include c64_crt_host.h

all: $(BUILD)/c64CrtTest
	./$(BUILD)/c64CrtTest > $(BUILD)/loader.log

# The cartridge type constants and cart_def, taken from c64.h itself.
$(BUILD)/cart_defs.h: ../c64.h
	mkdir -p $(BUILD)
	sed -n -e '/^#define \(CART_\|VARIANT_\)/p' -e '/^typedef struct _cart/,/^} cart_def;/p' $< > $@

$(BUILD)/c64CrtTest: c64_crt_test.cc c64_crt_host.h ../c64_crt.cc ../c64_crt.h $(BUILD)/cart_defs.h
	$(CXX) $(CXXFLAGS) c64_crt_test.cc ../c64_crt.cc $(SRC)/components/mystring.cc -o $@

clean:
	rm -rf $(BUILD)

.PHONY: all clean
