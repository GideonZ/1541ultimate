# Host test of the TLS version-confusion guard in software/wifi/tls/tls_engine.c (Mbed TLS advisory 2026-11), device-free.
# tls_engine.c is compiled as is, against Mbed TLS 4.1.1 (the ESP-IDF v6.1.1 submodule) with its default config; ESP-IDF,
# FreeRTOS and the radio entropy gate are stand-ins (software/test/tls_engine/stub).
#   make MBEDTLS_SRC=<mbedtls tree> MBEDTLS_LIB=<libmbed.a> test    green: the tree's engine, with its guard
#   make ... red                                                   red: a copy of the engine with the guard branch disabled
# build-mbedtls.sh (next to this Makefile) builds the library inside the espressif/idf:v6.1.1 image, which carries the source.
OUTPUT = output
RESULT = result

PATH_SW = ../../../../software
TEST    = $(PATH_SW)/test/tls_engine
TLS_DIR = $(PATH_SW)/wifi/tls

MBEDTLS_SRC ?= mbedtls
MBEDTLS_LIB ?= libmbed.a
ENGINE_SRC  ?= $(abspath $(TLS_DIR)/tls_engine.c)

MBED_INC = -I$(MBEDTLS_SRC)/include -I$(MBEDTLS_SRC)/library -I$(MBEDTLS_SRC)/tf-psa-crypto/include \
           -I$(MBEDTLS_SRC)/tf-psa-crypto/core -I$(MBEDTLS_SRC)/tf-psa-crypto/extras \
           -I$(MBEDTLS_SRC)/tf-psa-crypto/utilities -I$(MBEDTLS_SRC)/tf-psa-crypto/platform \
           -I$(MBEDTLS_SRC)/tf-psa-crypto/dispatch -I$(MBEDTLS_SRC)/tf-psa-crypto/drivers/builtin/include \
           -I$(MBEDTLS_SRC)/tf-psa-crypto/drivers/builtin/src
CFLAGS = -std=gnu11 -DMBEDTLS_USER_CONFIG_FILE=\"tlsguard_mbedtls_config.h\" -g -O0 -Wall -Wextra -Wno-unused-parameter -Wno-sign-compare -I$(TEST)/stub -I$(TEST) -I$(TLS_DIR) $(MBED_INC)

# The guard is the TLS 1.2 ServerHello branch of tls_note_handshake() that tests tls_version; the red copy makes it unreachable.
GUARD_LINE = } else if (s->ssl.tls_version != MBEDTLS_SSL_VERSION_TLS1_2) {

.PHONY: all test red clean

all: $(RESULT)/tlsguard

$(RESULT)/tlsguard: $(TEST)/tls_guard_test.c $(TEST)/stub_rng_gate.c $(ENGINE_SRC)
	@mkdir -p $(RESULT)
	$(CC) $(CFLAGS) -DTLS_ENGINE_SRC='"$(ENGINE_SRC)"' -o $@ $(TEST)/tls_guard_test.c $(TEST)/stub_rng_gate.c $(MBEDTLS_LIB)

test: all
	env -u LD_PRELOAD ./$(RESULT)/tlsguard

$(OUTPUT)/tls_engine_noguard.c: $(TLS_DIR)/tls_engine.c
	@mkdir -p $(OUTPUT)
	@test `grep -cF '$(GUARD_LINE)' $<` = 1 || { echo "guard line not found exactly once"; exit 3; }
	sed 's/} else if (s->ssl.tls_version != MBEDTLS_SSL_VERSION_TLS1_2) {/} else if (0 \&\& s->ssl.tls_version != MBEDTLS_SSL_VERSION_TLS1_2) {/' $< > $@
	@test `grep -cF 'else if (0 && s->ssl.tls_version' $@` = 1 || { echo "red copy not made"; exit 3; }

red: $(OUTPUT)/tls_engine_noguard.c
	$(MAKE) ENGINE_SRC=$(abspath $<) RESULT=$(OUTPUT)/red all
	-env -u LD_PRELOAD ./$(OUTPUT)/red/tlsguard

clean:
	rm -rf $(OUTPUT) $(RESULT) $(OUTPUT)/red
