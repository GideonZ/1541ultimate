#!/usr/bin/env python3
"""E2E: the SID player's info screen names every SID a tune uses, and the model
of each: FOUND lines for what is measured at each address, NEEDS lines for what
the file asks for; the lines for a second and third SID are numbered.

The tests build their tunes here rather than shipping them, because what
matters about each is a handful of header bytes:

  lines           a three-SID tune at $D400/$D420/$D440 gets three FOUND lines,
                  one per address. The screen showed only $D400 before.
  inherited       a two-SID tune that leaves SID #2's model open shows SID #1's
                  model on the NEEDS2 line, as the SID file format defines it
                  ("the second SID will be set to the same SID model as the
                  first SID"), and UNKNOWN when SID #1 is open too. A three-SID
                  tune 6581, 8580, open shows 6581 for SID #3: it copies SID #1,
                  not the SID before it.
  mirror          a three-SID tune on a machine with nothing at $D420 and
                  $D440 shows UNKNOWN there. A SID decodes five address lines,
                  so a C64's own SID answers at both; the detection used to
                  measure it again and report two chips that are not there.
                  On a cartridge that is always the case; on a C64 Ultimate
                  or U64 the suite unmaps whatever sits at those addresses.
  speed           the first NEEDS line ends in the song's speed: (VBI) without
                  the speed flag, (CIA) with it. A two-song tune whose second
                  song has the flag shows (VBI) for song 1 and (CIA) for song 2.
  any             a tune made for both models and both clocks shows ANY for
                  each; with the clock left open, ANY / UNKNOWN, and the speed
                  still fits on the line.
  mus             a Compute's Sidplayer file, uploaded under its name, plays
                  with the MUS player: the file name as title, one SID, and the
                  model, clock and speed the device's made-up header asks for.
  mus-stereo      the same with a .str file next to the .mus, both copied to
                  the device by FTP: a second SID at $D500, with a FOUND2 and
                  a NEEDS2 line for it. What FOUND2 says depends on the
                  machine: the model of a SID mapped there, or UNKNOWN on a C64
                  whose only SID answers at $D500 as a mirror.
  ultisid-model   an UltiSID alone at $D420, set to 6581 and then to 8580, shows
                  that model on its FOUND line. The timing check alone calls
                  every UltiSID a 6581; the 8580 is told by its combined
                  waveforms.

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
import ftp as ftp_lib                                            # noqa: E402
from api import UltimateApi                                      # noqa: E402
from report import (Failure, check, check_skip, detail,          # noqa: E402
                    format_exception, suite_fail, suite_ok, teardown_step)

SUITE = "sidplayer_system_lines_test"

TESTS = ("lines", "inherited", "mirror", "speed", "any", "mus", "mus-stereo", "ultisid-model")

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
MODEL_ANY = 0b11
# Clock codes, bits 2-3 of the flags.
CLOCK_UNKNOWN = 0b00
PAL = 0b01
CLOCK_ANY = 0b11

# Where the VIC takes the screen from; see the module docstring.
CIA2_PORT_A = 0xDD00
VIC_MEMORY = 0xD018
SCREEN_COLUMNS = 40
SCREEN_ROWS = 25
SCREEN_TIMEOUT_SECONDS = 10.0
POLL_SECONDS = 0.3

# "FOUND :" and "NEEDS :" for the first SID, "FOUND2:" and "NEEDS2:" and so on
# for the others.
FOUND_LINE = re.compile(r"FOUND[ 23]: \$(D[0-9A-F]{3}) : (\S+)")
NEEDS_LINE = re.compile(r"NEEDS([ 23]): \$(D[0-9A-F]{3}) : (\S+)")

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


def psid(name: bytes, sids: list[tuple[int, int]], clock: int = PAL,
         songs: int = 1, speed: int = 0) -> bytes:
    """A PSID v4 for `sids`, a list of (address, model) for SID #1 to #3.

    `speed` is the header's speed flags, bit 0 for song 1: a set bit means
    the song runs on a CIA timer, a clear one on the 50/60 Hz default.
    """
    header = bytearray(PSID_HEADER_BYTES)
    header[:4] = b"PSID"
    # Big-endian: version, data offset, load address (0: taken from the data),
    # init, play, `songs` songs, starting at one, and the speed flags.
    struct.pack_into(">7HI", header, 4, 4, PSID_HEADER_BYTES, 0,
                     INIT_ADDRESS, PLAY_ADDRESS, songs, 1, speed)
    for offset, text in ((0x16, name), (0x36, b"E2E"), (0x56, b"2026")):
        header[offset:offset + 0x20] = text.ljust(0x20, b"\0")
    flags = clock << 2
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


def drawn(lines: list[str]) -> bool:
    """Whether the player has finished the screen: the cartridge writes the
    FOUND and NEEDS lines, then the advanced player ends the first NEEDS line in
    the song's speed."""
    return (any(FOUND_LINE.search(line) for line in lines)
            and any(NEEDS_LINE.search(line) and line.endswith(")") for line in lines))


def play_and_read(device: UltimateApi, tune: bytes,
                  song: int | None = None) -> list[str]:
    """Start `tune`, at `song` if given, and return the info screen once the
    player has drawn it."""
    # Every play tells the firmware to keep the SID mapping over the next reset
    # (SidAutoConfig() sets skipReset, even with autoconfig off). If that next
    # reset is the one that starts this tune, the SIDs stay where the previous
    # tune put them, not where the settings say. This reset uses it up.
    device.machine.reset(force=True)
    device.runners.upload("sidplay", tune,
                          params={"songnr": song} if song is not None else None)
    deadline = time.monotonic() + SCREEN_TIMEOUT_SECONDS
    lines: list[str] = []
    while time.monotonic() < deadline:
        lines = decode(device.machine.readmem(screen_address(device),
                                              SCREEN_COLUMNS * SCREEN_ROWS))
        if drawn(lines):
            return lines
        time.sleep(POLL_SECONDS)
    raise Failure("the player's screen was not complete within "
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
    """SID number -> model, from the NEEDS lines; the first one is unnumbered."""
    return {m.group(1).strip() or "1": m.group(3)
            for m in (NEEDS_LINE.search(line) for line in lines) if m}


def test_inherited(device: UltimateApi) -> None:
    for first, expected in ((MODEL_6581, "6581"), (MODEL_UNKNOWN, "UNKNOWN")):
        label = (f"SID #2 with its model left open shows {expected}"
                 + (", SID #1's model" if first != MODEL_UNKNOWN else
                    ", as SID #1 is open too"))
        with check(label):
            sids = [(0xD400, first), (0xD420, MODEL_UNKNOWN)]
            models = sid_line_models(play_and_read(device, psid(b"MODEL LEFT OPEN", sids)))
            detail(f"NEEDS lines: {models}")
            if models.get("2") != expected:
                raise Failure(f"the NEEDS2 line says {models.get('2')!r}, "
                              f"expected {expected}")
    with check("SID #3 with its model left open copies SID #1, not SID #2"):
        sids = [(0xD400, MODEL_6581), (0xD420, MODEL_8580), (0xD440, MODEL_UNKNOWN)]
        models = sid_line_models(play_and_read(device, psid(b"THIRD LEFT OPEN", sids)))
        detail(f"NEEDS lines: {models}")
        if models.get("3") != "6581":
            raise Failure(f"the NEEDS3 line says {models.get('3')!r}, "
                          "expected 6581 from SID #1")


def test_mirror(device: UltimateApi) -> None:
    label = "a three-SID tune with nothing at $D420 and $D440 shows UNKNOWN there"
    sids = [(0xD400, MODEL_6581), (0xD420, MODEL_8580), (0xD440, MODEL_8580)]
    addressable = bool(configured(device, ADDRESS_STORE, ULTISID1_ADDRESS))
    saved = {}
    if addressable:
        saved[(U64_STORE, AUTOCONFIG_ITEM)] = configured(device, U64_STORE, AUTOCONFIG_ITEM)
        for item in ADDRESS_ITEMS:
            saved[(ADDRESS_STORE, item)] = configured(device, ADDRESS_STORE, item)
    try:
        if addressable:
            # Autoconfig would map SIDs to the tune's addresses again.
            device.configs.set(U64_STORE, AUTOCONFIG_ITEM, "Disabled")
            for item in ADDRESS_ITEMS:
                if saved[(ADDRESS_STORE, item)] in ("$D420", "$D440"):
                    device.configs.set(ADDRESS_STORE, item, UNMAPPED)
        with check(label):
            found = found_lines(play_and_read(device, psid(b"NOTHING THERE", sids)))
            detail(f"FOUND lines: {found}")
            for address in ("D420", "D440"):
                if found.get(address) != "UNKNOWN":
                    raise Failure(f"${address} shows {found.get(address)!r}, "
                                  "expected UNKNOWN")
    finally:
        for (store, item), value in saved.items():
            if value:
                teardown_step(f"restore {item} to {value!r}",
                              lambda s=store, i=item, v=value: device.configs.set(s, i, v))


def first_want_line(lines: list[str]) -> str:
    """The unnumbered NEEDS line, the one that carries the clock and speed."""
    for line in lines:
        match = NEEDS_LINE.search(line)
        if match and match.group(1) == " ":
            return line
    raise Failure(f"no NEEDS line on the screen: {lines!r}")


def test_speed(device: UltimateApi) -> None:
    sids = [(0xD400, MODEL_6581)]
    for speed, expected in ((0, "6581 / PAL (VBI)"), (1, "6581 / PAL (CIA)")):
        with check(f"speed flag {speed} shows {expected.split()[-1]}"):
            line = first_want_line(play_and_read(device, psid(b"SPEED", sids, speed=speed)))
            detail(line)
            if not line.endswith(expected):
                raise Failure(f"{line!r} does not end in {expected!r}")
    for song, expected in ((1, "(VBI)"), (2, "(CIA)")):
        with check(f"a tune whose song 2 has the speed flag shows {expected} for song {song}"):
            line = first_want_line(play_and_read(
                device, psid(b"TWO SPEEDS", sids, songs=2, speed=0b10), song))
            detail(line)
            if not line.endswith(expected):
                raise Failure(f"{line!r} does not end in {expected!r}")


def test_any(device: UltimateApi) -> None:
    for clock, expected in ((CLOCK_ANY, "ANY / ANY (VBI)"),
                            (CLOCK_UNKNOWN, "ANY / UNKNOWN (VBI)")):
        with check(f"a tune for both models shows {expected}"):
            line = first_want_line(play_and_read(
                device, psid(b"ANY MODEL", [(0xD400, MODEL_ANY)], clock=clock)))
            detail(line)
            if not line.endswith(expected):
                raise Failure(f"{line!r} does not end in {expected!r}")


def mus(text: bytes) -> bytes:
    """Compute's Sidplayer data whose three voices only halt.

    A load address, the three voice lengths, each voice the two-byte halt
    command, and the text, ended by a zero: what FileTypeSID reads, and all
    the info screen needs.
    """
    voices = bytes((0x01, 0x4F)) * 3
    return b"\x00\x10" + bytes((2, 0, 2, 0, 2, 0)) + voices + text + b"\r\x00"


# The header the device makes up for MUS data asks for an 8580, NTSC and CIA
# speed.
MUS_NEEDS = "NEEDS : $D400 : 8580 / NTSC (CIA)"


def mus_screen(device: UltimateApi) -> list[str]:
    """The info screen once the MUS player has drawn it."""
    deadline = time.monotonic() + SCREEN_TIMEOUT_SECONDS
    lines: list[str] = []
    while time.monotonic() < deadline:
        lines = decode(device.machine.readmem(screen_address(device),
                                              SCREEN_COLUMNS * SCREEN_ROWS))
        if any("MUS PLAYER" in line for line in lines) and drawn(lines):
            return lines
        time.sleep(POLL_SECONDS)
    raise Failure("no MUS player screen within "
                  f"{SCREEN_TIMEOUT_SECONDS:.0f}s; last screen: {lines!r}")


def test_mus(device: UltimateApi) -> None:
    with check("an uploaded .mus plays with the MUS player"):
        status, _, body = device.runners.upload_file("sidplay", "E2E_MONO.mus",
                                                     mus(b"E2E MONO"))
        if status != 200:
            raise Failure(f"sidplay returned HTTP {status}: {body[:160]!r}")
        lines = mus_screen(device)
        detail("\n".join(line for line in lines if line.strip()))
        if not any("TITLE : E2E MONO" in line for line in lines):
            raise Failure("the title is not the file name")
        if not any(MUS_NEEDS in line for line in lines):
            raise Failure(f"no line {MUS_NEEDS!r}")
        if any(line.startswith("NEEDS2") for line in lines):
            raise Failure("a mono song shows a second SID")


def test_mus_stereo(device: UltimateApi, host: str, password: str | None) -> None:
    paths = ("/Temp/E2E_STEREO.mus", "/Temp/E2E_STEREO.str")
    with check("a .mus with its .str plays on two SIDs, the second at $D500"):
        with ftp_lib.session(host, password) as client:
            ftp_lib.store(client, paths[0], mus(b"E2E STEREO LEFT"))
            ftp_lib.store(client, paths[1], mus(b"E2E STEREO RIGHT"))
        try:
            device.runners.sidplay(paths[0])
            lines = mus_screen(device)
            detail("\n".join(line for line in lines if line.strip()))
            for wanted in ("FOUND2: $D500 : ", "NEEDS2: $D500 : 8580"):
                if not any(wanted in line for line in lines):
                    raise Failure(f"no line {wanted!r}")
        finally:
            with ftp_lib.session(host, password) as client:
                for path in paths:
                    ftp_lib.delete_quietly(client, path)


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
            with check(f"an UltiSID set to {model} shows {model} on its FOUND line"):
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
             "mirror": test_mirror,
             "speed": test_speed,
             "any": test_any,
             "mus": test_mus,
             "mus-stereo": lambda d: test_mus_stereo(d, args.host, args.password or None),
             "ultisid-model": test_ultisid_model}[name](device)
    finally:
        teardown_step("stop the tune", lambda: device.machine.reset(force=True))


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Check the FOUND and NEEDS lines on the SID player's info screen.")
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
