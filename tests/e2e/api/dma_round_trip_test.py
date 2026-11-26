#!/usr/bin/env python3
# E2E: a REST memory write reads back exactly, in every state the machine can be in.

"""writemem followed by readmem returns the bytes written, whatever the machine is doing.

The machine is taken through running at the READY prompt, paused through REST,
and the menu open under each Interface Type the machine offers, one on a
cartridge. Whether the open menu stops the machine is measured: Overlay leaves
it running when an HDMI display is connected. In each state the
suite writes and reads back fixed windows that straddle the boundaries where
the frozen transfer is split into regions ($0400, $0800, $1000, $8000, $D000
and the colour RAM), then a seeded run of random windows across the RAM the
idle prompt leaves alone. The areas touched are saved before each state and
written back after it, so the C64 is unchanged at the end.

While the menu holds the machine stopped, machine:pause and machine:resume must
leave it stopped: the menu has its own screen, charset and cartridge in place
until it closes, and every frozen transfer assumes the 6510 is not running.

Colour RAM is four bits wide and compared on the low nibble. The ROM and I/O
windows are not written: readmem shows ROM and I/O where $01 maps them, so a
write there does not read back by design. Screen and colour RAM are written
at random only in rows 10 to 19: the KERNAL rewrites the cursor's cell and its
colour while the cursor blinks on row 6.

A frozen transfer that sends a write and its read back to the same wrong place
reads back correctly, so with the menu holding the machine the boundary
windows are also written, and read once the menu has closed.
"""

from __future__ import annotations

import argparse
import random
import sys
from pathlib import Path

# The one stanza that puts the shared library on sys.path; see tests/lib/bootstrap.py.
sys.path.insert(0, str(next(p for p in Path(__file__).resolve().parents
                            if (p / "tests" / "lib").is_dir()) / "tests" / "lib"))
import bootstrap  # noqa: E402,F401

sys.path.insert(0, bootstrap.directory("e2e", "lib"))
import cli  # noqa: E402
import machine as machine_lib  # noqa: E402
from api import UltimateApi, identify_machine  # noqa: E402
from ui_backend import MODE_FREEZE, MODE_OVERLAY, make_backend  # noqa: E402
from report import (Failure, check, detail,  # noqa: E402
                    format_exception, suite_fail, suite_ok, teardown_step)

SUITE = "dma_round_trip_test"
UI_STORE, UI_ITEM = "User Interface Settings", "Interface Type"

# (start, end) windows across each split point of the frozen transfer.
BOUNDARIES = ((0x03F0, 0x0420), (0x07E0, 0x0820), (0x0FF0, 0x1010),
              (0x7FF0, 0x8010), (0xCFE0, 0xD000), (0xD800, 0xD828), (0xDBD8, 0xDC00))
# Where random windows go: screen and colour rows 10-19, free BASIC RAM and $C000.
RANDOM_AREAS = ((0x0590, 0x0720), (0x0810, 0xA000), (0xC000, 0xD000), (0xD990, 0xDB20))
RANDOM_WINDOWS = 100
MAX_WINDOW = 600
COLOR_RAM = (0xD800, 0xDC00)
SCREEN = (0x0400, 0x0800)


def masked(start: int, data: bytes) -> bytes:
    """Colour RAM keeps four bits, so only those are compared."""
    return bytes(b & 0x0F if COLOR_RAM[0] <= start + i < COLOR_RAM[1] else b
                 for i, b in enumerate(data))


def allowed(start: int, end: int, screen: bool) -> bool:
    return screen or end <= SCREEN[0] or start >= SCREEN[1]


def windows(rng: random.Random, screen: bool) -> list[tuple[int, int]]:
    chosen = [w for w in BOUNDARIES if allowed(*w, screen)]
    while len(chosen) < len(BOUNDARIES) + RANDOM_WINDOWS:
        lo, hi = rng.choice(RANDOM_AREAS)
        start = rng.randrange(lo, hi)
        window = (start, min(hi, start + rng.randint(1, MAX_WINDOW)))
        if allowed(*window, screen):
            chosen.append(window)
    return chosen


def touched() -> list[tuple[int, int]]:
    """Every window and area, merged, so it can be saved once and put back once."""
    merged: list[list[int]] = []
    for lo, hi in sorted(BOUNDARIES + RANDOM_AREAS):
        if merged and lo <= merged[-1][1]:
            merged[-1][1] = max(merged[-1][1], hi)
        else:
            merged.append([lo, hi])
    return [(lo, hi) for lo, hi in merged]


def round_trips(api: UltimateApi, rng: random.Random, screen: bool) -> list[str]:
    """Every window that did not read back as written, described."""
    saved = [(lo, api.machine.readmem(lo, hi - lo)) for lo, hi in touched()]
    lost = []
    try:
        for start, end in windows(rng, screen):
            length = end - start
            data = rng.randbytes(length)
            api.machine.writemem(start, data)
            got = api.machine.readmem(start, length)
            if masked(start, got) != masked(start, data):
                wrong = [start + i for i, (g, d) in enumerate(zip(masked(start, got), masked(start, data)))
                         if g != d]
                lost.append(f"${start:04X}-${end - 1:04X}: {len(wrong)} of {length} wrong, "
                            f"first ${wrong[0]:04X}")
    finally:
        for lo, original in saved:
            api.machine.writemem(lo, original)
    return lost


def reach_the_c64(api: UltimateApi, rng: random.Random, screen: bool) -> list[str]:
    """Boundary windows written while the menu holds the machine that the C64 does not hold once it closes."""
    chosen = [w for w in BOUNDARIES if allowed(*w, screen)]
    saved = [(lo, api.machine.readmem(lo, hi - lo)) for lo, hi in chosen]
    written = [(lo, hi, rng.randbytes(hi - lo)) for lo, hi in chosen]
    lost = []
    try:
        for lo, _, data in written:
            api.machine.writemem(lo, data)
        api.machine.close_menu_from_anywhere()
        for lo, hi, data in written:
            got = api.machine.readmem(lo, hi - lo)
            if masked(lo, got) != masked(lo, data):
                lost.append(f"${lo:04X}-${hi - 1:04X}")
    finally:
        for lo, original in saved:
            api.machine.writemem(lo, original)
    return lost


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    cli.add_device_arguments(parser, timeout=10.0)
    parser.add_argument("--seed", type=int, default=None,
                        help="seed for the random windows (default: a new one, printed)")
    args = parser.parse_args()

    seed = args.seed if args.seed is not None else random.randrange(1 << 32)
    api = UltimateApi(args.host, args.password or None, args.timeout)
    device = identify_machine(args.host, args.password or None, args.timeout)
    # A machine without the setting, a cartridge, has one menu and it freezes.
    modes = ((MODE_FREEZE, MODE_OVERLAY) if UI_ITEM in api.configs.category(UI_STORE)
             else (MODE_FREEZE,))
    failed = []

    def round_trip_check(label: str, screen: bool = True) -> None:
        try:
            with check(f"every window reads back as written: {label}"):
                if not screen:
                    detail("screen windows left out: this firmware serves the menu's screen while frozen")
                lost = round_trips(api, random.Random(f"{seed}-{label}"), screen)
                if lost:
                    raise Failure(f"{len(lost)} windows lost bytes\n" + "\n".join(lost[:10]))
        except Failure:
            failed.append(label)

    try:
        detail(f"seed {seed}; rerun with --seed {seed}")
        with check("reset to the READY prompt"):
            api.machine.close_menu_from_anywhere()
            if not api.machine.reset(force=True):
                raise Failure("the machine did not reach READY after a reset")

        round_trip_check("running")
        api.machine.pause()
        try:
            round_trip_check("paused through REST")
        finally:
            api.machine.resume()

        for mode in modes:
            # The backend switches Interface Type with the menu closed and puts it back.
            backend = make_backend(mode, args.host, args.password or None, args.timeout)
            try:
                backend.ensure_ready()
                frozen = not api.machine.cpu_runs()
                label = f"menu open ({mode}, machine {'stopped' if frozen else 'running'})"
                # Without the fix, REST reaches the menu's screen while frozen; see machine.py.
                frozen_screen = not frozen or not device.lacks_fix(machine_lib.FROZEN_SCREEN_DMA)
                round_trip_check(label, screen=frozen_screen)
                if not frozen:
                    detail(f"{mode}: the menu leaves the machine running, so pause and resume "
                           "are not checked against it")
                elif not device.skip_without_fix(machine_lib.FROZEN_PAUSE_RESUME,
                                                 f"pause and resume leave the frozen machine stopped ({mode})"):
                    try:
                        with check(f"pause and resume leave the frozen machine stopped ({mode})"):
                            api.machine.pause()
                            api.machine.resume()
                            if api.machine.cpu_runs():
                                raise Failure("the 6510 runs under the open menu after machine:resume")
                    except Failure:
                        failed.append(f"pause and resume while frozen ({mode})")
                if frozen:
                    try:
                        with check(f"windows written with the menu open are in the C64 once it closes ({mode})"):
                            lost = reach_the_c64(api, random.Random(f"{seed}-{mode}-close"),
                                                 frozen_screen)
                            if lost:
                                raise Failure(f"{len(lost)} windows differ: {', '.join(lost)}")
                    except Failure:
                        failed.append(f"writes reach the C64 ({mode})")
            finally:
                teardown_step(f"close the {mode} session", backend.close)

        if failed:
            raise Failure(f"{len(failed)} checks failed: {', '.join(failed)}")
        suite_ok(SUITE)
        return 0
    except Exception as exc:  # noqa: BLE001
        suite_fail(SUITE, format_exception(exc))
        return 1
    finally:
        teardown_step("close the menu", api.machine.close_menu_from_anywhere)


if __name__ == "__main__":
    raise SystemExit(main())
