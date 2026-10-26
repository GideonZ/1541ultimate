#!/usr/bin/env python3
"""E2E: a Magic Desk Plus cartridge banks with seven bits, serves its store, and saves it.

Magic Desk Plus (GideonZ/1541ultimate#727) is a Magic Desk with one more bank
bit and a non-volatile store: 128 banks of 8K selected by $DE00 bits 0 to 6,
bit 7 switching the ROM off, and a 256 byte window at $DF00 onto either 128K of
battery-backed SRAM or an 8K/32K EEPROM. $DE01 picks the page inside the
window, $DE03 picks what the window looks at: bit 5 chooses SRAM over EEPROM
and bit 0 chooses which 64K half of the SRAM. Murder on the Mississippi
Remastered uses the format because the SRAM takes a note at a time, which is
what Flash cannot do.

The suite does not ship a ROM image. It builds a 128-bank type 87 CRT here —
the id VICE assigns to Magic Desk Plus — and the store as two raw images beside
it, the way VICE keeps them: mdplus_e2e.crt goes with mdplus_e2e.sram and
mdplus_e2e.eeprom. All three go to /Temp over FTP and the cartridge is started
from there, so the firmware finds the store files by the CRT's name. Bank 0
autostarts, copies a routine to $C000 and runs it from RAM, because the first
$DE00 write replaces the ROM it would otherwise be executing. The routine then:

- selects all 128 banks in turn and copies each one's marker at $9FF0 to
  screen RAM, so the row read back says which bank every selection reached
- writes $80 to $DE00 and reads $9FF0 again, which has to come from the RAM
  under the cartridge once the ROM is off
- points the window at seven places and copies the first two bytes of each to
  screen RAM. Every page of the store carries its own page number in byte 0
  and a tag for its area in byte 1, so those two bytes say which page of which
  area the window reached. One of the seven asks for page $85 of a 32K EEPROM,
  which has 128 pages: the page register is masked to $7F there, so it has to
  arrive at page $05. The last selects the EEPROM with bit 0 of $DE03 still
  set from the SRAM's second half: bit 0 picks the SRAM half only, so it has
  to reach the same EEPROM page as with bit 0 clear
- writes a byte into the window, reads it back, and reads the same page of the
  other SRAM half to show the write did not land in both
- does a window read and a window write with the ROM switched off, which is
  what the cartridge does on hardware and what its file system relies on

Then it saves the cartridge through the task menu's "Save Cartridge" into
/Temp and fetches what was written: a CRT with the ROM banks alone, and beside
it the SRAM with exactly the two bytes the routine wrote changed, and the
EEPROM as it was loaded.

Two more things about saving follow. A save under a name whose store file is
already there without its CRT has to ask first, and answered No it has to
leave that file alone. And once the machine is rebooted into no cartridge,
"Save Cartridge" must be gone from the task menu: the store lived in the
memory the REU uses, and a save then would write the REU's contents as the
game's. The configured cartridge is set to none for that and put back after.

The mapper is cartridge logic in the FPGA image rather than firmware, and no
shipped image has it, so the suite is gated on
`machine.MAGIC_DESK_PLUS_MAPPER` and reports SKIP until the table says a
machine has it. `run-tests --assume-fix magic-desk-plus-mapper` runs it on a
machine whose image was built from this branch.
"""

from __future__ import annotations

import argparse
import os
import sys
import time
from pathlib import Path

# The one stanza that puts the shared library on sys.path; see tests/lib/bootstrap.py.
sys.path.insert(0, str(next(p for p in Path(__file__).resolve().parents
                            if (p / "tests" / "lib").is_dir()) / "tests" / "lib"))
import bootstrap  # noqa: E402,F401

import cli                                                      # noqa: E402
import ftp as ftp_lib                                           # noqa: E402
import machine as machine_lib                                   # noqa: E402
from api import UltimateApi, identify_machine                   # noqa: E402
from report import (Failure, check, detail, format_exception,   # noqa: E402
                    note_assumed_fix, suite_fail, suite_ok, suite_skip,
                    teardown_step)
from ui_backend import add_mode_argument, make_browser          # noqa: E402

SUITE = "magicdesk_plus_cartridge_test"

BANKS = 128                 # $DE00 bits 0 to 6
MARKER = 0x1FF0             # $9FF0 in each bank
ROM_DISABLE = 0x80          # $DE00 bit 7

# The store, as the cartridge logic addresses it: the EEPROM first and the
# 128K of SRAM above it, both in the memory the REU uses.
EEPROM_SIZE = 0x8000        # 32K, so the page register is masked to $7F
EEPROM_PAGES = EEPROM_SIZE // 0x100
SRAM_SIZE = 0x20000         # 128K, in address order: half 0, then half 1
PAGES_PER_HALF = 0x100      # the page register is eight bits

# The cartridge and its store, loaded from /Temp, and the name it is saved as.
DIRECTORY = "/Temp"
LOADED = "mdplus_e2e"
SAVED = "mdplus_saved"
ORPHAN = "mdplus_orphan"     # a store file whose CRT is not there
ORPHAN_BYTES = b"\x42" * 0x1000
CART_CATEGORY = "C64 and Cartridge Settings"
CART_ITEM = "Cartridge"
EXTENSIONS = (".crt", ".sram", ".eeprom")
SAVE_TIMEOUT_SECONDS = 20.0

ENTRY_ROWS = range(2, 24)
STATUS_ROW = 24
TELNET_ENTRY_ROWS = range(2, 23)
TELNET_STATUS_ROW = 23

EEPROM_TAG = 0xEE           # byte 1 of every page says which area it belongs to
SRAM_TAG = (0x50, 0x51)     # SRAM half 0 and half 1

SRAM = 0x20                 # $DE03 bit 5: the window looks at the SRAM
HALF1 = 0x01                # $DE03 bit 0: the second 64K of it

# Where the routine leaves its answers, all of them in screen RAM.
BANK_ROW = 0x0400           # 128 bytes: the marker each bank selection reached
PAGE_ROW = 0x0480           # seven bytes: the page number each probe read
TAG_ROW = 0x0490            # seven bytes: the area tag each probe read
PROBE_ROW = 0x04A0          # five bytes: the write and ROM-off probes
DONE = 0x04F0               # set to $01 when the routine has run
BLANK = 0xA0                # no marker, tag or probe result is this byte

RAM_MARK = 0x33             # stored in the RAM under $9FF0, read back with the ROM off
WINDOW_WRITE = 0xA5         # written into the window with the ROM on
ROM_OFF_WRITE = 0x5A        # written into the window with the ROM off
PROBE_PAGE = 0x10           # the page the write probes use
ROM_OFF_PAGE = 0x05         # the page the ROM-off probes use

# What the seven window probes ask for: the $DE03 value, the page, and the page
# and tag they have to arrive at. The second asks a 32K EEPROM for page $85;
# the last goes from SRAM half 1 to the EEPROM by clearing only bit 5.
PROBES = (
    ("the EEPROM at page $05", 0x00, 0x05, 0x05, EEPROM_TAG),
    ("the EEPROM at page $85, masked to $05", 0x00, 0x85, 0x05, EEPROM_TAG),
    ("SRAM half 0 at page $05", SRAM, 0x05, 0x05, SRAM_TAG[0]),
    ("SRAM half 0 at page $85", SRAM, 0x85, 0x85, SRAM_TAG[0]),
    ("SRAM half 1 at page $05", SRAM | HALF1, 0x05, 0x05, SRAM_TAG[1]),
    ("SRAM half 1 at page $85", SRAM | HALF1, 0x85, 0x85, SRAM_TAG[1]),
    ("the EEPROM at page $05 with bit 0 set", HALF1, 0x05, 0x05, EEPROM_TAG),
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
    """A type 87 CRT of 128 8K banks; revision 0 says SRAM and a 32K EEPROM."""
    header = bytearray(0x40)
    header[0:16] = b"C64 CARTRIDGE   "
    header[0x10:0x14] = (0x40).to_bytes(4, "big")
    header[0x14:0x16] = b"\x01\x00"
    header[0x16:0x18] = (87).to_bytes(2, "big")     # Magic Desk Plus, the id VICE assigns
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
    return image


def sram_image() -> bytes:
    """The SRAM file: half 0 and then half 1, every page tagged."""
    return b"".join(store_page(SRAM_TAG[half], page)
                    for half in (0, 1) for page in range(PAGES_PER_HALF))


def eeprom_image() -> bytes:
    """The EEPROM file, 32K, every page tagged."""
    return b"".join(store_page(EEPROM_TAG, page) for page in range(EEPROM_PAGES))


def sram_after_run() -> bytes:
    """The SRAM as the routine leaves it: two bytes of half 0 written."""
    sram = bytearray(sram_image())
    sram[PROBE_PAGE * 0x100 + 0x80] = WINDOW_WRITE
    sram[ROM_OFF_PAGE * 0x100 + 0x81] = ROM_OFF_WRITE
    return bytes(sram)


def first_difference(got: bytes, want: bytes) -> str:
    if len(got) != len(want):
        return f"{len(got)} bytes, want {len(want)}"
    at = next(i for i in range(len(want)) if got[i] != want[i])
    return f"offset ${at:05X} holds ${got[at]:02X}, want ${want[at]:02X}"


def chunk_loads(image: bytes) -> list[int]:
    """The load address of every CHIP chunk in a CRT."""
    loads = []
    offset = int.from_bytes(image[0x10:0x14], "big")
    while offset + 0x10 <= len(image):
        length = int.from_bytes(image[offset + 4:offset + 8], "big")
        loads.append(int.from_bytes(image[offset + 0x0C:offset + 0x0E], "big"))
        offset += max(length, 0x10)
    return loads


def store_files(name: str) -> list[str]:
    return [f"{DIRECTORY}/{name}{ext}" for ext in EXTENSIONS]


def remove_files(args) -> None:
    with ftp_lib.session(args.host, args.password or None, timeout=20) as client:
        for path in store_files(LOADED) + store_files(SAVED) + store_files(ORPHAN):
            ftp_lib.delete_quietly(client, path)


def listing(args) -> list[str]:
    with ftp_lib.session(args.host, args.password or None, timeout=20) as client:
        return [name.rsplit("/", 1)[-1] for name in client.nlst(DIRECTORY)]


def close_menu(device) -> None:
    """Close the menu with its button, not a key: a cartridge in a real C64
    takes no injected keys."""
    if device.machine.menu_open():
        device.machine.menu_button()


def task_items(browser) -> list[str]:
    """The entries of the machine's task menu category, which is left open."""
    browser.backend.ensure_ready()
    browser.go_to_directory(DIRECTORY.strip("/"))
    categories = browser.open_task_menu()
    if not categories:
        raise Failure(f"no task menu appeared; screen was:\n{browser.screen()}")
    before = browser.rows()
    browser.choose_overlay_item(categories, browser.backend.machine.machine_task_category)
    return browser.wait_for_overlay(before)


def save_over_orphan(browser, args) -> None:
    """Save as a name whose .sram is there without its CRT, and answer No."""
    with ftp_lib.session(args.host, args.password or None, timeout=20) as client:
        ftp_lib.store(client, f"{DIRECTORY}/{ORPHAN}.sram", ORPHAN_BYTES)
    browser.backend.ensure_ready()
    browser.go_to_directory(DIRECTORY.strip("/"))
    browser.invoke_task_action(browser.backend.machine.machine_task_category, "Save Cartridge")
    browser.wait_for_text("Save Cartridge as..")
    browser.fill_edit_field(ORPHAN)
    try:
        browser.wait_for_text("Store files exist", timeout=SAVE_TIMEOUT_SECONDS / 2)
    except Failure:
        time.sleep(2)       # let a save that did not ask finish, so the listing shows it
        names = listing(args)
        raise Failure(f"no question before replacing {ORPHAN}.sram; {DIRECTORY} now holds "
                      f"{sorted(n for n in names if n.startswith(ORPHAN))}") from None
    browser.press_popup_button("n")
    time.sleep(2)
    with ftp_lib.session(args.host, args.password or None, timeout=20) as client:
        kept = ftp_lib.retrieve(client, f"{DIRECTORY}/{ORPHAN}.sram")
    names = listing(args)
    if kept != ORPHAN_BYTES:
        raise Failure(f"answered No, {ORPHAN}.sram was replaced anyway ({len(kept)} bytes)")
    if f"{ORPHAN}.crt" in names:
        raise Failure(f"answered No, {ORPHAN}.crt was written anyway")


def save_through_menu(browser) -> None:
    """Save Cartridge into /Temp; it shows no popup when it succeeds."""
    browser.backend.ensure_ready()
    browser.go_to_directory(DIRECTORY.strip("/"))
    browser.invoke_task_action(browser.backend.machine.machine_task_category, "Save Cartridge")
    browser.wait_for_text("Save Cartridge as..")
    browser.fill_edit_field(SAVED)


def fetch_saved(args) -> dict[str, bytes]:
    """The three saved files, once the last of them is complete."""
    crt, sram, eeprom = store_files(SAVED)
    deadline = time.monotonic() + SAVE_TIMEOUT_SECONDS
    while True:
        try:
            with ftp_lib.session(args.host, args.password or None, timeout=20) as client:
                files = {path: ftp_lib.retrieve(client, path) for path in (crt, sram, eeprom)}
            if len(files[eeprom]) == EEPROM_SIZE:
                return files
        except Exception as exc:        # noqa: BLE001
            if time.monotonic() >= deadline:
                raise Failure(f"the saved cartridge did not appear in {DIRECTORY}: {exc}") from exc
        if time.monotonic() >= deadline:
            raise Failure(f"{eeprom} is incomplete after {SAVE_TIMEOUT_SECONDS:.0f} s")
        time.sleep(0.5)


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

    browser = None
    configured = ""
    crt = magicdesk_plus_crt()
    try:
        with check("the generated Magic Desk Plus cartridge starts from /Temp with its "
                   "store files beside it, and runs its routine"):
            remove_files(args)
            loaded_crt, loaded_sram, loaded_eeprom = store_files(LOADED)
            with ftp_lib.session(args.host, args.password or None, timeout=20) as client:
                ftp_lib.store(client, loaded_crt, crt)
                ftp_lib.store(client, loaded_sram, sram_image())
                ftp_lib.store(client, loaded_eeprom, eeprom_image())
            # Blank every row first, so bytes left by an earlier run cannot pass.
            device.machine.writemem(BANK_ROW, bytes([BLANK]) * (DONE + 1 - BANK_ROW))
            device.runners.run_crt(loaded_crt)
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

        browser = make_browser(
            args.mode, args.host, args.password or None, args.timeout,
            entry_rows=ENTRY_ROWS, status_row=STATUS_ROW,
            telnet_port=args.telnet_port,
            telnet_entry_rows=TELNET_ENTRY_ROWS, telnet_status_row=TELNET_STATUS_ROW,
        )
        with check(f"Save Cartridge through the {args.mode} task menu writes the CRT "
                   "and its store files"):
            save_through_menu(browser)
            saved = fetch_saved(args)
        saved_crt, saved_sram, saved_eeprom = (saved[path] for path in store_files(SAVED))

        def crt_is_rom_only() -> None:
            if saved_crt != crt:
                stray = sorted({f"${load:04X}" for load in chunk_loads(saved_crt)} - {"$8000"})
                detail(f"chunks at {', '.join(stray) or 'only $8000'}")
                raise Failure(f"the saved CRT is not the ROM image it was loaded from: "
                              f"{first_difference(saved_crt, crt)}")

        def sram_saved() -> None:
            if saved_sram != sram_after_run():
                raise Failure(f"the saved SRAM is not the loaded one with the routine's two "
                              f"writes: {first_difference(saved_sram, sram_after_run())}")

        def eeprom_saved() -> None:
            if saved_eeprom != eeprom_image():
                raise Failure(f"the saved EEPROM is not the loaded one: "
                              f"{first_difference(saved_eeprom, eeprom_image())}")

        record("the saved CRT carries the ROM banks alone, no store", crt_is_rom_only)
        record("the saved SRAM file holds what the cartridge wrote", sram_saved)
        record("the saved EEPROM file holds the EEPROM as loaded", eeprom_saved)
        record("a save over a store file without its CRT asks first, and No leaves it alone",
               lambda: save_over_orphan(browser, args))

        def nothing_to_save_after_reboot() -> None:
            nonlocal configured
            configured = str(device.configs.current(CART_CATEGORY, CART_ITEM))
            if configured:
                device.configs.set(CART_CATEGORY, CART_ITEM, "")
            close_menu(device)
            device.machine.reboot()
            time.sleep(3)
            items = task_items(browser)
            if "Save Cartridge" in items:
                raise Failure(f"after a reboot into no cartridge the task menu still offers "
                              f"Save Cartridge: {items}")

        record("after a reboot into no cartridge there is nothing left to save",
               nothing_to_save_after_reboot)
        if failures:
            raise failures[0]
    finally:
        teardown_step("close the menu", lambda: close_menu(device))
        if browser is not None:
            teardown_step("close the browser session", browser.close)
        teardown_step("remove the cartridge files", lambda: remove_files(args))
        if configured:
            teardown_step("put the configured cartridge back",
                          lambda: device.configs.set(CART_CATEGORY, CART_ITEM, configured))
        # A reboot removes the cartridge; a reset would boot straight back into it.
        teardown_step("restore the configured cartridge", device.machine.reboot)
    return True


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Check that a Magic Desk Plus cartridge banks with seven bits, "
                    "serves its store through the $DF00 window, and saves it beside "
                    "its CRT.")
    cli.add_device_arguments(parser)
    parser.add_argument("--telnet-port", type=int,
                        default=int(os.environ.get("U64_TELNET_PORT", "23")))
    add_mode_argument(parser)
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
