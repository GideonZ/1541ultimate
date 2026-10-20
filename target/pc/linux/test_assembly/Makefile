PRJ = test_assembly
FINAL = $(RESULT)/$(PRJ)
LWIP = nolwip
include ../../../common/environment.mk

VPATH += $(PATH_SW)/io/usb/tests
CC = gcc
CPP = g++
SRCS_C = http_protocol.c multipart.c url.c
SRCS_CC = assembly.cc http_request.cc json.cc mystring.cc pattern.cc small_printf.cc \
          filemanager.cc path.cc file_system.cc filesystem_root.cc \
          filesystem_a64.cc directory.cc partition.cc blockdev.cc init_function.cc \
          test_assembly.cc
OPTIONS = -g -O0 -ffunction-sections -fdata-sections -DRUNS_ON_PC -DMHS_PORT=8001 \
          -Wno-format -Wno-format-extra-args -fsanitize=address,undefined \
          -fno-sanitize-recover=all
COPTIONS = $(OPTIONS) -std=gnu99
CPPOPT = $(OPTIONS) -std=c++17
PATH_INC = $(addprefix -I, $(VPATH))
include ../../../common/rules.mk

gitinfo:: | $(OUTPUT)
$(OBJS_C) $(OBJS_CC): | $(OUTPUT)

$(FINAL): $(OBJS_C) $(OBJS_CC) | $(RESULT)
	$(CPP) $(OPTIONS) $(ALL_OBJS) -Wl,--gc-sections \
	    -Wl,--wrap=gethostbyname_r,--wrap=connect -pthread -o $@

.PHONY: test
test: $(FINAL)
	@timeout 45s env LD_PRELOAD="$$($(CPP) -print-file-name=libasan.so)$${LD_PRELOAD:+:$$LD_PRELOAD}" $(FINAL)
