#!/usr/bin/env python3
"""E2E: an Ultimax cartridge has to reach the VIC stream pixel for pixel.

Jupiter Lander is an 8K cartridge at $E000 with EXROM high and GAME low, so the
machine runs it in Ultimax mode: the C64's own ROMs and RAM are banked out and
the VIC fetches characters and colour through the cartridge. That is the mode
the picture is evidence for, and a defect in it shows in what the VIC draws
rather than in anything the REST surface can be asked.

The check starts the cartridge, taps F1 to start the game, and compares the
whole 384x272 frame against `jupiter_lander.png`, index for index, with no
tolerance. Two things make an exact comparison the right assertion here rather
than a brittle one:

- The screen is still. The game waits for the player before anything moves, so
  there is no frame timing to absorb: measured on an Ultimate 64-II over more
  than twenty starts, the frame was identical to the reference every time, and
  it arrived 2.1s to 2.9s after the cartridge started.
- The failure it is for is not subtle. The Ultimax fault measured on this bench
  drew every shape in the right place and gave every non-black pixel the same
  colour, white: the geometry matched exactly and 67.1% of pixels did. A
  tolerance loose enough to be safe against nothing would still have caught it.

`jupiter_lander.png` is a palette-indexed image whose indices are what the
stream carries, so the comparison never converts colour and never guesses which
palette the device is running. The palette attached to it is the device's
built-in one, which is what makes the file viewable and what a saved failure
frame is rendered in; nothing in the comparison reads it.

The same image is then started again with 96 bytes of 0x1A after its last chip
packet, and has to reach the same frame. A header that follows the chip packets
and lacks the CHIP signature marks the end of the file, as it did up to firmware
3.10 and does in VICE (#900). A file whose first packet is not a chip packet is
still refused.
"""

from __future__ import annotations

import argparse
import os
import sys
import time
from collections import Counter
from pathlib import Path

# The one stanza that puts the shared library on sys.path; see tests/lib/bootstrap.py.
sys.path.insert(0, str(next(p for p in Path(__file__).resolve().parents
                            if (p / "tests" / "lib").is_dir()) / "tests" / "lib"))
import bootstrap  # noqa: E402,F401

import cli                                                      # noqa: E402
import streams                                                  # noqa: E402
import targets                                                  # noqa: E402
from api import UltimateApi                                     # noqa: E402
from PIL import Image                                           # noqa: E402
from report import (Failure, check, detail, format_exception,   # noqa: E402
                    suite_fail, suite_ok, suite_skip, teardown_step)
from vic_video import VicStreamCapture                          # noqa: E402

SUITE = "ultimax_cartridge_test"

SCRIPT_DIR = Path(__file__).resolve().parent
CARTRIDGE = SCRIPT_DIR / "jupiter_lander.crt"
REFERENCE = SCRIPT_DIR / "jupiter_lander.png"
# The video core the reference was captured on. Another core draws the same
# game screen with different pixels: core 1.4F scores exactly 72.66% against
# it, the same on every run, which is a picture that cannot match rather than a
# cartridge that did not start.
REFERENCE_CORE = "1.50"

# F1 starts the game from the cartridge's title screen. The cartridge scans the
# keyboard itself, so a tap sent before it starts is read by nothing: measured,
# a tap 2.0s after the start request landed and the game screen followed 80ms
# later. A single injected keystroke is occasionally not delivered on this
# bench, so the tap repeats until the screen arrives; measured, a tap that
# lands while the game screen is already up leaves it unchanged, so the retries
# cost nothing when the first one worked.
START_KEY = "f1"
FIRST_TAP_SECONDS = 2.0
TAP_INTERVAL_SECONDS = 1.5
# Against the 2.1s to 2.9s the screen was measured at, this covers a machine
# that needs several taps to see one.
MATCH_TIMEOUT_SECONDS = 15.0

# The copy of an EasyFlash game attached to #900 is the released file followed
# by these bytes, which pad it to a multiple of 128 bytes. Firmware 3.11 to 3.15
# refused it with "Error detected in file format" before reading any chip data,
# so the same tail on this image reproduces the refusal.
TRAILING_PADDING = b"\x1a" * 96
CRT_HEADER_SIZE = 0x40


def golden_frame() -> tuple[bytes, tuple[int, int], list[int]]:
    """The reference frame's palette indices, its geometry and its palette."""
    with Image.open(REFERENCE) as image:
        if image.mode != "P":
            raise Failure(f"{REFERENCE.name} is a {image.mode} image; the "
                          f"comparison needs the palette indices the stream carries")
        return image.tobytes(), image.size, image.getpalette()


def matching_pixels(pixels: bytes, reference: bytes) -> int:
    return sum(1 for actual, wanted in zip(pixels, reference) if actual == wanted)


def save_frame(pixels: bytes, size: tuple[int, int], palette: list[int]) -> None:
    """Write a captured frame beside this suite run's other files.

    The stem is the run's JSONL path, the way the Telnet transcript's is, so a
    reader who has the report has the picture by changing the suffix. A run
    without `-o` has nowhere to put it and reports the numbers alone.
    """
    jsonl = os.environ.get("E2E_JSONL") or ""
    if not jsonl.endswith(".jsonl"):
        return
    path = jsonl[:-len(".jsonl")] + ".frame.png"
    image = Image.frombytes("P", size, pixels)
    image.putpalette(palette)
    image.save(path)
    detail(f"the closest frame is in {path}")


def start_cartridge(device: UltimateApi, image: bytes) -> None:
    """Start a cartridge image and require the device to have accepted it.

    POST rather than a path: the image travels with the request, so the check
    needs nothing on the device's storage.
    """
    code, _, body = device.runners.upload("run_crt", image)
    if code != 200:
        raise Failure(f"runners:run_crt returned HTTP {code}: {body[:160]!r}")


def frame_pixels(capture: VicStreamCapture) -> bytes | None:
    """The next frame's palette indices, or None when a gap cut the frame short.

    The socket drops datagrams once its buffer is full, which happens while the
    suite waits on a request and does not read. A capture whose packet budget
    spans that gap cannot assemble a frame. Measured on an Ultimate 64 with the
    host's 2 MB default buffer: of 120 captures taken as the first or second
    read after a 0.5s to 1.4s pause, 13 failed, each with about 6,250 packets
    missing inside its window, and the next capture succeeded every time. That
    says nothing about the picture, so the polling loops read on.
    """
    try:
        return capture.capture_image().tobytes()
    except Failure as exc:
        if "complete VIC frame" not in str(exc):
            raise
        return None


def wait_for_restart(capture: VicStreamCapture, reference: bytes) -> None:
    """Read frames until one is not the game screen.

    The previous start left the game screen on the stream, and the socket still
    holds frames of it from before the restart. A match read before the screen
    has changed could be one of those frames rather than the new start.
    """
    deadline = time.monotonic() + MATCH_TIMEOUT_SECONDS
    while time.monotonic() < deadline:
        pixels = frame_pixels(capture)
        if pixels is not None and pixels != reference:
            return
    raise Failure("the game screen of the previous start never went away")


def wait_for_game_screen(device: UltimateApi, capture: VicStreamCapture,
                         reference: bytes, size: tuple[int, int],
                         palette: list[int], cartridge: str) -> None:
    """Tap the start key until the stream carries the reference frame.

    Every frame is compared as it arrives rather than one being taken after a
    settling delay: the frame that matches is the verdict, and a delay long
    enough to be safe would be long enough for the game to have moved on.
    """
    started = time.monotonic()
    deadline = started + MATCH_TIMEOUT_SECONDS
    next_tap = started + FIRST_TAP_SECONDS
    taps = 0
    best = -1
    closest = b""
    while time.monotonic() < deadline:
        if time.monotonic() >= next_tap:
            device.machine.press(START_KEY)
            taps += 1
            next_tap = time.monotonic() + TAP_INTERVAL_SECONDS
        pixels = frame_pixels(capture)
        if pixels is None:
            continue
        if pixels == reference:
            detail(f"the game screen arrived {time.monotonic() - started:.1f}s "
                   f"after the cartridge started, on tap {taps}")
            return
        matched = matching_pixels(pixels, reference)
        if matched > best:
            best, closest = matched, pixels
    histogram = " ".join(f"{index}:{count}"
                         for index, count in sorted(Counter(closest).items()))
    detail(f"{taps} {START_KEY.upper()} taps over {MATCH_TIMEOUT_SECONDS:.0f}s; "
           f"the closest frame matched {best}/{len(reference)} pixels "
           f"({best / len(reference):.2%}), colours {histogram}")
    if capture.foreign_packets:
        # Another machine is streaming to the same group. None of its packets
        # reached the comparison, but a bench that has not separated the two
        # with the U64_VIDEO_* variables is worth saying out loud.
        detail(f"{capture.foreign_packets} packets on the group came from "
               f"another address and were ignored")
    save_frame(closest, size, palette)
    raise Failure(f"the {cartridge} did not draw {REFERENCE.name}")


def run(args) -> str:
    """Run the check. Returns the reason the suite skipped, or an empty string."""
    reference, size, palette = golden_frame()
    device = UltimateApi(args.host, args.password or None, args.timeout)
    # The picture comes from the machine with the VIC, which for a cartridge
    # target is the computer, so that is the core the reference has to match.
    video = targets.resolve(args.host).video_host
    info = UltimateApi(video, args.password or None, args.timeout).info()
    core = info.extra.get("core_version")
    if core != REFERENCE_CORE:
        return (f"{REFERENCE.name} was captured on video core {REFERENCE_CORE} and "
                f"{video} runs core {core}, which draws the game screen differently")
    capture = VicStreamCapture(args.host)
    started = False
    try:
        with streams.Arming(device, args.host) as arming:
            if not arming.start("video"):
                raise Failure("the video stream could not be started: "
                              + arming.failures.get("video", "no reason given"))
            # The reference is a PAL frame. An NTSC machine streams 240 lines,
            # which is a different picture rather than a wrong one, and there is
            # no NTSC reference to compare it against.
            streaming = capture.capture_image().size
            if streaming != size:
                return (f"the reference is a {size[0]}x{size[1]} PAL frame and "
                        f"this machine streams {streaming[0]}x{streaming[1]}")
            image = CARTRIDGE.read_bytes()
            with check(f"an Ultimax cartridge draws {REFERENCE.name}"):
                started = True
                start_cartridge(device, image)
                wait_for_game_screen(device, capture, reference, size, palette,
                                     "Ultimax cartridge")
            with check(f"the cartridge with trailing padding draws {REFERENCE.name}"):
                start_cartridge(device, image + TRAILING_PADDING)
                wait_for_restart(capture, reference)
                wait_for_game_screen(device, capture, reference, size, palette,
                                     "cartridge with trailing padding")
            with check("a file whose first packet is not a chip packet is refused"):
                code, _, body = device.runners.upload(
                    "run_crt", image[:CRT_HEADER_SIZE] + TRAILING_PADDING)
                if code != 415:
                    raise Failure(f"runners:run_crt returned HTTP {code}, "
                                  f"expected 415: {body[:160]!r}")
    finally:
        capture.close()
        if started:
            # A reboot restores the configured cartridge, which is what removes
            # the one this started; a reset alone boots straight back into it
            # and would hand the next suite a machine running a game.
            teardown_step("restore the configured cartridge", device.machine.reboot)
    return ""


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Check that an Ultimax cartridge reaches the VIC stream "
                    "pixel for pixel.")
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
