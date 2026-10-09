#!/usr/bin/env python3
# E2E: a REST memory write reads back exactly, in every state the machine can be in.

"""writemem followed by readmem returns the bytes written, whatever the machine is doing.

The machine is taken through four states: running at the READY prompt, paused
through REST, and frozen by the menu under each Interface Type. In each one the
suite writes and reads back fixed windows that straddle the boundaries where
the frozen transfer is split into regions ($0400, $0800, $1000, $8000, $D000
and the colour RAM), then a seeded run of random windows across the RAM the
idle prompt leaves alone. The areas touched are saved before each state and
written back after it, so the C64 is unchanged at the end.

While the menu has the machine frozen, machine:pause and machine:resume must
leave it stopped: the menu has its own screen, charset and cartridge in place
until it closes, and every frozen transfer assumes the 6510 is not running.

Colour RAM is four bits wide and compared on the low nibble. The ROM and I/O
windows are not written: readmem shows ROM and I/O where $01 maps them, so a
write there does not read back by design. Screen and colour RAM are written
at random only in rows 10 to 20: the KERNAL rewrites the cursor's cell and its
colour while the cursor blinks on row 6.
"""

from __future__ import annotations

import argparse
import random
import sys
import time
from pathlib import Path

# The one stanza that puts the shared library on sys.path; see tests/lib/bootstrap.py.
sys.path.insert(0, str(next(p for p in Path(__file__).resolve().parents
                            if (p / "tests" / "lib").is_dir()) / "tests" / "lib"))
import bootstrap  # noqa: E402,F401

sys.path.insert(0, bootstrap.directory("e2e", "lib"))
import cli  # noqa: E402
import machine as machine_lib  # noqa: E402
import pacing  # noqa: E402
from api import UltimateApi, identify_machine  # noqa: E402
from rest_backend import OVERLAY_MODE  # noqa: E402
from report import (Failure, check, detail,  # noqa: E402
                    format_exception, suite_fail, suite_ok, teardown_step)

SUITE = "dma_round_trip_test"
UI_STORE, UI_ITEM = "User Interface Settings", "Interface Type"

# (start, end) windows across each split point of the frozen transfer.
BOUNDARIES = ((0x03F0, 0x0420), (0x07E0, 0x0820), (0x0FF0, 0x1010),
              (0x7FF0, 0x8010), (0xCFE0, 0xD000), (0xD800, 0xD828), (0xDBD8, 0xDC00))
# Where random windows go: screen and colour rows 10-20, free BASIC RAM and $C000.
RANDOM_AREAS = ((0x0590, 0x0720), (0x0810, 0xA000), (0xC000, 0xD000), (0xD990, 0xDB20))
RANDOM_WINDOWS = 100
MAX_WINDOW = 600
COLOR_RAM = (0xD800, 0xDC00)
SCREEN = (0x0400, 0x0800)
JIFFY_CLOCK = 0x00A0
JIFFY_WATCH_SECONDS = 0.2


def masked(start: int, data: bytes) -> bytes:
    """Colour RAM keeps four bits, so only those are compared."""
    return bytes(b & 0x0F if COLOR_RAM[0] <= start + i < COLOR_RAM[1] else b
                 for i, b in enumerate(data))


def windows(rng: random.Random, screen: bool) -> list[tuple[int, int]]:
    def allowed(start: int, end: int) -> bool:
        return screen or end <= SCREEN[0] or start >= SCREEN[1]
    chosen = [w for w in BOUNDARIES if allowed(*w)]
    while len(chosen) < len(BOUNDARIES) + RANDOM_WINDOWS:
        lo, hi = rng.choice(RANDOM_AREAS)
        start = rng.randrange(lo, hi)
        window = (start, min(hi, start + rng.randint(1, MAX_WINDOW)))
        if allowed(*window):
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


def jiffy_advances(api: UltimateApi) -> bool:
    """Whether the KERNAL's jiffy clock at $A0-$A2 moves, which it does 60 times a second."""
    before = api.machine.readmem(JIFFY_CLOCK, 3)
    time.sleep(JIFFY_WATCH_SECONDS)
    return api.machine.readmem(JIFFY_CLOCK, 3) != before


def interface_type(api: UltimateApi) -> str | None:
    """The Interface Type setting, or None on a machine that has none."""
    try:
        return api.configs.current(UI_STORE, UI_ITEM)
    except Failure:
        return None


def wait_for_menu(api: UltimateApi, want_open: bool) -> None:
    deadline = time.monotonic() + pacing.MENU_TOGGLE_SETTLE_SECONDS * 10
    while api.machine.menu_open() != want_open:
        if time.monotonic() > deadline:
            raise Failure(f"the menu did not {'open' if want_open else 'close'}")
        time.sleep(0.05)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    cli.add_device_arguments(parser, timeout=10.0)
    parser.add_argument("--seed", type=int, default=None,
                        help="seed for the random windows (default: a new one, printed)")
    args = parser.parse_args()

    seed = args.seed if args.seed is not None else random.randrange(1 << 32)
    api = UltimateApi(args.host, args.password or None, args.timeout)
    device = identify_machine(args.host, args.password or None, args.timeout)
    interface = interface_type(api)
    # Without the fix, REST reaches the menu's screen while frozen; see machine.py.
    frozen_screen = device.has_fix(machine_lib.FROZEN_SCREEN_DMA)
    failed = []

    def state(label: str, enter, leave, screen: bool = True) -> None:
        try:
            with check(f"every window reads back as written: {label}"):
                rng = random.Random(f"{seed}-{label}")
                enter()
                try:
                    lost = round_trips(api, rng, screen)
                finally:
                    leave()
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

        state("running", lambda: None, lambda: None)
        state("paused through REST", api.machine.pause, api.machine.resume)
        # A machine without the setting, a cartridge, always freezes for its menu.
        offered = api.configs.item(UI_STORE, UI_ITEM).get("values", []) if interface else ["Freeze"]
        for kind in [k for k in ("Freeze", OVERLAY_MODE) if k in offered]:
            def enter(kind=kind) -> None:
                if interface:
                    api.configs.set(UI_STORE, UI_ITEM, kind)
                api.machine.menu_button()
                wait_for_menu(api, True)

            def leave() -> None:
                api.machine.close_menu_from_anywhere()
            state(f"frozen by the menu ({kind})", enter, leave, screen=frozen_screen)
            try:
                with check(f"pause and resume leave the frozen machine stopped ({kind})"):
                    enter()
                    try:
                        api.machine.pause()
                        api.machine.resume()
                        if jiffy_advances(api):
                            raise Failure("the 6510 runs under the open menu after machine:resume")
                    finally:
                        leave()
            except Failure:
                failed.append(f"pause and resume while frozen ({kind})")

        if failed:
            raise Failure(f"{len(failed)} checks failed: {', '.join(failed)}")
        suite_ok(SUITE)
        return 0
    except Exception as exc:  # noqa: BLE001
        suite_fail(SUITE, format_exception(exc))
        return 1
    finally:
        teardown_step("close the menu", api.machine.close_menu_from_anywhere)
        if interface:
            teardown_step(f"put Interface Type back to {interface}",
                          lambda: api.configs.set(UI_STORE, UI_ITEM, interface))


if __name__ == "__main__":
    raise SystemExit(main())
