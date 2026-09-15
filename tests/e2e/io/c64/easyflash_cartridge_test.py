#!/usr/bin/env python3
"""E2E: an EasyFlash bank write with bit 6 or bit 7 set has to select the bank
in bits 0 to 5.

EasyFlash (CRT type 32) decodes six bits of its $DE00 bank register, so a write
of $40+n, $80+n or $C0+n selects bank n. The FPGA takes all eight bits, and on
a product with 4 MB of cartridge memory bank $40 starts at offset 1 MB. The
firmware repeats the image up to the end of the cartridge memory, so those
banks read copies of banks 0 to 63. An EasyFlash image of Maniac Mansion
writes $78 to $7B to $DE00 while it starts, and stays at a blue screen when
those banks read $FF.

The suite does not ship a ROM image. It builds a 64-bank type 32 CRT here: the
cartridge starts in Ultimax mode, and bank 0 at $E000 copies a routine to
$0800 and runs it from RAM. The routine switches to 16K mode and, for each of
$00, $40, $80 and $C0 in the upper bits, selects banks 63 to 0 and copies the
byte at $9FF0 to one screen RAM row. The byte at $9FF0 of bank n is n, so each
row reads 00 to 3f when every write reaches the bank in its low six bits.

A product with 1 MB of cartridge memory decodes 20 address bits, which drops
bits 6 and 7 of the bank number, so the suite passes there without the
repeated image.
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

SUITE = "easyflash_cartridge_test"

BANKS = 64
MARKER = 0x1FF0             # $9FF0 in each bank's ROML chip
UPPER_BITS = (0x00, 0x40, 0x80, 0xC0)
FIRST_ROW = 0x0400          # screen RAM, one 64-byte row per value in UPPER_BITS
DONE = 0x0500               # set to $01 when the routine ran
BLANK = 0xA0                # not a marker value, so a byte the routine never wrote cannot pass
ROUTINE_ADDRESS = 0x0800    # RAM that Ultimax mode also maps
ROUTINE_OFFSET = 0x20       # of the routine in bank 0's ROMH chip


def select_loop(upper_bits: int, row: int) -> bytes:
    """Select banks 63 to 0 with `upper_bits` set and copy $9FF0 to `row`,X."""
    return bytes([
        0xA2, BANKS - 1,        # LDX #$3F
        0x8A,                   # TXA
        0x09, upper_bits,       # ORA #upper_bits
        0x8D, 0x00, 0xDE,       # STA $DE00
        0xAD, 0xF0, 0x9F,       # LDA $9FF0
        0x9D, row & 0xFF, row >> 8,  # STA row,X
        0xCA,                   # DEX
        0x10, 0xF1,             # BPL to TXA
    ])


def routine() -> bytes:
    code = bytes([
        0xA9, 0x07,             # LDA #$07
        0x8D, 0x02, 0xDE,       # STA $DE02        16K mode
    ])
    for index, upper_bits in enumerate(UPPER_BITS):
        code += select_loop(upper_bits, FIRST_ROW + index * BANKS)
    end = ROUTINE_ADDRESS + len(code) + 5
    code += bytes([
        0xA9, 0x01,             # LDA #$01
        0x8D, DONE & 0xFF, DONE >> 8,   # STA DONE
        0x4C, end & 0xFF, end >> 8,     # JMP to itself
    ])
    return code


def boot(length: int) -> bytes:
    """Bank 0 at $E000 in Ultimax mode: copy the routine to RAM and run it."""
    return bytes([
        0x78,                   # E000  SEI
        0xA2, 0xFF,             # E001  LDX #$FF
        0x9A,                   # E003  TXS
        0xD8,                   # E004  CLD
        0xA2, length - 1,       # E005  LDX #len-1
        0xBD, ROUTINE_OFFSET, 0xE0,     # E007  LDA $E020,X
        0x9D, ROUTINE_ADDRESS & 0xFF, ROUTINE_ADDRESS >> 8,  # E00A  STA $0800,X
        0xCA,                   # E00D  DEX
        0x10, 0xF7,             # E00E  BPL $E007
        0x4C, ROUTINE_ADDRESS & 0xFF, ROUTINE_ADDRESS >> 8,  # E010  JMP $0800
        0x40,                   # E013  RTI        NMI and IRQ vectors
    ])


def chip(bank: int, load: int, rom: bytes) -> bytes:
    header = b"CHIP" + (0x10 + len(rom)).to_bytes(4, "big") + (0).to_bytes(2, "big")
    return header + bank.to_bytes(2, "big") + load.to_bytes(2, "big") + len(rom).to_bytes(2, "big") + rom


def easyflash_crt() -> bytes:
    """A type 32 CRT of 64 banks; the byte at $9FF0 of bank n is n."""
    header = bytearray(0x40)
    header[0:16] = b"C64 CARTRIDGE   "
    header[0x10:0x14] = (0x40).to_bytes(4, "big")
    header[0x14:0x16] = b"\x01\x00"
    header[0x16:0x18] = (32).to_bytes(2, "big")     # EasyFlash
    header[0x18] = 1                                # EXROM high, GAME low: Ultimax at start
    header[0x20:0x2F] = b"EASYFLASH-BANKS"
    image = bytes(header)
    code = routine()
    romh = bytearray(b"\xff" * 0x2000)
    start = boot(len(code))
    romh[0:len(start)] = start
    romh[ROUTINE_OFFSET:ROUTINE_OFFSET + len(code)] = code
    rti = 0xE000 + len(start) - 1
    for vector in (0x1FFA, 0x1FFE):                 # NMI, IRQ
        romh[vector:vector + 2] = rti.to_bytes(2, "little")
    romh[0x1FFC:0x1FFE] = (0xE000).to_bytes(2, "little")   # RESET
    for bank in range(BANKS):
        roml = bytearray(b"\xff" * 0x2000)
        roml[MARKER] = bank
        image += chip(bank, 0x8000, bytes(roml))
        if bank == 0:
            image += chip(0, 0xA000, bytes(romh))
    return image


def run(args) -> None:
    device = UltimateApi(args.host, args.password or None, args.timeout)
    wanted = bytes(range(BANKS))
    try:
        with check("the generated EasyFlash cartridge starts and runs its routine"):
            # Blank the rows and the done flag first, so bytes left by an earlier run cannot pass.
            device.machine.writemem(FIRST_ROW, bytes([BLANK]) * (DONE + 1 - FIRST_ROW))
            device.runners.upload("run_crt", easyflash_crt())
            deadline = time.monotonic() + 10.0
            while device.machine.readmem(DONE, 1)[0] != 0x01:
                if time.monotonic() >= deadline:
                    raise Failure("the routine copied from bank 0 did not finish within 10 s")
                time.sleep(0.25)
            rows = device.machine.readmem(FIRST_ROW, BANKS * len(UPPER_BITS))
        # Each row is reported, so a failure says which upper bits are wrong.
        failures = []
        for index, upper_bits in enumerate(UPPER_BITS):
            row = rows[index * BANKS:(index + 1) * BANKS]
            label = (f"EasyFlash writes of ${upper_bits:02X} to ${upper_bits | 0x3F:02X} "
                     f"select banks 0 to 63")
            try:
                with check(label):
                    if row != wanted:
                        wrong = sum(1 for bank in range(BANKS) if row[bank] != bank)
                        detail(f"{wrong} of {BANKS} selections reached another bank")
                        for start in range(0, BANKS, 16):
                            detail(f"  banks {start:2d}-{start + 15:2d} read markers "
                                   f"{row[start:start + 16].hex(' ')}")
                        raise Failure(f"a write of ${upper_bits:02X} + n did not select bank n")
            except Failure as exc:
                failures.append(exc)
        if failures:
            raise failures[0]
    finally:
        # A reboot removes the cartridge; a reset would boot straight back into it.
        teardown_step("restore the configured cartridge", device.machine.reboot)


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Check that EasyFlash bank writes with bit 6 or bit 7 set select "
                    "the bank in bits 0 to 5.")
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
