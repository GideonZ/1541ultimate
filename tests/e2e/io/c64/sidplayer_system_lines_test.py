#!/usr/bin/env python3
"""E2E: the SID player's info screen names every SID a tune uses, and the model
of each: FOUND lines for what is measured at each address, WANT lines for what
the file asks for, numbered when the tune has more than one SID.

Three tests, each a tune built here rather than shipped, because what matters
about it is a handful of header bytes:

  lines           a three-SID tune at $D400/$D420/$D440 gets three FOUND lines,
                  one per address. The screen showed only $D400 before.
  inherited       a two-SID tune that leaves SID #2's model open shows SID #1's
                  model on the WANT 2 line, as the SID file format defines it
                  ("the second SID will be set to the same SID model as the
                  first SID"), and UNKNOWN when SID #1 is open too. A three-SID
                  tune 6581, 8580, open shows 6581 for SID #3: it copies SID #1,
                  not the SID before it.
  ultisid-model   an UltiSID alone at $D420, set to 8580 and then to 6581, shows
                  that model on its FOUND line. The 6581 half passes on every
                  core; the 8580 half needs the core change proposed in #951 and
                  is gated on machine.ULTISID_8580_OSC3_DELAY until then.

The player places its screen wherever the tune leaves room, not at $0400:
for these tunes, loading at $1000, it was $8C00 in VICE. The zero-page cell it
keeps the page in ($FD, SCREEN_LOCATION in sidcommon.asm) is only valid while it
sets up. So the suite asks the VIC where the screen is: the bank from $DD00,
the offset from $D018.

ultisid-model needs an UltiSID the C64 can read back, which a cartridge cannot
offer: on an Ultimate II+ the CPU reads $D4xx from its own SID. It changes the
SID addressing, the UltiSID's waveforms and filter curve, and turns SID Player
Autoconfig off, and puts every one of them back.
"""

from __future__ import annotations

import argparse
import re
import struct
import sys
import time
from pathlib import Path

# The one stanza that puts the shared library on sys.path; see tests/lib/bootstrap.py.
sys.path.insert(0, str(next(p for p in Path(__file__).resolve().parents
                            if (p / "tests" / "lib").is_dir()) / "tests" / "lib"))
import bootstrap  # noqa: E402,F401

import cli                                                       # noqa: E402
import machine                                                   # noqa: E402
from api import UltimateApi                                      # noqa: E402
from report import (Failure, check, check_skip, detail,          # noqa: E402
                    format_exception, suite_fail, suite_ok, teardown_step)

SUITE = "sidplayer_system_lines_test"

TESTS = ("lines", "inherited", "ultisid-model")

# The PSID container: version 4 carries a second and a third SID address.
PSID_HEADER_BYTES = 0x7C
LOAD_ADDRESS = 0x1000
INIT_ADDRESS = 0x1000
PLAY_ADDRESS = 0x1001
CODE = bytes((0x60, 0x60))          # init: RTS, play: RTS

# Model codes in the header's flags, two bits per SID.
MODEL_UNKNOWN = 0b00
MODEL_6581 = 0b01
MODEL_8580 = 0b10
PAL = 0b01

# Where the VIC takes the screen from; see the module docstring.
CIA2_PORT_A = 0xDD00
VIC_MEMORY = 0xD018
SCREEN_COLUMNS = 40
SCREEN_ROWS = 25
SCREEN_TIMEOUT_SECONDS = 10.0
POLL_SECONDS = 0.3

# "FOUND :" and "WANT  :" for one SID, "FOUND1:" and "WANT 1:" for several.
FOUND_LINE = re.compile(r"FOUND[ 1-3]: \$(D[0-9A-F]{3}) : (\S+)")
WANT_LINE = re.compile(r"WANT ([ 1-3]): \$(D[0-9A-F]{3}) : (\S+)")

U64_STORE = "U64 Specific Settings"
AUTOCONFIG_ITEM = "SID Player Autoconfig"
ADDRESS_STORE = "SID Addressing"
ULTISID_STORE = "UltiSID Configuration"
ULTISID1_ADDRESS = "UltiSID 1 Address"
ADDRESS_ITEMS = ("SID Socket 1 Address", "SID Socket 2 Address",
                 ULTISID1_ADDRESS, "UltiSID 2 Address")
WAVES_ITEM = "UltiSID 1 Combined Waveforms"
FILTER_ITEM = "UltiSID 1 Filter Curve"
UNMAPPED = "Unmapped"
FILTER_FOR = {"8580": "8580 Lo", "6581": "6581"}


def psid(name: bytes, sids: list[tuple[int, int]]) -> bytes:
    """A PSID v4 for `sids`, a list of (address, model) for SID #1 to #3."""
    header = bytearray(PSID_HEADER_BYTES)
    header[:4] = b"PSID"
    # Big-endian: version, data offset, load address (0: taken from the data),
    # init, play, one song, starting at one.
    struct.pack_into(">7H", header, 4, 4, PSID_HEADER_BYTES, 0,
                     INIT_ADDRESS, PLAY_ADDRESS, 1, 1)
    for offset, text in ((0x16, name), (0x36, b"E2E"), (0x56, b"2026")):
        header[offset:offset + 0x20] = text.ljust(0x20, b"\0")
    flags = PAL << 2
    for index, (_, model) in enumerate(sids):
        flags |= model << (4 + 2 * index)
    struct.pack_into(">H", header, 0x76, flags)
    # The second and third SID's addresses are stored as $Dxx0 >> 4.
    for index, (address, _) in enumerate(sids[1:]):
        header[0x7A + index] = (address >> 4) & 0xFF
    if len(header) != PSID_HEADER_BYTES:
        # A wrong-width slice grows a bytearray instead of failing.
        raise Failure(f"the PSID header is {len(header)} bytes, not "
                      f"{PSID_HEADER_BYTES}")
    return bytes(header) + LOAD_ADDRESS.to_bytes(2, "little") + CODE


def decode(screen: bytes) -> list[str]:
    """The screen as 25 lines of text: screen codes, or the ASCII they came from."""
    text = []
    for code in screen:
        code &= 0x7F                # reverse video
        if 0x01 <= code <= 0x1A:
            text.append(chr(0x40 + code))
        elif 0x20 <= code <= 0x5A:
            text.append(chr(code))
        else:
            text.append(" ")
    joined = "".join(text)
    return [joined[row * SCREEN_COLUMNS:(row + 1) * SCREEN_COLUMNS].rstrip()
            for row in range(SCREEN_ROWS)]


def screen_address(device: UltimateApi) -> int:
    """Where the VIC currently reads its screen from."""
    bank = 3 - (device.machine.readmem(CIA2_PORT_A, 1)[0] & 0x03)
    offset = (device.machine.readmem(VIC_MEMORY, 1)[0] >> 4) * 0x400
    return bank * 0x4000 + offset


def play_and_read(device: UltimateApi, tune: bytes) -> list[str]:
    """Start `tune` and return the info screen once its FOUND line is there."""
    device.runners.upload("sidplay", tune)
    deadline = time.monotonic() + SCREEN_TIMEOUT_SECONDS
    lines: list[str] = []
    while time.monotonic() < deadline:
        lines = decode(device.machine.readmem(screen_address(device),
                                              SCREEN_COLUMNS * SCREEN_ROWS))
        if any(FOUND_LINE.search(line) for line in lines):
            return lines
        time.sleep(POLL_SECONDS)
    raise Failure("no FOUND line on the player's screen within "
                  f"{SCREEN_TIMEOUT_SECONDS:.0f}s; last screen: {lines!r}")


def found_lines(lines: list[str]) -> dict[str, str]:
    """Address -> model, from the FOUND lines."""
    return {m.group(1): m.group(2)
            for m in (FOUND_LINE.search(line) for line in lines) if m}


def test_lines(device: UltimateApi) -> None:
    sids = [(0xD400, MODEL_8580), (0xD420, MODEL_8580), (0xD440, MODEL_8580)]
    with check("a three-SID tune gets a FOUND line for each of its addresses"):
        lines = play_and_read(device, psid(b"THREE SIDS", sids))
        found = found_lines(lines)
        detail(f"FOUND lines: {found}")
        wanted = {"D400", "D420", "D440"}
        if set(found) != wanted:
            raise Failure(f"FOUND lines for {sorted(found)}, "
                          f"expected {sorted(wanted)}")


def sid_line_models(lines: list[str]) -> dict[str, str]:
    """SID number -> model, from the WANT lines."""
    return {m.group(1): m.group(3)
            for m in (WANT_LINE.search(line) for line in lines) if m}


def test_inherited(device: UltimateApi) -> None:
    for first, expected in ((MODEL_6581, "6581"), (MODEL_UNKNOWN, "UNKNOWN")):
        label = (f"SID #2 with its model left open shows {expected}"
                 + (", SID #1's model" if first != MODEL_UNKNOWN else
                    ", as SID #1 is open too"))
        with check(label):
            sids = [(0xD400, first), (0xD420, MODEL_UNKNOWN)]
            models = sid_line_models(play_and_read(device, psid(b"MODEL LEFT OPEN", sids)))
            detail(f"WANT lines: {models}")
            if models.get("2") != expected:
                raise Failure(f"the WANT 2 line says {models.get('2')!r}, "
                              f"expected {expected}")
    with check("SID #3 with its model left open copies SID #1, not SID #2"):
        sids = [(0xD400, MODEL_6581), (0xD420, MODEL_8580), (0xD440, MODEL_UNKNOWN)]
        models = sid_line_models(play_and_read(device, psid(b"THIRD LEFT OPEN", sids)))
        detail(f"WANT lines: {models}")
        if models.get("3") != "6581":
            raise Failure(f"the WANT 3 line says {models.get('3')!r}, "
                          "expected 6581 from SID #1")


def configured(device: UltimateApi, store: str, item: str) -> str:
    """One setting's value, or "" where this machine does not serve it."""
    try:
        return device.configs.current(store, item)
    except Failure:
        return ""


def test_ultisid_model(device: UltimateApi) -> None:
    if not configured(device, ADDRESS_STORE, ULTISID1_ADDRESS):
        with check("an UltiSID's FOUND line follows its configured model"):
            check_skip("no UltiSID the C64 can read back on this machine")
        return

    saved = {(U64_STORE, AUTOCONFIG_ITEM): configured(device, U64_STORE, AUTOCONFIG_ITEM),
             (ULTISID_STORE, WAVES_ITEM): configured(device, ULTISID_STORE, WAVES_ITEM),
             (ULTISID_STORE, FILTER_ITEM): configured(device, ULTISID_STORE, FILTER_ITEM)}
    for item in ADDRESS_ITEMS:
        saved[(ADDRESS_STORE, item)] = configured(device, ADDRESS_STORE, item)
    try:
        # UltiSID 1 alone at $D420: nothing else may answer there.
        device.configs.set(U64_STORE, AUTOCONFIG_ITEM, "Disabled")
        for item in ADDRESS_ITEMS:
            if item != ULTISID1_ADDRESS and saved[(ADDRESS_STORE, item)] == "$D420":
                device.configs.set(ADDRESS_STORE, item, UNMAPPED)
        device.configs.set(ADDRESS_STORE, ULTISID1_ADDRESS, "$D420")

        for model, code in (("6581", MODEL_6581), ("8580", MODEL_8580)):
            label = f"an UltiSID set to {model} shows {model} on its FOUND line"
            if model == "8580" and device.machine.skip_without_fix(
                    machine.ULTISID_8580_OSC3_DELAY, label):
                continue
            with check(label):
                device.configs.set(ULTISID_STORE, WAVES_ITEM, model)
                device.configs.set(ULTISID_STORE, FILTER_ITEM, FILTER_FOR[model])
                sids = [(0xD400, code), (0xD420, code)]
                found = found_lines(play_and_read(device, psid(b"ULTISID MODEL", sids)))
                detail(f"FOUND lines: {found}")
                if found.get("D420") != model:
                    raise Failure(f"$D420 shows {found.get('D420')!r}, expected {model}")
    finally:
        for (store, item), value in saved.items():
            if value:
                teardown_step(f"restore {item} to {value!r}",
                              lambda s=store, i=item, v=value: device.configs.set(s, i, v))


def run(args) -> None:
    device = UltimateApi(args.host, args.password or None, args.timeout)
    tests = TESTS if args.test == "all" else (args.test,)
    try:
        for name in tests:
            {"lines": test_lines,
             "inherited": test_inherited,
             "ultisid-model": test_ultisid_model}[name](device)
    finally:
        teardown_step("stop the tune", lambda: device.machine.reset(force=True))


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Check the FOUND and WANT lines on the SID player's info screen.")
    cli.add_device_arguments(parser)
    parser.add_argument("--test", choices=("all", *TESTS), default="all")
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
