#!/usr/bin/env python3
# E2E: "Save C64 Memory" writes the C64's RAM, not what the bus shows (#978).

"""The task menu's "Save C64 Memory" saves the 64 KB of C64 RAM.

The suite writes a known pattern into RAM the idle BASIC prompt does not use,
saves the memory through the menu into /Temp, fetches the file over FTP and
compares it with the pattern. It also reads the BASIC and KERNAL ROMs through
the CPU's view, and requires the file to hold the RAM under them instead.

The dump has to read RAM directly. While the menu is open the machine is frozen
with the freezer's Ultimax cartridge banked in, which decodes nothing at
$1000-$7FFF and $A000-$CFFF, so a read through the bus returns $FF there. From
Telnet the machine is running, and the same read returns the ROMs instead of
the RAM beneath them. Run it with each --mode: freeze and overlay freeze the
machine, telnet does not. Under telnet a second save is taken with the machine
paused through REST, which stops it without freezing it, so the backups the
menu keeps of $0400-$0FFF are stale and must stay out of the file.

On an Ultimate II+ cartridge the file is the CPU view instead: the cartridge
reaches memory through the 6510's own $01 mapping, so it holds the ROMs where
the CPU sees them, as machine:readmem does.
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
from ui_backend import MODE_TELNET, add_mode_argument, make_browser

SUITE = "save_memory_test"

FILE_NAME = "savemem_e2e"
FILE_PATH = f"/Temp/{FILE_NAME}.bin"
RAM_SIZE = 0x10000
FIRST_SEED = 0x5A
SECOND_SEED = 0xA5

# Free at the READY prompt: past the empty program's end markers, and $C000.
PATTERN_RANGES = ((0x0810, 0xA000), (0xC000, 0xD000))
ROM_RANGES = (("BASIC", 0xA000, 0xC000), ("KERNAL", 0xE000, 0x10000))

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


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    cli.add_device_arguments(parser, timeout=10.0)
    parser.add_argument("--telnet-port", type=int,
                        default=int(os.environ.get("U64_TELNET_PORT", "23")))
    add_mode_argument(parser)
    args = parser.parse_args()

    api = UltimateApi(args.host, args.password or None, args.timeout)
    device = identify_machine(args.host, args.password or None, args.timeout)
    # A cartridge reaches memory through the 6510's own $01 mapping, so its file
    # holds the CPU view, ROMs included, the same bytes machine:readmem returns.
    cartridge = device.kind == machine_lib.U2
    if device.skip_without_fix(machine_lib.MEMORY_VIEWS_AGREE, "save C64 memory through the task menu"):
        suite_ok(SUITE)
        return 0

    browser = make_browser(
        args.mode, args.host, args.password or None, args.timeout,
        entry_rows=ENTRY_ROWS, status_row=STATUS_ROW,
        telnet_port=args.telnet_port,
        telnet_entry_rows=TELNET_ENTRY_ROWS, telnet_status_row=TELNET_STATUS_ROW,
    )
    try:
        with check("write the RAM pattern at the READY prompt"):
            remove(args.host, args.password)
            if not api.machine.reset(force=True):
                raise Failure("the machine did not reach READY after a reset")
            write_pattern(api, FIRST_SEED)
            roms = {name: api.machine.readmem(start, end - start)
                    for name, start, end in ROM_RANGES}

        saved = save_and_fetch(browser, args, f"save C64 memory through the {args.mode} task menu")
        check_pattern(saved, FIRST_SEED)

        if cartridge:
            with check("the file holds the ROMs the CPU sees, as readmem does"):
                differ = [name for name, start, end in ROM_RANGES
                          if saved[start:end] != roms[name]]
                if differ:
                    raise Failure(f"the file does not hold the {' and '.join(differ)} ROM image")
        else:
            with check("the file holds the RAM under the ROMs, not the ROMs"):
                shadowed = [name for name, start, end in ROM_RANGES
                            if saved[start:end] == roms[name]]
                if shadowed:
                    raise Failure(f"the file holds the {' and '.join(shadowed)} ROM image")
                detail("$A000-$BFFF and $E000-$FFFF differ from the ROMs the CPU sees")

        if args.mode == MODE_TELNET:
            # Stopped but not frozen: the menu's RAM backups are stale and must not be used.
            with check("pause the machine through REST with a second pattern"):
                remove(args.host, args.password)
                write_pattern(api, SECOND_SEED)
                api.machine.pause()
            saved = save_and_fetch(browser, args, "save C64 memory while the machine is paused")
            check_pattern(saved, SECOND_SEED)

        suite_ok(SUITE)
        return 0
    except Exception as exc:  # noqa: BLE001
        suite_fail(SUITE, format_exception(exc))
        return 1
    finally:
        teardown_step("close the menu", api.machine.close_menu_from_anywhere)
        if args.mode == MODE_TELNET:
            teardown_step("resume the machine", api.machine.resume)
        teardown_step("close the browser session", browser.close)
        teardown_step("remove the saved file", lambda: remove(args.host, args.password))
        teardown_step("reset the machine", lambda: api.machine.reset(force=True))


if __name__ == "__main__":
    raise SystemExit(main())
