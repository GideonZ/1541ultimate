#!/usr/bin/env python3
"""E2E: a SID header before version 4 gives no third SID.

Byte $7B of a PSID/RSID header is the third SID's address from version 4 on;
before that it is reserved. `ConfigSIDs()` already ignores it there, but the
header goes into C64 memory as it is, and the player cartridge reads $7B
whatever the version. Each tune is a PSID with SID #2 at $D420 ($7A = $42) and
$7B = $50, copied to /Temp by FTP and started by its path.

  control     the version 4 header: the info screen shows a third SID at
              $D500.
  version 3   the same header as version 3: the info screen shows no third
              SID. Master showed "SID #3: $D500".

Every check runs, so a red run on master still reports the control.
"""

from __future__ import annotations

import argparse
import struct
import sys
import time
from pathlib import Path

# The one stanza that puts the shared library on sys.path; see tests/lib/bootstrap.py.
sys.path.insert(0, str(next(p for p in Path(__file__).resolve().parents
                            if (p / "tests" / "lib").is_dir()) / "tests" / "lib"))
import bootstrap  # noqa: E402,F401

import cli                                            # noqa: E402
import ftp as ftp_lib                                 # noqa: E402
from api import UltimateApi                           # noqa: E402
from report import (Failure, check, detail,           # noqa: E402
                    format_exception, suite_fail, suite_ok, teardown_step)

SUITE = "sid_reserved_third_sid_test"

V4 = "/Temp/E2ERSV4.SID"
V3 = "/Temp/E2ERSV3.SID"
SECOND_SID_BYTE = 0x42          # $D420
RESERVED_BYTE = 0x50            # $D500, if it were read as an address
SECOND_SID_LINE = "SID #2: $D420"
THIRD_SID_LABEL = "SID #3:"
THIRD_SID_LINE = "SID #3: $D500"

LOAD_ADDRESS = 0x1000
HEADER_SIZE = 0x7C
FLAGS_PAL_8580 = 0x0024

# Where the VIC takes the screen from: the player does not put it at $0400.
CIA2_PORT_A = 0xDD00
VIC_MEMORY = 0xD018
SCREEN_COLUMNS = 40
SCREEN_ROWS = 25
SCREEN_BYTES = SCREEN_COLUMNS * SCREEN_ROWS
SCREEN_SPACE = 0x20
SCREEN_TIMEOUT_SECONDS = 10.0
POLL_SECONDS = 0.3
PLAYER_BANNER = "SID PLAYER"
# Song numbers are printed after the title and SID addresses.
SONG_LINE = "SONG  : 1 / 1"


def psid(version: int, name: bytes) -> bytes:
    """A PSID whose init and play return at once, with $7A and $7B set."""
    header = bytearray(HEADER_SIZE)
    header[0:4] = b"PSID"
    struct.pack_into(">HHHHHHHI", header, 4, version, HEADER_SIZE, 0,
                     LOAD_ADDRESS, LOAD_ADDRESS + 3, 1, 1, 0)
    header[0x16:0x16 + len(name)] = name
    struct.pack_into(">H", header, 0x76, FLAGS_PAL_8580)
    header[0x7A] = SECOND_SID_BYTE
    header[0x7B] = RESERVED_BYTE
    rts = 0x60
    return bytes(header) + struct.pack("<H", LOAD_ADDRESS) + bytes((rts, 0, 0, rts))


def screen_address(device: UltimateApi) -> int:
    """Where the VIC fetches its screen from: the CIA 2 bank and the $D018 matrix."""
    bank = 3 - (device.machine.readmem(CIA2_PORT_A, 1)[0] & 0x03)
    matrix = device.machine.readmem(VIC_MEMORY, 1)[0] >> 4
    return bank * 0x4000 + matrix * 0x400


def screen_lines(memory: bytes) -> list[str]:
    """Screen codes as 25 lines of text; letters, digits and punctuation."""
    out = []
    for code in memory:
        code &= 0x7F                # reverse video
        if 1 <= code <= 26:
            out.append(chr(code + 64))
        elif 0x20 <= code <= 0x3F:
            out.append(chr(code))
        else:
            out.append(" ")
    text = "".join(out)
    return [text[row * SCREEN_COLUMNS:(row + 1) * SCREEN_COLUMNS].rstrip()
            for row in range(SCREEN_ROWS)]


def play(device: UltimateApi, path: str) -> list[str]:
    """Start `path` and return the info screen once the player has drawn it.

    The screen is cleared first: until the new tune has drawn its own, the
    previous one's would still be up, and both tunes' screens share all but
    one line.
    """
    device.machine.writemem(screen_address(device), bytes([SCREEN_SPACE]) * SCREEN_BYTES)
    device.runners.sidplay(path)
    deadline = time.monotonic() + SCREEN_TIMEOUT_SECONDS
    lines: list[str] = []
    while time.monotonic() < deadline:
        lines = screen_lines(device.machine.readmem(screen_address(device), SCREEN_BYTES))
        if any(PLAYER_BANNER in line for line in lines) and SONG_LINE in lines:
            detail("\n".join(line for line in lines if line.strip()))
            return lines
        time.sleep(POLL_SECONDS)
    raise Failure(f"the SID player did not show its screen within "
                  f"{SCREEN_TIMEOUT_SECONDS:.0f}s; last screen: {lines!r}")


def sid_lines(lines: list[str]) -> list[str]:
    return [line for line in lines if line.startswith("SID #")]


def run(args) -> None:
    device = UltimateApi(args.host, args.password or None, args.timeout)
    failures = []
    with ftp_lib.session(args.host, args.password or None) as client:
        ftp_lib.store(client, V4, psid(4, b"E2E RESERVED V4"))
        ftp_lib.store(client, V3, psid(3, b"E2E RESERVED V3"))
    try:
        try:
            with check(f"[control] {V4} shows a third SID at $D500"):
                lines = play(device, V4)
                if not any(line.startswith(THIRD_SID_LINE) for line in lines):
                    raise Failure(f"no {THIRD_SID_LINE!r}: {sid_lines(lines)!r}")
        except Failure as exc:
            failures.append(exc)
        try:
            with check(f"[wrong on master] {V3} shows no third SID"):
                lines = play(device, V3)
                if not any(line.startswith(SECOND_SID_LINE) for line in lines):
                    raise Failure(f"no {SECOND_SID_LINE!r}: {sid_lines(lines)!r}")
                third = [line for line in lines if line.startswith(THIRD_SID_LABEL)]
                if third:
                    raise Failure(f"the reserved byte $7B was taken for a third SID: {third[0]!r}")
        except Failure as exc:
            failures.append(exc)
        if failures:
            raise failures[0]
    finally:
        # A reboot removes the player's cartridge, as a reset would not.
        teardown_step("stop the tune", device.machine.reboot)
        with ftp_lib.session(args.host, args.password or None) as client:
            for path in (V4, V3):
                ftp_lib.delete_quietly(client, path)


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Check that a SID header before version 4 gives no third SID.")
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
