#!/usr/bin/env python3
# E2E: a USB mouse wheel arrives on control port 1 as Micromys wheel pulses.

"""Turn a USB mouse wheel and check `tools/c64/micromys-wheel.asm` accepts every pulse.

`micromys-wheel.asm` is the conformance probe for the Micromys wheel protocol:
for each pulse on joystick line 2 (up) or line 3 (down) of port 1 it measures
how long the line stays active, and prints one line such as

    PAL U CYC=49151 US=49887 ERR=-00113 OK

where OK means the pulse lasted 50,000 us within the 2,000 us the program
accepts. The suite runs that program unchanged, turns the wheel of the Pico
2 W fixture's USB mouse (`tests/lib/pico_hid.py`), and reads the printed lines
back from screen memory.

The suite sets Mouse Mode to "Mouse + Wheel" and Mouse Wheel Direction to
Normal, and puts the settings back when it ends.

Checks:

- At wheel sensitivity 1, each detent up prints one U line and each detent
  down one D line, all OK.

Bursts at higher sensitivities are not checked here: the program converts
each measurement by repeated subtraction and prints it, which can take longer
than the 50ms gap to the next pulse of a burst, so it can miss a pulse that
is in specification. The `wheel-micromys` scenario of `usb-mouse` counts
bursts with `tools/c64/mouse-listener.asm` instead.

Needs the Pico fixture on a USB port of the machine, so it is manual.
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
import cli  # noqa: E402
from api import UltimateApi  # noqa: E402
from assembler import assemble  # noqa: E402
from mouse import PicoMouse  # noqa: E402
from pico_hid import Pico, discover_pico  # noqa: E402
from report import Failure, check, detail, format_exception, suite_fail, suite_ok  # noqa: E402

SUITE = "micromys_wheel_test"
PROGRAM = Path(__file__).resolve().parents[4] / "tools" / "c64" / "micromys-wheel.asm"
CATEGORY = "U64 Specific Settings"
SETTINGS = {
    "Mouse Mode": "Mouse + Wheel",
    "Mouse Wheel Sensitivity": 1,
    "Mouse Wheel Direction": "Normal",
    "Joystick Swapper": "Normal",
}
HEADER = "MICROMYS WHEEL TEST"
START_TIMEOUT_SECONDS = 10.0
# Printing a line takes the program a few milliseconds after the pulse ends.
SETTLE_SECONDS = 0.5


def screen_lines(api: UltimateApi) -> list[str]:
    """The text screen as 25 lines, for the upper case character set the program uses."""
    screen = api.machine.readmem(0x0400, 1000)

    def character(code: int) -> str:
        code &= 0x7F
        if 1 <= code <= 26:
            return chr(code + 64)
        return chr(code) if 32 <= code <= 63 else " "

    return ["".join(character(c) for c in screen[row:row + 40]).rstrip() for row in range(0, 1000, 40)]


def result_lines(api: UltimateApi) -> list[str]:
    return [line for line in screen_lines(api) if line.startswith(("PAL ", "NTSC "))]


def start_program(api: UltimateApi) -> None:
    status, _, body = api.runners.upload("run_prg", assemble(PROGRAM))
    if status != 200:
        raise Failure(f"runners:run_prg returned HTTP {status}: {body[:160]!r}")
    deadline = time.monotonic() + START_TIMEOUT_SECONDS
    while HEADER not in screen_lines(api):
        if time.monotonic() > deadline:
            raise Failure(f"{PROGRAM.name} did not start")
        time.sleep(0.2)


def turn(api: UltimateApi, mouse: PicoMouse, vertical: int, pulses: int) -> list[str]:
    """Turn the wheel, then return the lines printed for it."""
    before = len(result_lines(api))
    mouse.wheel(vertical=vertical, pulses_per_detent=pulses)
    time.sleep(SETTLE_SECONDS)
    lines = result_lines(api)[before:]
    for line in lines:
        detail(line)
    return lines


def require_lines(lines: list[str], direction: str, count: int) -> None:
    directions = [line.split()[1] for line in lines]
    if directions != [direction] * count:
        raise Failure(f"expected {count} {direction} lines, got {directions}")
    failed = [line for line in lines if not line.endswith(" OK")]
    if failed:
        raise Failure(f"pulses outside the Micromys specification: {failed}")


def run(api: UltimateApi, mouse: PicoMouse) -> None:
    start_program(api)

    with check("at wheel sensitivity 1 each detent prints one line, all OK"):
        require_lines(turn(api, mouse, 3, 1), "U", 3)
        require_lines(turn(api, mouse, -2, 1), "D", 2)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    cli.add_device_arguments(parser, password=None)
    parser.add_argument("--pico-host", help="fixture IP address; required on networks that do not "
                        "forward broadcast between the wired test host and the Wi-Fi client")
    args = parser.parse_args()
    api = UltimateApi(args.host, args.password, args.timeout)
    pico = Pico(args.pico_host or discover_pico())
    with check("the Pico fixture offers a USB mouse"):
        pico.require_mouse()
    saved = {item: api.configs.item(CATEGORY, item).get("current") for item in SETTINGS}
    detail("saved settings: " + ", ".join(f"{item}={value!r}" for item, value in saved.items()))
    try:
        for item, value in SETTINGS.items():
            api.configs.set(CATEGORY, item, value)
        run(api, PicoMouse(pico))
    finally:
        for step in (lambda: [api.configs.set(CATEGORY, item, value)
                              for item, value in saved.items() if value is not None],
                     lambda: api.machine.reset(force=True)):
            try:
                step()
            except Exception as exc:
                detail("cleanup failure: " + format_exception(exc))
        pico.close()
    suite_ok(SUITE)
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Failure as exc:
        suite_fail(SUITE, format_exception(exc))
        raise SystemExit(1)
