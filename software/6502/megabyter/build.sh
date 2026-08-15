#!/bin/sh
# Build the Megabyter test cartridge:
#   megabyter.tas -> megabyter.bin (flat ROM) -> megabyter.crt
#
# Usage:
#   ./build.sh          full 256-bank / 2 MiB hardware image, UCI test enabled
#   ./build.sh 1m       128-bank / 1 MiB hardware image, UCI test enabled
#   ./build.sh vice      128-bank / 1 MiB image with the UCI test compiled
#                         out, for testing under VICE (which supports neither
#                         2 MiB Megabyter images nor the UCI hardware)
set -e

TASS=../../../tools/64tass/64tass

cd "$(dirname "$0")"

if [ "$1" = "vice" ]; then
    BIN=megabyter_vice.bin
    CRT=megabyter_vice.crt
    EXPECT_SIZE=1048576
    EXTRA_DEFS="-D BANK_COUNT=128 -D SKIP_UCI_TEST=1"
elif [ "$1" = "1m" ]; then
    BIN=megabyter_1m.bin
    CRT=megabyter_1m.crt
    EXPECT_SIZE=1048576
    # This is the hardware test image: unlike the VICE variant above, do
    # not define SKIP_UCI_TEST, so megabyter.tas retains its default of 0.
    EXTRA_DEFS="-D BANK_COUNT=128"
else
    BIN=megabyter.bin
    CRT=megabyter.crt
    EXPECT_SIZE=2097152
    EXTRA_DEFS=""
fi

# -b (raw, no start address) -X (3-byte start/len, raises the raw-output
# cap from 64 KiB to 16 MiB) - required because the linear image can be
# up to 2 MiB.
"$TASS" -b -X $EXTRA_DEFS -o "$BIN" megabyter.tas

SIZE=$(wc -c < "$BIN")
if [ "$SIZE" -ne "$EXPECT_SIZE" ]; then
    echo "ERROR: $BIN is $SIZE bytes, expected exactly $EXPECT_SIZE" >&2
    exit 1
fi

python3 make_crt.py "$BIN" "$CRT"

echo "Build OK: $BIN ($SIZE bytes), $CRT"
