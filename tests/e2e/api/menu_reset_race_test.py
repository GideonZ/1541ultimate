#!/usr/bin/env python3
# E2E: machine:reset while the menu is opening or open must not crash the device.

"""REST machine:reset racing the menu that a menu_button press is opening.

The reset releases the menu's user interface from the HTTP task. If that
deletes the menu's windows while the menu task is still drawing them, the
firmware takes an exception and the device stops answering altogether. A reset
while a blocking dialog (the help screen) is open must reset cleanly too. The
race needs a fast host link (wired, about 1 ms round trip); a WiFi link rarely
hits it.

Each round presses the menu button, waits a delay that sweeps from "before the
menu has drawn" to "long after", sends machine:reset without waiting for READY,
and then requires the device to answer REST again and, after a short settle, any
menu left open to be closable. A reset the firmware refuses because the menu
task did not let go in time (HTTP 500) is allowed and counted, never a crash.
The machine is checked at READY at the end and after each dialog reset. A background reader keeps REST readmem traffic
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
# Resets the firmware may refuse (HTTP 500) in the race rounds before the check fails.
MAX_REFUSED = 3
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


def reset_under_dialog(api: UltimateApi) -> None:
    """machine:reset while the help screen is open must reset cleanly.

    The help screen is a read-only viewer, a blocking loop in the menu task, not
    in the menu's own poll. The reset's release request closes it, so the menu
    task can tear the menu down. Only read-only viewers are closed this way:
    question and input dialogs (popup, choice, string box, forms) are not,
    because ending them would hand their callers a result they read as an answer,
    and a REST reset gets HTTP 500 while one is open. This row guards the viewer
    unwind path; it is not a RED reproducer, because the unfixed firmware also
    resets here. F3 is the only key pressed to open the screen; no menu row is
    navigated or activated.
    """
    api.machine.menu_button()
    time.sleep(1.5)
    api.machine.press("f3")
    time.sleep(1.0)
    if not any(HELP_MARKER in row for row in api.machine.menu_rows()):
        raise Failure("the help screen did not open")
    api.machine.reset(force=True, wait=False)
    if wait_answers(api, ANSWER_TIMEOUT_SECONDS) is None:
        raise Failure("the device stopped answering after machine:reset with the help screen open")
    time.sleep(SETTLE_SECONDS)
    if api.machine.menu_open():
        api.machine.close_menu_from_anywhere()
        raise Failure("the menu was still open after machine:reset with the help screen open")
    if not api.machine.wait_until_ready():
        raise Failure("the machine did not reach READY after machine:reset with the help screen open")


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
            refused = 0
            for n in range(args.rounds):
                delay = DELAYS[n % len(DELAYS)]
                presser.machine.menu_button()
                time.sleep(delay)
                try:
                    api.machine.reset(force=True, wait=False)
                except Failure:
                    refused += 1
                back = wait_answers(api, ANSWER_TIMEOUT_SECONDS)
                if back is None:
                    raise Failure(f"round {n + 1} (delay {delay:.2f}s): the device stopped "
                                  f"answering for {ANSWER_TIMEOUT_SECONDS:.0f}s after machine:reset")
                time.sleep(SETTLE_SECONDS)
                if api.machine.menu_open():
                    api.machine.close_menu_from_anywhere()
                if n % 10 == 9:
                    detail(f"{n + 1} rounds done, last answer back after {back:.2f}s, "
                           f"{refused} resets refused so far")
        stop.set()
        thread.join(timeout=5)
        with check(f"machine:reset with a blocking dialog open, {POPUP_ROUNDS} times: it resets, the menu closes"):
            for _ in range(POPUP_ROUNDS):
                reset_under_dialog(api)
        with check("few resets were refused in the race rounds"):
            if refused > MAX_REFUSED:
                raise Failure(f"{refused} of {args.rounds} resets were refused, more than {MAX_REFUSED}: "
                              "the menu task is not letting go in time")
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
