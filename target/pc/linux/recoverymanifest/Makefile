#
# Host build of the recovery manifest reader of the control module. The
# signature check is built against the mbedtls that ESP-IDF ships, compiled
# here for the host, so the test needs IDF_PATH but no ESP32 toolchain.
#
#   make          build
#   make test     build and run
#
PRJ     = recoverymanifest
RESULT  = result
OUTPUT  = output

PATH_SW = ../../../../software
IDF_PATH ?= $(HOME)/esp/esp-idf
MBEDTLS = $(IDF_PATH)/components/mbedtls/mbedtls
LIBMBED = $(OUTPUT)/mbedtls/library/libmbedcrypto.a

VPATH   = $(PATH_SW)/u64ctrl/main $(PATH_SW)/test/recovery_manifest

CC      = gcc
COPTIONS = -std=c99 -g -O0 -Wall -Wextra -Wno-unused-parameter \
           -DVECTORS_DIR=\"$(abspath $(PATH_SW)/test/recovery_manifest/vectors)\" \
           $(addprefix -I, $(VPATH)) -I$(MBEDTLS)/include

SRCS_C  = recovery_manifest.c recovery_verify.c recovery_manifest_test.c
OBJS    = $(addprefix $(OUTPUT)/, $(SRCS_C:%.c=%.o))

.PHONY: all test clean

all: $(RESULT)/$(PRJ)

$(LIBMBED):
	@test -f $(MBEDTLS)/CMakeLists.txt || { echo "mbedtls not found; set IDF_PATH to an ESP-IDF checkout"; exit 2; }
	@cmake -S $(MBEDTLS) -B $(OUTPUT)/mbedtls -DENABLE_TESTING=OFF -DENABLE_PROGRAMS=OFF \
	       -DCMAKE_BUILD_TYPE=Release >/dev/null
	@cmake --build $(OUTPUT)/mbedtls --target mbedcrypto -j8 >/dev/null

$(OUTPUT)/%.o: %.c $(LIBMBED)
	@mkdir -p $(OUTPUT)
	$(CC) $(COPTIONS) -c -o $@ $<

$(RESULT)/$(PRJ): $(OBJS) $(LIBMBED)
	@mkdir -p $(RESULT)
	$(CC) -o $@ $(OBJS) $(LIBMBED)

test: all
	@$(RESULT)/$(PRJ)

clean:
	@rm -rf $(OUTPUT) $(RESULT)
