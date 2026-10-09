#!/usr/bin/env python3
# E2E: every way of reading C64 memory agrees on what it shows.

"""REST readmem, the machine code monitor and "Save C64 Memory" agree.

Two views of memory are on offer, and each reader is held to the one it claims:

  - the CPU view, with BASIC, KERNAL, I/O and colour RAM where $01 maps them:
    REST `machine:readmem`, which is also all the web Live Monitor reads, and
    the monitor at CPU7;
  - the RAM view, the 64 KB alone: "Save C64 Memory" and the monitor at CPU0.

With the menu open under Freeze or Overlay the machine is frozen, and the menu
uses $0400-$0FFF and the colour RAM for its own screen. Every reader has to
show the C64's contents there, which the firmware keeps in its freeze backups,
and not the menu's. Under Telnet the machine keeps running.

The suite writes a screen row, a colour row and a RAM pattern at the READY
prompt, opens the UI with --mode, saves the memory through the task menu and
reads all 64 KB over REST. Where the two views coincide, the file and REST are
compared byte for byte. The monitor is then read at one page per distinct
path: the screen and RAM backups, BASIC, colour RAM and KERNAL. Every
comparison is its own check, so one run reports every disagreement.

Colour RAM is four bits wide, so it is compared on the low nibble. Registers
are left out: the 6510's port at $00/$01, which DMA cannot see, and the I/O at
$D000-$D7FF and $DC00-$DFFF, where reading has side effects and a frozen
machine shows the menu's hardware.

On an Ultimate II+ cartridge every reader shows the CPU view, because the
cartridge reaches memory through the 6510's own $01 mapping and cannot see
the RAM under the ROMs. There the saved file, REST and the monitor's CPU VIEW
are all compared with each other.
"""

from __future__ import annotations

import argparse
import os
import re
import sys
from pathlib import Path

# The one stanza that puts the shared library on sys.path; see tests/lib/bootstrap.py.
sys.path.insert(0, str(next(p for p in Path(__file__).resolve().parents
                            if (p / "tests" / "lib").is_dir()) / "tests" / "lib"))
import bootstrap  # noqa: E402,F401

sys.path.insert(0, bootstrap.directory("e2e", "io", "c64"))
import cli  # noqa: E402
import machine as machine_lib  # noqa: E402
import monitor_test as mon  # noqa: E402
import save_memory_test as savemem  # noqa: E402
from api import UltimateApi, identify_machine  # noqa: E402
from report import (Failure, check, format_exception,  # noqa: E402
                    suite_fail, suite_ok, teardown_step)
from ui_backend import MODE_TELNET, add_mode_argument, make_browser  # noqa: E402

SUITE = "memory_views_test"
SEED = 0x3C
RAM_SIZE = 0x10000

SCREEN_ROW = 0x0590
SCREEN_TEXT = bytes(range(1, 41))
COLOR_ROW = 0xD990
COLOR_VALUES = bytes((i * 5 + 3) & 0x0F for i in range(40))

# BASIC, I/O and KERNAL are where the two views differ by design.
VIEWS_DIFFER = ((0xA000, 0xC000), (0xD000, RAM_SIZE))
# Registers, not memory. DMA reads the RAM beneath the 6510's port at $00/$01,
# while the monitor shows the port of the bank it is set to.
REGISTERS = ((0x0000, 0x0002), (0xD000, 0xD800), (0xDC00, 0xE000))
COLOR_RAM = ((0xD800, 0xDC00),)

# One monitor page per path: screen backup, RAM backup, BASIC, colour RAM, KERNAL.
MONITOR_PAGES = (SCREEN_ROW, 0x0900, 0xA000, COLOR_ROW, 0xFF80)
CPU_VIEW = "CPU7 $A:BAS $D:I/O $E:KRN"
RAM_VIEW = "CPU0 $A:RAM $D:RAM $E:RAM"

ROW_RE = re.compile(r"^\|?([0-9A-F]{4})((?: [0-9A-F]{2}){8})\b")


def inside(address: int, ranges) -> bool:
    return any(lo <= address < hi for lo, hi in ranges)


def require_same(addresses, got, want, cpu_view: bool) -> None:
    """`got` and `want` are indexed by address; colour RAM is four bits wide."""
    found = []
    for address in addresses:
        if inside(address, REGISTERS):
            continue
        a, b = got[address], want[address]
        if cpu_view and inside(address, COLOR_RAM):
            a, b = a & 0x0F, b & 0x0F
        if a != b:
            found.append(f"${address:04X} ${a:02X} not ${b:02X}")
    if found:
        raise Failure(f"{len(found)} bytes differ, first: {', '.join(found[:6])}")


def fixtures(cpu_view: bool) -> dict[int, int]:
    """What was written, by address."""
    want = dict(enumerate(SCREEN_TEXT, SCREEN_ROW))
    if cpu_view:
        want.update(enumerate(COLOR_VALUES, COLOR_ROW))
    for start, end in savemem.PATTERN_RANGES:
        want.update(enumerate(savemem.pattern(start, end, SEED), start))
    return want


def monitor_page(session, address: int) -> dict[int, int]:
    """Every byte on the monitor page that J `address` shows."""
    snapshot = session.goto(f"{address:04X}")
    page: dict[int, int] = {}
    for line in snapshot.lines:
        match = ROW_RE.match(line.strip())
        if match:
            base = int(match.group(1), 16)
            for offset, byte in enumerate(bytes.fromhex(match.group(2))):
                page[(base + offset) & 0xFFFF] = byte
    if address not in page:
        raise Failure(f"no memory row for ${address:04X} on the monitor page\n{snapshot.text()}")
    return page


class Comparisons:
    """Each comparison is its own check, and a failed one does not stop the next."""

    def __init__(self) -> None:
        self.failed: list[str] = []

    def run(self, label: str, body) -> None:
        try:
            with check(label):
                body()
        except Failure:
            self.failed.append(label)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    cli.add_device_arguments(parser, timeout=10.0)
    parser.add_argument("--telnet-port", type=int,
                        default=int(os.environ.get("U64_TELNET_PORT", "23")))
    add_mode_argument(parser)
    args = parser.parse_args()

    api = UltimateApi(args.host, args.password or None, args.timeout)
    device = identify_machine(args.host, args.password or None, args.timeout)
    # A cartridge reaches memory through the 6510's own $01 mapping, so every
    # reader there shows the CPU view, and its monitor has no bank to select.
    cartridge = device.kind == machine_lib.U2
    if device.skip_without_fix(machine_lib.MEMORY_VIEWS_AGREE, "compare the memory views"):
        suite_ok(SUITE)
        return 0

    compare = Comparisons()
    browser = session = None
    try:
        with check("write the screen row, colour row and RAM pattern at READY"):
            savemem.remove(args.host, args.password)
            if not api.machine.reset(force=True):
                raise Failure("the machine did not reach READY after a reset")
            api.machine.writemem(SCREEN_ROW, SCREEN_TEXT)
            api.machine.writemem(COLOR_ROW, COLOR_VALUES)
            savemem.write_pattern(api, SEED)
            written = fixtures(cpu_view=True)
            require_same(written, api.machine.readmem(0, RAM_SIZE), written, cpu_view=True)

        # Opened after the fixture is written, because a REST backend opens the menu.
        browser = make_browser(
            args.mode, args.host, args.password or None, args.timeout,
            entry_rows=savemem.ENTRY_ROWS, status_row=savemem.STATUS_ROW,
            telnet_port=args.telnet_port,
            telnet_entry_rows=savemem.TELNET_ENTRY_ROWS, telnet_status_row=savemem.TELNET_STATUS_ROW,
        )
        with check(f"save C64 memory through the {args.mode} task menu, and read 64 KB over REST"):
            savemem.save_through_menu(browser, browser.backend.machine.machine_task_category)
            saved = savemem.fetch(args.host, args.password)
            rest = api.machine.readmem(0, RAM_SIZE)

        # Telnet leaves the machine running, so the KERNAL's workspace and the
        # cursor move between the two reads.
        differ = (() if cartridge else VIEWS_DIFFER) + (((0x0002, 0x0800),) if args.mode == MODE_TELNET else ())
        for label, view, cpu_view in (("Save C64 Memory", saved, cartridge),
                                      ("REST readmem", rest, True)):
            want = fixtures(cpu_view)
            compare.run(f"{label} shows what was written",
                        lambda view=view, want=want, cpu_view=cpu_view:
                        require_same(want, view, want, cpu_view))
        compare.run("REST readmem and Save C64 Memory agree wherever the views coincide",
                    lambda: require_same([a for a in range(RAM_SIZE) if not inside(a, differ)],
                                         rest, saved, cpu_view=cartridge))

        session = mon.MonitorSession(browser.backend)
        mon.ensure_hex_width(session, 8)
        # On a cartridge the file was compared with REST in full above.
        views = (((None, rest, True, "CPU VIEW matches REST readmem"),) if cartridge else
                 ((CPU_VIEW, rest, True, "CPU7 matches REST readmem"),
                  (RAM_VIEW, saved, False, "CPU0 matches Save C64 Memory")))
        for status, reference, cpu_view, name in views:
            if status:
                mon.ensure_status(session, status)
            for address in MONITOR_PAGES:
                def same_page(address=address, reference=reference, cpu_view=cpu_view) -> None:
                    page = monitor_page(session, address)
                    require_same(page, page, reference, cpu_view)
                compare.run(f"monitor {name} at ${address:04X}", same_page)

        if compare.failed:
            raise Failure(f"{len(compare.failed)} comparisons failed")
        suite_ok(SUITE)
        return 0
    except Exception as exc:  # noqa: BLE001
        suite_fail(SUITE, format_exception(exc))
        return 1
    finally:
        if session is not None and not cartridge:
            teardown_step("put the monitor back on CPU7", lambda: mon.ensure_status(session, CPU_VIEW))
        if session is not None:
            teardown_step("leave the monitor", lambda: mon.leave_monitor_fully(session))
        teardown_step("close the menu", api.machine.close_menu_from_anywhere)
        if browser is not None:
            teardown_step("close the browser session", browser.close)
        teardown_step("remove the saved file", lambda: savemem.remove(args.host, args.password))
        teardown_step("reset the machine", lambda: api.machine.reset(force=True))


if __name__ == "__main__":
    raise SystemExit(main())
