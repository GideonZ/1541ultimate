#!/bin/sh
# Build the TwoMegabyter test cartridge:
#   twomegabyter.tas -> twomegabyter.bin (flat ROM) -> twomegabyter.crt
#
# Usage:
#   ./build_twomeg.sh          full 128-bank / 2 MiB hardware image, UCI test enabled
#   ./build_twomeg.sh test      16-bank / 256 KiB image with the UCI test compiled
#                               out, for quick bench/emulator testing
set -e

TASS=../../../tools/64tass/64tass

cd "$(dirname "$0")"

if [ "$1" = "test" ]; then
    BIN=twomegabyter_test.bin
    CRT=twomegabyter_test.crt
    EXPECT_SIZE=262144
    EXTRA_DEFS="-D BANK_COUNT=16 -D SKIP_UCI_TEST=1"
else
    BIN=twomegabyter.bin
    CRT=twomegabyter.crt
    EXPECT_SIZE=2097152
    EXTRA_DEFS=""
fi

# -b (raw, no start address) -X (3-byte start/len, raises the raw-output
# cap from 64 KiB to 16 MiB) - required because the linear image can be
# up to 2 MiB.
"$TASS" -b -X $EXTRA_DEFS -o "$BIN" twomegabyter.tas

SIZE=$(wc -c < "$BIN")
if [ "$SIZE" -ne "$EXPECT_SIZE" ]; then
    echo "ERROR: $BIN is $SIZE bytes, expected exactly $EXPECT_SIZE" >&2
    exit 1
fi

python3 make_crt_twomeg.py "$BIN" "$CRT"

echo "Build OK: $BIN ($SIZE bytes), $CRT"
