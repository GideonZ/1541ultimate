#!/usr/bin/env python3
"""E2E: what the SID player's info screen says about each SID a tune uses: the
model measured at each address, and the model, video standard and interrupt the
file asks for.

The checks state facts and read them from the screen whatever its layout.
Master shows one measured line, "SYSTEM: $D400 : 6581 / PAL", and the requested
ones as "SID #2: $D420 : 8580"; #949 shows "FOUND : $D400 : 6581    : PAL" and
"NEEDS2: $D420 : 8580". So every check runs on both, and on master it fails only
where master shows a wrong value or none. Each check says which:

  [wrong on master]  master shows the value, and it is wrong
  [control]          both show the right value
  [new]              master does not show it

  real-chips      [control] each socket's real chip, alone at $D400, is
                  measured as the model its socket detected.
  ultisid-model   UltiSID 1 alone at $D400, set to 6581 [control] and to 8580
                  [wrong on master]: the timing check calls every UltiSID a
                  6581; #949 tells the 8580 by its combined waveforms.
  ultisid-follows the player makes the UltiSIDs it maps the model the tune
                  asks for. Both sockets off, both UltiSIDs set to 6581, a tune
                  for two 8580s: they play as 8580s [wrong on master: master
                  leaves them at 6581]. The next tune, with autoconfig off and
                  UltiSID 1 alone at $D400, measures what was left in it,
                  since its start reset keeps the SID setup: 8580 [wrong on
                  master]; 6581 without "Allow Autoconfig uses UltiSid", with
                  SID Player Autoconfig off, or after a reset [control].
  inherited      SID #2 with its model left open shows SID #1's model, as the
                  SID file format defines it [wrong on master: UNKNOWN]; UNKNOWN
                  when SID #1 is open too [control]; SID #3 left open copies
                  SID #1, not SID #2 [control].
  lines           [new] a three-SID tune gets a measured line for $D400, $D420
                  and $D440.
  mirror          [new] with nothing at $D420 and $D440 they show UNKNOWN: a
                  SID decodes five address lines, so the SID at $D400 answers
                  there too, and must not be measured twice.
  irq             [new] the first requested line names the song's interrupt:
                  VBI without the speed flag, CIA with it, per song, and RSID
                  for an RSID tune, which sets up its own.
  any             [control] a tune for both models and both clocks is shown as
                  for both: ANY, or master's "6581 / 8580" and "PAL / NTSC".
  mus             a Compute's Sidplayer file, uploaded under its name, plays
                  with the MUS player and asks for an 8580 and NTSC, as the
                  device's made-up header does [control], on a CIA [new].
  mus-stereo      the same with a .str beside it, copied by FTP: a second
                  requested SID at $D500 [control], and a measured line for it
                  [new].

The player places its screen wherever the tune leaves room, not at $0400, so
the suite asks the VIC where it is: the bank from $DD00, the offset from $D018.

real-chips, ultisid-model, ultisid-follows and mirror need a machine whose
SIDs can be mapped, a U64 or C64 Ultimate; a cartridge skips them where it
cannot. They change SID Player Autoconfig and its UltiSID permission, the SID
addressing, the sockets and the UltiSIDs' waveforms and filters, each with the
machine reset first or halted, and put every one of them back.
"""

from __future__ import annotations

import argparse
from contextlib import contextmanager
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

TESTS = ("real-chips", "ultisid-model", "ultisid-follows", "inherited", "lines", "mirror", "irq",
         "any", "mus", "mus-stereo")

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

# A measured line: master's "SYSTEM:", or "FOUND :", "FOUND2:", "FOUND3:".
MEASURED = re.compile(r"^(SYSTEM|FOUND[ 23]): \$(D[0-9A-F]{3}) : (.*)$")
# A requested line: master's "SID   :" or "SID #n:", or "NEEDS :", "NEEDS2:", ...
REQUESTED = re.compile(r"^(SID   |SID #[123]|NEEDS[ 23]): \$(D[0-9A-F]{3}) : (.*)$")
# The advanced player's clock, once it runs: the screen is complete.
CLOCK = re.compile(r"^\d\d:\d\d")
MODELS = ("6581", "8580")
VIDEO = ("PAL", "NTSC")

U64_STORE = "U64 Specific Settings"
AUTOCONFIG_ITEM = "SID Player Autoconfig"
ALLOW_ULTISID_ITEM = "Allow Autoconfig uses UltiSid"
ADDRESS_STORE = "SID Addressing"
ULTISID_STORE = "UltiSID Configuration"
ULTISID1_ADDRESS = "UltiSID 1 Address"
ADDRESS_ITEMS = ("SID Socket 1 Address", "SID Socket 2 Address",
                 ULTISID1_ADDRESS, "UltiSID 2 Address")
WAVES_ITEM = "UltiSID 1 Combined Waveforms"
FILTER_ITEM = "UltiSID 1 Filter Curve"
UNMAPPED = "Unmapped"
FILTER_FOR = {"8580": "8580 Lo", "6581": "6581"}
SOCKET_STORE = "SID Sockets Configuration"
MODEL_CODE = {"6581": MODEL_6581, "8580": MODEL_8580}


def psid(name: bytes, sids: list[tuple[int, int]], clock: int = PAL,
         songs: int = 1, speed: int = 0, magic: bytes = b"PSID",
         play: int = PLAY_ADDRESS) -> bytes:
    """A PSID v4 for `sids`, a list of (address, model) for SID #1 to #3.

    `speed` is the header's speed flags, bit 0 for song 1: a set bit means
    the song runs on a CIA timer, a clear one on the 50/60 Hz default.
    """
    header = bytearray(PSID_HEADER_BYTES)
    header[:4] = magic
    # Big-endian: version, data offset, load address (0: taken from the data),
    # init, play, `songs` songs, starting at one, and the speed flags.
    struct.pack_into(">7HI", header, 4, 4, PSID_HEADER_BYTES, 0,
                     INIT_ADDRESS, play, songs, 1, speed)
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


# Checks that failed, so that a red run on master still reaches every check
# after the first: a red/green comparison needs the controls run on both.
FAILED: list[str] = []


@contextmanager
def checked(label: str):
    """check(), but a Failure is reported and noted rather than ending the
    suite; anything else still ends it."""
    try:
        with check(label):
            yield
    except Failure:
        FAILED.append(label)


def fields(rest: str) -> tuple[str | None, str | None, str | None]:
    """Model, video standard and interrupt from what follows the address.

    #949 puts ":" between the columns. Master puts "/" both between the fields
    and between the two values of one, "6581 / 8580 / PAL / NTSC", so its
    tokens are sorted by what they are: a second UNKNOWN is the video standard.
    Both values of a field read as ANY, as #949 writes them.
    """
    if ":" in rest:
        parts = [part.strip() for part in rest.split(":")] + [None, None]
        return parts[0], parts[1] or None, parts[2] or None
    models: list[str] = []
    video: list[str] = []
    for token in (token.strip() for token in rest.split("/")):
        if token in MODELS or (token == "UNKNOWN" and not models):
            models.append(token)
        elif token in VIDEO or token == "UNKNOWN":
            video.append(token)
    model = "ANY" if set(models) == set(MODELS) else (models[0] if models else None)
    clock = "ANY" if set(video) == set(VIDEO) else (video[0] if video else None)
    return model, clock, None


def sid_lines(lines: list[str]) -> tuple[dict[str, tuple], dict[str, tuple]]:
    """Measured: address -> (model, video, irq). Requested: SID number ->
    (address, model, video, irq)."""
    measured: dict[str, tuple] = {}
    requested: dict[str, tuple] = {}
    for line in lines:
        match = MEASURED.match(line)
        if match:
            measured[match.group(2)] = fields(match.group(3))
            continue
        match = REQUESTED.match(line)
        if match:
            number = match.group(1)[-1] if match.group(1)[-1] in "123" else "1"
            requested[number] = (match.group(2), *fields(match.group(3)))
    return measured, requested


def drawn(lines: list[str]) -> bool:
    """Whether the player has finished the screen: the cartridge writes the SID
    lines, then the advanced player starts its clock and rewrites the first
    measured line."""
    measured, requested = sid_lines(lines)
    return bool(measured and requested and any(CLOCK.match(line) for line in lines[20:]))


def read_screen(device: UltimateApi, extra=lambda lines: True) -> list[str]:
    deadline = time.monotonic() + SCREEN_TIMEOUT_SECONDS
    lines: list[str] = []
    while time.monotonic() < deadline:
        lines = decode(device.machine.readmem(screen_address(device),
                                              SCREEN_COLUMNS * SCREEN_ROWS))
        if drawn(lines) and extra(lines):
            return lines
        time.sleep(POLL_SECONDS)
    raise Failure("the player's screen was not complete within "
                  f"{SCREEN_TIMEOUT_SECONDS:.0f}s; last screen: {lines!r}")


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
    lines = read_screen(device)
    detail("\n".join(line for line in lines if MEASURED.match(line) or REQUESTED.match(line)))
    return lines


def expect(what: str, shown, wanted) -> None:
    if shown is None:
        raise Failure(f"{what} is not shown, expected {wanted}")
    if shown != wanted:
        raise Failure(f"{what} is {shown!r}, expected {wanted}")


def configured(device: UltimateApi, store: str, item: str) -> str:
    """One setting's value, or "" where this machine does not serve it."""
    try:
        return device.configs.current(store, item)
    except Failure:
        return ""


class Settings:
    """Change settings with the machine reset first, and put them all back.

    A SID player running while the SID addressing changes has hung a C64
    Ultimate; the reset stops it.
    """

    def __init__(self, device: UltimateApi):
        self.device = device
        self.saved: dict[tuple[str, str], str] = {}

    def set(self, store: str, item: str, value: str) -> None:
        if (store, item) not in self.saved:
            self.saved[(store, item)] = configured(self.device, store, item)
        self.device.machine.reset(force=True)
        self.device.configs.set(store, item, value)

    def alone_at_d400(self, item: str) -> None:
        """`item`'s SID at $D400, nothing else mapped, autoconfig off."""
        self.set(U64_STORE, AUTOCONFIG_ITEM, "Disabled")
        for other in ADDRESS_ITEMS:
            if other != item:
                self.set(ADDRESS_STORE, other, UNMAPPED)
        self.set(ADDRESS_STORE, item, "$D400")

    def alone_at_d400_halted(self, item: str) -> None:
        """alone_at_d400(), with the CPU halted instead of a reset: a reset
        would put the SIDs back as the settings have them."""
        self.device.machine.pause()
        for store, other, value in ([(U64_STORE, AUTOCONFIG_ITEM, "Disabled")]
                                    + [(ADDRESS_STORE, a, "$D400" if a == item else UNMAPPED)
                                       for a in ADDRESS_ITEMS]):
            if (store, other) not in self.saved:
                self.saved[(store, other)] = configured(self.device, store, other)
            self.device.configs.set(store, other, value)

    def restore(self) -> None:
        self.device.machine.reset(force=True)
        for (store, item), value in self.saved.items():
            if value:
                teardown_step(f"restore {item} to {value!r}",
                              lambda s=store, i=item, v=value: self.device.configs.set(s, i, v))


def mappable(device: UltimateApi, label: str) -> bool:
    if configured(device, ADDRESS_STORE, ULTISID1_ADDRESS):
        return True
    with check(label):
        check_skip("this machine cannot map its SIDs")
    return False


def test_real_chips(device: UltimateApi) -> None:
    label = "[control] a real chip alone at $D400 is measured as its model"
    if not mappable(device, label):
        return
    chips = [(n, configured(device, SOCKET_STORE, f"SID Detected Socket {n}")) for n in (1, 2)]
    chips = [(n, model) for n, model in chips if model in MODELS]
    if not chips:
        with check(label):
            check_skip("no 6581 or 8580 detected in a socket")
        return
    settings = Settings(device)
    try:
        for n, model in chips:
            with checked(f"[control] the real {model} in socket {n}, alone at $D400, "
                       f"is measured as {model}"):
                settings.set(SOCKET_STORE, f"SID Socket {n}", "Enabled")
                settings.alone_at_d400(f"SID Socket {n} Address")
                measured, _ = sid_lines(play_and_read(
                    device, psid(b"REAL CHIP", [(0xD400, MODEL_CODE[model])])))
                expect("$D400", measured.get("D400", (None,))[0], model)
    finally:
        settings.restore()


def test_ultisid_model(device: UltimateApi) -> None:
    if not mappable(device, "an UltiSID is measured as the model it is set to"):
        return
    settings = Settings(device)
    try:
        settings.alone_at_d400(ULTISID1_ADDRESS)
        for model, kind in (("6581", "control"), ("8580", "wrong on master")):
            with checked(f"[{kind}] UltiSID 1 set to {model}, alone at $D400, "
                       f"is measured as {model}"):
                settings.set(ULTISID_STORE, WAVES_ITEM, model)
                settings.set(ULTISID_STORE, FILTER_ITEM, FILTER_FOR[model])
                measured, _ = sid_lines(play_and_read(
                    device, psid(b"ULTISID MODEL", [(0xD400, MODEL_CODE[model])])))
                expect("$D400", measured.get("D400", (None,))[0], model)
    finally:
        settings.restore()


def test_ultisid_follows(device: UltimateApi) -> None:
    if not mappable(device, "the player makes the UltiSIDs it maps the model asked for"):
        return
    two_8580s = psid(b"TWO 8580S", [(0xD400, MODEL_8580), (0xD420, MODEL_8580)])
    afterwards = psid(b"ULTISID AFTERWARDS", [(0xD400, MODEL_6581)])
    # allow, autoconfig, reset before the next tune, what it measures, kind
    cases = (("Yes", "Enabled", False, "8580", "wrong on master"),
             ("No", "Enabled", False, "6581", "control"),
             ("Yes", "Disabled", False, "6581", "control"),
             ("Yes", "Enabled", True, "6581", "control"))
    settings = Settings(device)
    try:
        for allow, autoconfig, reset, left, kind in cases:
            settings.set(U64_STORE, AUTOCONFIG_ITEM, autoconfig)
            settings.set(U64_STORE, ALLOW_ULTISID_ITEM, allow)
            for n in (1, 2):
                if configured(device, SOCKET_STORE, f"SID Socket {n}"):
                    settings.set(SOCKET_STORE, f"SID Socket {n}", "Disabled")
                settings.set(ULTISID_STORE, f"UltiSID {n} Combined Waveforms", "6581")
                settings.set(ULTISID_STORE, f"UltiSID {n} Filter Curve", FILTER_FOR["6581"])
            setup = f"autoconfig {autoconfig.lower()}, UltiSID permission {allow.lower()}"
            measured, _ = sid_lines(play_and_read(device, two_8580s))
            if (allow, autoconfig) == ("Yes", "Enabled") and not reset:
                with checked("[wrong on master] a tune for two 8580s, with only UltiSIDs set "
                             "to 6581 to play it, gets an 8580 at $D400"):
                    expect("$D400", measured.get("D400", (None,))[0], "8580")
            settings.alone_at_d400_halted(ULTISID1_ADDRESS)
            if reset:
                device.machine.reset(force=True)
            else:
                device.machine.resume()
            with checked(f"[{kind}] after that tune ({setup}"
                         f"{', then a reset' if reset else ''}), "
                         f"UltiSID 1 alone at $D400 is measured as {left}"):
                device.runners.upload("sidplay", afterwards)
                measured, _ = sid_lines(read_screen(device))
                expect("$D400", measured.get("D400", (None,))[0], left)
    finally:
        settings.restore()


def test_inherited(device: UltimateApi) -> None:
    for first, expected, kind in ((MODEL_6581, "6581", "wrong on master"),
                                  (MODEL_UNKNOWN, "UNKNOWN", "control")):
        with checked(f"[{kind}] SID #2 with its model left open shows {expected}"
                   + (", SID #1's model" if first != MODEL_UNKNOWN else
                      ", as SID #1 is open too")):
            _, requested = sid_lines(play_and_read(device, psid(
                b"MODEL LEFT OPEN", [(0xD400, first), (0xD420, MODEL_UNKNOWN)])))
            expect("SID #2's model", requested.get("2", (None, None))[1], expected)
    with checked("[control] SID #3 with its model left open copies SID #1, not SID #2"):
        _, requested = sid_lines(play_and_read(device, psid(b"THIRD LEFT OPEN", [
            (0xD400, MODEL_6581), (0xD420, MODEL_8580), (0xD440, MODEL_UNKNOWN)])))
        expect("SID #3's model", requested.get("3", (None, None))[1], "6581")


def test_lines(device: UltimateApi) -> None:
    with checked("[new] a three-SID tune gets a measured line for each of its addresses"):
        measured, _ = sid_lines(play_and_read(device, psid(b"THREE SIDS", [
            (0xD400, MODEL_8580), (0xD420, MODEL_8580), (0xD440, MODEL_8580)])))
        expect("the measured addresses", sorted(measured) or None, ["D400", "D420", "D440"])


def test_mirror(device: UltimateApi) -> None:
    label = "[new] a three-SID tune with nothing at $D420 and $D440 shows UNKNOWN there"
    settings = Settings(device)
    try:
        if configured(device, ADDRESS_STORE, ULTISID1_ADDRESS):
            settings.set(U64_STORE, AUTOCONFIG_ITEM, "Disabled")
            for item in ADDRESS_ITEMS:
                if configured(device, ADDRESS_STORE, item) in ("$D420", "$D440"):
                    settings.set(ADDRESS_STORE, item, UNMAPPED)
        with checked(label):
            measured, _ = sid_lines(play_and_read(device, psid(b"NOTHING THERE", [
                (0xD400, MODEL_6581), (0xD420, MODEL_8580), (0xD440, MODEL_8580)])))
            for address in ("D420", "D440"):
                expect(f"${address}", measured.get(address, (None,))[0], "UNKNOWN")
    finally:
        settings.restore()


def test_irq(device: UltimateApi) -> None:
    sids = [(0xD400, MODEL_6581)]
    for speed, expected in ((0, "VBI"), (1, "CIA")):
        with checked(f"[new] speed flag {speed} shows {expected}"):
            _, requested = sid_lines(play_and_read(device, psid(b"SPEED", sids, speed=speed)))
            expect("the interrupt", requested.get("1", (None,) * 4)[3], expected)
    for song, expected in ((1, "VBI"), (2, "CIA")):
        with checked(f"[new] a tune whose song 2 has the speed flag shows {expected} for song {song}"):
            _, requested = sid_lines(play_and_read(
                device, psid(b"TWO SPEEDS", sids, songs=2, speed=0b10), song))
            expect("the interrupt", requested.get("1", (None,) * 4)[3], expected)
    with checked("[new] an RSID tune shows RSID, as it sets up its own interrupt"):
        _, requested = sid_lines(play_and_read(
            device, psid(b"RSID", sids, magic=b"RSID", play=0)))
        expect("the interrupt", requested.get("1", (None,) * 4)[3], "RSID")


def test_any(device: UltimateApi) -> None:
    for clock, video in ((CLOCK_ANY, "ANY"), (CLOCK_UNKNOWN, "UNKNOWN")):
        with checked(f"[control] a tune for both models shows ANY and video {video}"):
            _, requested = sid_lines(play_and_read(
                device, psid(b"ANY MODEL", [(0xD400, MODEL_ANY)], clock=clock)))
            first = requested.get("1", (None,) * 4)
            expect("the model", first[1], "ANY")
            expect("the video standard", first[2], video)


def mus(text: bytes) -> bytes:
    """Compute's Sidplayer data whose three voices only halt.

    A load address, the three voice lengths, each voice the two-byte halt
    command, and the text, ended by a zero: what FileTypeSID reads, and all
    the info screen needs.
    """
    voices = bytes((0x01, 0x4F)) * 3
    return b"\x00\x10" + bytes((2, 0, 2, 0, 2, 0)) + voices + text + b"\r\x00"


def mus_screen(device: UltimateApi) -> list[str]:
    """The info screen once the MUS player has drawn it."""
    lines = read_screen(device, lambda lines: any("MUS PLAYER" in line for line in lines))
    detail("\n".join(line for line in lines if line.strip()))
    return lines


def test_mus(device: UltimateApi) -> None:
    lines: list[str] = []
    with checked("[control] an uploaded .mus plays with the MUS player and asks for 8580 and NTSC"):
        status, _, body = device.runners.upload_file("sidplay", "E2E_MONO.mus",
                                                     mus(b"E2E MONO"))
        if status != 200:
            raise Failure(f"sidplay returned HTTP {status}: {body[:160]!r}")
        lines = mus_screen(device)
        if not any("TITLE : E2E MONO" in line for line in lines):
            raise Failure("the title is not the file name")
        _, requested = sid_lines(lines)
        first = requested.get("1", (None,) * 4)
        expect("the model", first[1], "8580")
        expect("the video standard", first[2], "NTSC")
        if "2" in requested:
            raise Failure("a mono song shows a second SID")
    with checked("[new] the MUS player names its CIA interrupt"):
        if not lines:
            raise Failure("the MUS player did not start")
        _, requested = sid_lines(lines)
        expect("the interrupt", requested.get("1", (None,) * 4)[3], "CIA")


def test_mus_stereo(device: UltimateApi, host: str, password: str | None) -> None:
    paths = ("/Temp/E2E_STEREO.mus", "/Temp/E2E_STEREO.str")
    lines: list[str] = []
    with ftp_lib.session(host, password) as client:
        ftp_lib.store(client, paths[0], mus(b"E2E STEREO LEFT"))
        ftp_lib.store(client, paths[1], mus(b"E2E STEREO RIGHT"))
    try:
        with checked("[control] a .mus with its .str asks for a second SID at $D500"):
            device.runners.sidplay(paths[0])
            lines = mus_screen(device)
            _, requested = sid_lines(lines)
            second = requested.get("2", (None,) * 4)
            expect("SID #2's address", second[0], "D500")
            expect("SID #2's model", second[1], "8580")
        with checked("[new] the second SID at $D500 gets a measured line"):
            if not lines:
                raise Failure("the MUS player did not start")
            measured, _ = sid_lines(lines)
            if "D500" not in measured:
                raise Failure("no measured line for $D500")
    finally:
        with ftp_lib.session(host, password) as client:
            for path in paths:
                ftp_lib.delete_quietly(client, path)


def run(args) -> None:
    device = UltimateApi(args.host, args.password or None, args.timeout)
    tests = TESTS if args.test == "all" else (args.test,)
    try:
        for name in tests:
            {"real-chips": test_real_chips,
             "ultisid-model": test_ultisid_model,
             "ultisid-follows": test_ultisid_follows,
             "inherited": test_inherited,
             "lines": test_lines,
             "mirror": test_mirror,
             "irq": test_irq,
             "any": test_any,
             "mus": test_mus,
             "mus-stereo": lambda d: test_mus_stereo(d, args.host, args.password or None)}[name](device)
    finally:
        teardown_step("stop the tune", lambda: device.machine.reset(force=True))
    if FAILED:
        raise Failure(f"{len(FAILED)} checks failed: " + "; ".join(FAILED))


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Check what the SID player's info screen says about each SID.")
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
