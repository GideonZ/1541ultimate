#!/usr/bin/env python3
"""E2E: the freezer takes the machine's sound quietly and every way out returns it.

Two ways out, and they are different code: closing the menu runs
`C64::unfreeze`, launching anything from the menu runs `C64::start_cartridge`.
Only the first restored the audio until 35023e32, which is what #887 reports as
a SID player that shows a tune playing and makes no sound. Launching a PRG
covers every launch, because `C64_Subsys::dma_load`, `FileTypeSID::play_file`
and `runners:run_crt` all end in `start_cartridge`.

Where the FPGA audio mixer does the muting, taking the machine is also silent.
The older method, writing the SID master volume, steps the output's DC offset
and is heard as a click, so the two entry checks skip on a machine that still
uses it. "Audio Mixer" in the config stores is what tells the two apart.

The audio stream is the instrument here, not the subject; what the streams
themselves have to do is tests/e2e/av/stream_test.py's. One packet's RMS is the
whole measurement, so nothing here depends on a note, a frequency or a system
mode.
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

import cli                                                       # noqa: E402
import menu as menu_lib                                          # noqa: E402
from api import UltimateApi                                      # noqa: E402
from assembler import assemble                                   # noqa: E402
from av_stream import AvStreamCapture, audio_rms                 # noqa: E402
from report import (Failure, check, check_skip, detail,          # noqa: E402
                    format_exception, suite_fail, suite_ok, suite_skip,
                    teardown_step)

SUITE = "freezer_audio_test"

SCRIPT_DIR = Path(__file__).resolve().parent
TONE = SCRIPT_DIR / "freeze_tone.asm"

UI_STORE = "User Interface Settings"
UI_ITEM = "Interface Type"
FREEZE = "Freeze"
MIXER_STORE = "Audio Mixer"
NO_MIXER = ("this machine mutes by writing the SID master volume, which steps "
            "the DC offset and is meant to click")

# The RMS that separates an audible machine from a silent one. The stimulus is
# a full-volume sawtooth and a silent machine measures far below this.
AUDIBLE_RMS = 0.01
# One measurement window, and the slice a menu-button wait measures in.
WINDOW_SECONDS = 0.4
SLICE_SECONDS = 0.1
# Launching resets the machine and loads, so the tone is seconds away.
TONE_TIMEOUT_SECONDS = 10.0
# What counts as muted, against the tone's own peak, floored for a quiet
# baseline. Conservative rather than measured; every run prints its peaks.
MUTE_RATIO = 0.10
MUTE_FLOOR = 0.003
# How much louder than the tone the menu entry may be. The SID-volume mute was
# measured at peak sample 19000 against a silent-machine floor of 19, which is
# far above this whatever the tone's own level turns out to be.
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

    Measured in slices rather than over a fixed window so that the window
    provably spans the transition: the mute happens in `backup_io`, before the
    menu screen this waits for.
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


def audible_within(capture: AvStreamCapture, threshold: float,
                   timeout: float) -> float:
    """Measure until the peak clears `threshold`, and answer the loudest seen.

    A fixed window would have to be sized for the slowest launch and would make
    every check pay for it.
    """
    deadline = time.monotonic() + timeout
    loudest = 0.0
    while True:
        loudest = max(loudest, peak(capture))
        if loudest > threshold or time.monotonic() >= deadline:
            return loudest


def mute_ceiling(baseline: float) -> float:
    """The peak a silenced machine has to stay under, given the tone's own peak."""
    return max(MUTE_FLOOR, baseline * MUTE_RATIO)


def require_silence(capture: AvStreamCapture, ceiling: float, what: str) -> float:
    quiet = peak(capture)
    if quiet > ceiling:
        raise Failure(f"{what} left the machine audible at RMS {quiet:.3f}, "
                      f"over the {ceiling:.3f} a silenced machine stays under")
    return quiet


def config_value(device: UltimateApi, store: str, item: str) -> str:
    """One setting's value, or "" where this machine does not serve the item."""
    try:
        return device.configs.current(store, item)
    except Failure:
        return ""


def mutes_with_mixer(device: UltimateApi) -> bool:
    """Whether this machine mutes through the FPGA mixer rather than $D418."""
    try:
        return MIXER_STORE in device.configs.category_names()
    except Failure:
        return False


def run_checks(device: UltimateApi, mixer_mute: bool) -> None:
    device.machine.reset(force=True)
    device.configs.set(UI_STORE, UI_ITEM, FREEZE)
    program = assemble(TONE)
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

        with check("a continuous tone reaches the audio stream"):
            device.runners.upload("run_prg", program)
            baseline = audible_within(capture, AUDIBLE_RMS, TONE_TIMEOUT_SECONDS)
            if baseline <= AUDIBLE_RMS:
                raise Failure(f"the tone never got past RMS {AUDIBLE_RMS:.3f}; "
                              f"its peak was {baseline:.3f}")
            detail(f"baseline RMS {baseline:.3f}")
        ceiling = mute_ceiling(baseline)

        with check("entering the freezer menu is silent while a tone plays"):
            entry = peak_entering_menu(device, capture)
            detail(f"entry peak RMS {entry:.3f} against a {baseline:.3f} tone")
            if not mixer_mute:
                check_skip(NO_MIXER)
            elif entry > baseline * POP_HEADROOM:
                raise Failure(f"entering the freezer menu rose to RMS "
                              f"{entry:.3f}, over the {baseline * POP_HEADROOM:.3f} "
                              f"a {baseline:.3f} tone stays under")

        with check("the freezer menu silences the machine"):
            frozen = require_silence(capture, ceiling, "the freezer menu")
            detail(f"frozen RMS {frozen:.3f}, under the {ceiling:.3f} bound")

        with check("closing the freezer menu gives the sound back"):
            close_menu(device)
            resumed = audible_within(capture, ceiling, TONE_TIMEOUT_SECONDS)
            if resumed <= ceiling:
                raise Failure(f"closing the menu left the machine at RMS "
                              f"{resumed:.3f}, under the {ceiling:.3f} an "
                              f"audible tone clears")
            detail(f"resumed RMS {resumed:.3f}")

        with check("launching a program from the freezer menu gives the sound back"):
            peak_entering_menu(device, capture)
            require_silence(capture, ceiling, "the second freeze")
            device.runners.upload("run_prg", program)
            relaunched = audible_within(capture, ceiling, TONE_TIMEOUT_SECONDS)
            if relaunched <= ceiling:
                raise Failure(f"the program started with the machine still "
                              f"silenced, at RMS {relaunched:.3f} against the "
                              f"{ceiling:.3f} an audible tone clears")
            detail(f"relaunched RMS {relaunched:.3f}")


def run(args) -> str:
    """Run the checks. Returns the reason the suite skipped, or an empty string."""
    device = UltimateApi(args.host, args.password or None, args.timeout)
    original_interface = config_value(device, UI_STORE, UI_ITEM)
    if not original_interface:
        return f"this machine does not serve {UI_ITEM!r}, so it has no freezer"
    mixer_mute = mutes_with_mixer(device)
    try:
        run_checks(device, mixer_mute)
    finally:
        # The tone plays until something stops it, and a reset does: the C64's
        # reset line reaches the SID. teardown_step so a teardown that cannot
        # reach the device reports itself rather than replacing the verdict.
        teardown_step("stop the tone", lambda: device.machine.reset(force=True))
        teardown_step(
            f"restore {UI_ITEM} to {original_interface!r}",
            lambda: device.configs.set(UI_STORE, UI_ITEM, original_interface))
    return ""


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Check that the freezer silences the machine without a "
                    "click and that every way out gives the sound back.")
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
