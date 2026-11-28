#!/bin/bash
# Builds the Mbed TLS the test links against: the ESP-IDF v6.1.1 submodule (4.1.1 plus 18 commits), default config but for
# tlsguard_mbedtls_config.h, as one static lib. Runs in the espressif/idf:v6.1.1 image, which carries the source.
# The submodule copy has no top-level framework/ dir, so upstream's CMake cannot run: the sources are compiled directly.
#   usage: [MBEDTLS_PATCH=<diff>] build-mbedtls.sh <out dir>      writes <out dir>/libmbed.a
# With MBEDTLS_PATCH (a unified diff, -p1 against the Mbed TLS tree) the build is of a scratch copy of the tree with the
# diff applied, e.g. a fixed 4.1.x, so the test can be run against a library that refuses the ServerHello itself.
# Any compile or patch error fails the script.
set -e
OUT=$(mkdir -p "$1" && cd "$1" && pwd)
HERE=$(cd "$(dirname "$0")" && pwd)
CFG=$(cd "$HERE/../../../../software/test/tls_engine" && pwd)
PATCH_MOUNT=()
PATCH_PATH=
if [ -n "$MBEDTLS_PATCH" ]; then
    PATCH_MOUNT=(-v "$(cd "$(dirname "$MBEDTLS_PATCH")" && pwd)/$(basename "$MBEDTLS_PATCH"):/patch.diff:ro")
    PATCH_PATH=/patch.diff
fi
docker run --rm --name "tlsguard2-mbedbuild-$$" -e PATCH_PATH="$PATCH_PATH" "${PATCH_MOUNT[@]}" \
    -v "$CFG:/cfg:ro" -v "$OUT:/build" espressif/idf:v6.1.1 bash -c '
 set -e
 SRC=/opt/esp/idf/components/mbedtls/mbedtls
 if [ -n "$PATCH_PATH" ]; then rm -rf /tmp/src && cp -r $SRC /tmp/src && SRC=/tmp/src && (cd $SRC && patch -p1 < $PATCH_PATH); fi
 cd $SRC
 INC="-Iinclude -Itf-psa-crypto/dispatch -Itf-psa-crypto/platform -Ilibrary -Itf-psa-crypto/extras -Itf-psa-crypto/include -Itf-psa-crypto/core -Itf-psa-crypto/utilities -Itf-psa-crypto/drivers/builtin/include -Itf-psa-crypto/drivers/builtin/src -Itf-psa-crypto/drivers/everest/include -I/cfg -I/build/shim"
 mkdir -p /build/shim/mbedtls /build/obj
 echo "/* the tree names these headers in bignum.c and ecp.c and does not ship them */" | tee /build/shim/mbedtls/bignum.h > /build/shim/mbedtls/ecp.h
 rm -f /build/obj/*.o /build/libmbed.a
 ls library/*.c tf-psa-crypto/extras/*.c tf-psa-crypto/utilities/*.c tf-psa-crypto/platform/*.c tf-psa-crypto/core/*.c tf-psa-crypto/drivers/builtin/src/*.c |
   xargs -P 4 -I{} sh -c "gcc -std=c99 -g -O0 -w -D_DEFAULT_SOURCE -DMBEDTLS_USER_CONFIG_FILE=\\\"tlsguard_mbedtls_config.h\\\" $INC -c {} -o /build/obj/\$(echo {} | tr / _).o"
 ar rcs /build/libmbed.a /build/obj/*.o; ls -la /build/libmbed.a'
