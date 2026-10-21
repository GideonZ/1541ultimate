#
# Host build of the recovery manifest reader of the control module. The
# signature check is built against the TF-PSA-Crypto that ESP-IDF 6 ships
# (mbedTLS 4), compiled here for the host, so the test needs IDF_PATH but no
# ESP32 toolchain. The control module is built against that same library, so
# this must run with an ESP-IDF 6 checkout, not the 5.3.1 of my_docker_image.
#
#   make          build
#   make test     build and run
#
PRJ     = recoverymanifest
RESULT  = result
OUTPUT  = output

PATH_SW = ../../../../software
IDF_PATH ?= $(HOME)/esp/esp-idf
TFPSA   = $(IDF_PATH)/components/mbedtls/mbedtls/tf-psa-crypto
LIBMBED = $(OUTPUT)/tfpsa/core/libtfpsacrypto.a

VPATH   = $(PATH_SW)/u64ctrl/main $(PATH_SW)/test/recovery_manifest

CC      = gcc
COPTIONS = -std=c99 -g -O0 -Wall -Wextra -Wno-unused-parameter \
           -DVECTORS_DIR=\"$(abspath $(PATH_SW)/test/recovery_manifest/vectors)\" \
           $(addprefix -I, $(VPATH)) -I$(TFPSA)/include -I$(TFPSA)/drivers/builtin/include

SRCS_C  = recovery_manifest.c recovery_verify.c recovery_manifest_test.c
OBJS    = $(addprefix $(OUTPUT)/, $(SRCS_C:%.c=%.o))

.PHONY: all test clean

all: $(RESULT)/$(PRJ)

$(LIBMBED):
	@test -f $(TFPSA)/CMakeLists.txt || { echo "TF-PSA-Crypto not found; set IDF_PATH to an ESP-IDF 6 checkout"; exit 2; }
	@# This ESP-IDF's bignum.c and ecp.c include mbedtls/bignum.h and mbedtls/ecp.h,
	@# which only ESP-IDF's own port provides; the shims stand in for it. The
	@# pre-generated sources stand in for GEN_FILES, which needs python's jsonschema.
	@mkdir -p $(OUTPUT)/shim/mbedtls
	@for h in bignum ecp; do echo "#include \"mbedtls/private/$$h.h\"" > $(OUTPUT)/shim/mbedtls/$$h.h; done
	@cmake -S $(TFPSA) -B $(OUTPUT)/tfpsa -DENABLE_TESTING=OFF -DENABLE_PROGRAMS=OFF -DGEN_FILES=OFF \
	       -DCMAKE_C_FLAGS=-I$(abspath $(OUTPUT))/shim -DCMAKE_BUILD_TYPE=Release >/dev/null
	@cmake --build $(OUTPUT)/tfpsa --target tfpsacrypto -j8 >/dev/null

$(OUTPUT)/%.o: %.c $(LIBMBED)
	@mkdir -p $(OUTPUT)
	$(CC) $(COPTIONS) -c -o $@ $<

$(RESULT)/$(PRJ): $(OBJS) $(LIBMBED)
	@mkdir -p $(RESULT)
	$(CC) -o $@ $(OBJS) $(LIBMBED) -Wl,--wrap=psa_crypto_init -Wl,--wrap=psa_hash_update -Wl,--wrap=psa_hash_finish

test: all
	@$(RESULT)/$(PRJ)

clean:
	@rm -rf $(OUTPUT) $(RESULT)
