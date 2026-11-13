PRJ      =  test_http
FINAL    =  $(RESULT)/$(PRJ)
LWIP     =  nolwip

include ../../../common/environment.mk

INCLUDES =  $(wildcard $(addsuffix /*.h, $(VPATH)))

PATH_INC =  $(addprefix -I, $(VPATH))

CROSS     = 
CC		  = $(CROSS)gcc
CPP		  = $(CROSS)g++
LD		  = $(CROSS)ld
OBJDUMP   = $(CROSS)objdump
OBJCOPY	  = $(CROSS)objcopy
SIZE	  = $(CROSS)size

.SUFFIXES:


SRCS_C   =	dump_hex.c \
			small_printf.c \
			server.c \
			http_protocol.c \
			middleware.c \
			multipart.c \
			url.c \
			dummy_api.c

SRCS_CC	 =  mystring.cc \
			json.cc \
			http_target.cc \
			pattern.cc \
			http_request.cc \
			test_http_transport.cc \
			test_http_framing.cc \
			test_http_target.cc


SRCS_ASM =  
SRCS_6502 = 
SRCS_BIN =  
SRCS_IEC = 
SRCS_NANO = 

# FreeRTOS on POSIX threads, for the target's worker task.
PATH_INC =  -I$(PATH_SW)/io/command_interface/tests/host_rtos $(addprefix -I, $(VPATH))
VPATH += $(PATH_SW)/network/tests
OPTIONS  = -g -ffunction-sections -O0 -DRUNS_ON_PC -Wno-format -Wno-format-extra-args -DENABLE_STATIC_FILE=1 -DMHS_PORT=8001 -DTEMP_FILE_PATH=\"./\"
COPTIONS = $(OPTIONS) -std=c99 -D_DEFAULT_SOURCE
CPPOPT   = $(OPTIONS) -fno-exceptions -fno-rtti -fno-threadsafe-statics
LINK 	 = ./mb_app.lds
LIBS     =  -lgcc -pthread -Wl,--wrap=close -Wl,--wrap=gethostbyname_r -Wl,--wrap=socket -Wl,--wrap=setsockopt
LFLAGS   = --gc-sections 

include ../../../common/rules.mk

# Exercise sign extension even on hosts where plain char is signed by default.
.PHONY: test-integer-widths
test-integer-widths:
	@$(MAKE) OUTPUT=output/integer-widths RESULT=result/integer-widths \
	    CPP="$(CPP) -funsigned-char" all
	@result/integer-widths/$(PRJ) --integer-widths

$(RESULT)/$(PRJ): $(OBJS_C) $(OBJS_CC) $(OBJS_ASM) $(OBJS_6502) $(OBJS_BIN) $(OBJS_IEC) $(OBJS_NANO)
	@echo Linking...
	$(CPP) $(ALL_OBJS) $(LLIB) -o $(RESULT)/$(PRJ) $(LIBS)

# Run the removal regression independently of the external JSON fixture.
.PHONY: test-body-removal
test-body-removal:
	@$(MAKE) OUTPUT=output/body-removal RESULT=result/body-removal \
	    CC="$(CC) -fsanitize=address,undefined -fno-sanitize-recover=all" \
	    CPP="$(CPP) -fsanitize=address,undefined -fno-sanitize-recover=all" all
	# ASan must precede any libraries preloaded by the build environment.
	@LD_PRELOAD="$$($(CPP) -print-file-name=libasan.so)$${LD_PRELOAD:+:$$LD_PRELOAD}" result/body-removal/$(PRJ) --body-removal

# Exercise real refused connections on loopback without an external service.
.PHONY: test-connect-cleanup
test-connect-cleanup:
	@$(MAKE) OUTPUT=output/connect-cleanup RESULT=result/connect-cleanup \
	    CC="$(CC) -fsanitize=address,undefined -fno-sanitize-recover=all" \
	    CPP="$(CPP) -fsanitize=address,undefined -fno-sanitize-recover=all" all
	@timeout 30s env LD_PRELOAD="$$($(CPP) -print-file-name=libasan.so)$${LD_PRELOAD:+:$$LD_PRELOAD}" result/connect-cleanup/$(PRJ) --connect-cleanup

.PHONY: test-response-completion test-response-boundaries test-response-cleanup test-transport
test-response-completion test-response-boundaries test-response-cleanup test-transport:
	@$(MAKE) OUTPUT=output/response-completion RESULT=result/response-completion \
	    CC="$(CC) -fsanitize=address,undefined -fno-sanitize-recover=all" \
	    CPP="$(CPP) -fsanitize=address,undefined -fno-sanitize-recover=all" all
	@timeout 30s env LD_PRELOAD="$$($(CPP) -print-file-name=libasan.so)$${LD_PRELOAD:+:$$LD_PRELOAD}" result/response-completion/$(PRJ) --$(patsubst test-%,%,$@)

# The exchange on the target's worker: an abort while the server stalls the request or
# the response, and a server that answers slowly for longer than the timeout.
.PHONY: test-uci-worker
test-uci-worker:
	@$(MAKE) OUTPUT=output/uci-worker RESULT=result/uci-worker \
	    CC="$(CC) -fsanitize=address,undefined -fno-sanitize-recover=all" \
	    CPP="$(CPP) -DHTTP_REQUEST_TIMEOUT_MS=2000 -fsanitize=address,undefined -fno-sanitize-recover=all" all
	@timeout 60s env LD_PRELOAD="$$($(CPP) -print-file-name=libasan.so)$${LD_PRELOAD:+:$$LD_PRELOAD}" result/uci-worker/$(PRJ) --worker

# Servers that read slowly, stop reading, or never answer. The timeout is cut to
# two seconds so that the run stays short.
.PHONY: test-slow-server
test-slow-server:
	@$(MAKE) OUTPUT=output/slow-server RESULT=result/slow-server \
	    CC="$(CC) -fsanitize=address,undefined -fno-sanitize-recover=all" \
	    CPP="$(CPP) -DHTTP_REQUEST_TIMEOUT_MS=2000 -fsanitize=address,undefined -fno-sanitize-recover=all" all
	@timeout 60s env LD_PRELOAD="$$($(CPP) -print-file-name=libasan.so)$${LD_PRELOAD:+:$$LD_PRELOAD}" result/slow-server/$(PRJ) --slow-server
