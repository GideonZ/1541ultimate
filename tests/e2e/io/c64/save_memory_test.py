#!/usr/bin/env python3
# E2E: "Save C64 Memory" writes the C64's RAM, not what the bus shows (#978).

"""The task menu's "Save C64 Memory" saves the C64's memory, and changes nothing.

The suite writes a known pattern into RAM the idle BASIC prompt does not use,
saves the memory through the menu into /Temp, fetches the file over FTP and
compares it with the pattern. On an Ultimate 64 the file must hold the RAM
under the BASIC and KERNAL ROMs, which the suite reads through the CPU's view
to tell apart. A cartridge reaches memory through the 6510's own $01 mapping,
so there the file must hold the ROMs as machine:readmem shows them.

Saving must not read the I/O registers, because some reads change the machine:
a read of $DD0D acknowledges CIA 2's interrupt flags. The suite latches CIA 2's
timer A flag before the save and requires it to be set afterwards.

Run it with each --mode. Whether the 6510 keeps running while the user
interface is up is measured: Telnet leaves it running, Freeze stops it, and
Overlay stops it unless an HDMI display is connected. With it running, a
second save is taken with the machine paused through REST, which stops it
without freezing it, so the backups the menu keeps of $0400-$0FFF and of the
colour RAM are stale and must stay out of the file. A screen row is written
for that save, and on a cartridge a colour row.

The file's RAM under the ROMs is only told apart from the ROM images here;
memory_views_test compares it with the monitor's RAM view.
"""

from __future__ import annotations

import argparse
import os
import sys
from pathlib import Path

# The one stanza that puts the shared library on sys.path; see tests/lib/bootstrap.py.
sys.path.insert(0, str(next(p for p in Path(__file__).resolve().parents
                            if (p / "tests" / "lib").is_dir()) / "tests" / "lib"))
import bootstrap  # noqa: E402,F401
import cli  # noqa: E402

from api import UltimateApi, identify_machine
import ftp as ftp_lib
import machine as machine_lib
from report import (Failure, check, detail, format_exception,
                    suite_fail, suite_ok, teardown_step)
from ui_backend import add_mode_argument, make_browser

SUITE = "save_memory_test"

FILE_NAME = "savemem_e2e"
FILE_PATH = f"/Temp/{FILE_NAME}.bin"
RAM_SIZE = 0x10000
FIRST_SEED = 0x5A
SECOND_SEED = 0xA5

# Free at the READY prompt: past the empty program's end markers, and $C000.
PATTERN_RANGES = ((0x0810, 0xA000), (0xC000, 0xD000))
ROM_RANGES = (("BASIC", 0xA000, 0xC000), ("KERNAL", 0xE000, 0x10000))
# Row 10 of the screen and of colour RAM, clear of the cursor on row 6.
SCREEN_ROW = (0x0590, 0x05B8)
COLOR_ROW = (0xD990, 0xD9B8)

CIA2_TIMER_A = 0xDD04
CIA2_ICR = 0xDD0D
CIA2_CONTROL_A = 0xDD0E
CIA_START_ONE_SHOT = 0x19  # start, one-shot, force load

ENTRY_ROWS = range(2, 24)
STATUS_ROW = 24
TELNET_ENTRY_ROWS = range(2, 23)
TELNET_STATUS_ROW = 23


def pattern(start: int, end: int, seed: int) -> bytes:
    # Varies within and across pages, so neither a fill nor a shifted copy matches it.
    return bytes((a * 7 + (a >> 8) * 13 + seed) & 0xFF for a in range(start, end))


def block_report(saved: bytes, start: int, end: int, seed: int) -> str:
    """Per-4KB differences, in the form the issue reported them."""
    lines = []
    expected = pattern(start, end, seed)
    for block in range(start & ~0xFFF, end, 0x1000):
        lo, hi = max(block, start), min(block + 0x1000, end)
        got = saved[lo:hi]
        want = expected[lo - start:hi - start]
        differ = sum(1 for g, w in zip(got, want) if g != w)
        if differ:
            lines.append(f"${lo:04X}-${hi - 1:04X}: {differ} of {hi - lo} bytes differ, "
                         f"{len(set(got))} distinct values in the file")
    return "\n".join(lines)


def save_through_menu(browser, category: str) -> None:
    browser.backend.ensure_ready()
    browser.go_to_directory("Temp")
    browser.invoke_task_action(category, "Save C64 Memory")
    browser.wait_for_text("Save RAM as..")
    # The field opens holding "memory".
    browser.fill_edit_field(FILE_NAME, clear_taps=len("memory"))
    browser.wait_for_text("bytes saved: 65536")
    browser.press_popup_button("o")


def fetch(host: str, password: str) -> bytes:
    with ftp_lib.session(host, password, timeout=20) as ftp:
        return ftp_lib.retrieve(ftp, FILE_PATH)


def remove(host: str, password: str) -> None:
    with ftp_lib.session(host, password, timeout=20) as ftp:
        ftp_lib.delete_quietly(ftp, FILE_PATH)


def write_pattern(api, seed: int) -> None:
    for start, end in PATTERN_RANGES:
        api.machine.writemem(start, pattern(start, end, seed))
        if api.machine.readmem(start, end - start) != pattern(start, end, seed):
            raise Failure(f"${start:04X}-${end - 1:04X} did not read back as written")


def save_and_fetch(browser, args, label: str) -> bytes:
    with check(label):
        save_through_menu(browser, browser.backend.machine.machine_task_category)
        saved = fetch(args.host, args.password)
        if len(saved) != RAM_SIZE:
            raise Failure(f"the file holds {len(saved)} bytes, expected {RAM_SIZE}")
    return saved


def check_pattern(saved: bytes, seed: int) -> None:
    with check("the file holds the RAM pattern"):
        report = "\n".join(r for r in (block_report(saved, start, end, seed)
                                       for start, end in PATTERN_RANGES) if r)
        if report:
            raise Failure(f"the saved RAM differs from what was written\n{report}")


def arm_cia_flag(api) -> None:
    """Start CIA 2's timer A as a one-shot, so its interrupt flag latches.

    The KERNAL leaves CIA 2's NMI masked, so nothing reads $DD0D and the flag
    stays set until something does: a read through the bus clears it.
    """
    api.machine.writemem(CIA2_TIMER_A, bytes((0x10, 0x00)))
    api.machine.writemem(CIA2_CONTROL_A, bytes((CIA_START_ONE_SHOT,)))


def cia_flag_set(api) -> bool:
    return bool(api.machine.readmem(CIA2_ICR, 1)[0] & 0x01)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    cli.add_device_arguments(parser, timeout=10.0)
    parser.add_argument("--telnet-port", type=int,
                        default=int(os.environ.get("U64_TELNET_PORT", "23")))
    add_mode_argument(parser)
    args = parser.parse_args()

    api = UltimateApi(args.host, args.password or None, args.timeout)
    device = identify_machine(args.host, args.password or None, args.timeout)
    if device.skip_without_fix(machine_lib.MEMORY_VIEWS_AGREE, "save C64 memory through the task menu"):
        suite_ok(SUITE)
        return 0

    browser = None
    machine_runs = False
    try:
        with check("write the RAM pattern at the READY prompt and latch a CIA flag"):
            remove(args.host, args.password)
            if not api.machine.reset(force=True):
                raise Failure("the machine did not reach READY after a reset")
            arm_cia_flag(api)
            if not cia_flag_set(api):
                raise Failure("CIA 2's timer A flag did not latch, so the side-effect check cannot fail")
            arm_cia_flag(api)
            write_pattern(api, FIRST_SEED)
            roms = {name: api.machine.readmem(start, end - start)
                    for name, start, end in ROM_RANGES}

        # Opened after the fixture is written, because a REST backend opens the menu.
        browser = make_browser(
            args.mode, args.host, args.password or None, args.timeout,
            entry_rows=ENTRY_ROWS, status_row=STATUS_ROW,
            telnet_port=args.telnet_port,
            telnet_entry_rows=TELNET_ENTRY_ROWS, telnet_status_row=TELNET_STATUS_ROW,
        )
        saved = save_and_fetch(browser, args, f"save C64 memory through the {args.mode} task menu")
        machine_runs = api.machine.cpu_runs()
        detail(f"the 6510 {'runs' if machine_runs else 'is stopped'} while this user interface is up")
        check_pattern(saved, FIRST_SEED)

        if device.reaches_ram_under_rom:
            with check("the file holds the RAM under the ROMs, not the ROMs"):
                shadowed = [name for name, start, end in ROM_RANGES
                            if saved[start:end] == roms[name]]
                if shadowed:
                    raise Failure(f"the file holds the {' and '.join(shadowed)} ROM image")
        else:
            with check("the file holds the ROMs the CPU sees, as readmem does"):
                differ = [name for name, start, end in ROM_RANGES
                          if saved[start:end] != roms[name]]
                if differ:
                    raise Failure(f"the file does not hold the {' and '.join(differ)} ROM image")

        with check("saving left the I/O registers alone: CIA 2's interrupt flag is still set"):
            # Read with the menu closed: while it holds the machine, readmem of I/O
            # does not reach the CIA on every machine.
            api.machine.close_menu_from_anywhere()
            if not cia_flag_set(api):
                raise Failure("$DD0D read back clear: the save read the CIA and acknowledged its flag")

        if machine_runs:
            # Stopped but not frozen: the menu's backups are stale and must not be used.
            # Colour RAM is in the file only where it holds the CPU view.
            rows = (SCREEN_ROW,) + (() if device.reaches_ram_under_rom else (COLOR_ROW,))
            with check("pause the machine through REST with a second pattern and screen row"):
                remove(args.host, args.password)
                write_pattern(api, SECOND_SEED)
                for start, end in rows:
                    api.machine.writemem(start, pattern(start, end, SECOND_SEED))
                api.machine.pause()
            saved = save_and_fetch(browser, args, "save C64 memory while the machine is paused")
            check_pattern(saved, SECOND_SEED)
            with check("the file holds the screen row written while paused, not the menu's backup"):
                for start, end in rows:
                    nibble = 0x0F if start == COLOR_ROW[0] else 0xFF
                    want = bytes(b & nibble for b in pattern(start, end, SECOND_SEED))
                    got = bytes(b & nibble for b in saved[start:end])
                    if got != want:
                        raise Failure(f"${start:04X}-${end - 1:04X} holds {got[:8].hex(' ')}..., "
                                      f"written {want[:8].hex(' ')}...")

        suite_ok(SUITE)
        return 0
    except Exception as exc:  # noqa: BLE001
        suite_fail(SUITE, format_exception(exc))
        return 1
    finally:
        teardown_step("close the menu", api.machine.close_menu_from_anywhere)
        if machine_runs:
            teardown_step("resume the machine", api.machine.resume)
        if browser is not None:
            teardown_step("close the browser session", browser.close)
        teardown_step("remove the saved file", lambda: remove(args.host, args.password))
        teardown_step("reset the machine", lambda: api.machine.reset(force=True))


if __name__ == "__main__":
    raise SystemExit(main())
