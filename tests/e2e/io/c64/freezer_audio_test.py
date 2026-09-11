#!/usr/bin/env python3
"""E2E: the freezer takes the machine's sound quietly and every way out returns it.

Two ways out, and they are different code: closing the menu runs
`C64::unfreeze`, launching anything from the menu runs `C64::start_cartridge`.
Only the first restored the audio until 35023e32, which is what #887 reports as
a SID player that shows a tune playing and makes no sound.

The launch has to be a SID play, and "SID Player Autoconfig" has to be off,
which is the reporter's configuration. Both are what leave the FPGA mixer for
`start_cartridge` to restore. Measured on an Ultimate 64 Elite with the fix
reverted: a relaunched tune is silent with autoconfig off and audible with it
on, and a relaunched PRG is audible either way.

  - `U64Config::SidAutoConfig` sets `skipReset`, so the C64 reset the launch
    causes does not re-effectuate the settings, and the mixer stays closed. A
    PRG launch sets no such flag, so the reset handler reprograms the mixer and
    hides the defect.
  - With autoconfig on, `SetMixerAutoSid` rewrites the SID mixer channels while
    it remaps them, which restores them as a side effect.

Where the FPGA audio mixer does the muting, taking the machine is also silent.
The older method, writing the SID master volume, steps the output's DC offset
and is heard as a click, so the two entry checks skip on a machine that still
uses it. "Audio Mixer" in the config stores tells the two apart.

The audio stream is the instrument here, not the subject; what the streams
themselves have to do is tests/e2e/av/stream_test.py's. One packet's RMS is the
whole measurement, so nothing here depends on a note, a frequency or a system
mode.
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
from report import (Failure, check, check_skip, detail,          # noqa: E402
                    format_exception, suite_fail, suite_ok, teardown_step)

SUITE = "freezer_audio_test"

SCRIPT_DIR = Path(__file__).resolve().parent
TONE = SCRIPT_DIR / "freeze_tone.asm"

UI_STORE = "User Interface Settings"
UI_ITEM = "Interface Type"
FREEZE = "Freeze"
U64_STORE = "U64 Specific Settings"
AUTOCONFIG_ITEM = "SID Player Autoconfig"
MIXER_STORE = "Audio Mixer"
MIXER_ITEM = "Vol Master"
NO_MIXER = ("this machine mutes by writing the SID master volume, which steps "
            "the DC offset and is meant to click")

# The PSID container, and the two entry points freeze_tone.asm fixes.
PSID_HEADER_BYTES = 0x7C
INIT_ADDRESS = 0x1000
PLAY_ADDRESS = 0x1040

# The RMS that separates an audible machine from a silent one. The stimulus is
# a full-volume sawtooth and a silent machine measures far below this: measured
# 0.178 against 0.001 on an Ultimate 64 Elite.
AUDIBLE_RMS = 0.01
# One measurement window, and the slice a menu-button wait measures in.
WINDOW_SECONDS = 0.4
SLICE_SECONDS = 0.1
# How long the tone has to appear in. A launch resets the machine and loads.
TONE_TIMEOUT_SECONDS = 6.0
# How long a transient has to die away in, measured in whole windows.
SILENCE_TIMEOUT_SECONDS = 2.0
# What counts as muted, against the tone's own peak, floored for a quiet
# baseline. Measured frozen RMS is 0.000, so this is not a tight bound.
MUTE_RATIO = 0.10
MUTE_FLOOR = 0.003
# How much louder than the tone the menu entry may be. Measured 0.176 against a
# 0.178 tone with the mixer mute; the SID-volume mute it replaced was measured
# at peak sample 19000 against a silent-machine floor of 19.
POP_HEADROOM = 1.5


def _loudest(capture: AvStreamCapture, seconds: float) -> float:
    capture.capture(seconds)
    if not capture.audio_packets:
        raise Failure("no audio packets captured")
    return max(audio_rms(packet) for packet in capture.audio_packets)


def peak(capture: AvStreamCapture, seconds: float = WINDOW_SECONDS) -> float:
    """The loudest packet in a window starting now."""
    capture.clear()
    return _loudest(capture, seconds)


def peak_entering_menu(device: UltimateApi, capture: AvStreamCapture) -> float:
    """Open the freezer menu; answer the loudest packet between button and menu.

    Measured in slices so the window provably spans the transition: the mute
    happens in `backup_io`, before the menu screen this waits for.
    """
    capture.clear()
    device.machine.menu_button()
    deadline = time.monotonic() + menu_lib.MENU_TOGGLE_TIMEOUT_SECONDS
    loudest = 0.0
    while True:
        loudest = max(loudest, _loudest(capture, SLICE_SECONDS))
        if device.machine.menu_open():
            return loudest
        if time.monotonic() >= deadline:
            raise Failure("the freezer menu did not open")


def close_menu(device: UltimateApi) -> None:
    if not menu_lib.toggle_menu(device.machine.menu_button,
                                device.machine.menu_open, False):
        raise Failure("the freezer menu did not close")


def settled_peak(capture: AvStreamCapture, threshold: float,
                 timeout: float) -> float:
    """Wait for the machine to clear `threshold`, then answer its steady level.

    Two windows, and the quieter of them. A tune starts with the SID's own DC
    step, measured at RMS 0.660 against a 0.178 note, and one window taken
    straight after the wait can still hold it; the step is shorter than a
    window, so it cannot inflate both. The wait itself is for the launch, which
    resets the machine and loads.
    """
    deadline = time.monotonic() + timeout
    while peak(capture) <= threshold and time.monotonic() < deadline:
        pass
    return min(peak(capture), peak(capture))


def mute_ceiling(baseline: float) -> float:
    """The peak a silenced machine has to stay under, given the tone's own peak."""
    return max(MUTE_FLOOR, baseline * MUTE_RATIO)


def require_silence(capture: AvStreamCapture, ceiling: float, what: str) -> float:
    """Wait for the machine to go quieter than `ceiling`, and answer that level.

    What the checks assert is that it goes quiet, not that it was already:
    transients have to be allowed to pass. The SID-volume mute steps the DC
    offset, measured on u2@c64u at RMS 0.051 in the window the freeze lands in
    and 0.000 in the next, and a reset clicks to RMS 0.014 about half a second
    in on an Ultimate 64 Elite.
    """
    deadline = time.monotonic() + SILENCE_TIMEOUT_SECONDS
    quiet = peak(capture)
    while quiet > ceiling and time.monotonic() < deadline:
        quiet = peak(capture)
    if quiet > ceiling:
        raise Failure(f"{what} left the machine audible at RMS {quiet:.3f}, "
                      f"over the {ceiling:.3f} a silenced machine stays under")
    return quiet


def tone_sid() -> bytes:
    """freeze_tone.asm wrapped in a minimal PSID, for runners:sidplay."""
    program = assemble(TONE)
    if program[:2] != INIT_ADDRESS.to_bytes(2, "little"):
        raise Failure(f"the tone assembles at "
                      f"${int.from_bytes(program[:2], 'little'):04X}, not "
                      f"${INIT_ADDRESS:04X}")
    header = bytearray(PSID_HEADER_BYTES)
    header[:4] = b"PSID"
    # Big-endian: version 2, data offset, load address (0, so it is taken from
    # the first two bytes of the data), init, play, one song, starting at one.
    struct.pack_into(">7H", header, 4, 2, PSID_HEADER_BYTES, 0,
                     INIT_ADDRESS, PLAY_ADDRESS, 1, 1)
    for offset, text in ((0x16, b"FREEZE TONE"), (0x36, b"E2E"), (0x56, b"2026")):
        header[offset:offset + 0x20] = text.ljust(0x20, b"\0")
    if len(header) != PSID_HEADER_BYTES:
        # A slice assigned the wrong width grows a bytearray rather than
        # failing, which moves the data past the header's own data offset.
        raise Failure(f"the PSID header is {len(header)} bytes, not "
                      f"{PSID_HEADER_BYTES}")
    return bytes(header) + program


def config_value(device: UltimateApi, store: str, item: str) -> str:
    """One setting's value, or "" where this machine does not serve the item."""
    try:
        return device.configs.current(store, item)
    except Failure:
        return ""


def serves_store(device: UltimateApi, store: str) -> bool:
    """Whether this machine lists `store` among its config categories."""
    try:
        return store in device.configs.category_names()
    except Failure:
        return False


def refresh_mixer(device: UltimateApi) -> None:
    """Reprogram the FPGA mixer, by rewriting one of the items that programs it.

    A machine left frozen keeps its mixer closed across a reset whenever
    `skipReset` is set, so this is both the setup that guarantees a known
    starting point and the teardown that hands one on.
    """
    volume = config_value(device, MIXER_STORE, MIXER_ITEM)
    if volume:
        device.configs.set(MIXER_STORE, MIXER_ITEM, volume)


def run_checks(device: UltimateApi, interface: str, mixer_mute: bool) -> None:
    refresh_mixer(device)
    device.machine.reset(force=True)
    if interface:
        # Only where the machine offers a choice. An Ultimate II+L has no such
        # item and always freezes.
        device.configs.set(UI_STORE, UI_ITEM, FREEZE)
    tune = tone_sid()
    with AvStreamCapture(device.target) as capture:
        with check("entering the freezer menu is silent on a silent machine"):
            require_silence(capture, AUDIBLE_RMS, "the BASIC prompt")
            entry = peak_entering_menu(device, capture)
            close_menu(device)
            detail(f"entry peak RMS {entry:.3f}")
            if not mixer_mute:
                check_skip(NO_MIXER)
            elif entry > AUDIBLE_RMS:
                raise Failure(f"entering the freezer menu made a noise of RMS "
                              f"{entry:.3f}, over {AUDIBLE_RMS:.3f}")

        with check("a SID tune reaches the audio stream"):
            device.runners.upload("sidplay", tune)
            baseline = settled_peak(capture, AUDIBLE_RMS, TONE_TIMEOUT_SECONDS)
            if baseline <= AUDIBLE_RMS:
                raise Failure(f"the tune settled at RMS {baseline:.3f}, under "
                              f"the {AUDIBLE_RMS:.3f} an audible tune clears")
            detail(f"baseline RMS {baseline:.3f}")
        ceiling = mute_ceiling(baseline)

        with check("entering the freezer menu is silent while a tune plays"):
            entry = peak_entering_menu(device, capture)
            detail(f"entry peak RMS {entry:.3f} against a {baseline:.3f} tune")
            if not mixer_mute:
                check_skip(NO_MIXER)
            elif entry > baseline * POP_HEADROOM:
                raise Failure(f"entering the freezer menu rose to RMS "
                              f"{entry:.3f}, over the "
                              f"{baseline * POP_HEADROOM:.3f} a {baseline:.3f} "
                              f"tune stays under")

        with check("the freezer menu silences the machine"):
            frozen = require_silence(capture, ceiling, "the freezer menu")
            detail(f"frozen RMS {frozen:.3f}, under the {ceiling:.3f} bound")

        with check("closing the freezer menu gives the sound back"):
            close_menu(device)
            resumed = settled_peak(capture, ceiling, TONE_TIMEOUT_SECONDS)
            if resumed <= ceiling:
                raise Failure(f"closing the menu left the machine at RMS "
                              f"{resumed:.3f}, under the {ceiling:.3f} an "
                              f"audible tune clears")
            detail(f"resumed RMS {resumed:.3f}")

        with check("playing a SID from the freezer menu gives the sound back"):
            peak_entering_menu(device, capture)
            require_silence(capture, ceiling, "the second freeze")
            device.runners.upload("sidplay", tune)
            relaunched = settled_peak(capture, ceiling, TONE_TIMEOUT_SECONDS)
            if relaunched <= ceiling:
                raise Failure(f"the tune started with the machine still "
                              f"silenced, at RMS {relaunched:.3f} against the "
                              f"{ceiling:.3f} an audible tune clears")
            detail(f"relaunched RMS {relaunched:.3f}")


def run(args) -> None:
    """Set the machine up, run the checks, and put back what was changed."""
    device = UltimateApi(args.host, args.password or None, args.timeout)
    interface = config_value(device, UI_STORE, UI_ITEM)
    autoconfig = config_value(device, U64_STORE, AUTOCONFIG_ITEM)
    mixer_mute = serves_store(device, MIXER_STORE)
    try:
        if autoconfig:
            # On, the SID player rewrites the mixer channels itself while it
            # remaps the SIDs, which restores them whatever the freezer did.
            device.configs.set(U64_STORE, AUTOCONFIG_ITEM, "Disabled")
        run_checks(device, interface, mixer_mute)
    finally:
        # The tune plays until something stops it, and the mixer can be left
        # closed across a reset, so both are put back. teardown_step so a
        # teardown that cannot reach the device reports itself rather than
        # replacing the verdict.
        teardown_step("stop the tune", lambda: device.machine.reset(force=True))
        teardown_step("reprogram the audio mixer", lambda: refresh_mixer(device))
        if autoconfig:
            teardown_step(
                f"restore {AUTOCONFIG_ITEM} to {autoconfig!r}",
                lambda: device.configs.set(U64_STORE, AUTOCONFIG_ITEM, autoconfig))
        if interface:
            teardown_step(
                f"restore {UI_ITEM} to {interface!r}",
                lambda: device.configs.set(UI_STORE, UI_ITEM, interface))


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Check that the freezer silences the machine without a "
                    "click and that every way out gives the sound back.")
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
