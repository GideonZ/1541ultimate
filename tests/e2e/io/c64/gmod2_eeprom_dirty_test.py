#!/usr/bin/env python3
"""E2E: reading the GMod2 EEPROM's changed flag does not throw the change away.

A GMod2 game saves into the cartridge's serial EEPROM. The hardware sets a
flag when the C64 writes to it, and *Save Cartridge* copies the EEPROM out of
the hardware only when that flag is set. Reading the flag used to clear it in
hardware, so anything that looked at it first took the change with it: System
Info (F4) reads it to print the EEPROM's state, and a save taken afterwards
wrote the EEPROM as it was when the cartridge was loaded, silently losing the
game's save.

The flag is now held in firmware until the data is actually taken, which makes
reading it free of side effects. That is what this suite checks, through the
one screen that reads it: System Info has to report the EEPROM as changed every
time it is opened, not only the first time.

The suite builds its own GMod2 cartridge (CRT type 60) rather than shipping a
ROM, and drives the EEPROM with the repository's own runtime, from RAM, the way
a GMod2 game does. The harness asks for the write over machine:writemem, so the
EEPROM's state is read once before it too.

The EEPROM is an optional part of the FPGA build; System Info says so, and the
suite reports SKIP on a machine without one.
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

import cli                                                      # noqa: E402
import menu as menu_lib                                         # noqa: E402
import pacing                                                   # noqa: E402
import wait                                                     # noqa: E402
from api import UltimateApi                                     # noqa: E402
from assembler import assemble                                  # noqa: E402
from report import (Failure, check, check_ok, check_skip,       # noqa: E402
                    check_start, detail, format_exception, section,
                    suite_fail, suite_ok, suite_skip, teardown_step)

SUITE = "gmod2_eeprom_dirty_test"

SOURCE = Path(__file__).resolve().parent / "gmod2_eeprom_write.asm"

# Addresses the stimulus and the harness share; see gmod2_eeprom_write.asm.
DONE = 0x0400
GO = 0x0402
RUNNING = 0x0405

GMOD2_TYPE = 60

# This cartridge is uploaded over REST and therefore runs from /Temp, which
# cartridge auto-save cannot write back: once the game has written the EEPROM,
# that feature greets every menu open with a notice, over the screen this suite
# is reading. The setting is absent on a firmware without the feature.
AUTOSAVE_CATEGORY = "C64 and Cartridge Settings"
AUTOSAVE_ITEM = "Save Changed Cartridge"

ROUTINE_TIMEOUT_SECONDS = 15.0
WRITE_TIMEOUT_SECONDS = 15.0
SYSINFO_TIMEOUT_SECONDS = 20.0
# How long a key is given to show on screen before it is sent again.
KEY_WATCH_SECONDS = 2.0

EEPROM_LINE = "EEPROM:"
DIRTY = "Dirty"
CLEAN = "Clean"
UNSUPPORTED = "Not Supported"
TITLE = "System Information"

# F4 opens System Info, and a C64 keyboard makes F4 from shift and F3.
SYSINFO_KEYS = ("left_shift", "f3")

# CLR/HOME puts a text view back on its first line (editor.cc, KEY_HOME). Not
# F2, which the user interface maps to its settings before an editor sees it.
HOME_KEYS = ("clr_home",)

# System Info is longer than one screen, and the EEPROM sits in the cartridge
# section below the fold.
SYSINFO_PAGES = 8


def chip(bank: int, load: int, rom: bytes) -> bytes:
    """One CHIP chunk, as the CRT format has it."""
    return (b"CHIP" + (0x10 + len(rom)).to_bytes(4, "big")
            + (0).to_bytes(2, "big") + bank.to_bytes(2, "big")
            + load.to_bytes(2, "big") + len(rom).to_bytes(2, "big") + rom)


def gmod2_crt() -> bytes:
    """A one-bank type 60 CRT that starts the stimulus.

    No EEPROM chunk: the firmware creates an empty one on load and clears the
    EEPROM in hardware with it (c64_crt.cc, find_eeprom), which is the state
    this suite wants to start from.
    """
    header = bytearray(0x40)
    header[0x00:0x10] = b"C64 CARTRIDGE   "
    header[0x10:0x14] = (0x40).to_bytes(4, "big")
    header[0x14:0x16] = b"\x01\x00"
    header[0x16:0x18] = GMOD2_TYPE.to_bytes(2, "big")
    header[0x18] = 0                                # EXROM low, GAME high: 8K
    header[0x19] = 1
    header[0x20:0x2B] = b"GMOD2-EEPROM"[:11]
    return bytes(header) + chip(0, 0x8000, assemble(SOURCE)[2:])


class Device:
    """The device under test, and the screens this suite reads."""

    def __init__(self, args) -> None:
        self.api = UltimateApi(args.host, args.password or None, args.timeout)

    def quiet_cartridge_autosave(self) -> None:
        """Hold cartridge auto-save off while this suite runs, where it exists.

        Not put back here: writing a setting leaves the firmware holding a user
        interface object that closing the menu only hides, and the gate that
        follows a suite then reports the root browser as not on top. The runner
        captures every setting before a run and restores what changed
        (tests/lib/config_snapshot.py), so the one write below is undone there.
        A suite run on its own leaves the setting Off.
        """
        try:
            current = str(self.api.configs.current(AUTOSAVE_CATEGORY, AUTOSAVE_ITEM))
        except Failure:
            return          # a firmware without the feature
        if current != "Off":
            self.api.configs.set(AUTOSAVE_CATEGORY, AUTOSAVE_ITEM, "Off")

    def start_cartridge(self) -> None:
        code, _, body = self.api.runners.upload("run_crt", gmod2_crt())
        if code != 200:
            raise Failure(f"starting the cartridge returned HTTP {code}: {body[:160]!r}")
        wait.wait_until(lambda: self.api.machine.readmem(RUNNING, 1)[0] == 0x01,
                        "the cartridge reaches its routine",
                        timeout=ROUTINE_TIMEOUT_SECONDS)

    def write_eeprom(self) -> None:
        """Have the C64 write one word into the EEPROM, and wait for it."""
        self.api.machine.writemem(GO, bytes([0x01]))
        wait.wait_until(lambda: self.api.machine.readmem(DONE, 1)[0] == 0x01,
                        "the cartridge finishes writing the EEPROM",
                        timeout=WRITE_TIMEOUT_SECONDS)

    def set_menu(self, want_open: bool) -> None:
        """Open or close the menu, with one press of its button.

        menu_lib.toggle_menu presses once and then polls, which is the only
        safe way to drive a toggle: pressing again because the state has not
        changed yet closes a menu that was still on its way open.
        """
        if not menu_lib.toggle_menu(self.api.machine.menu_button,
                                    self.api.machine.menu_open, want_open):
            raise Failure("the menu did not "
                          + ("open" if want_open else "close"))

    def open_menu(self) -> None:
        """Leave the menu freshly open, whatever it held before.

        Closed and opened with the menu button alone, and both states are read
        back. Backing out with F8 instead leaves the overlay reporting an open
        menu that no user interface object has focus in, and every key sent
        into that goes to the C64 rather than to the firmware.
        """
        self.set_menu(False)
        self.set_menu(True)

    def eeprom_state(self) -> str:
        """Open System Info, read its EEPROM line, and close it again.

        Every reading in this suite goes through here, so each one is a fresh
        open of the screen that reads the flag.
        """
        self.open_menu()
        self.open_sysinfo()
        rows: list[str] = []
        for _ in range(SYSINFO_PAGES):
            rows = self.api.machine.menu_rows()
            for row in rows:
                if EEPROM_LINE in row:
                    self.close_sysinfo()
                    return row.split(EEPROM_LINE, 1)[1].strip()
            self.api.machine.press("f7")        # the report is longer than one screen
            time.sleep(pacing.MENU_TOGGLE_SETTLE_SECONDS)
        detail("last screen the harness read:")
        for row in rows:
            if row.strip():
                detail(f"  {row}")
        raise Failure(f"System Info showed no {EEPROM_LINE} line in "
                      f"{SYSINFO_PAGES} screens")

    def open_sysinfo(self) -> None:
        """Open System Info over the browser, and leave it on its first line.

        The key is sent once per screen change, not repeatedly: a second F4
        while the view is already up pages it, and the title then scrolls off
        the top, which is the one thing this waits on. A key that reached
        nothing leaves the screen as it was, and only then is it sent again.
        """
        deadline = time.monotonic() + SYSINFO_TIMEOUT_SECONDS
        while True:
            before = self.api.machine.menu_rows()
            self.api.machine.press(*SYSINFO_KEYS)
            if self.wait_screen_change(before, KEY_WATCH_SECONDS):
                break
            if time.monotonic() >= deadline:
                self.report_screen()
                raise Failure(f"{TITLE!r} did not open within "
                              f"{SYSINFO_TIMEOUT_SECONDS:.0f} s")
        self.press_home()

    def press_home(self) -> None:
        """Put the open text view back on its first line, so the title shows."""
        deadline = time.monotonic() + SYSINFO_TIMEOUT_SECONDS
        while not any(TITLE in row for row in self.api.machine.menu_rows()):
            if time.monotonic() >= deadline:
                self.report_screen()
                raise Failure(f"{TITLE!r} did not come back into view")
            self.api.machine.press(*HOME_KEYS)
            time.sleep(pacing.MENU_TOGGLE_SETTLE_SECONDS)

    def wait_screen_change(self, before: list[str], timeout: float) -> bool:
        """Whether the screen became something other than `before` in time."""
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            time.sleep(pacing.MENU_TOGGLE_SETTLE_SECONDS)
            if self.api.machine.menu_rows() != before:
                return True
        return False

    def report_screen(self) -> None:
        rows = self.api.machine.menu_rows()
        detail(f"menu_open={self.api.machine.menu_open()} rows={len(rows)}")
        for row in rows:
            if row.strip():
                detail(f"  |{row.rstrip()}")

    def close_sysinfo(self) -> None:
        """Leave the text view again, so the browser is back on top.

        RUN/STOP is what closes it; F8 does not reach an editor. A view left
        open survives the menu being closed and reopened, and the next reading
        would then page through the old one instead of opening a new one, so
        this waits for the title to go rather than pressing once.
        """
        self.press_home()               # reading the EEPROM line pages down
        deadline = time.monotonic() + SYSINFO_TIMEOUT_SECONDS
        while any(TITLE in row for row in self.api.machine.menu_rows()):
            if time.monotonic() >= deadline:
                raise Failure("System Info stayed open after RUN/STOP")
            self.api.machine.press("run_stop")
            time.sleep(pacing.MENU_TOGGLE_SETTLE_SECONDS)

    def close_menu(self) -> None:
        """Pop every level and leave the menu closed.

        RUN/STOP leaves a text view, and leaves the menu once the browser has
        focus, so pressing it until the menu is gone takes the stack down one
        object at a time. F8 would be one press, but it exits the user
        interface instead of unwinding it, and the gate that follows a suite
        then finds a menu that will not close from the browser.
        """
        deadline = time.monotonic() + SYSINFO_TIMEOUT_SECONDS
        while self.api.machine.menu_open():
            if time.monotonic() >= deadline:
                raise Failure("the menu would not close")
            self.api.machine.press("run_stop")
            time.sleep(pacing.MENU_TOGGLE_SETTLE_SECONDS)


def run(args) -> str | None:
    device = Device(args)
    try:
        section("a GMod2 cartridge whose game saves into the EEPROM")
        device.quiet_cartridge_autosave()
        with check("the cartridge starts and its EEPROM routine is ready"):
            device.start_cartridge()

        check_start("the machine has an EEPROM")
        state = device.eeprom_state()
        if UNSUPPORTED in state:
            reason = "this FPGA build carries no GMod2 EEPROM"
            check_skip(reason)
            return reason
        check_ok(f"System Info reports {state!r}")

        with check("the EEPROM starts out unchanged"):
            if CLEAN not in state:
                raise Failure(f"System Info reports {state!r} before the game wrote "
                              "anything; the cartridge was loaded with a clean EEPROM")

        with check("System Info reports the EEPROM as changed after the game saved"):
            device.write_eeprom()
            state = device.eeprom_state()
            if DIRTY not in state:
                raise Failure(f"System Info reports {state!r} after the C64 wrote "
                              "a word into the EEPROM")

        with check("opening System Info again still reports the change"):
            state = device.eeprom_state()
            if DIRTY not in state:
                raise Failure(f"the second reading reports {state!r}: reading the flag "
                              "took the change with it, and a save would now write the "
                              "EEPROM as it was loaded")
        return None
    finally:
        teardown_step("close the menu", device.close_menu)
        # A reboot removes the cartridge; a reset would boot straight back into it.
        teardown_step("restore the configured cartridge", device.api.machine.reboot)


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Check that reading the GMod2 EEPROM's changed flag does not "
                    "clear it.")
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
