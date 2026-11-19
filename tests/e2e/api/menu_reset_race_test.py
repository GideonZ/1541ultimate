#!/usr/bin/env python3
# E2E: machine:reset while the menu is opening or open must not crash the device.

"""REST machine:reset racing the menu that a menu_button press is opening.

The reset releases the menu's user interface from the HTTP task. If that
deletes the menu's windows while the menu task is still drawing them, the
firmware takes an exception and the device stops answering altogether. The
race needs a fast host link (wired, about 1 ms round trip); a WiFi link rarely
hits it.

Each round presses the menu button, waits a delay that sweeps from "before the
menu has drawn" to "long after", sends machine:reset without waiting for READY,
and then requires the device to answer REST again, the menu to be closed and
the machine to reach READY. A background reader keeps REST readmem traffic
going so the HTTP task is busy, as in the heavy load that first showed this.
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
from report import (Failure, check, detail, format_exception,  # noqa: E402
                    suite_fail, suite_ok, warn)

SUITE = "menu_reset_race_test"
ROUNDS = 60
# Delay between the button press and the reset, cycled. The menu opens within
# roughly 0.1 to 0.5 s; the sweep covers before, during and after.
DELAYS = [0.0, 0.01, 0.03, 0.06, 0.1, 0.15, 0.2, 0.3, 0.45, 0.7]
# The device must answer again within this after a reset.
ANSWER_TIMEOUT_SECONDS = 20.0
SETTLE_SECONDS = 0.6
POPUP_ROUNDS = 3
HELP_MARKER = "Selection context menu"


def wait_answers(api: UltimateApi, timeout: float) -> float | None:
    """Seconds until a readmem is answered, or None if it never is."""
    start = time.monotonic()
    while time.monotonic() - start < timeout:
        try:
            api.machine.readmem(0x0400, 1)
            return time.monotonic() - start
        except Exception:  # noqa: BLE001 - polling a device that may be down
            time.sleep(0.2)
    return None


def popup_then_reset(api: UltimateApi) -> None:
    """Open the help screen, a nested blocking loop of the menu task, then reset over REST.

    The help screen runs in the same kind of loop as popup() and string_box():
    the menu task is inside it, not in the menu's own poll. The reset has to
    make that loop return so the menu task can tear the menu down. F3 is the
    only key pressed; no menu row is navigated or activated.
    """
    api.machine.menu_button()
    time.sleep(1.5)
    api.machine.press("f3")
    time.sleep(1.0)
    if not any(HELP_MARKER in row for row in api.machine.menu_rows()):
        raise Failure("the help screen did not open")
    api.machine.reset(force=True, wait=False)
    back = wait_answers(api, ANSWER_TIMEOUT_SECONDS)
    if back is None:
        raise Failure("the device stopped answering after machine:reset with the help screen open")
    time.sleep(SETTLE_SECONDS)
    if api.machine.menu_open():
        api.machine.close_menu_from_anywhere()
        raise Failure("the menu was still open after machine:reset with the help screen open")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    cli.add_device_arguments(parser, timeout=5.0)
    parser.add_argument("--rounds", type=int, default=ROUNDS)
    args = parser.parse_args()
    api = UltimateApi(args.host, args.password or None, args.timeout)
    presser = UltimateApi(args.host, args.password or None, args.timeout)
    reader = UltimateApi(args.host, args.password or None, args.timeout)

    stop = threading.Event()

    def read_loop() -> None:
        while not stop.is_set():
            try:
                reader.machine.readmem(0xC000, 64)
            except Exception:  # noqa: BLE001 - a miss is not a verdict
                stop.wait(0.2)
            stop.wait(0.005)

    thread = threading.Thread(target=read_loop, daemon=True)
    try:
        with check("reset to the READY prompt with the menu closed"):
            api.machine.close_menu_from_anywhere()
            if not api.machine.reset(force=True):
                raise Failure("the machine did not reach READY after a reset")
        thread.start()
        with check(f"{args.rounds} rounds of menu_button, delay, machine:reset: the device keeps answering"):
            for n in range(args.rounds):
                delay = DELAYS[n % len(DELAYS)]
                presser.machine.menu_button()
                time.sleep(delay)
                api.machine.reset(force=True, wait=False)
                back = wait_answers(api, ANSWER_TIMEOUT_SECONDS)
                if back is None:
                    raise Failure(f"round {n + 1} (delay {delay:.2f}s): the device stopped "
                                  f"answering for {ANSWER_TIMEOUT_SECONDS:.0f}s after machine:reset")
                time.sleep(SETTLE_SECONDS)
                if api.machine.menu_open():
                    api.machine.close_menu_from_anywhere()
                if n % 10 == 9:
                    detail(f"{n + 1} rounds done, last answer back after {back:.2f}s")
        stop.set()
        thread.join(timeout=5)
        with check(f"machine:reset with a blocking menu screen open, {POPUP_ROUNDS} times: it resets and the device answers"):
            for _ in range(POPUP_ROUNDS):
                popup_then_reset(api)
        with check("the machine reaches READY with the menu closed"):
            api.machine.close_menu_from_anywhere()
            if not api.machine.reset(force=True):
                raise Failure("the machine did not reach READY after the final reset")
    except Failure as exc:
        stop.set()
        try:
            api.machine.close_menu_from_anywhere()
        except Exception as inner:  # noqa: BLE001 - the original failure is the one to report
            warn(f"recovery failed: {format_exception(inner)}")
        suite_fail(SUITE, format_exception(exc))
        return 1
    suite_ok(SUITE)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
