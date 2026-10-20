#!/usr/bin/env python3
"""E2E: a 256K Ocean cartridge has to reach banks 16 to 31 at $A000.

Ocean type 1 (CRT type 5) cartridges select one 8K bank by writing to $DE00,
always with bit 7 set. RoboCop 2, Shadow of the Beast and Space Gun read banks
0 to 15 at $8000 and banks 16 to 31 at $A000, and their CRT files list those
upper banks at $A000. The firmware started every type 5 CRT in 8K mode, so
$A000 never showed the cartridge, and RoboCop 2 and Shadow of the Beast started
to a black screen.

The suite does not ship a ROM image. It builds a 32-bank type 5 CRT in that
layout here: bank 0 autostarts, copies a small routine to $C000 and runs it
from RAM, and the routine selects $9F down to $80 and copies one marker byte
from $8000 and one from $A000 of each bank to screen RAM. Each bank carries its
own marker, so the bytes read back say which bank every selection reached.
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
from api import UltimateApi                                     # noqa: E402
from report import (Failure, check, detail, format_exception,   # noqa: E402
                    suite_fail, suite_ok, teardown_step)

SUITE = "ocean_cartridge_test"

BANKS = 32
MARKER = 0x1FF0             # $9FF0 or $BFF0, depending on where the bank is read
ROML_ROW = 0x0400           # screen RAM row 0: the byte read at $9FF0
ROMH_ROW = 0x0428           # screen RAM row 1: the byte read at $BFF0

# Bank 0: the CBM80 autostart header, then a loop that copies ROUTINE to $C000
# and jumps to it. It has to run from RAM, because the first $DE00 write
# replaces the ROM it would otherwise be executing.
ROUTINE = bytes([
    0xA2, BANKS - 1,        # C000  LDX #31
    0x8A,                   # C002  TXA
    0x09, 0x80,             # C003  ORA #$80         bit 7, as the games write it
    0x8D, 0x00, 0xDE,       # C005  STA $DE00
    0xAD, 0xF0, 0x9F,       # C008  LDA $9FF0
    0x9D, 0x00, 0x04,       # C00B  STA $0400,X
    0xAD, 0xF0, 0xBF,       # C00E  LDA $BFF0
    0x9D, 0x28, 0x04,       # C011  STA $0428,X
    0xCA,                   # C014  DEX
    0x10, 0xEB,             # C015  BPL $C002
    0x4C, 0x17, 0xC0,       # C017  JMP $C017
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


def marker(bank: int) -> int:
    return 0x41 + bank      # never $20 (the blanked rows) and never $FF (no ROM)


def ocean_crt() -> bytes:
    """A 256K type 5 CRT: banks 0-15 at $8000, 16-31 at $A000, each with its own marker."""
    header = bytearray(0x40)
    header[0:16] = b"C64 CARTRIDGE   "
    header[0x10:0x14] = (0x40).to_bytes(4, "big")
    header[0x14:0x16] = b"\x01\x00"
    header[0x16:0x18] = (5).to_bytes(2, "big")      # Ocean type 1
    header[0x20:0x25] = b"OCEAN"                    # EXROM and GAME 0: 16K
    image = bytes(header)
    for bank in range(BANKS):
        rom = bytearray(b"\xff" * 0x2000)
        if bank == 0:
            rom[0:len(BOOT)] = BOOT
            rom[ROUTINE_OFFSET:ROUTINE_OFFSET + len(ROUTINE)] = ROUTINE
        rom[MARKER] = marker(bank)
        load = 0x8000 if bank < 16 else 0xA000
        chip = b"CHIP" + (0x2010).to_bytes(4, "big") + (0).to_bytes(2, "big")
        chip += bank.to_bytes(2, "big") + load.to_bytes(2, "big") + (0x2000).to_bytes(2, "big")
        image += chip + bytes(rom)
    return image


def reached(roml: bytes, romh: bytes) -> bytes:
    """The byte each bank selection reached where the games read it."""
    return roml[:16] + romh[16:]


def run(args) -> None:
    device = UltimateApi(args.host, args.password or None, args.timeout)
    wanted = bytes(marker(b) for b in range(BANKS))
    try:
        with check("256K Ocean CRT: $80-$8F reach banks 0-15 at $8000 "
                   "and $90-$9F reach banks 16-31 at $A000"):
            # Blank both rows first, so bytes left by an earlier run cannot pass.
            device.machine.writemem(ROML_ROW, b"\x20" * (ROMH_ROW + BANKS - ROML_ROW))
            device.runners.upload("run_crt", ocean_crt())
            deadline = time.monotonic() + 10.0
            while True:
                roml = device.machine.readmem(ROML_ROW, BANKS)
                romh = device.machine.readmem(ROMH_ROW, BANKS)
                if reached(roml, romh) == wanted or time.monotonic() >= deadline:
                    break
                time.sleep(0.25)
            if reached(roml, romh) != wanted:
                detail(f"read at $8000 for $80-$9F: {roml.hex(' ')}")
                detail(f"read at $A000 for $80-$9F: {romh.hex(' ')}")
                detail(f"want banks 0-15 at $8000 and 16-31 at $A000: {wanted.hex(' ')}")
                raise Failure("a bank selection did not reach its bank")
    finally:
        # A reboot removes the cartridge; a reset would boot straight back into it.
        teardown_step("restore the configured cartridge", device.machine.reboot)


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Check that a 256K Ocean cartridge reaches banks 16 to 31 at $A000.")
    cli.add_device_arguments(parser)
    args = parser.parse_args()
    try:
        run(args)
    except Exception as exc:            # noqa: BLE001
        suite_fail(SUITE, format_exception(exc))
        return 1
    suite_ok(SUITE)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
