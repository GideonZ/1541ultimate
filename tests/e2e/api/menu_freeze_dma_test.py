#!/usr/bin/env python3
# E2E: REST readmem/writemem stay correct while the menu opens and closes under them.

"""REST memory access while the menu is freezing and unfreezing the C64.

A host reads, or writes and reads back, 128 bytes at $C000 as fast as the
device answers while `machine:menu_button` is pressed every 2.5 seconds. Opening
the menu sets the bus to Ultimax at the first step of the freeze, well before
the freeze is recorded as done. A REST copy that lands in between used to read
all $FF and drop its writes, with HTTP 200 (GideonZ/1541ultimate#1106), for
seconds at a time under steady traffic.

Every read must return the pattern, every write must be in RAM 0.3 seconds
later, and nothing may fail. The menu is closed at the end, without a reset
while it is open, and the machine must be at the READY prompt.
"""

from __future__ import annotations

import argparse
import sys
import threading
import time
from pathlib import Path

# The one stanza that puts the shared library on sys.path; see tests/lib/bootstrap.py.
sys.path.insert(0, str(next(p for p in Path(__file__).resolve().parents
                            if (p / "tests" / "lib").is_dir()) / "tests" / "lib"))
import bootstrap  # noqa: E402,F401
import cli  # noqa: E402
from api import UltimateApi  # noqa: E402
from report import Failure, check, detail, format_exception, suite_fail, suite_ok  # noqa: E402

SUITE = "menu_freeze_dma_test"
ADDRESS = 0xC000
LENGTH = 128
PATTERN = bytes(range(LENGTH))
ALL_FF = bytes([0xFF]) * LENGTH
# Presses per phase. Even, so a phase leaves the menu where it found it.
PRESSES = 8
PRESS_INTERVAL_SECONDS = 2.5
READBACK_SETTLE_SECONDS = 0.3
MENU_CLOSE_SETTLE_SECONDS = 1.5


def read_phase(api: UltimateApi, presser: UltimateApi) -> tuple[int, list[str]]:
    """Read back to back through PRESSES menu presses. Returns (operations, faults)."""
    return flood(api, presser, write=False)


def write_phase(api: UltimateApi, presser: UltimateApi) -> tuple[int, list[str]]:
    """Write and read back, then recheck, through PRESSES menu presses."""
    return flood(api, presser, write=True)


def flood(api: UltimateApi, presser: UltimateApi, write: bool) -> tuple[int, list[str]]:
    stop = threading.Event()
    press_errors: list[str] = []

    def press() -> None:
        for _ in range(PRESSES):
            if stop.is_set():
                return
            try:
                presser.machine.menu_button()
            except Exception as exc:  # noqa: BLE001 - reported by the main thread
                press_errors.append(format_exception(exc))
            stop.wait(PRESS_INTERVAL_SECONDS)

    thread = threading.Thread(target=press, daemon=True)
    started = time.monotonic()
    thread.start()
    operations = 0
    faults: list[str] = []
    counter = 0
    try:
        while thread.is_alive():
            at = time.monotonic() - started
            operations += 1
            try:
                if not write:
                    data = api.machine.readmem(ADDRESS, LENGTH)
                    if data != PATTERN:
                        faults.append(f"{at:6.2f}s read {kind(data)}")
                    continue
                counter = (counter + 1) % 250
                payload = bytes((counter + i) & 0xFF for i in range(LENGTH))
                api.machine.writemem(ADDRESS, payload)
                data = api.machine.readmem(ADDRESS, LENGTH)
                if data != payload:
                    faults.append(f"{at:6.2f}s read back {kind(data)} after a write")
                time.sleep(READBACK_SETTLE_SECONDS)
                if api.machine.readmem(ADDRESS, LENGTH) != payload:
                    faults.append(f"{at:6.2f}s write lost")
                    api.machine.writemem(ADDRESS, PATTERN)
            except Failure as exc:
                faults.append(f"{at:6.2f}s {format_exception(exc)[:120]}")
    finally:
        stop.set()
        thread.join(timeout=10)
    faults.extend(f"menu_button: {e[:120]}" for e in press_errors)
    return operations, faults


def kind(data: bytes) -> str:
    return "all $FF" if data == ALL_FF else f"{data[:8].hex()}..."


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    cli.add_device_arguments(parser, timeout=10.0)
    args = parser.parse_args()
    api = UltimateApi(args.host, args.password or None, args.timeout)
    presser = UltimateApi(args.host, args.password or None, args.timeout)

    try:
        with check("reset to the READY prompt with the menu closed"):
            api.machine.close_menu_from_anywhere()
            if not api.machine.reset(force=True):
                raise Failure("the machine did not reach READY after a reset")
            api.machine.writemem(ADDRESS, PATTERN)
            if api.machine.readmem(ADDRESS, LENGTH) != PATTERN:
                raise Failure("the pattern at $C000 did not read back with the menu closed")

        for label, phase in (("reads back to back", read_phase),
                             ("writes with a read back and a recheck", write_phase)):
            with check(f"{label}, {PRESSES} menu presses: every answer correct"):
                operations, faults = phase(api, presser)
                detail(f"{operations} operations, {len(faults)} faults")
                if faults:
                    raise Failure(f"{len(faults)} of {operations} operations went wrong, "
                                  f"first: {'; '.join(faults[:5])}")
            # Settle, then put the menu back before the next phase, never by reset.
            time.sleep(MENU_CLOSE_SETTLE_SECONDS)
            api.machine.close_menu_from_anywhere()
            api.machine.writemem(ADDRESS, PATTERN)

        with check("the menu is closed and the machine is at READY"):
            if api.machine.menu_open():
                raise Failure("the menu is still open")
            if not api.machine.wait_until_ready():
                raise Failure("the READY prompt is not on the screen")
    except Failure as exc:
        try:
            api.machine.close_menu_from_anywhere()
        except Exception:  # noqa: BLE001 - the original failure is the one to report
            pass
        suite_fail(SUITE, format_exception(exc))
        return 1
    suite_ok(SUITE)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
