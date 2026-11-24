# Host test of the TLS version-confusion guard in software/wifi/tls/tls_engine.c (Mbed TLS advisory 2026-11), device-free.
# tls_engine.c is compiled as is, against Mbed TLS 4.1.1 (the ESP-IDF v6.1.1 submodule) with its default config; ESP-IDF,
# FreeRTOS and the radio entropy gate are stand-ins (software/test/tls_engine/stub). Not part of CI: it needs the IDF image.
#
# How to run (from the repository root; Docker must be able to mount the paths, so use a directory under $HOME):
#   LIB=$HOME/tlsguard-lib
#   target/pc/linux/tlsguard/build-mbedtls.sh $LIB/411                 # Mbed TLS 4.1.1 as in the IDF v6.1.1 image
#   MBEDTLS_PATCH=<fix.diff> target/pc/linux/tlsguard/build-mbedtls.sh $LIB/fixed    # optional: a 4.1.x with the upstream fix
#   docker run --rm -v "$PWD:/w" -v "$LIB/411:/build" espressif/idf:v6.1.1 bash -c \
#     'cd /w/target/pc/linux/tlsguard && make MBEDTLS_SRC=/opt/esp/idf/components/mbedtls/mbedtls MBEDTLS_LIB=/build/libmbed.a test red'
# (MBEDTLS_SRC is the source inside the image, for the headers; MBEDTLS_LIB is the library built above. For the fixed
# library mount $LIB/fixed on /build instead; the headers are the same.)
#
#   make test   the guard test (default mode) on the tree's engine, then the "barrier" mode on a copy of the engine with the
#               guard disabled (the second barrier). Exit 0 only if both pass, on 4.1.1 and on the fixed library alike.
#   make red    the guard test on that guard-disabled copy: on 4.1.1 it must FAIL (the guard is what stops the attack), and
#               make red exits non-zero if it passes. On the fixed library the library refuses before the guard is reached,
#               so red is not applicable; it says so and exits 0.
#
# test_key.h and test_cert.h are a throwaway test key (EC P-256, PKCS#8 DER) and its self-signed certificate; the key
# protects nothing. Generated with:
#   openssl ecparam -name prime256v1 -genkey -noout | openssl pkcs8 -topk8 -nocrypt -outform der -out key.der
#   openssl req -new -x509 -key key.der -keyform der -subj "/CN=tlsguard test server" -days 36500 -sha256 -outform der -out cert.der
#   xxd -i key.der  | sed 's/unsigned char key_der/static const unsigned char test_key_der/'  > test_key.h
#   xxd -i cert.der | sed 's/unsigned char cert_der/static const unsigned char test_cert_der/' > test_cert.h
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

.PHONY: all test noguard red clean

all: $(RESULT)/tlsguard

$(RESULT)/tlsguard: $(TEST)/tls_guard_test.c $(TEST)/stub_rng_gate.c $(ENGINE_SRC) $(MBEDTLS_LIB)
	@mkdir -p $(RESULT)
	$(CC) $(CFLAGS) -DTLS_ENGINE_SRC='"$(ENGINE_SRC)"' -o $@ $(TEST)/tls_guard_test.c $(TEST)/stub_rng_gate.c $(MBEDTLS_LIB)

test: all noguard
	./$(RESULT)/tlsguard
	./$(OUTPUT)/red/tlsguard barrier

$(OUTPUT)/tls_engine_noguard.c: $(TLS_DIR)/tls_engine.c
	@mkdir -p $(OUTPUT)
	@test `grep -cF '$(GUARD_LINE)' $<` = 1 || { echo "guard line not found exactly once"; exit 3; }
	sed 's/} else if (s->ssl.tls_version != MBEDTLS_SSL_VERSION_TLS1_2) {/} else if (0 \&\& s->ssl.tls_version != MBEDTLS_SSL_VERSION_TLS1_2) {/' $< > $@
	@test `grep -cF 'else if (0 && s->ssl.tls_version' $@` = 1 || { echo "red copy not made"; exit 3; }

noguard: $(OUTPUT)/tls_engine_noguard.c
	$(MAKE) ENGINE_SRC=$(abspath $<) RESULT=$(OUTPUT)/red all

# RED must be red: the guard-disabled build has to fail the attack checks. If the library refuses the ServerHello itself
# (a fixed Mbed TLS) the guard is never reached and the comparison means nothing: say so.
red: noguard
	@./$(OUTPUT)/red/tlsguard > $(OUTPUT)/red.log; rc=$$?; cat $(OUTPUT)/red.log; \
	if grep -q '^case: library refuses first' $(OUTPUT)/red.log; then \
	  echo "RED: not applicable: library refuses first (the guard is not reached on this Mbed TLS)"; exit 0; \
	elif [ $$rc -eq 0 ]; then \
	  echo "RED IS NOT RED: the guard-disabled build passed on an Mbed TLS that accepts the ServerHello"; exit 1; \
	else echo "RED is red as expected: the guard-disabled build fails ($$rc)"; exit 0; fi

clean:
	rm -rf $(OUTPUT) $(RESULT)
