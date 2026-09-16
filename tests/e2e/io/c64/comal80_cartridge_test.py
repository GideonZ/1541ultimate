#!/usr/bin/env python3
"""E2E: a COMAL 80 cartridge has to reach every bank it selects with bit 7 set,
and bit 6 of the same register has to switch the cartridge off.

COMAL 80 (CRT type 21) is four 16K banks selected by writing to $DE00, and the
ROM always sets bit 7: it writes $80, $82 and $83, never 0 to 3. The FPGA takes
all eight bits of that write as the bank number, so on a product with 4 MB of
cartridge memory $80 addresses offset 2 MB. The firmware used to mirror a
small image only up to 1 MB, so everything the ROM switched to there read $FF
and COMAL 80 started to a blank screen (GideonZ/1541ultimate#899).

On the Commodore (black) cartridge, bit 6 drives GAME and EXROM, so a write
with bit 6 set switches the cartridge off. COMAL 80 writes $41 before it reads
each glyph of `plottext` from the character ROM; when the cartridge stayed on,
the glyphs came from RAM and the text was garbled
(GideonZ/ultimate_releases#49). The older grey cartridge uses bit 6 for GAME
only; VICE marks it with CRT subtype 1, and the firmware keeps the cartridge
on for that subtype.

The suite does not ship a ROM image. It builds four-bank type 21 CRTs here:
bank 0 autostarts, copies a small routine to $C000 and runs it from RAM. The
routine stores $EE in the RAM under $9FF0, then selects banks 3 to 0 with a
given value in the upper bits and copies one marker byte from ROML and one
from ROMH of each selection to screen RAM. Each bank carries its own markers,
so the eight bytes read back say which bank every selection reached, and $EE
in the ROML row says the cartridge was off.

The bit 6 check needs the cartridge logic in the FPGA image, and the Ultimate
64 images ship prebuilt, so that check is gated on
`machine.COMAL80_CARTRIDGE_OFF_BIT` and reports SKIP on a machine the table
lists. `run-tests --assume-fix comal80-cartridge-off-bit` runs it there.
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
                    suite_fail, suite_ok, teardown_step)

SUITE = "comal80_cartridge_test"

BANKS = 4
ROML_MARKER = 0x1FF0        # $9FF0 in each bank
ROMH_MARKER = 0x3FF0        # $BFF0 in each bank
ROML_ROW = 0x0400           # screen RAM row 0
ROMH_ROW = 0x0428           # screen RAM row 1
DONE = 0x0450               # screen RAM row 2, set to $01 when the routine ran
BLANK = 0xA0                # not a marker, RAM_MARK or $01, so a byte the routine never wrote cannot pass
RAM_MARK = 0xEE             # stored in the RAM under $9FF0; read back when the cartridge is off

BLACK = 0                   # CRT subtype of the Commodore cartridge
GREY = 1                    # CRT subtype of the older grey cartridge (VICE)

BANK_SELECT = 0x80          # bit 7, as COMAL 80 writes it
CARTRIDGE_OFF = 0xC0        # bit 7 and bit 6


def routine(upper_bits: int) -> bytes:
    """The code copied to $C000: select banks 3 to 0 with `upper_bits` set."""
    return bytes([
        0xA9, RAM_MARK,         # C000  LDA #$EE
        0x8D, 0xF0, 0x9F,       # C002  STA $9FF0        RAM under ROML
        0xA2, 0x03,             # C005  LDX #$03
        0x8A,                   # C007  TXA
        0x09, upper_bits,       # C008  ORA #upper_bits
        0x8D, 0x00, 0xDE,       # C00A  STA $DE00
        0xAD, 0xF0, 0x9F,       # C00D  LDA $9FF0        ROML marker, or RAM
        0x9D, 0x00, 0x04,       # C010  STA $0400,X
        0xAD, 0xF0, 0xBF,       # C013  LDA $BFF0        ROMH marker, or BASIC ROM
        0x9D, 0x28, 0x04,       # C016  STA $0428,X
        0xCA,                   # C019  DEX
        0x10, 0xEB,             # C01A  BPL $C007
        0xA9, 0x01,             # C01C  LDA #$01
        0x8D, 0x50, 0x04,       # C01E  STA $0450        done
        0x4C, 0x21, 0xC0,       # C021  JMP $C021
    ])


# Bank 0 at $8000: the CBM80 autostart header, then a loop that copies the
# routine to $C000 and jumps to it. It has to run from RAM, because the first
# $DE00 write replaces the ROM it would otherwise be executing.
def boot(length: int) -> bytes:
    return bytes([
        0x09, 0x80, 0x09, 0x80,             # $8000 cold and warm start vectors: $8009
        0xC3, 0xC2, 0xCD, 0x38, 0x30,       # $8004 "CBM80"
        0x78,                               # $8009 SEI
        0xA2, length - 1,                   # $800A LDX #len-1
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


def comal80_crt(upper_bits: int, subtype: int) -> bytes:
    """A type 21 CRT of four 16K banks, each with its own marker bytes."""
    code = routine(upper_bits)
    header = bytearray(0x40)
    header[0:16] = b"C64 CARTRIDGE   "
    header[0x10:0x14] = (0x40).to_bytes(4, "big")
    header[0x14:0x16] = b"\x01\x00"
    header[0x16:0x18] = (21).to_bytes(2, "big")     # COMAL 80
    header[0x1A] = subtype                          # EXROM and GAME stay 0: 16K
    header[0x20:0x28] = b"COMAL-80"
    image = bytes(header)
    for bank in range(BANKS):
        rom = bytearray(b"\xff" * 0x4000)
        if bank == 0:
            start = boot(len(code))
            rom[0:len(start)] = start
            rom[ROUTINE_OFFSET:ROUTINE_OFFSET + len(code)] = code
        rom[ROML_MARKER] = roml_marker(bank)
        rom[ROMH_MARKER] = romh_marker(bank)
        chip = b"CHIP" + (0x4010).to_bytes(4, "big") + (0).to_bytes(2, "big")
        chip += bank.to_bytes(2, "big") + (0x8000).to_bytes(2, "big") + (0x4000).to_bytes(2, "big")
        image += chip + bytes(rom)
    return image


def run_routine(device: UltimateApi, upper_bits: int, subtype: int) -> tuple[bytes, bytes]:
    """Start a generated CRT and return the ROML and ROMH rows its routine wrote."""
    # Blank both rows and the done flag first, so bytes left by an earlier run cannot pass.
    device.machine.writemem(ROML_ROW, bytes([BLANK]) * (DONE + 1 - ROML_ROW))
    device.runners.upload("run_crt", comal80_crt(upper_bits, subtype))
    deadline = time.monotonic() + 10.0
    while device.machine.readmem(DONE, 1)[0] != 0x01:
        if time.monotonic() >= deadline:
            raise Failure("the routine copied from bank 0 did not finish within 10 s")
        time.sleep(0.25)
    return device.machine.readmem(ROML_ROW, BANKS), device.machine.readmem(ROMH_ROW, BANKS)


def report_rows(upper_bits: int, seen: tuple[bytes, bytes], wanted: str) -> None:
    detail(f"selections ${upper_bits | 3:02X} to ${upper_bits:02X}: ROML row read "
           f"{seen[0].hex(' ')}, ROMH row read {seen[1].hex(' ')}; want {wanted}")


def run(args) -> None:
    device = UltimateApi(args.host, args.password or None, args.timeout)
    machine = identify_machine(args.host, args.password or None, args.timeout)
    markers = (bytes(roml_marker(b) for b in range(BANKS)),
               bytes(romh_marker(b) for b in range(BANKS)))
    markers_text = f"{markers[0].hex(' ')} and {markers[1].hex(' ')}"

    def bank_select() -> None:
        seen = run_routine(device, BANK_SELECT, BLACK)
        if seen != markers:
            report_rows(BANK_SELECT, seen, markers_text)
            raise Failure("a $DE00 write of $80 to $83 did not select banks 0 to 3")

    def cartridge_off() -> None:
        seen = run_routine(device, CARTRIDGE_OFF, BLACK)
        # BASIC ROM is at $BFF0 with the cartridge off; its byte is not a
        # marker, and the RAM under $9FF0 holds what the routine stored.
        if seen[0] != bytes([RAM_MARK]) * BANKS or set(seen[1]) & set(markers[1]):
            report_rows(CARTRIDGE_OFF, seen,
                        f"{RAM_MARK:02x} (RAM) in the ROML row and no marker in the ROMH row")
            raise Failure("a $DE00 write of $C0 to $C3 left the cartridge on")

    def grey_stays_on() -> None:
        seen = run_routine(device, CARTRIDGE_OFF, GREY)
        if seen != markers:
            report_rows(CARTRIDGE_OFF, seen, markers_text)
            raise Failure("a $DE00 write of $C0 to $C3 on a grey cartridge did not "
                          "select banks 0 to 3")

    cartridge_off_label = "a COMAL 80 $DE00 write with bit 6 set switches the cartridge off"
    checks = (
        ("COMAL 80 bank selections with bit 7 set reach banks 0 to 3", bank_select, None),
        (cartridge_off_label, cartridge_off, machine_lib.COMAL80_CARTRIDGE_OFF_BIT),
        ("a grey COMAL 80 CRT (subtype 1) keeps the cartridge on with bit 6 set",
         grey_stays_on, None),
    )
    # Every check runs, so one failure does not hide the result of the others.
    failures = []
    try:
        for label, body, fix in checks:
            if fix is not None and machine.skip_without_fix(fix, label):
                continue
            try:
                with check(label):
                    body()
            except Failure as exc:
                failures.append(exc)
        if failures:
            raise failures[0]
    finally:
        # A reboot removes the cartridge; a reset would boot straight back into it.
        teardown_step("restore the configured cartridge", device.machine.reboot)


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Check that a COMAL 80 cartridge reaches every bank it selects "
                    "and that bit 6 switches it off.")
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
