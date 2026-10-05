#!/usr/bin/env python3
"""E2E: a SID tune that reaches the last page of memory plays, and is left intact.

A load image whose last byte is $FFFF ends at $10000, which does not fit in the
16-bit end address. Three places in the SID player use that end:

- `FileTypeSID::prepare()` refuses an image that does not fit below $10000.
- The cartridge's `readLoadAddresses` rounds the end up to a page, and the
  cartridge places its screen and player outside the image.
- The cartridge calls a PSID's init with BASIC ROM switched out when the image
  reaches $A000.

HVSC #85 has 7 tunes whose last byte is $FFFF, and 36 that load below $A000 and
whose last byte is in $FF00-$FFFE.

Each case is a PSID that loads at $1000 and ends at a chosen byte. The image is
filled with seeded random bytes around an init that records $01 and a play
routine that counts its calls. The image ending at $FEFF is the control: it
ends below the last page.

For each image, the suite checks five things:

- the device accepts the image;
- init is called with BASIC ROM switched out;
- the play routine is being called;
- every RAM byte of the image reads back as it was loaded;
- the player's info screen shows the title, and sits outside the image.

$A000-$BFFF and $D000-$FFFF are left out of the comparison. A read through the
6510's memory map returns ROM and I/O there, not the RAM underneath.

The suite also checks that a PSID whose file stops before its data is refused.
Its end lies below its start, so nothing would be loaded at the init address.
"""

from __future__ import annotations

import argparse
import json
import random
import struct
import sys
import time
from pathlib import Path

# The one stanza that puts the shared library on sys.path; see tests/lib/bootstrap.py.
sys.path.insert(0, str(next(p for p in Path(__file__).resolve().parents
                            if (p / "tests" / "lib").is_dir()) / "tests" / "lib"))
import bootstrap  # noqa: E402,F401

import cli                                            # noqa: E402
from api import UltimateApi                           # noqa: E402
from assembler import assemble                        # noqa: E402
from report import (Failure, check, detail,           # noqa: E402
                    format_exception, suite_fail, suite_ok, teardown_step)

SUITE = "sid_top_of_memory_test"

PROGRAM = Path(__file__).resolve().parent / "sid_top_of_memory.asm"

PSID_HEADER_BYTES = 0x7C
LOAD_ADDRESS = 0x1000
INIT_ADDRESS = 0x1000
PLAY_ADDRESS = 0x1040
CALLS_ADDRESS = 0x1080
BANK_ADDRESS = 0x1082
# The page holding init, play and the call counter; the rest is filler.
CODE_END = 0x1100

# Last byte of each image. $FEFF is the control below the last page.
LAST_BYTES = (0xFEFF, 0xFFEF, 0xFFFF)

# RAM that a read through the 6510's memory map can see: BASIC ROM covers
# $A000-$BFFF, and I/O and the KERNAL cover $D000-$FFFF.
VISIBLE_RAM = ((0x0000, 0xA000), (0xC000, 0xD000))

# Changed bytes closer together than this are reported as one range; random
# filler matches what overwrote it about one byte in 256.
RANGE_GAP_BYTES = 16

# The player calls play 50 or 60 times a second.
PLAY_START_TIMEOUT_SECONDS = 8.0
CALL_WINDOW_SECONDS = 1.0
MIN_CALLS_PER_WINDOW = 20

SCREEN_BYTES = 1000

# $01 with BASIC ROM switched out, I/O and the KERNAL in. Every image here
# reaches $A000, so init must not see BASIC ROM over its data.
INIT_BANK = 0x36


def title_for(last: int) -> str:
    return f"TOP OF MEMORY {last:04X}"


def image(last: int) -> bytes:
    """A PSID that loads at $1000 and whose last byte lands at `last`."""
    program = assemble(PROGRAM)
    if program[:2] != LOAD_ADDRESS.to_bytes(2, "little"):
        raise Failure(f"the play routine assembles at "
                      f"${int.from_bytes(program[:2], 'little'):04X}, not ${LOAD_ADDRESS:04X}")
    code = program[2:]
    if LOAD_ADDRESS + len(code) > CODE_END:
        raise Failure(f"the play routine runs past ${CODE_END:04X}")
    body = bytearray(random.Random(last).randbytes(last + 1 - LOAD_ADDRESS))
    body[:len(code)] = code

    header = bytearray(PSID_HEADER_BYTES)
    header[:4] = b"PSID"
    # Big-endian: version 2, data offset, load address (0: taken from the
    # data), init, play, one song, starting at one. Speed and flags stay 0.
    struct.pack_into(">7H", header, 4, 2, PSID_HEADER_BYTES, 0,
                     INIT_ADDRESS, PLAY_ADDRESS, 1, 1)
    for offset, text in ((0x16, title_for(last).encode()), (0x36, b"E2E"), (0x56, b"2026")):
        header[offset:offset + 0x20] = text.ljust(0x20, b"\0")
    if len(header) != PSID_HEADER_BYTES:
        raise Failure(f"the PSID header is {len(header)} bytes, not {PSID_HEADER_BYTES}")
    return bytes(header) + LOAD_ADDRESS.to_bytes(2, "little") + bytes(body)


def header_only() -> bytes:
    """A PSID that names $1000 as its load address and has no data after the header."""
    header = bytearray(image(LAST_BYTES[0])[:PSID_HEADER_BYTES])
    struct.pack_into(">H", header, 8, LOAD_ADDRESS)
    return bytes(header)


def calls(device: UltimateApi) -> int:
    return int.from_bytes(device.machine.readmem(CALLS_ADDRESS, 2), "little")


def play_calls(device: UltimateApi) -> int:
    """How often play was called in one window, once the player has started."""
    deadline = time.monotonic() + PLAY_START_TIMEOUT_SECONDS
    first = calls(device)
    while calls(device) == first:
        if time.monotonic() >= deadline:
            raise Failure(f"play at ${PLAY_ADDRESS:04X} was not called within "
                          f"{PLAY_START_TIMEOUT_SECONDS:.0f}s")
        time.sleep(0.1)
    start = calls(device)
    time.sleep(CALL_WINDOW_SECONDS)
    return (calls(device) - start) & 0xFFFF


def damaged_ranges(device: UltimateApi, last: int, data: bytes) -> list[tuple[int, int]]:
    """The visible RAM ranges of the image that no longer hold what was loaded."""
    damaged: list[tuple[int, int]] = []
    for low, high in VISIBLE_RAM:
        low, high = max(low, CODE_END), min(high, last + 1)
        if low >= high:
            continue
        memory = device.machine.readmem(low, high - low)
        expected = data[low - LOAD_ADDRESS:high - LOAD_ADDRESS]
        for offset, (got, want) in enumerate(zip(memory, expected)):
            if got == want:
                continue
            address = low + offset
            if damaged and address - damaged[-1][1] <= RANGE_GAP_BYTES:
                damaged[-1] = (damaged[-1][0], address)
            else:
                damaged.append((address, address))
    return damaged


def screen_address(device: UltimateApi) -> int:
    """Where the VIC fetches its screen from: the CIA 2 bank and the $D018 matrix."""
    bank = 3 - (device.machine.readmem(0xDD00, 1)[0] & 0x03)
    matrix = device.machine.readmem(0xD018, 1)[0] >> 4
    return bank * 0x4000 + matrix * 0x400


def screen_text(memory: bytes) -> str:
    """Screen codes as text; letters, digits and spaces are all this suite needs."""
    out = []
    for code in memory:
        code &= 0x7F
        if 1 <= code <= 26:
            out.append(chr(code + 64))
        elif 0x20 <= code <= 0x3F:
            out.append(chr(code))
        else:
            out.append(".")
    return "".join(out)


def check_image(device: UltimateApi, last: int) -> None:
    data = image(last)[PSID_HEADER_BYTES + 2:]
    code, _, body = device.runners.upload("sidplay", image(last))
    if code != 200:
        try:
            reason = "; ".join(json.loads(body).get("errors", []))
        except ValueError:
            reason = body[:80].decode(errors="replace")
        raise Failure(f"the device refused the tune with HTTP {code}: {reason}")

    problems = []
    try:
        per_window = play_calls(device)
        detail(f"play called {per_window} times in {CALL_WINDOW_SECONDS:.0f}s")
        if per_window < MIN_CALLS_PER_WINDOW:
            problems.append(f"play was called {per_window} times in "
                            f"{CALL_WINDOW_SECONDS:.0f}s, under {MIN_CALLS_PER_WINDOW}")
    except Failure as exc:
        problems.append(str(exc))

    bank = device.machine.readmem(BANK_ADDRESS, 1)[0]
    detail(f"init called with $01 = ${bank:02X}")
    if bank != INIT_BANK:
        problems.append(f"init was called with $01 = ${bank:02X}, not ${INIT_BANK:02X}")

    damaged = damaged_ranges(device, last, data)
    if damaged:
        shown = ", ".join(f"${a:04X}-${b:04X}" for a, b in damaged[:6])
        problems.append(f"the image changed after loading at {shown}")
    else:
        detail("every visible RAM byte of the image reads back as loaded")

    screen = screen_address(device)
    text = screen_text(device.machine.readmem(screen, SCREEN_BYTES))
    detail(f"info screen at ${screen:04X}")
    if screen + SCREEN_BYTES - 1 >= LOAD_ADDRESS and screen <= last:
        problems.append(f"the info screen at ${screen:04X} lies inside the image "
                        f"${LOAD_ADDRESS:04X}-${last:04X}")
    if title_for(last) not in text:
        problems.append(f"the info screen at ${screen:04X} does not show the title "
                        f"{title_for(last)!r}")

    if problems:
        raise Failure("; ".join(problems))


def run(args) -> None:
    device = UltimateApi(args.host, args.password or None, args.timeout)
    # Every check runs, so one failure does not hide the result of the others.
    failures = []
    try:
        for last in LAST_BYTES:
            try:
                with check(f"a tune loaded at ${LOAD_ADDRESS:04X}-${last:04X} plays "
                           f"and stays intact"):
                    check_image(device, last)
            except Failure as exc:
                failures.append(exc)
        try:
            with check("a PSID whose file stops before its data is refused"):
                code, _, _ = device.runners.upload("sidplay", header_only())
                if code == 200:
                    raise Failure("the device accepted a PSID that has no data")
        except Failure as exc:
            failures.append(exc)
        if failures:
            raise failures[0]
    finally:
        teardown_step("stop the tune", lambda: device.machine.reset(force=True))


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Check that a SID tune reaching the last page of memory plays "
                    "and that the player leaves its memory intact.")
    cli.add_device_arguments(parser)
    args = parser.parse_args()
    try:
        run(args)
    except Exception as exc:            # noqa: BLE001
        suite_fail(SUITE, format_exception(exc))
        return 1
    suite_ok(SUITE)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
