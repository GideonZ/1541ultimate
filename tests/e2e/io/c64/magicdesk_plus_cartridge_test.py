#!/usr/bin/env python3
"""E2E: a Magic Desk Plus cartridge banks with seven bits and serves its store.

Magic Desk Plus (GideonZ/1541ultimate#727) is a Magic Desk with one more bank
bit and a non-volatile store: 128 banks of 8K selected by $DE00 bits 0 to 6,
bit 7 switching the ROM off, and a 256 byte window at $DF00 onto either 128K of
battery-backed SRAM or an 8K/32K EEPROM. $DE01 picks the page inside the
window, $DE03 picks what the window looks at: bit 5 chooses SRAM over EEPROM
and bit 0 chooses which 64K half of the SRAM. Murder on the Mississippi
Remastered uses the format because the SRAM takes a note at a time, which is
what Flash cannot do.

The suite does not ship a ROM image. It builds a 128-bank type 19 CRT here,
with its store in CHIP chunks at $DF00 — the address of the window, since the
bank field is what says which piece a chunk is. Bank 0 autostarts, copies a
routine to $C000 and runs it from RAM, because the first $DE00 write replaces
the ROM it would otherwise be executing. The routine then:

- selects all 128 banks in turn and copies each one's marker at $9FF0 to
  screen RAM, so the row read back says which bank every selection reached
- writes $80 to $DE00 and reads $9FF0 again, which has to come from the RAM
  under the cartridge once the ROM is off
- points the window at six places and copies the first two bytes of each to
  screen RAM. Every page of the store carries its own page number in byte 0
  and a tag for its area in byte 1, so those two bytes say which page of which
  area the window reached. One of the six asks for page $85 of a 32K EEPROM,
  which has 128 pages: the page register is masked to $7F there, so it has to
  arrive at page $05
- writes a byte into the window, reads it back, and reads the same page of the
  other SRAM half to show the write did not land in both
- does a window read and a window write with the ROM switched off, which is
  what the cartridge does on hardware and what its file system relies on

The mapper is cartridge logic in the FPGA image rather than firmware, and no
shipped image has it, so the suite is gated on
`machine.MAGIC_DESK_PLUS_MAPPER` and reports SKIP until the table says a
machine has it. `run-tests --assume-fix magic-desk-plus-mapper` runs it on a
machine whose image was built from this branch.
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

SUITE = "magicdesk_plus_cartridge_test"

BANKS = 128                 # $DE00 bits 0 to 6
MARKER = 0x1FF0             # $9FF0 in each bank
ROM_DISABLE = 0x80          # $DE00 bit 7

# The store, as the cartridge logic addresses it: the EEPROM first and the
# 128K of SRAM above it, both in the memory the REU uses.
EEPROM_SIZE = 0x8000        # 32K, so the page register is masked to $7F
EEPROM_PAGES = EEPROM_SIZE // 0x100
SRAM_CHUNK = 0x8000         # the size of one CHIP chunk of SRAM
SRAM_CHUNKS = 4             # 128K in four quarters, in address order
PAGES_PER_HALF = 0x100      # the page register is eight bits

EEPROM_TAG = 0xEE           # byte 1 of every page says which area it belongs to
SRAM_TAG = (0x50, 0x51)     # SRAM half 0 and half 1

SRAM = 0x20                 # $DE03 bit 5: the window looks at the SRAM
HALF1 = 0x01                # $DE03 bit 0: the second 64K of it

# Where the routine leaves its answers, all of them in screen RAM.
BANK_ROW = 0x0400           # 128 bytes: the marker each bank selection reached
PAGE_ROW = 0x0480           # six bytes: the page number each probe read
TAG_ROW = 0x0490            # six bytes: the area tag each probe read
PROBE_ROW = 0x04A0          # five bytes: the write and ROM-off probes
DONE = 0x04F0               # set to $01 when the routine has run
BLANK = 0xA0                # no marker, tag or probe result is this byte

RAM_MARK = 0x33             # stored in the RAM under $9FF0, read back with the ROM off
WINDOW_WRITE = 0xA5         # written into the window with the ROM on
ROM_OFF_WRITE = 0x5A        # written into the window with the ROM off
PROBE_PAGE = 0x10           # the page the write probes use
ROM_OFF_PAGE = 0x05         # the page the ROM-off probes use

# What the six window probes ask for: the $DE03 value, the page, and the page
# and tag they have to arrive at. The second asks a 32K EEPROM for page $85.
PROBES = (
    ("the EEPROM at page $05", 0x00, 0x05, 0x05, EEPROM_TAG),
    ("the EEPROM at page $85, masked to $05", 0x00, 0x85, 0x05, EEPROM_TAG),
    ("SRAM half 0 at page $05", SRAM, 0x05, 0x05, SRAM_TAG[0]),
    ("SRAM half 0 at page $85", SRAM, 0x85, 0x85, SRAM_TAG[0]),
    ("SRAM half 1 at page $05", SRAM | HALF1, 0x05, 0x05, SRAM_TAG[1]),
    ("SRAM half 1 at page $85", SRAM | HALF1, 0x85, 0x85, SRAM_TAG[1]),
)

# What the five probes after them have to read, in order.
PROBE_WANTED = (
    ("a byte written into the window reads back", WINDOW_WRITE),
    ("the same page of the other SRAM half is untouched", SRAM_TAG[1]),
    ("the window still reads with the ROM off", ROM_OFF_PAGE),
    ("the window still takes a write with the ROM off", ROM_OFF_WRITE),
    ("$9FF0 reads the RAM under the cartridge with the ROM off", RAM_MARK),
)


def abs_store(address: int) -> bytes:
    return bytes([address & 0xFF, address >> 8])


def select_bank_and_read_markers() -> bytes:
    """Select banks $7F down to $00 and copy each one's marker to screen RAM."""
    return bytes([
        0xA9, RAM_MARK,         # LDA #RAM_MARK
        0x8D, 0xF0, 0x9F,       # STA $9FF0        the RAM under the ROM
        0xA2, 0x7F,             # LDX #$7F
        0x8A,                   # TXA              bank X, bit 7 clear: ROM on
        0x8D, 0x00, 0xDE,       # STA $DE00
        0xAD, 0xF0, 0x9F,       # LDA $9FF0        that bank's marker
        0x9D, 0x00, 0x04,       # STA $0400,X
        0xCA,                   # DEX
        0x10, 0xF3,             # BPL back to the TXA
    ])


def probe(ctrl: int, page: int, index: int) -> bytes:
    """Point the window at one page of one area and copy its first two bytes."""
    return bytes([
        0xA9, ctrl,             # LDA #ctrl
        0x8D, 0x03, 0xDE,       # STA $DE03        SRAM or EEPROM, and which half
        0xA9, page,             # LDA #page
        0x8D, 0x01, 0xDE,       # STA $DE01        the page inside it
        0xAD, 0x00, 0xDF,       # LDA $DF00        byte 0: the page number
    ]) + bytes([0x8D]) + abs_store(PAGE_ROW + index) + bytes([
        0xAD, 0x01, 0xDF,       # LDA $DF01        byte 1: the area tag
    ]) + bytes([0x8D]) + abs_store(TAG_ROW + index)


def write_probes() -> bytes:
    """Write into the window, read it back, and read the other half's page."""
    return bytes([
        0xA9, SRAM,             # LDA #$20
        0x8D, 0x03, 0xDE,       # STA $DE03        SRAM, half 0
        0xA9, PROBE_PAGE,       # LDA #$10
        0x8D, 0x01, 0xDE,       # STA $DE01
        0xA9, WINDOW_WRITE,     # LDA #$A5
        0x8D, 0x80, 0xDF,       # STA $DF80
        0xAD, 0x80, 0xDF,       # LDA $DF80
    ]) + bytes([0x8D]) + abs_store(PROBE_ROW + 0) + bytes([
        0xA9, SRAM | HALF1,     # LDA #$21
        0x8D, 0x03, 0xDE,       # STA $DE03        the same page, the other half
        0xAD, 0x80, 0xDF,       # LDA $DF80
    ]) + bytes([0x8D]) + abs_store(PROBE_ROW + 1)


def rom_off_probes() -> bytes:
    """Switch the ROM off, then read and write the window and read $9FF0."""
    return bytes([
        0xA9, ROM_DISABLE,      # LDA #$80
        0x8D, 0x00, 0xDE,       # STA $DE00        ROM off, bank 0
        0xA9, SRAM,             # LDA #$20
        0x8D, 0x03, 0xDE,       # STA $DE03
        0xA9, ROM_OFF_PAGE,     # LDA #$05
        0x8D, 0x01, 0xDE,       # STA $DE01
        0xAD, 0x00, 0xDF,       # LDA $DF00        still served
    ]) + bytes([0x8D]) + abs_store(PROBE_ROW + 2) + bytes([
        0xA9, ROM_OFF_WRITE,    # LDA #$5A
        0x8D, 0x81, 0xDF,       # STA $DF81        still writable
        0xAD, 0x81, 0xDF,       # LDA $DF81
    ]) + bytes([0x8D]) + abs_store(PROBE_ROW + 3) + bytes([
        0xAD, 0xF0, 0x9F,       # LDA $9FF0        the RAM under the cartridge
    ]) + bytes([0x8D]) + abs_store(PROBE_ROW + 4)


def routine() -> bytes:
    """The code copied to $C000, and a halt at the end of it."""
    body = select_bank_and_read_markers()
    for index, (_, ctrl, page, _, _) in enumerate(PROBES):
        body += probe(ctrl, page, index)
    body += write_probes() + rom_off_probes()
    halt = 0xC000 + len(body) + 5
    code = body + bytes([0xA9, 0x01]) + bytes([0x8D]) + abs_store(DONE) \
        + bytes([0x4C]) + abs_store(halt)
    # The boot code copies one page, which is all this needs and all it may be.
    assert len(code) <= 0x100, f"the routine is {len(code)} bytes, one page at most"
    return code


ROUTINE_OFFSET = 0x100      # the routine sits at $8100, so the copy is page aligned

# Bank 0 at $8000: the CBM80 autostart header, then a loop that copies the
# routine to $C000 and jumps to it.
BOOT = bytes([
    0x09, 0x80, 0x09, 0x80,             # $8000 cold and warm start vectors: $8009
    0xC3, 0xC2, 0xCD, 0x38, 0x30,       # $8004 "CBM80"
    0x78,                               # $8009 SEI
    0xA2, 0x00,                         # $800A LDX #$00
    0xBD, 0x00, 0x81,                   # $800C LDA $8100,X
    0x9D, 0x00, 0xC0,                   # $800F STA $C000,X
    0xE8,                               # $8012 INX
    0xD0, 0xF7,                         # $8013 BNE $800C
    0x4C, 0x00, 0xC0,                   # $8015 JMP $C000
])


def store_page(tag: int, page: int) -> bytes:
    """One page of the store: its page number, its area tag, then the tag."""
    return bytes([page, tag]) + bytes([tag]) * 0xFE


def chip(bank: int, load: int, data: bytes) -> bytes:
    return (b"CHIP" + (len(data) + 0x10).to_bytes(4, "big") + (0).to_bytes(2, "big")
            + bank.to_bytes(2, "big") + load.to_bytes(2, "big")
            + len(data).to_bytes(2, "big") + data)


def magicdesk_plus_crt() -> bytes:
    """A type 19 CRT of 128 8K banks, a 32K EEPROM and 128K of SRAM."""
    header = bytearray(0x40)
    header[0:16] = b"C64 CARTRIDGE   "
    header[0x10:0x14] = (0x40).to_bytes(4, "big")
    header[0x14:0x16] = b"\x01\x00"
    header[0x16:0x18] = (19).to_bytes(2, "big")     # Magic Desk, Domark, HES Australia
    header[0x18] = 0                                # EXROM low
    header[0x19] = 1                                # GAME high: 8K mode
    header[0x20:0x20 + 11] = b"MDPLUS-TEST"
    image = bytes(header)

    code = routine()
    for bank in range(BANKS):
        rom = bytearray(b"\xff" * 0x2000)
        if bank == 0:
            rom[0:len(BOOT)] = BOOT
            rom[ROUTINE_OFFSET:ROUTINE_OFFSET + len(code)] = code
        rom[MARKER] = bank
        image += chip(bank, 0x8000, bytes(rom))

    # Bank 0 of the store is the EEPROM; banks 1 to 4 are the SRAM in address
    # order, which is half 0 and then half 1, each in two quarters.
    eeprom = b"".join(store_page(EEPROM_TAG, page) for page in range(EEPROM_PAGES))
    image += chip(0, 0xDF00, eeprom)
    sram = b"".join(store_page(SRAM_TAG[half], page)
                    for half in (0, 1) for page in range(PAGES_PER_HALF))
    for part in range(SRAM_CHUNKS):
        image += chip(part + 1, 0xDF00, sram[part * SRAM_CHUNK:(part + 1) * SRAM_CHUNK])
    return image


def report_banks(seen: bytes) -> None:
    wrong = [bank for bank in range(BANKS) if seen[bank] != bank]
    detail(f"{len(wrong)} of {BANKS} bank selections reached another bank")
    for start in range(0, BANKS, 16):
        detail(f"  banks {start:3d}-{start + 15:3d} read markers "
               f"{seen[start:start + 16].hex(' ')}")


def run(args) -> bool:
    """Run the checks; False when the machine's FPGA image lacks the mapper."""
    device = UltimateApi(args.host, args.password or None, args.timeout)
    machine = identify_machine(args.host, args.password or None, args.timeout)
    absent = machine.missing_fix(machine_lib.MAGIC_DESK_PLUS_MAPPER)
    if absent:
        suite_skip(SUITE, absent)
        return False
    if machine.assumed_fix(machine_lib.MAGIC_DESK_PLUS_MAPPER):
        note_assumed_fix(machine_lib.MAGIC_DESK_PLUS_MAPPER, machine.kind)

    try:
        with check("the generated Magic Desk Plus cartridge starts and runs its routine"):
            # Blank every row first, so bytes left by an earlier run cannot pass.
            device.machine.writemem(BANK_ROW, bytes([BLANK]) * (DONE + 1 - BANK_ROW))
            device.runners.upload("run_crt", magicdesk_plus_crt())
            deadline = time.monotonic() + 20.0
            while device.machine.readmem(DONE, 1)[0] != 0x01:
                if time.monotonic() >= deadline:
                    raise Failure("the routine copied from bank 0 did not finish within 20 s")
                time.sleep(0.25)
            banks = device.machine.readmem(BANK_ROW, BANKS)
            pages = device.machine.readmem(PAGE_ROW, len(PROBES))
            tags = device.machine.readmem(TAG_ROW, len(PROBES))
            probes = device.machine.readmem(PROBE_ROW, len(PROBE_WANTED))

        # Every check runs, so one failure does not hide the result of the others.
        failures = []

        def record(label, body) -> None:
            try:
                with check(label):
                    body()
            except Failure as exc:
                failures.append(exc)

        def banks_reached() -> None:
            if banks != bytes(range(BANKS)):
                report_banks(banks)
                raise Failure("a $DE00 write of $00 to $7F did not select banks 0 to 127")

        record("Magic Desk Plus bank selections reach all 128 banks", banks_reached)

        for index, (what, ctrl, page, want_page, want_tag) in enumerate(PROBES):
            def window_reached(index=index, what=what, ctrl=ctrl, page=page,
                               want_page=want_page, want_tag=want_tag) -> None:
                if pages[index] != want_page or tags[index] != want_tag:
                    detail(f"$DE03=${ctrl:02X} $DE01=${page:02X} read page "
                           f"${pages[index]:02X} tag ${tags[index]:02X}, "
                           f"want page ${want_page:02X} tag ${want_tag:02X}")
                    raise Failure(f"the window did not reach {what}")
            record(f"the $DF00 window reaches {what}", window_reached)

        for index, (what, wanted) in enumerate(PROBE_WANTED):
            def probe_matches(index=index, what=what, wanted=wanted) -> None:
                if probes[index] != wanted:
                    detail(f"probe {index} read ${probes[index]:02X}, want ${wanted:02X}")
                    raise Failure(f"{what}: it did not")
            record(what, probe_matches)

        if failures:
            raise failures[0]
    finally:
        # A reboot removes the cartridge; a reset would boot straight back into it.
        teardown_step("restore the configured cartridge", device.machine.reboot)
    return True


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Check that a Magic Desk Plus cartridge banks with seven bits "
                    "and serves its store through the $DF00 window.")
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
