#!/usr/bin/env python3
"""E2E: a C64 Game System cartridge selects its bank by the IO1 address.

The C64GS / System 3 cartridge (CRT type 15) is 64 banks of 8K at $8000. Its
bank register is a 6-bit latch on A0 to A5, clocked by any IO1 access with R/W
not connected, so `STA $DE05` and `LDA $DE05` both select bank 5 whatever byte
is on the data bus (#335, from the cartridge PCB). The C64GS 4-in-1 relies on
both: its loader writes `STA $DE00` with A = $37 and then calls into bank 0,
and it finishes with `BIT $DE00`. When the FPGA latched the data byte instead
of the address, every game on that cartridge crashed on start.

The suite does not ship a ROM image. It builds a 64-bank type 15 CRT here:
bank 0 autostarts, copies a small routine to $C000 and runs it from RAM, and
the routine selects every bank twice, copying the marker byte at $9FF0 to
screen RAM each time:

- by writing, with a data byte that names a different bank: bank X is
  selected with `STA $DE00,X` and A = X xor $3F
- by reading, with `LDA $DE00,X`

Each bank's marker is its own number, so the two rows read back say which bank
every selection reached. A data latch shows the write row reversed and the read
row as whatever the bus held.

The fix is in the FPGA image, and the Ultimate 64 images ship prebuilt, so the
suite is gated on `machine.C64GS_BANK_FROM_ADDRESS` and reports SKIP on a
machine the table lists. `run-tests --assume-fix c64gs-bank-from-address`
runs it there.
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
import machine as machine_lib                                   # noqa: E402
from api import UltimateApi, identify_machine                   # noqa: E402
from report import (Failure, check, detail, format_exception,   # noqa: E402
                    note_assumed_fix, suite_fail, suite_ok, suite_skip,
                    teardown_step)

SUITE = "c64gs_cartridge_test"

BANKS = 64
MARKER = 0x1FF0             # $9FF0 in each bank
WRITE_ROW = 0x0400          # screen RAM rows 0 and 1
READ_ROW = 0x0450           # screen RAM rows 2 and 3
DONE = 0x04A0               # screen RAM row 4, set to $01 when both passes ran
BLANK = 0xA0                # not a marker value, so a byte the routine never wrote cannot pass

# Bank 0 at $8000: the CBM80 autostart header, then a loop that copies ROUTINE
# to $C000 and jumps to it. It has to run from RAM, because the first IO1
# access replaces the ROM it would otherwise be executing.
ROUTINE = bytes([
    0xA2, 0x3F,             # C000  LDX #$3F
    0x8A,                   # C002  TXA
    0x49, 0x3F,             # C003  EOR #$3F         the data byte names bank 63-X
    0x9D, 0x00, 0xDE,       # C005  STA $DE00,X      the address names bank X
    0xAD, 0xF0, 0x9F,       # C008  LDA $9FF0        marker of the bank now at $8000
    0x9D, 0x00, 0x04,       # C00B  STA $0400,X
    0xCA,                   # C00E  DEX
    0x10, 0xF1,             # C00F  BPL $C002
    0xA2, 0x3F,             # C011  LDX #$3F
    0xBD, 0x00, 0xDE,       # C013  LDA $DE00,X      a read names bank X too
    0xAD, 0xF0, 0x9F,       # C016  LDA $9FF0
    0x9D, 0x50, 0x04,       # C019  STA $0450,X
    0xCA,                   # C01C  DEX
    0x10, 0xF4,             # C01D  BPL $C013
    0xA9, 0x01,             # C01F  LDA #$01
    0x8D, 0xA0, 0x04,       # C021  STA $04A0
    0x4C, 0x24, 0xC0,       # C024  JMP $C024
])
BOOT = bytes([
    0x09, 0x80, 0x09, 0x80,             # $8000 cold and warm start vectors: $8009
    0xC3, 0xC2, 0xCD, 0x38, 0x30,       # $8004 "CBM80"
    0x78,                               # $8009 SEI
    0xA2, len(ROUTINE) - 1,             # $800A LDX #len-1
    0xBD, 0x20, 0x80,                   # $800C LDA $8020,X
    0x9D, 0x00, 0xC0,                   # $800F STA $C000,X
    0xCA,                               # $8012 DEX
    0x10, 0xF7,                         # $8013 BPL $800C
    0x4C, 0x00, 0xC0,                   # $8015 JMP $C000
])
ROUTINE_OFFSET = 0x20


def c64gs_crt() -> bytes:
    """A type 15 CRT of 64 8K banks; the byte at $9FF0 of bank n is n."""
    header = bytearray(0x40)
    header[0:16] = b"C64 CARTRIDGE   "
    header[0x10:0x14] = (0x40).to_bytes(4, "big")
    header[0x14:0x16] = b"\x01\x00"
    header[0x16:0x18] = (15).to_bytes(2, "big")     # C64 Game System, System 3
    header[0x18] = 0                                # EXROM low
    header[0x19] = 1                                # GAME high: 8K mode
    header[0x20:0x2A] = b"C64GS-TEST"
    image = bytes(header)
    for bank in range(BANKS):
        rom = bytearray(b"\xff" * 0x2000)
        if bank == 0:
            rom[0:len(BOOT)] = BOOT
            rom[ROUTINE_OFFSET:ROUTINE_OFFSET + len(ROUTINE)] = ROUTINE
        rom[MARKER] = bank
        chip = b"CHIP" + (0x2010).to_bytes(4, "big") + (0).to_bytes(2, "big")
        chip += bank.to_bytes(2, "big") + (0x8000).to_bytes(2, "big") + (0x2000).to_bytes(2, "big")
        image += chip + bytes(rom)
    return image


def report_row(name: str, seen: bytes) -> None:
    wrong = [bank for bank in range(BANKS) if seen[bank] != bank]
    detail(f"{name}: {len(wrong)} of {BANKS} selections reached another bank")
    for start in range(0, BANKS, 16):
        detail(f"  banks {start:2d}-{start + 15:2d} read markers "
               f"{seen[start:start + 16].hex(' ')}")


def run(args) -> bool:
    """Run the checks; False when the machine's FPGA image lacks the fix."""
    device = UltimateApi(args.host, args.password or None, args.timeout)
    machine = identify_machine(args.host, args.password or None, args.timeout)
    absent = machine.missing_fix(machine_lib.C64GS_BANK_FROM_ADDRESS)
    if absent:
        suite_skip(SUITE, absent)
        return False
    if machine.assumed_fix(machine_lib.C64GS_BANK_FROM_ADDRESS):
        note_assumed_fix(machine_lib.C64GS_BANK_FROM_ADDRESS, machine.kind)

    wanted = bytes(range(BANKS))
    try:
        with check("the generated C64GS cartridge starts and runs its routine"):
            # Blank both rows and the done flag first, so bytes left by an
            # earlier run cannot pass.
            device.machine.writemem(WRITE_ROW, bytes([BLANK]) * (DONE + 1 - WRITE_ROW))
            device.runners.upload("run_crt", c64gs_crt())
            deadline = time.monotonic() + 10.0
            while device.machine.readmem(DONE, 1)[0] != 0x01:
                if time.monotonic() >= deadline:
                    raise Failure("the routine copied from bank 0 did not finish within 10 s")
                time.sleep(0.25)
            written = device.machine.readmem(WRITE_ROW, BANKS)
            read = device.machine.readmem(READ_ROW, BANKS)
        # Both selections are reported, so a failure says which access is wrong.
        failures = []
        for label, row, access in (
                ("C64GS bank writes select the bank their address names",
                 written, "STA $DE00,X with A = X xor $3F"),
                ("C64GS bank reads select the bank their address names",
                 read, "LDA $DE00,X")):
            try:
                with check(label):
                    if row != wanted:
                        report_row(access, row)
                        raise Failure(f"{access} did not select bank X")
            except Failure as exc:
                failures.append(exc)
        if failures:
            raise failures[0]
    finally:
        # A reboot removes the cartridge; a reset would boot straight back into it.
        teardown_step("restore the configured cartridge", device.machine.reboot)
    return True


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Check that a C64 Game System cartridge selects banks by the IO1 address.")
    cli.add_device_arguments(parser)
    args = parser.parse_args()
    try:
        ran = run(args)
    except Exception as exc:            # noqa: BLE001
        suite_fail(SUITE, format_exception(exc))
        return 1
    if ran:
        suite_ok(SUITE)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
