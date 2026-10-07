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
			test_http_target.cc


SRCS_ASM =  
SRCS_6502 = 
SRCS_BIN =  
SRCS_IEC = 
SRCS_NANO = 

PATH_INC =  $(addprefix -I, $(VPATH))
OPTIONS  = -g -ffunction-sections -O0 -D_DEFAULT_SOURCE -DRUNS_ON_PC -Wno-format -Wno-format-extra-args -DENABLE_STATIC_FILE=1 -DMHS_PORT=8001 -DTEMP_FILE_PATH=\"./\"
COPTIONS = $(OPTIONS) -std=c99
CPPOPT   = $(OPTIONS) -fno-exceptions -fno-rtti -fno-threadsafe-statics
LINK 	 = ./mb_app.lds
LIBS     =  -lgcc 
LFLAGS   = --gc-sections 

ifeq ($(TEST_HTTP_REAL_TLS),1)
VPATH += $(PATH_SW)/network/https $(PATH_SW)/network/https/tests
PATH_INC += -I$(MBEDTLS_SOURCE)/include
OPTIONS += -DTEST_HTTP_REAL_TLS
SRCS_C += tls_stream.c https_client.c
SRCS_CC += tls_uci_connection.cc
LIBS += $(MBEDTLS_BUILD)/library/libmbedtls.a $(MBEDTLS_BUILD)/library/libmbedx509.a $(MBEDTLS_BUILD)/library/libmbedcrypto.a
endif

include ../../../common/rules.mk

# Exercise sign extension even on hosts where plain char is signed by default.
.PHONY: test-secure-exchange
test-secure-exchange:
	@mkdir -p output/secure-exchange result/secure-exchange
	@$(MAKE) OUTPUT=output/secure-exchange RESULT=result/secure-exchange \
	    CC="$(CC) -fsanitize=address,undefined -fno-sanitize-recover=all" \
	    CPP="$(CPP) -fsanitize=address,undefined -fno-sanitize-recover=all" all
	@LD_PRELOAD="$$($(CPP) -print-file-name=libasan.so)$${LD_PRELOAD:+:$$LD_PRELOAD}" result/secure-exchange/$(PRJ) --secure-exchange

.PHONY: test-real-tls
test-real-tls:
	@test -n "$(MBEDTLS_SOURCE)" -a -n "$(MBEDTLS_BUILD)" || (echo "Set MBEDTLS_SOURCE and MBEDTLS_BUILD (see HTTPS tests README)"; exit 1)
	@mkdir -p output/real-tls result/real-tls
	@$(MAKE) TEST_HTTP_REAL_TLS=1 OUTPUT=output/real-tls RESULT=result/real-tls \
	    CC="$(CC) -D_DEFAULT_SOURCE -fsanitize=address,undefined -fno-sanitize-recover=all" \
	    CPP="$(CPP) -fsanitize=address,undefined -fno-sanitize-recover=all" all
	@python3 $(PATH_SW)/network/https/tests/test_tls_uci.py --probe result/real-tls/$(PRJ)

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
