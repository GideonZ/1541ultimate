#!/usr/bin/env python3
"""E2E: a COMAL 80 cartridge has to reach every bank it selects with bit 7 set.

COMAL 80 (CRT type 21) is four 16K banks selected by writing to $DE00, and the
ROM always sets bit 7: it writes $80, $82 and $83, never 0 to 3. The FPGA takes
all eight bits of that write as the bank number, so on a product with 4 MB of
cartridge memory $80 addresses offset 2 MB. The firmware used to mirror a
small image only up to 1 MB, so everything the ROM switched to there read $FF
and COMAL 80 started to a blank screen (GideonZ/1541ultimate#899).

The suite does not ship a ROM image. It builds a four-bank type 21 CRT here:
bank 0 autostarts, copies a small routine to $C000 and runs it from RAM, and
the routine selects $83, $82, $81 and $80 in turn and copies one marker byte
from ROML and one from ROMH of each bank to screen RAM. Each bank carries its
own markers, so the eight bytes read back say which bank every selection
reached. Before the fix all eight read $FF.
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

SUITE = "comal80_cartridge_test"

BANKS = 4
ROML_MARKER = 0x1FF0        # $9FF0 in each bank
ROMH_MARKER = 0x3FF0        # $BFF0 in each bank
ROML_ROW = 0x0400           # screen RAM row 0
ROMH_ROW = 0x0428           # screen RAM row 1

# Bank 0 at $8000: the CBM80 autostart header, then a loop that copies ROUTINE
# to $C000 and jumps to it. It has to run from RAM, because the first $DE00
# write replaces the ROM it would otherwise be executing.
ROUTINE = bytes([
    0xA2, 0x03,             # C000  LDX #$03
    0x8A,                   # C002  TXA
    0x09, 0x80,             # C003  ORA #$80         bit 7, as COMAL 80 writes it
    0x8D, 0x00, 0xDE,       # C005  STA $DE00
    0xAD, 0xF0, 0x9F,       # C008  LDA $9FF0        ROML marker of this bank
    0x9D, 0x00, 0x04,       # C00B  STA $0400,X
    0xAD, 0xF0, 0xBF,       # C00E  LDA $BFF0        ROMH marker of this bank
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


def roml_marker(bank: int) -> int:
    return 0x30 + bank      # screen codes "0".."3"


def romh_marker(bank: int) -> int:
    return 0x01 + bank      # screen codes "A".."D"


def comal80_crt() -> bytes:
    """A type 21 CRT of four 16K banks, each with its own marker bytes."""
    header = bytearray(0x40)
    header[0:16] = b"C64 CARTRIDGE   "
    header[0x10:0x14] = (0x40).to_bytes(4, "big")
    header[0x14:0x16] = b"\x01\x00"
    header[0x16:0x18] = (21).to_bytes(2, "big")     # COMAL 80
    header[0x20:0x28] = b"COMAL-80"                 # EXROM and GAME 0: 16K
    image = bytes(header)
    for bank in range(BANKS):
        rom = bytearray(b"\xff" * 0x4000)
        if bank == 0:
            rom[0:len(BOOT)] = BOOT
            rom[ROUTINE_OFFSET:ROUTINE_OFFSET + len(ROUTINE)] = ROUTINE
        rom[ROML_MARKER] = roml_marker(bank)
        rom[ROMH_MARKER] = romh_marker(bank)
        chip = b"CHIP" + (0x4010).to_bytes(4, "big") + (0).to_bytes(2, "big")
        chip += bank.to_bytes(2, "big") + (0x8000).to_bytes(2, "big") + (0x4000).to_bytes(2, "big")
        image += chip + bytes(rom)
    return image


def run(args) -> None:
    device = UltimateApi(args.host, args.password or None, args.timeout)
    wanted = (bytes(roml_marker(b) for b in range(BANKS)),
              bytes(romh_marker(b) for b in range(BANKS)))
    try:
        with check("COMAL 80 bank selections with bit 7 set reach banks 0 to 3"):
            # Blank both rows first, so bytes left by an earlier run cannot pass.
            device.machine.writemem(ROML_ROW, b"\x20" * (ROMH_ROW + BANKS - ROML_ROW))
            device.runners.upload("run_crt", comal80_crt())
            deadline = time.monotonic() + 10.0
            while True:
                seen = (device.machine.readmem(ROML_ROW, BANKS),
                        device.machine.readmem(ROMH_ROW, BANKS))
                if seen == wanted or time.monotonic() >= deadline:
                    break
                time.sleep(0.25)
            if seen != wanted:
                detail(f"ROML markers of banks 0-3: read {seen[0].hex(' ')}, "
                       f"want {wanted[0].hex(' ')}")
                detail(f"ROMH markers of banks 0-3: read {seen[1].hex(' ')}, "
                       f"want {wanted[1].hex(' ')}")
                raise Failure("a $DE00 write of $80 to $83 did not select banks 0 to 3")
    finally:
        # A reboot removes the cartridge; a reset would boot straight back into it.
        teardown_step("restore the configured cartridge", device.machine.reboot)


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Check that a COMAL 80 cartridge reaches every bank it selects.")
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
