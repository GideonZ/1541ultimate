#!/bin/bash
# Builds the Mbed TLS the test links against: the ESP-IDF v6.1.1 submodule (4.1.1 plus 18 commits), default config but for
# tlsguard_mbedtls_config.h, as one static lib. Runs in the espressif/idf:v6.1.1 image, which carries the source.
# The submodule copy has no top-level framework/ dir, so upstream's CMake cannot run: the sources are compiled directly.
#   usage: build-mbedtls.sh <out dir>      writes <out dir>/libmbed.a
set -e
OUT=$(mkdir -p "$1" && cd "$1" && pwd)
HERE=$(cd "$(dirname "$0")" && pwd)
CFG=$HERE/../../../../software/test/tls_engine
CFG=$(cd "$CFG" && pwd)
docker run --rm --name tlsguard-mbedbuild -v "$CFG:/cfg:ro" -v "$OUT:/build" espressif/idf:v6.1.1 bash -c '
 set -e; cd /opt/esp/idf/components/mbedtls/mbedtls
 INC="-Iinclude -Itf-psa-crypto/dispatch -Itf-psa-crypto/platform -Ilibrary -Itf-psa-crypto/extras -Itf-psa-crypto/include -Itf-psa-crypto/core -Itf-psa-crypto/utilities -Itf-psa-crypto/drivers/builtin/include -Itf-psa-crypto/drivers/builtin/src -Itf-psa-crypto/drivers/everest/include -I/cfg -I/build/shim"
 mkdir -p /build/shim/mbedtls /build/obj; echo "/* the tree names these headers in bignum.c and ecp.c and does not ship them */" | tee /build/shim/mbedtls/bignum.h > /build/shim/mbedtls/ecp.h && rm -f /build/obj/*.o
 for f in library/*.c tf-psa-crypto/extras/*.c tf-psa-crypto/utilities/*.c tf-psa-crypto/platform/*.c tf-psa-crypto/core/*.c tf-psa-crypto/drivers/builtin/src/*.c; do
      gcc -std=c99 -g -O0 -w -D_DEFAULT_SOURCE -DMBEDTLS_USER_CONFIG_FILE=\"tlsguard_mbedtls_config.h\" $INC -c $f -o /build/obj/$(echo $f | tr / _).o || echo "FAIL $f"
 done
 rm -f /build/libmbed.a; ar rcs /build/libmbed.a /build/obj/*.o; ls -la /build/libmbed.a'
