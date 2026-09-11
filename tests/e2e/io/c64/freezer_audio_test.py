#!/usr/bin/env python3
"""E2E: the freezer silences the machine, and every way out gives the sound back.

Taking the machine has to take its sound with it, and handing it back has to
hand the sound back. There are two ways back: closing the menu, and starting a
cartridge from it. The second was missed, so a cartridge started from the
freezer menu ran with the mixer still closed until GideonZ/1541ultimate
35023e32 called `freezer_unmute_sids` from `C64::start_cartridge`. This is the
regression guard for both.

The firmware has two mute mechanisms, and which one runs decides what each
check proves:

- On the Ultimate 64 family `freezer_mute_sids` closes the FPGA audio mixer.
  That sits after the SID, so while the menu is open nothing the 6502 does can
  be heard, and only the firmware can open it again.
- Everywhere else it writes 0 to the SID master volume registers, $D418 among
  them. A program running on the C64 can write those back.

Entering the menu and closing it normally are therefore checked on every
target: both mechanisms mute, and `C64::unfreeze` restores both. The cartridge
check is a guard on the `start_cartridge` fix where the mixer mute is in use,
and a check that the machine came back audible at all elsewhere, because the
cartridge image sets its own SID volume when it starts.

The observable is the device's own audio stream, used here only as the
instrument that can hear the machine; what the stream itself has to do is
tests/e2e/av/stream_test.py's subject. The measurement is the RMS of one audio
packet, so it needs no note, no frequency and no PAL timing, and it is the same
on a machine in either system mode.
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

import cli                                                       # noqa: E402
import menu as menu_lib                                          # noqa: E402
from api import UltimateApi                                      # noqa: E402
from assembler import assemble                                   # noqa: E402
from av_stream import AvStreamCapture, audio_rms                 # noqa: E402
from report import (Failure, check, detail, format_exception,    # noqa: E402
                    suite_fail, suite_ok, suite_skip, teardown_step)

SUITE = "freezer_audio_test"

SCRIPT_DIR = Path(__file__).resolve().parent
TONE = SCRIPT_DIR / "freeze_tone.asm"
# The same source assembled as an 8 KiB Ultimax ROM image; see freeze_tone.asm.
TONE_CARTRIDGE_DEFINES = {"CARTRIDGE": 1}

UI_STORE = "User Interface Settings"
UI_ITEM = "Interface Type"
FREEZE = "Freeze"

# How loud one audio packet has to be for the tone to count as playing at all.
# An RMS of 0.01 is about -40dBFS; the stimulus plays a full-volume sawtooth,
# and a silent machine measures orders of magnitude below that.
TONE_FLOOR = 0.01
# One measurement window. Long enough to hold about fifty audio packets, short
# enough that the whole suite is a handful of them.
WINDOW_SECONDS = 0.4
# How long the tone has to appear in. runners:run_prg resets the machine, loads
# the program and starts it, and runners:run_crt resets it into a cartridge, so
# both are seconds rather than milliseconds.
TONE_TIMEOUT_SECONDS = 10.0
# What counts as muted, as a fraction of the tone's own measured peak. A mute
# that worked measures at the silent-machine floor, so the whole band between
# that floor and a tenth of the tone is unclaimed: these checks only have to
# tell "the machine has been silenced" from "the tone is playing", and the
# bound is deliberately conservative rather than tight. MUTE_FLOOR keeps it
# above the stream's own noise for a quiet baseline. Neither is measured on
# hardware yet; every run reports the peaks it saw, which is what a later
# tightening should be based on.
MUTE_RATIO = 0.10
MUTE_FLOOR = 0.003

# The CRT container tone_cartridge() writes, and the image it wraps. An 8 KiB
# ROM at $E000, which is two bytes more as a .prg because of the load address.
CRT_HEADER_BYTES = 0x40
CRT_CHIP_HEADER_BYTES = 0x10
IMAGE_BYTES = 0x2000
IMAGE_PRG_BYTES = IMAGE_BYTES + 2


def audio_peak(capture: AvStreamCapture, seconds: float) -> float:
    """The loudest packet in a short, fresh audio window."""
    capture.clear()
    capture.capture(seconds)
    if not capture.audio_packets:
        raise Failure("no audio packets captured")
    return max(audio_rms(packet) for packet in capture.audio_packets)


def loudest_within(capture: AvStreamCapture, threshold: float,
                   timeout: float) -> float:
    """Measure until the tone is louder than `threshold`, and answer the peak.

    Every step that expects the tone to be playing waits for something the
    device does in its own time: loading and starting a PRG, leaving the
    freezer, resetting into a cartridge. One fixed window sized for the slowest
    of those would make every check pay for it, and one sized for the fastest
    would report a slow machine as a machine that stayed muted.

    The muted steps do not use this. A mute is complete before the menu is
    drawn, so they measure one window and wait for nothing.
    """
    deadline = time.monotonic() + timeout
    loudest = 0.0
    while True:
        loudest = max(loudest, audio_peak(capture, WINDOW_SECONDS))
        if loudest > threshold or time.monotonic() >= deadline:
            return loudest


def mute_ceiling(baseline: float) -> float:
    """The peak a silenced machine has to stay under, given the tone's own peak."""
    return max(MUTE_FLOOR, baseline * MUTE_RATIO)


def toggle_menu(device: UltimateApi, want_open: bool) -> None:
    """Press the menu button and wait for the menu to reach `want_open`."""
    if not menu_lib.toggle_menu(device.machine.menu_button,
                                device.machine.menu_open, want_open):
        raise Failure(f"the freezer menu did not "
                      f"{'open' if want_open else 'close'}")


def freeze_and_confirm_silence(device: UltimateApi, capture: AvStreamCapture,
                               ceiling: float, what: str) -> float:
    """Open the freezer menu, and answer the peak it left behind."""
    toggle_menu(device, want_open=True)
    peak = audio_peak(capture, WINDOW_SECONDS)
    if peak > ceiling:
        raise Failure(f"{what} left the machine audible at RMS {peak:.3f}, "
                      f"over the {ceiling:.3f} a silenced machine has to stay "
                      f"under")
    return peak


def tone_cartridge() -> bytes:
    """A minimal Ultimax CRT that starts the same continuous tone after reset.

    Built here rather than committed, so the cartridge and the PRG cannot drift
    apart: both are freeze_tone.asm. The layout is the CRT container's, all of
    it big-endian: a $40-byte file header, then one CHIP packet holding the
    $2000-byte image that loads at $E000.
    """
    program = assemble(TONE, TONE_CARTRIDGE_DEFINES)
    if program[:2] != b"\x00\xe0" or len(program) != IMAGE_PRG_BYTES:
        raise Failure(f"the freeze tone cartridge build is {len(program)} bytes "
                      f"at ${int.from_bytes(program[:2], 'little'):04X}, not an "
                      f"8 KiB image at $E000")
    header = bytearray(CRT_HEADER_BYTES)
    header[:16] = b"C64 CARTRIDGE   "
    struct.pack_into(">I", header, 0x10, CRT_HEADER_BYTES)
    struct.pack_into(">H", header, 0x14, 0x0100)       # container version 1.0
    # Type 0, "normal cartridge", is left at $16. EXROM inactive and GAME
    # active is what puts the machine in Ultimax mode; in this header a 1 is
    # the inactive line.
    header[0x18:0x1A] = b"\x01\x00"
    header[0x20:0x40] = b"FREEZE TONE".ljust(0x20, b"\0")
    chip = bytearray(CRT_CHIP_HEADER_BYTES)
    chip[:4] = b"CHIP"
    struct.pack_into(">I", chip, 4, CRT_CHIP_HEADER_BYTES + IMAGE_BYTES)
    struct.pack_into(">H", chip, 12, 0xE000)           # load address
    struct.pack_into(">H", chip, 14, IMAGE_BYTES)
    return bytes(header + chip + program[2:])


def interface_type(device: UltimateApi) -> str:
    """The machine's Interface Type, or "" where it does not serve the item.

    Asking for an item a machine has no store for is an answer rather than a
    fault: a machine with no freezer interface to select has nothing here to
    measure.
    """
    try:
        return device.configs.current(UI_STORE, UI_ITEM)
    except Failure:
        return ""


def run_checks(device: UltimateApi) -> None:
    """The four checks, in the order the machine has to go through them."""
    device.machine.reset(force=True)
    device.configs.set(UI_STORE, UI_ITEM, FREEZE)
    # The handle rather than the host name: for a cartridge target the audio
    # belongs to the computer it is plugged into, and only the handle knows
    # which machine that is.
    with AvStreamCapture(device.target) as capture:
        with check("a continuous tone reaches the audio stream"):
            device.runners.upload("run_prg", assemble(TONE))
            baseline = loudest_within(capture, TONE_FLOOR, TONE_TIMEOUT_SECONDS)
            if baseline <= TONE_FLOOR:
                raise Failure(f"the tone never got past RMS {TONE_FLOOR:.3f}; "
                              f"its peak was {baseline:.3f}")
            detail(f"baseline RMS {baseline:.3f}")
        ceiling = mute_ceiling(baseline)

        with check("opening the freezer menu silences the machine"):
            frozen = freeze_and_confirm_silence(device, capture, ceiling,
                                                "the freezer menu")
            detail(f"frozen RMS {frozen:.3f}, under the {ceiling:.3f} bound")

        with check("closing the freezer menu gives the sound back"):
            toggle_menu(device, want_open=False)
            resumed = loudest_within(capture, ceiling, TONE_TIMEOUT_SECONDS)
            if resumed <= ceiling:
                raise Failure(f"closing the menu left the machine at RMS "
                              f"{resumed:.3f}, under the {ceiling:.3f} an "
                              f"audible tone has to clear")
            detail(f"resumed RMS {resumed:.3f}")

        with check("starting a cartridge from the freezer menu gives the "
                   "sound back"):
            # Freezing again is this check's precondition rather than its
            # subject: without it a cartridge start would be measured against a
            # machine that was already audible.
            freeze_and_confirm_silence(device, capture, ceiling,
                                       "the second freeze")
            # POST rather than a path: the image travels with the request, so
            # the check needs nothing on the device's storage.
            device.runners.upload("run_crt", tone_cartridge())
            started = loudest_within(capture, ceiling, TONE_TIMEOUT_SECONDS)
            if started <= ceiling:
                raise Failure(f"the cartridge started with the machine still "
                              f"silenced, at RMS {started:.3f} against the "
                              f"{ceiling:.3f} an audible tone has to clear")
            detail(f"cartridge RMS {started:.3f}")


def run(args) -> str:
    """Run the checks. Returns the reason the suite skipped, or an empty string."""
    device = UltimateApi(args.host, args.password or None, args.timeout)
    original_interface = interface_type(device)
    if not original_interface:
        return f"this machine does not serve {UI_ITEM!r}, so it has no freezer"
    try:
        run_checks(device)
    finally:
        # A reboot is what removes the uploaded cartridge and returns the
        # machine to its configured one; the setting goes back after it, for
        # the suite that runs next. teardown_step rather than a bare call, so a
        # teardown that cannot reach the device reports itself instead of
        # replacing the verdict of the check that failed.
        teardown_step("restore the configured cartridge", device.machine.reboot)
        teardown_step(
            f"restore {UI_ITEM} to {original_interface!r}",
            lambda: device.configs.set(UI_STORE, UI_ITEM, original_interface))
    return ""


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Check that the freezer silences the machine and that "
                    "every way out of it gives the sound back.")
    cli.add_device_arguments(parser)
    args = parser.parse_args()
    try:
        skipped = run(args)
    except Exception as exc:            # noqa: BLE001
        suite_fail(SUITE, format_exception(exc))
        return 1
    if skipped:
        suite_skip(SUITE, skipped)
        return 0
    suite_ok(SUITE)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
