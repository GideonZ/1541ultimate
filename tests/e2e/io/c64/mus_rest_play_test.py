#!/usr/bin/env python3
"""E2E: a Compute's Sidplayer file started over REST plays on the MUS player.

`runners:sidplay` tells a .mus or .str from a SID file by its extension, and
names a .mus after its file, since the format has no title of its own. Each
tune is copied to /Temp by FTP and started by its path.

  control     /Temp/E2EUPPER.MUS, with /Temp/E2EUPPER.STR beside it, plays on
              the MUS player in stereo, with the second SID at $D500.
  lower-case  /Temp/e2elower.mus, with /Temp/e2elower.str beside it, plays on
              the MUS player in stereo, with the second SID at $D500. Master
              compared the extension in its own case with "MUS", took the file
              for a SID file and refused it, "Error detected in file format".
  str         /Temp/e2elower.str started itself plays the pair the same way:
              the player loads the .mus first. Master refused it likewise.
  title       the info screen titles /Temp/E2EUPPER.MUS "E2EUPPER". Master
              took the title from the whole path it was given, "/TEMP/E2EUPPER";
              the file browser passes the name alone, so only REST showed it.

Every check runs, so a red run on master still reports the control.
"""

from __future__ import annotations

import argparse
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

SUITE = "mus_rest_play_test"

UPPER = "/Temp/E2EUPPER.MUS"
UPPER_STR = "/Temp/E2EUPPER.STR"
LOWER = "/Temp/e2elower.mus"
LOWER_STR = "/Temp/e2elower.str"
TITLE = "E2EUPPER"
# The address the device gives a .str's voices.
SECOND_SID = "$D500"

# Where the VIC takes the screen from: the MUS player does not put it at $0400.
CIA2_PORT_A = 0xDD00
VIC_MEMORY = 0xD018
SCREEN_COLUMNS = 40
SCREEN_ROWS = 25
SCREEN_BYTES = SCREEN_COLUMNS * SCREEN_ROWS
SCREEN_SPACE = 0x20
SCREEN_TIMEOUT_SECONDS = 10.0
POLL_SECONDS = 0.3
PLAYER_BANNER = "MUS PLAYER"
TITLE_LABEL = "TITLE :"
SONG_LINE = "SONG  : 1 / 1"


def mus(text: bytes) -> bytes:
    """Compute's Sidplayer data whose three voices only halt.

    A load address, the three voice lengths, each voice the two-byte halt
    command, and the text, ended by a zero: what FileTypeSID reads, and all
    the info screen needs.
    """
    voices = bytes((0x01, 0x4F)) * 3
    return b"\x00\x10" + bytes((2, 0, 2, 0, 2, 0)) + voices + text + b"\r\x00"


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


def title_line(lines: list[str]) -> str | None:
    return next((line for line in lines if line.startswith(TITLE_LABEL)), None)


def play(device: UltimateApi, path: str) -> list[str]:
    """Start `path` and return the info screen once the MUS player shows it.

    The screen is cleared first: until the new tune has drawn its own, the
    previous one's would still be up, and two plays of the same pair look
    alike.
    """
    device.machine.writemem(screen_address(device), bytes([SCREEN_SPACE]) * SCREEN_BYTES)
    device.runners.sidplay(path)
    deadline = time.monotonic() + SCREEN_TIMEOUT_SECONDS
    lines: list[str] = []
    while time.monotonic() < deadline:
        lines = screen_lines(device.machine.readmem(screen_address(device), SCREEN_BYTES))
        # Song numbers are printed after the title and SID addresses.
        if any(PLAYER_BANNER in line for line in lines) and SONG_LINE in lines:
            detail("\n".join(line for line in lines if line.strip()))
            return lines
        time.sleep(POLL_SECONDS)
    raise Failure(f"the MUS player did not show its screen within "
                  f"{SCREEN_TIMEOUT_SECONDS:.0f}s; last screen: {lines!r}")


def expect_stereo(lines: list[str]) -> None:
    if not any(SECOND_SID in line for line in lines):
        raise Failure(f"no second SID at {SECOND_SID}: the .str was not loaded")


def run(args) -> None:
    device = UltimateApi(args.host, args.password or None, args.timeout)
    failures = []
    upper: list[str] = []
    with ftp_lib.session(args.host, args.password or None) as client:
        ftp_lib.store(client, UPPER, mus(b"E2E UPPER LEFT"))
        ftp_lib.store(client, UPPER_STR, mus(b"E2E UPPER RIGHT"))
        ftp_lib.store(client, LOWER, mus(b"E2E LOWER LEFT"))
        ftp_lib.store(client, LOWER_STR, mus(b"E2E LOWER RIGHT"))
    try:
        try:
            with check(f"[control] {UPPER} plays in stereo with its .str"):
                upper = play(device, UPPER)
                expect_stereo(upper)
        except Failure as exc:
            failures.append(exc)
        try:
            with check(f"[wrong on master] {LOWER} plays in stereo with its .str"):
                expect_stereo(play(device, LOWER))
        except Failure as exc:
            failures.append(exc)
        try:
            with check(f"[wrong on master] {LOWER_STR} plays the same pair"):
                expect_stereo(play(device, LOWER_STR))
        except Failure as exc:
            failures.append(exc)
        try:
            with check(f"[wrong on master] {UPPER} is titled {TITLE!r}, not by its path"):
                if not upper:
                    raise Failure(f"{UPPER} did not play")
                shown = (title_line(upper) or "")[len(TITLE_LABEL):].strip()
                if shown != TITLE:
                    raise Failure(f"the title is {shown!r}")
        except Failure as exc:
            failures.append(exc)
        if failures:
            raise failures[0]
    finally:
        # A reboot removes the player's cartridge, as a reset would not.
        teardown_step("stop the tune", device.machine.reboot)
        with ftp_lib.session(args.host, args.password or None) as client:
            for path in (UPPER, UPPER_STR, LOWER, LOWER_STR):
                ftp_lib.delete_quietly(client, path)


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Check that a .mus started over REST plays on the MUS player "
                    "and is titled by its name.")
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
