#!/usr/bin/env python3
# E2E: Verifies every PRG browser context-menu action on files and disk images.

"""Validate every PRG context-menu action in the real U64 browser.

The fixture is a tiny BASIC+ML program that stores a known signature at
$C000 and prints a line through CHROUT. It is seeded into `/Temp` three
times: as a plain FAT file, under a name far longer than the boot cart can
display, and as the only file inside a D64 image, so the same program can be
launched from both sides of the disk boundary.

For each of those two locations the test reads what the context menu offers
and then drives every single entry, failing if the browser grows an action
that nothing here covers. Browser input uses the shared transport facade,
while verification remains outside the browser - C64 memory and screen for
the loaders, FTP and the raw D64 directory for the file operations:

  Run          the program executed -> signature present and output on screen
  Load         the program is in RAM -> load image present, signature absent
  DMA          the program is in RAM afterwards and RUN starts it
  Mount & Run  D64 on drive A, program executed
  Real Run     D64 on drive A, real 1541 LOAD (no FILE NOT FOUND), executed
  View         a viewer opens over the browser and closes again
  Hex View     the hex dump shows the fixture's own bytes
  Copy to...   the copy lands in the picked directory, original untouched
  Move to...   the file lands in the picked directory and leaves its origin
  Rename       the new name replaces the old one
  Delete       the file is gone
"""

import argparse
import ftplib
import json
import os
import sys
import time
from pathlib import Path
from typing import Dict, List, Optional, Tuple

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "api"))
# tests/lib holds the reporting rules every suite shares; tests/e2e/lib
# holds the shared UI backend.
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "lib"))
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "lib"))

from menu_screen_test import Failure, MenuScreenInfo, RestSession, check
import ftp as ftp_lib
import machine as machine_lib
import pacing
from report import check_skip, detail, section, suite_fail, suite_ok
from ui_backend import Browser, TelnetBackend, add_mode_argument, make_browser, strip_frame


FTP_USER = "user"
FTP_DEFAULT_PASSWORD = "password"

TEMP_PATH = "/Temp/"
FIXTURE_PREFIX = "prgmenu"
# 16 characters: the longest name a CBM directory entry can hold. Together with
# the ".prg" the browser appends this is the longest name the boot-cart loader
# ever sees, which is exactly the case that used to overflow its name buffer.
CBM_FILE_NAME = "DMATESTPROGRAM01"
DISK_NAME = "DMATEST"
LONG_NAME_LENGTH = 100

SIGNATURE_ADDRESS = 0xC000
# "Could not obtain lock of subsystem" (software/infra/subsys.h). Transient by
# definition, so a read that meets it waits and asks again.
HTTP_LOCKED = 423
LOCK_RETRY_SECONDS = 5.0
SIGNATURE = b"U64PRGOK"
LOAD_ADDRESS = 0x0801
MESSAGE = "U64 PRG TEST OK"

# Shared with every suite; see tests/lib/pacing.py.
MENU_SETTLE_SECONDS = pacing.KEY_SETTLE_SECONDS
RUN_TIMEOUT_SECONDS = 12.0
REAL_RUN_TIMEOUT_SECONDS = 40.0
BOOT_TIMEOUT_SECONDS = 15.0
SCREEN_TIMEOUT_SECONDS = 6.0
EDIT_FIELD_CLEAR_TAPS = 64

HEX_VIEW_FIRST_LINE = "0000 01 08 0B 08"

PICKER_TITLE = "Select Path"
PICKER_SELECT_ENTRY = "<< Select Current Dir >>"

ENTRY_ROWS = range(2, 24)
STATUS_ROW = 24
TELNET_ENTRY_ROWS = range(2, 23)
TELNET_STATUS_ROW = 23

# Offered entries this test knowingly does not drive, with the reason why.
UNEXERCISED_ACTIONS = {
    "Run with App": "only offered when /Flash/apps/<ext>.prg is installed",
    # Confirmed live, consistently, on a D64-contained file's context menu
    # under --mode telnet only: REST/Overlay and REST/Freeze never offer it
    # for the same fixture and location.
    "Select": "offered only over telnet, for a file inside a D64; no REST/Freeze/Overlay equivalent seen",
}

# 10 SYS 2064 ; the machine code stores SIGNATURE at $C000 and prints MESSAGE.
PRG_BYTES = bytes(
    [
        0x01, 0x08,                    # load address $0801
        0x0B, 0x08,                    # link to $080B
        0x0A, 0x00,                    # line 10
        0x9E,                          # SYS token
        0x32, 0x30, 0x36, 0x34,        # "2064"
        0x00,                          # end of line
        0x00, 0x00,                    # end of program
        0x00, 0x00, 0x00,              # filler up to $0810
        0xA2, 0x07,                    # ldx #$07
        0xBD, 0x29, 0x08,              # lda signature,x
        0x9D, 0x00, 0xC0,              # sta $C000,x
        0xCA,                          # dex
        0x10, 0xF7,                    # bpl -9
        0xA2, 0x00,                    # ldx #$00
        0xBD, 0x31, 0x08,              # lda message,x
        0xF0, 0x06,                    # beq done
        0x20, 0xD2, 0xFF,              # jsr $ffd2
        0xE8,                          # inx
        0xD0, 0xF5,                    # bne -11
        0x60,                          # rts
    ]
) + SIGNATURE + MESSAGE.encode("ascii") + bytes([0x0D, 0x00])

SECTORS_PER_TRACK = [21] * 17 + [19] * 7 + [18] * 6 + [17] * 5
D64_SIZE = 174848
D64_DIR_TRACK = 18
D64_FILE_TRACK = 17
D64_FILE_SECTOR = 0


def sector_offset(track: int, sector: int) -> int:
    return (sum(SECTORS_PER_TRACK[: track - 1]) + sector) * 256


def cbm_padded(name: str, length: int = 16) -> bytes:
    return name.upper().encode("ascii")[:length].ljust(length, b"\xa0")


def build_d64(disk_name: str, file_name: str, payload: bytes) -> bytes:
    """A 35-track image holding exactly one single-sector PRG."""
    if len(payload) > 254:
        raise Failure("fixture payload must fit in a single D64 sector")
    image = bytearray(D64_SIZE)

    bam = sector_offset(D64_DIR_TRACK, 0)
    image[bam + 0x00] = D64_DIR_TRACK
    image[bam + 0x01] = 0x01
    image[bam + 0x02] = 0x41  # DOS version 'A'
    for track in range(1, 36):
        entry = bam + 0x04 + (track - 1) * 4
        free = SECTORS_PER_TRACK[track - 1]
        bits = (1 << free) - 1
        image[entry] = free
        image[entry + 1] = bits & 0xFF
        image[entry + 2] = (bits >> 8) & 0xFF
        image[entry + 3] = (bits >> 16) & 0xFF
    image[bam + 0x90 : bam + 0xA0] = cbm_padded(disk_name)
    image[bam + 0xA0 : bam + 0xA2] = b"\xa0\xa0"
    image[bam + 0xA2 : bam + 0xA4] = b"64"
    image[bam + 0xA4] = 0xA0
    image[bam + 0xA5 : bam + 0xA7] = b"2A"
    image[bam + 0xA7 : bam + 0xAB] = b"\xa0" * 4

    def allocate(track: int, sector: int) -> None:
        entry = bam + 0x04 + (track - 1) * 4
        image[entry] -= 1
        image[entry + 1 + (sector >> 3)] &= ~(1 << (sector & 7)) & 0xFF

    allocate(D64_DIR_TRACK, 0)
    allocate(D64_DIR_TRACK, 1)
    allocate(D64_FILE_TRACK, D64_FILE_SECTOR)

    directory = sector_offset(D64_DIR_TRACK, 1)
    image[directory + 0x00] = 0x00
    image[directory + 0x01] = 0xFF
    image[directory + 0x02] = 0x82  # closed PRG
    image[directory + 0x03] = D64_FILE_TRACK
    image[directory + 0x04] = D64_FILE_SECTOR
    image[directory + 0x05 : directory + 0x15] = cbm_padded(file_name)
    image[directory + 0x1E] = 0x01  # one block
    image[directory + 0x1F] = 0x00

    data = sector_offset(D64_FILE_TRACK, D64_FILE_SECTOR)
    image[data + 0x00] = 0x00
    image[data + 0x01] = len(payload) + 1
    image[data + 0x02 : data + 0x02 + len(payload)] = payload

    return bytes(image)


def screencode_to_ascii(code: int) -> str:
    value = code & 0x7F
    if value < 0x20:
        return chr(value + 0x40)
    if value < 0x40:
        return chr(value)
    return "."


def _at_plain_root(rows: List[str], path: str) -> bool:
    """The unobstructed root listing, with no overlay (menu, popup, viewer)
    drawn over any part of it.

    The path the browser reports has to be "/" itself: every directory this
    suite descends into is under /Temp, whose own status row still contains
    "Temp", so the entry check below matches inside /Temp just as it does at
    the root (confirmed live: the /Temp listing was read as the root).

    Every overlay this suite drives is boxed in "+"/"|" border characters
    the plain listing never uses on its own (the same signal
    assembly64_test.py's at_root_browser uses for the same purpose); "Temp"
    alone is not enough, since text from behind a narrower overlay can still
    show through on rows it does not reach.
    """
    if path != "/":
        return False
    text = "\n".join(rows)
    if "+" in text or "|" in text:
        return False
    return "Temp" in text


class Machine:
    """REST C64 observations alongside transport-agnostic browser navigation."""

    def __init__(self, session: RestSession, browser: Browser) -> None:
        self.session = session
        self.browser = browser

    # ---- REST helpers ---------------------------------------------------
    def readmem(self, address: int, length: int) -> bytes:
        # HTTP 423 is the device saying it could not take the lock on the
        # machine subsystem (subsys.h http_response_map), which is a request
        # to come back rather than a result. It is answered, not raised, so
        # the shared transport retry never sees it: that retries a request
        # that did not arrive, and this one did. Measured on a C64 Ultimate,
        # reading the signature straight after a PRG was started: the read
        # raced whatever the launch still held, and one repeat was enough.
        deadline = time.monotonic() + LOCK_RETRY_SECONDS
        while True:
            status, _, body = self.session.request(
                "GET", "/v1/machine:readmem",
                params={"address": f"{address:04X}", "length": length})
            if status == 200:
                return body
            if status != HTTP_LOCKED or time.monotonic() >= deadline:
                raise Failure(f"readmem ${address:04X} failed with HTTP {status}")
            time.sleep(pacing.POLL_INTERVAL_SECONDS)

    def writemem(self, address: int, data: bytes) -> None:
        # Only ever used to blank a fixed block (the signature area, the load
        # area), so the same bytes arriving twice is the same outcome as once.
        # The device can be busy enough running a program to miss the request
        # window, which is a timeout rather than an answer.
        status, _, body = self.session.request(
            "PUT", "/v1/machine:writemem", idempotent=True,
            params={"address": f"{address:04X}", "data": data.hex()})
        if status != 200:
            raise Failure(f"writemem ${address:04X} failed with HTTP {status}: {body[:120]!r}")

    def reset(self) -> None:
        status, _, _ = self.session.request("PUT", "/v1/machine:reset")
        if status != 200:
            raise Failure(f"machine:reset failed with HTTP {status}")
        # Wait for the BASIC cold start to finish. Freezing before it has run
        # its NEW leaves the boot sequence to wipe the program area and the
        # BASIC pointers the moment the machine is released again.
        # Polled at the shared interval rather than every 0.25s: the boot takes
        # about 2.4s and this runs before every load/run action, so a coarse
        # poll added up to a fifth of a second an action for nothing.
        deadline = time.monotonic() + BOOT_TIMEOUT_SECONDS
        while time.monotonic() < deadline:
            if self.readmem(0x002B, 2) == b"\x01\x08" and "READY." in self.c64_screen():
                time.sleep(0.3)
                return
            time.sleep(pacing.POLL_INTERVAL_SECONDS)
        raise Failure(f"C64 did not reach the BASIC prompt:\n{self.c64_screen()}")

    def drive_a(self) -> Dict[str, object]:
        status, _, body = self.session.request("GET", "/v1/drives")
        if status != 200:
            raise Failure(f"drives query failed with HTTP {status}")
        for entry in json.loads(body.decode("utf-8")).get("drives", []):
            if "a" in entry:
                return entry["a"]
        raise Failure("drive A missing from the drives listing")

    def remove_drive_a(self) -> None:
        self.session.request("PUT", "/v1/drives/a:remove")

    # ---- C64 observation ------------------------------------------------
    def signature(self) -> bytes:
        return self.readmem(SIGNATURE_ADDRESS, len(SIGNATURE))

    def clear_signature(self) -> None:
        self.writemem(SIGNATURE_ADDRESS, bytes(len(SIGNATURE)))

    def clear_load_area(self) -> None:
        self.writemem(LOAD_ADDRESS, bytes(len(PRG_BYTES) - 2))

    def load_image(self) -> bytes:
        return self.readmem(LOAD_ADDRESS, len(PRG_BYTES) - 2)

    def c64_screen(self) -> str:
        data = self.readmem(0x0400, 1000)
        return "\n".join(
            "".join(screencode_to_ascii(b) for b in data[row * 40 : (row + 1) * 40])
            for row in range(25)
        )

    def visible_text(self) -> str:
        """Whatever the user is looking at: the menu when open, else the C64."""
        body = self.session.try_get_menu_screen()
        if body is not None:
            return MenuScreenInfo(body).text
        return self.c64_screen()

    def wait_for_signature(self, timeout: float) -> None:
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            if self.signature() == SIGNATURE:
                return
            time.sleep(0.25)
        raise Failure(
            f"program never ran (no {SIGNATURE!r} at ${SIGNATURE_ADDRESS:04X}); "
            f"screen was:\n{self.visible_text()}")

    def wait_for_load_image(self, timeout: float) -> None:
        expected = PRG_BYTES[2:]
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            if self.load_image() == expected:
                return
            time.sleep(0.25)
        raise Failure(
            f"program was never placed at ${LOAD_ADDRESS:04X}; "
            f"screen was:\n{self.visible_text()}")

    # ---- Native C64 keyboard input --------------------------------------
    def tap(self, inputs: List[str], settle: float = MENU_SETTLE_SECONDS) -> None:
        # The device serves a small number of HTTP connections, so a single
        # request can time out under load. Retry once: losing a keystroke here
        # would be reported as a firmware failure.
        try:
            self.session.tap_keyboard(inputs)
        except Failure:
            time.sleep(1.0)
            self.session.tap_keyboard(inputs)
        time.sleep(settle)

    def wait_menu_closed(self, timeout: float = 5.0) -> bool:
        """Wait for the on-device menu to be gone, so the C64 has its keyboard."""
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            if self.session.menu_screen_unavailable():
                return True
            time.sleep(0.2)
        return False

    def type_basic_line(self, text: str) -> None:
        """Type a direct-mode line, making sure every character really landed.

        REST key injection goes through the C64 keyboard matrix, so a tap can
        be missed if the KERNAL scan does not see it. Check the echo before
        pressing RETURN rather than sending BASIC a truncated command.
        """
        for attempt in range(5):
            # The menu disables the C64 keyboard matrix while it is up and only
            # re-enables it after the browser task has fully unwound, so wait
            # for it to be gone and give the machine its keyboard back before
            # typing. Without the wait, an action that hands control back by
            # closing the menu can be followed by typing into a machine whose
            # keyboard is still disabled, which produces no characters at all.
            if not self.wait_menu_closed():
                continue
            try:
                self.session.release_all_input()
            except Failure:
                pass
            time.sleep(0.5)
            for character in text:
                self.tap([character.lower()], 0.20)
            time.sleep(0.2)
            if any(row.strip() == text for row in self.c64_screen().splitlines()):
                self.tap(["return"], 0.40)
                return
            # Wipe whatever did land and try again.
            for _ in range(len(text) + 2):
                self.tap(["inst_del"], 0.06)
        still_open = not self.session.menu_screen_unavailable()
        raise Failure(
            f"could not type {text!r} on the C64"
            f"{' (the menu never closed, so the keyboard stayed disabled)' if still_open else ''}:"
            f"\n{self.c64_screen()}")

    # ---- Browser navigation ---------------------------------------------
    def close_menu(self) -> None:
        # Under Telnet the root browser is where this has to stop; under
        # REST/Overlay and REST/Freeze it is not, because there the root
        # browser is still the on-device menu being up, and while it is up
        # the C64's keyboard stays disabled. Stopping at the root under REST
        # left the menu open, so a later type_basic_line() produced no
        # characters at all (confirmed live: the DMA action failed with "the
        # menu never closed, so the keyboard stayed disabled" while the
        # browser sat in /Temp).
        telnet = isinstance(self.browser.backend, TelnetBackend)
        for _ in range(20):
            try:
                raw = self.browser.rows()
            except Failure:
                return
            rows = [strip_frame(row) for row in raw]
            fields = raw[self.browser.status_row].split()
            path = fields[0] if fields else ""
            if "Yes  No" in rows:
                self.browser.press_popup_button("n")
            elif "Ok" in rows:
                self.browser.press_popup_button("o")
            elif telnet and _at_plain_root(rows, path):
                # Nothing left to close over Telnet: its remote session never
                # closes on its own, so without this check the loop would keep
                # pressing F8 at the plain root forever. Pressing F8 there is
                # not a harmless no-op either: confirmed live, it closes the
                # whole Telnet UI session, breaking the socket on the very
                # next keypress. REST needs no such check -- its menu_screen
                # 404s once F8 has backed all the way out, which the Failure
                # branch above catches.
                return
            else:
                # Shift+F7 is F8, the full UI-exit command. RUN/STOP may only
                # hide a nested config/search stack, which then reappears when
                # this independently selected suite opens the menu.
                try:
                    self.browser.press("F8")
                except Failure:
                    return
        try:
            screen = "\n".join(self.browser.rows())
        except Failure:
            return
        raise Failure("could not close the menu; screen was:\n" + screen)

    def select_entry(self, prefix: str, max_steps: int = 128) -> None:
        # Browser.select_entry's default timeout (3s) assumes its own
        # default max_steps (30); /Temp accumulates fixtures across every
        # run of this suite, so a genuine scan here can need most of the 128
        # steps this suite asks for, each a real keypress-and-settle round
        # trip. The root browser also refreshes drive status on its own,
        # independent of any keypress (confirmed live: an identical select
        # for a root-level entry failed once, then succeeded on an
        # otherwise-identical retry a moment later), so a scan can race a
        # background redraw -- the budget needs to be generous enough for a
        # few retries to clear that, not just long enough for one full walk.
        self.browser.select_entry(prefix, max_steps=max_steps, timeout=45.0)

    def enter(self) -> None:
        self.browser.enter()

    def rows(self) -> List[str]:
        return self.browser.rows()

    def current_path(self) -> str:
        return self.browser.current_path()

    def open_temp(self) -> None:
        # The root browser refreshes drive status independently of any
        # keypress (the same behaviour documented for the task menu in
        # assembly64_test.py), so a scan can occasionally race a background
        # redraw -- confirmed live: identical reset+reopen+scan sequences
        # run back to back are not equally reliable right after a full-screen
        # overlay (View, Hex View) was open, though a fresh cycle in
        # isolation reliably succeeds. Retrying the whole sequence from a
        # clean reopen, not just the inner scan, is what proved reliable.
        last_exc: Optional[Failure] = None
        for _ in range(3):
            try:
                self.browser.backend.ensure_ready()
                self.browser.go_to_root()
                self.select_entry("Temp")
                self.enter()
                if self.current_path() != TEMP_PATH:
                    raise Failure(f"expected {TEMP_PATH!r}, got {self.current_path()!r}")
                return
            except Failure as exc:
                last_exc = exc
        raise last_exc

    def invoke_context_action(self, label: str) -> None:
        # Run, Mount & Run, Real Run and DMA hand control to a running
        # program as a direct effect of the ENTER that confirms them, which
        # closes the menu before Browser can settle-capture a post-action
        # screen (Backend.send_key() raises "menu screen unavailable" in
        # that case -- the same class of thing assembly64_test.py hit for
        # RUN/STOP; see its Device.send_key()). Every caller here verifies
        # the actual outcome independently afterward (a REST memory read, a
        # popup's own text, or the browser being reachable again), so a menu
        # closing here is not itself a failure to report.
        try:
            self.browser.invoke_context_action(label)
        except Failure as exc:
            if not str(exc).startswith("menu screen unavailable after"):
                raise

    def context_labels(self) -> List[str]:
        """Open the context menu of the selected entry and close it again."""
        labels = self.browser.open_context_menu()
        self.browser.press("RUNSTOP")
        return labels

    # ---- Nested screens the actions open --------------------------------
    def wait_for_text(self, text: str, timeout: float = SCREEN_TIMEOUT_SECONDS) -> str:
        self.browser.wait_for_text(text, timeout)
        return "\n".join(self.browser.rows())

    def press_popup_button(self, key: str) -> None:
        self.browser.press_popup_button(key)

    def leave_nested_screen(self) -> None:
        """RUN/STOP backs out of the editors and of the path picker."""
        self.browser.press("RUNSTOP")

    def replace_edit_field(self, text: str) -> None:
        self.browser.fill_edit_field(text, clear_taps=EDIT_FIELD_CLEAR_TAPS)

    def pick_directory(self, directory: str) -> None:
        self.browser.pick_directory(directory, PICKER_TITLE, PICKER_SELECT_ENTRY)

    def close(self) -> None:
        self.browser.close()


def default_fixture_token() -> str:
    # The browser caches a disk image's directory per path, so every run needs
    # its own image name to be sure it reads the freshly uploaded fixture.
    return f"{int(time.time()) % 100000:05d}"


def parse_d64_directory(image: bytes) -> List[str]:
    """Names of the live files in a D64, read straight from the image."""
    names = []
    track, sector = D64_DIR_TRACK, 1
    seen = set()
    while track and (track, sector) not in seen:
        seen.add((track, sector))
        base = sector_offset(track, sector)
        for entry in range(8):
            # 32 byte slots; the first two bytes of slot 0 are the sector link,
            # so the type/track/sector/name fields start at slot + 2.
            slot = base + entry * 32
            if image[slot + 2] == 0x00:  # deleted / unused slot
                continue
            raw = image[slot + 5 : slot + 21]
            names.append(bytes(b & 0x7F for b in raw).decode("ascii", "replace").rstrip(" "))
        track, sector = image[base], image[base + 1]
    return names


class Fixtures:
    def __init__(self, token: str) -> None:
        self.token = token
        self.prg = f"{FIXTURE_PREFIX}{token}.prg"
        self.d64 = f"{FIXTURE_PREFIX}{token}.d64"
        self.target_dir = f"{FIXTURE_PREFIX}{token}tgt"
        self.disk_serial = 0
        # A name far beyond the 16 characters the boot cart can display. The
        # loader has to shorten it for the C64 without overrunning its own
        # buffer, so this is the fixture that catches that overrun.
        stem = f"{FIXTURE_PREFIX}{token}_long_"
        self.long_prg = stem + "0123456789" * ((LONG_NAME_LENGTH - len(stem)) // 10) + ".prg"
        self.long_prefix = stem

    def seed(self, host: str, password: str) -> None:
        """All fixtures live side by side in /Temp: plain PRGs and a D64."""
        payload = {
            self.prg: PRG_BYTES,
            self.long_prg: PRG_BYTES,
            self.d64: build_d64(DISK_NAME, CBM_FILE_NAME, PRG_BYTES),
        }
        with ftp_lib.session(host, password, timeout=30) as ftp:
            ftp_lib.make_dir(ftp, f"{TEMP_PATH}{self.target_dir}")
            for name, blob in payload.items():
                self._store(ftp, name, blob)
            # Overlong names come back through a shortened FTP alias, so match
            # on the leading run that survives instead of the whole name.
            listing = ftp_lib.names(ftp, TEMP_PATH)
            for name in payload:
                stem = name[:20]
                if not any(entry.startswith(stem) for entry in listing):
                    raise Failure(f"fixture {name} was not stored in {TEMP_PATH}: {listing}")

    @staticmethod
    def _store(ftp: ftplib.FTP, name: str, blob: bytes) -> None:
        ftp_lib.delete_quietly(ftp, f"{TEMP_PATH}{name}")
        ftp_lib.store(ftp, f"{TEMP_PATH}{name}", blob)

    def reseed_prg(self, host: str, password: str) -> None:
        with ftp_lib.session(host, password, timeout=30) as ftp:
            self._store(ftp, self.prg, PRG_BYTES)

    def new_disk(self, host: str, password: str) -> None:
        """A fresh image name: the browser caches a disk directory per path."""
        self.disk_serial += 1
        self.d64 = f"{FIXTURE_PREFIX}{self.token}d{self.disk_serial}.d64"
        with ftp_lib.session(host, password, timeout=30) as ftp:
            self._store(ftp, self.d64, build_d64(DISK_NAME, CBM_FILE_NAME, PRG_BYTES))

    def temp_listing(self, host: str, password: str, directory: str = "") -> List[str]:
        with ftp_lib.session(host, password, timeout=30) as ftp:
            return ftp_lib.names(ftp, f"{TEMP_PATH}{directory}")

    def disk_listing(self, host: str, password: str) -> List[str]:
        """Read the fixture image back over FTP and decode its directory."""
        with ftp_lib.session(host, password, timeout=30) as ftp:
            image = ftp_lib.retrieve(ftp, f"{TEMP_PATH}{self.d64}")
        return parse_d64_directory(image)

    def remove(self, host: str, password: str) -> None:
        try:
            with ftp_lib.session(host, password, timeout=30) as ftp:
                for directory in (f"{self.target_dir}/", ""):
                    for name in ftp_lib.names(ftp, f"{TEMP_PATH}{directory}"):
                        if not name.startswith(FIXTURE_PREFIX) and directory == "":
                            continue
                        ftp_lib.delete_quietly(ftp, f"{TEMP_PATH}{directory}{name}")
                ftp_lib.delete_quietly(ftp, f"{TEMP_PATH}{self.target_dir}")
        except Failure:
            # Best effort cleanup: a device that will not answer here is
            # reported by whatever runs next, not by teardown.
            pass


def remove_leftovers_via_browser(machine: Machine, host: str, password: str) -> None:
    """Delete fixtures FTP cannot reach.

    An overlong name is only listed through a shortened FTP alias and DELE on
    that alias fails, so the long-named fixture has to go out the same way it
    came in view: through the browser, which resolves the real name.
    """
    with ftp_lib.session(host, password, timeout=30) as ftp:
        remaining = ftp_lib.names(ftp, TEMP_PATH, prefix=FIXTURE_PREFIX)
    if not remaining:
        return

    machine.open_temp()
    for _ in range(len(remaining)):
        try:
            machine.select_entry(FIXTURE_PREFIX)
        except Failure:
            break
        machine.invoke_context_action("Delete")
        machine.wait_for_text("Are you sure?")
        machine.press_popup_button("y")
    machine.close_menu()


def prepare(machine: Machine, mounted: bool = False) -> None:
    machine.close_menu()
    machine.reset()
    if not mounted:
        machine.remove_drive_a()
    machine.clear_signature()
    machine.clear_load_area()
    if machine.signature() == SIGNATURE:
        raise Failure("could not clear the signature before the action")


def open_plain_prg(machine: Machine, fixtures: Fixtures) -> None:
    machine.open_temp()
    machine.select_entry(fixtures.prg)


def open_long_name_prg(machine: Machine, fixtures: Fixtures) -> None:
    machine.open_temp()
    machine.select_entry(fixtures.long_prefix)


def open_disk_prg(machine: Machine, fixtures: Fixtures) -> None:
    machine.open_temp()
    machine.select_entry(fixtures.d64)
    machine.enter()
    expected = f"{TEMP_PATH}{fixtures.d64}/"
    if machine.current_path() != expected:
        raise Failure(f"expected to be inside {expected!r}, got {machine.current_path()!r}")
    machine.select_entry(CBM_FILE_NAME)


def assert_no_load_error(machine: Machine) -> None:
    screen = machine.c64_screen()
    for error in ("FILE NOT FOUND", "DEVICE NOT PRESENT"):
        if error in screen:
            raise Failure(f"drive reported {error}:\n{screen}")


def assert_signature_absent(machine: Machine) -> None:
    if machine.signature() == SIGNATURE:
        raise Failure("the program ran even though the action should only load it")


def run_action_run(machine: Machine, fixtures: Fixtures, open_entry) -> None:
    prepare(machine)
    open_entry(machine, fixtures)
    machine.invoke_context_action("Run")
    machine.wait_for_signature(RUN_TIMEOUT_SECONDS)
    if MESSAGE not in machine.c64_screen():
        raise Failure(f"program output missing from the screen:\n{machine.c64_screen()}")


def run_action_load(machine: Machine, fixtures: Fixtures, open_entry) -> None:
    prepare(machine)
    open_entry(machine, fixtures)
    machine.invoke_context_action("Load")
    machine.wait_for_load_image(RUN_TIMEOUT_SECONDS)
    assert_signature_absent(machine)


def run_action_dma(machine: Machine, fixtures: Fixtures, open_entry) -> None:
    prepare(machine)
    open_entry(machine, fixtures)
    machine.invoke_context_action("DMA")
    # A DMA-only load hands the machine back and leaves the menu, by design.
    machine.close_menu()
    machine.wait_for_load_image(RUN_TIMEOUT_SECONDS)
    assert_signature_absent(machine)
    machine.type_basic_line("RUN")
    machine.wait_for_signature(RUN_TIMEOUT_SECONDS)
    if MESSAGE not in machine.c64_screen():
        raise Failure(f"DMA-loaded program did not run:\n{machine.c64_screen()}")


def run_action_mount_and_run(machine: Machine, fixtures: Fixtures) -> None:
    prepare(machine)
    open_disk_prg(machine, fixtures)
    machine.invoke_context_action("Mount & Run")
    machine.wait_for_signature(RUN_TIMEOUT_SECONDS)
    assert_disk_mounted(machine, fixtures, "Mount & Run")


def run_action_real_run(machine: Machine, fixtures: Fixtures) -> None:
    prepare(machine)
    open_disk_prg(machine, fixtures)
    machine.invoke_context_action("Real Run")
    assert_disk_mounted(machine, fixtures, "Real Run")
    deadline = time.monotonic() + REAL_RUN_TIMEOUT_SECONDS
    while time.monotonic() < deadline:
        assert_no_load_error(machine)
        if machine.signature() == SIGNATURE:
            return
        time.sleep(0.5)
    raise Failure(f"real 1541 load/run never completed:\n{machine.c64_screen()}")


def assert_disk_mounted(machine: Machine, fixtures: Fixtures, action: str) -> None:
    # Mount & Run's own caller already waited out RUN_TIMEOUT_SECONDS for
    # the program's signature before checking this, by which point the
    # mount is long since finished; Real Run's caller checks immediately
    # after triggering the action, with nothing else to wait on first, and
    # can genuinely race the mount actually registering in /v1/drives.
    deadline = time.monotonic() + 5.0
    drive_a: Dict[str, object] = {}
    while time.monotonic() < deadline:
        drive_a = machine.drive_a()
        mounted = f"{drive_a.get('image_path', '')}{drive_a.get('image_file', '')}"
        if fixtures.d64 in mounted:
            return
        time.sleep(0.25)
    raise Failure(f"{action} did not mount the D64 on drive A: {drive_a!r}")


class PlainLocation:
    """The PRG as an ordinary file in /Temp."""

    label = "a plain PRG"

    def open(self, machine: Machine, fixtures: Fixtures) -> None:
        open_plain_prg(machine, fixtures)

    def entry_name(self, fixtures: Fixtures) -> str:
        return fixtures.prg

    def renamed_to(self, fixtures: Fixtures) -> str:
        return f"{FIXTURE_PREFIX}{fixtures.token}ren.prg"

    def listing(self, host: str, password: str, fixtures: Fixtures) -> List[str]:
        return fixtures.temp_listing(host, password)

    def refresh(self, host: str, password: str, fixtures: Fixtures) -> None:
        fixtures.reseed_prg(host, password)

    def forbidden_actions(self) -> Tuple[str, ...]:
        return ("Mount & Run", "Real Run")


class DiskLocation:
    """The same PRG as the only file inside a D64 image."""

    label = "a PRG inside a D64"

    def open(self, machine: Machine, fixtures: Fixtures) -> None:
        open_disk_prg(machine, fixtures)

    def entry_name(self, fixtures: Fixtures) -> str:
        return CBM_FILE_NAME

    def renamed_to(self, fixtures: Fixtures) -> str:
        return "RENAMEDPRG"

    def listing(self, host: str, password: str, fixtures: Fixtures) -> List[str]:
        return fixtures.disk_listing(host, password)

    def refresh(self, host: str, password: str, fixtures: Fixtures) -> None:
        fixtures.new_disk(host, password)

    def forbidden_actions(self) -> Tuple[str, ...]:
        return ()


def assert_present(names: List[str], wanted: str, what: str) -> None:
    if not any(name.startswith(wanted) for name in names):
        raise Failure(f"{what}: {wanted!r} is missing from {names}")


def assert_absent(names: List[str], wanted: str, what: str) -> None:
    if any(name.startswith(wanted) for name in names):
        raise Failure(f"{what}: {wanted!r} is still present in {names}")


def action_view(machine: Machine, fixtures: Fixtures, location, host: str, password: str) -> None:
    location.open(machine, fixtures)
    listing = machine.rows()
    machine.invoke_context_action("View")
    if machine.rows() == listing:
        raise Failure("View did not open anything over the browser")
    machine.leave_nested_screen()
    machine.select_entry(location.entry_name(fixtures))


def action_hex_view(machine: Machine, fixtures: Fixtures, location, host: str, password: str) -> None:
    location.open(machine, fixtures)
    machine.invoke_context_action("Hex View")
    screen = machine.wait_for_text(HEX_VIEW_FIRST_LINE)
    if "U64PRG" not in screen:
        raise Failure(f"hex view is not showing the fixture:\n{screen}")
    machine.leave_nested_screen()
    machine.select_entry(location.entry_name(fixtures))


def invoke_action_and_pick_directory(
    machine: Machine, fixtures: Fixtures, location, action_label: str, attempts: int = 5,
) -> None:
    # The "Select Path" picker marks its own selected row with a different
    # colour convention than the tree browser and every other overlay this
    # suite drives (confirmed live: none of its cells carry the tree
    # browser's marker SGR, at any column), which TelnetBackend.selected_row()
    # has no way to know to look for -- a transport-level gap, not something
    # fixable by retrying. Callers check the mode themselves and skip before
    # reaching here rather than retry a walk that cannot succeed under Telnet.
    directory = f"{TEMP_PATH}{fixtures.target_dir}"
    last_exc: Optional[Failure] = None
    for _ in range(attempts):
        try:
            location.open(machine, fixtures)
            machine.invoke_context_action(action_label)
            machine.pick_directory(directory)
            return
        except Failure as exc:
            last_exc = exc
            try:
                machine.close_menu()
            except Failure:
                pass
    raise last_exc


def action_copy_to(machine: Machine, fixtures: Fixtures, location, host: str, password: str) -> None:
    if isinstance(machine.browser.backend, TelnetBackend):
        check_skip("the 'Select Path' picker's own selection marker is not detectable over telnet")
        return
    clear_target_dir(host, password, fixtures)
    invoke_action_and_pick_directory(machine, fixtures, location, "Copy to...")
    machine.wait_for_text("Copy complete.")
    machine.press_popup_button("o")

    copied = fixtures.temp_listing(host, password, f"{fixtures.target_dir}/")
    if len(copied) != 1:
        raise Failure(f"expected exactly one copy in the target directory, got {copied}")
    if fetch_temp_file(host, password, f"{fixtures.target_dir}/{copied[0]}")[:len(PRG_BYTES)] != PRG_BYTES:
        raise Failure(f"the copy {copied[0]!r} does not hold the fixture bytes")
    assert_present(location.listing(host, password, fixtures),
                   location.entry_name(fixtures), "Copy to... removed the original")


def action_move_to(machine: Machine, fixtures: Fixtures, location, host: str, password: str) -> None:
    if isinstance(machine.browser.backend, TelnetBackend):
        check_skip("the 'Select Path' picker's own selection marker is not detectable over telnet")
        return
    clear_target_dir(host, password, fixtures)
    location.refresh(host, password, fixtures)
    invoke_action_and_pick_directory(machine, fixtures, location, "Move to...")
    machine.wait_for_text("Move complete.")
    machine.press_popup_button("o")

    moved = fixtures.temp_listing(host, password, f"{fixtures.target_dir}/")
    if len(moved) != 1:
        raise Failure(f"expected exactly one moved file in the target directory, got {moved}")
    assert_absent(location.listing(host, password, fixtures),
                  location.entry_name(fixtures), "Move to... left the original behind")


def action_rename(machine: Machine, fixtures: Fixtures, location, host: str, password: str) -> None:
    location.refresh(host, password, fixtures)
    location.open(machine, fixtures)
    original = location.entry_name(fixtures)
    renamed = location.renamed_to(fixtures)
    machine.invoke_context_action("Rename")
    machine.wait_for_text("Give a new name..")
    machine.replace_edit_field(renamed)

    names = location.listing(host, password, fixtures)
    assert_present(names, renamed, "Rename did not create the new name")
    assert_absent(names, original, "Rename left the old name behind")


def action_delete(machine: Machine, fixtures: Fixtures, location, host: str, password: str) -> None:
    location.refresh(host, password, fixtures)
    location.open(machine, fixtures)
    original = location.entry_name(fixtures)
    machine.invoke_context_action("Delete")
    machine.wait_for_text("Are you sure?")
    machine.press_popup_button("y")

    assert_absent(location.listing(host, password, fixtures), original, "Delete did not remove the file")


# --- The scenarios issue #729 reports, checked the way a user would see them ---

def scenario_dma_runnable(machine: Machine, fixtures: Fixtures, location, host, password) -> str:
    """DMA has to leave a program in RAM that the user can actually RUN."""
    prepare(machine)
    location.open(machine, fixtures)
    machine.invoke_context_action("DMA")
    machine.close_menu()
    in_ram = machine.load_image() == PRG_BYTES[2:]
    machine.type_basic_line("RUN")
    ran = False
    deadline = time.monotonic() + RUN_TIMEOUT_SECONDS
    while time.monotonic() < deadline:
        if machine.signature() == SIGNATURE:
            ran = True
            break
        time.sleep(0.25)
    problems = []
    if not in_ram:
        problems.append("program not in RAM")
    if not ran:
        pointers = machine.readmem(0x002B, 4).hex(" ")
        problems.append(f"RUN did not start it (TXTTAB/VARTAB {pointers})")
    return ", ".join(problems)


def scenario_real_run(machine: Machine, fixtures: Fixtures, location, host, password) -> str:
    """Real Run has to complete a real 1541 load, with no drive error."""
    prepare(machine)
    open_disk_prg(machine, fixtures)
    machine.invoke_context_action("Real Run")
    deadline = time.monotonic() + REAL_RUN_TIMEOUT_SECONDS
    while time.monotonic() < deadline:
        screen = machine.c64_screen()
        for error in ("FILE NOT FOUND", "DEVICE NOT PRESENT"):
            if error in screen:
                return f"drive reported {error}"
        if machine.signature() == SIGNATURE:
            return ""
        time.sleep(0.5)
    return "load/run never completed"


def scenario_long_name_run(machine: Machine, fixtures: Fixtures, location, host, password) -> str:
    """Run a PRG whose name is far longer than the boot cart can display.

    On firmware without the name-buffer fix this does not merely misbehave: it
    takes the whole device down, so the check has to survive the machine going
    away rather than raise on the first refused connection.
    """
    prepare(machine)
    machine.open_temp()
    machine.select_entry(fixtures.long_prefix)
    machine.invoke_context_action("Run")
    deadline = time.monotonic() + RUN_TIMEOUT_SECONDS
    while time.monotonic() < deadline:
        try:
            if machine.signature() == SIGNATURE:
                return ""
        except Failure as exc:
            return f"device stopped responding ({exc})"
        time.sleep(0.25)
    return "program never ran"


SCENARIOS = [
    ("long-name", "Run a PRG with a 101 character name", scenario_long_name_run, PlainLocation),
    ("dma-plain", "DMA a plain PRG", scenario_dma_runnable, PlainLocation),
    ("dma-disk", "DMA a PRG inside a D64", scenario_dma_runnable, DiskLocation),
    ("real-run", "Real Run a PRG inside a D64", scenario_real_run, DiskLocation),
]


def run_repeat_mode(machine: Machine, fixtures: Fixtures, host: str, password: str,
                    repeat: int, selected: List[str]) -> int:
    """Hammer selected scenarios so an intermittent defect cannot look like a pass."""
    chosen = [entry for entry in SCENARIOS if not selected or entry[0] in selected]
    tally = {label: [] for _, label, _, _ in chosen}
    for iteration in range(1, repeat + 1):
        for _, label, scenario, location_type in chosen:
            location = location_type()
            location.refresh(host, password, fixtures)
            try:
                problem = scenario(machine, fixtures, location, host, password)
            except Failure as exc:
                problem = str(exc)
            tally[label].append(problem)
            state = "ok  " if not problem else "FAIL"
            print(f"  [{iteration:02d}/{repeat}] {state} {label}"
                  + (f" -> {problem}" if problem else ""), flush=True)

    print("\n=== repeat summary ===")
    failed_total = 0
    for label, results in tally.items():
        bad = [r for r in results if r]
        failed_total += len(bad)
        reasons = sorted(set(bad))
        print(f"  {label}: {len(results) - len(bad)}/{len(results)} ok"
              + (f"  failures: {reasons}" if reasons else ""))
    return 1 if failed_total else 0


def fetch_temp_file(host: str, password: str, relative: str) -> bytes:
    with ftp_lib.session(host, password, timeout=30) as ftp:
        return ftp_lib.retrieve(ftp, f"{TEMP_PATH}{relative}")


def clear_target_dir(host: str, password: str, fixtures: Fixtures) -> None:
    directory = f"{TEMP_PATH}{fixtures.target_dir}"
    with ftp_lib.session(host, password, timeout=30) as ftp:
        for name in ftp_lib.names(ftp, f"{directory}/"):
            ftp_lib.delete_quietly(ftp, f"{directory}/{name}")


# Every action the browser offers for a PRG, in the order the test drives them:
# the destructive ones come last so the earlier checks run against a pristine
# fixture. The load/run entries are handled separately because they also need a
# freshly reset C64.
FILE_ACTIONS = [
    ("View", action_view),
    ("Hex View", action_hex_view),
    ("Copy to...", action_copy_to),
    ("Rename", action_rename),
    ("Move to...", action_move_to),
    ("Delete", action_delete),
]


def load_actions(machine: Machine, fixtures: Fixtures, location):
    """The load/run entries, bound to one location."""
    actions = [
        ("Run", lambda: run_action_run(machine, fixtures, location.open)),
        ("Load", lambda: run_action_load(machine, fixtures, location.open)),
        ("DMA", lambda: run_action_dma(machine, fixtures, location.open)),
    ]
    if isinstance(location, DiskLocation):
        actions.append(("Mount & Run", lambda: run_action_mount_and_run(machine, fixtures)))
        actions.append(("Real Run", lambda: run_action_real_run(machine, fixtures)))
    return actions


def offered_actions(machine: Machine, fixtures: Fixtures, location) -> List[str]:
    location.open(machine, fixtures)
    return machine.context_labels()


def run_context_menu_inventory(machine: Machine, fixtures: Fixtures, location, offered: List[str]) -> None:
    for label in location.forbidden_actions():
        if label in offered:
            raise Failure(f"{location.label} should not offer {label!r}: {offered}")
    handled = {label for label, _ in FILE_ACTIONS}
    handled.update(label for label, _ in load_actions(machine, fixtures, location))
    handled.update(UNEXERCISED_ACTIONS)
    missing = [label for label in offered if label not in handled]
    if missing:
        raise Failure(
            f"{location.label} offers actions this test never drives: {missing}. "
            "Add a handler for each, or list it in UNEXERCISED_ACTIONS with a reason.")


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Drive and verify every PRG context-menu action on real firmware.")
    parser.add_argument("-H", "--host", default=os.environ.get("U64_HOST", "u64"))
    parser.add_argument(
        "-p", "--password",
        default=os.environ.get("U64_PASS", ""))
    parser.add_argument(
        "-t", "--timeout", type=float,
        default=float(os.environ.get("U64_TIMEOUT", "15.0")))
    parser.add_argument("-P", "--telnet-port", "--port", dest="port", type=int,
                        default=int(os.environ.get("U64_TELNET_PORT", "23")))
    parser.add_argument("--rest-host", default=os.environ.get("U64_REST_HOST"))
    parser.add_argument(
        "--keep-fixtures", action="store_true",
        help="Leave the seeded /Temp fixtures in place for manual inspection.")
    parser.add_argument(
        "--repeat", type=int, default=0, metavar="N",
        help="Instead of the full matrix, repeat the load/run scenarios N times.")
    parser.add_argument(
        "--scenario", action="append", metavar="NAME",
        choices=[name for name, _, _, _ in SCENARIOS],
        help="Limit --repeat to this scenario. Repeatable. "
             "One of: " + ", ".join(name for name, _, _, _ in SCENARIOS))
    parser.add_argument(
        "--fixture-token", default=default_fixture_token(),
        help="Suffix that makes this run's /Temp fixture names unique.")
    add_mode_argument(parser)
    args = parser.parse_args()

    rest_host = args.rest_host or args.host
    session = RestSession(rest_host, args.password or None, args.timeout)
    browser = make_browser(
        args.mode,
        rest_host,
        args.password or None,
        args.timeout,
        entry_rows=ENTRY_ROWS,
        status_row=STATUS_ROW,
        telnet_host=args.host,
        telnet_port=args.port,
        # The context menu box is not drawn against the physical 40-column
        # edge the way REST/Overlay's is; at the standard width its own
        # labels render truncated to a stray character or two per row,
        # breaking overlay_items()'s before/after diff (confirmed live:
        # "Run"/"Load"/etc came back as ".", " ", "n"). The other two
        # migrated suites (assembly64_test.py's form, browser_long_filename
        # _test.py's file browser) both needed the same wider session.
        telnet_width=60,
        telnet_entry_rows=TELNET_ENTRY_ROWS,
        telnet_status_row=TELNET_STATUS_ROW,
    )
    machine = Machine(session, browser)
    fixtures = Fixtures(args.fixture_token)

    locations = [PlainLocation(), DiskLocation()]

    failures: List[Tuple[str, str]] = []
    total = 0

    # Every action is independent, so keep going after a failure: one run then
    # reports the whole context-menu matrix instead of stopping at the first hole.
    def run_case(label, action) -> None:
        nonlocal total
        total += 1
        try:
            with check(label):
                action()
        except Failure as exc:
            failures.append((label, str(exc)))

    try:
        with check("reset the machine to a clean starting state"):
            machine.close_menu()
            machine.reset()

        with check(f"seed /Temp with {fixtures.prg}, {fixtures.d64} and a long-named PRG"):
            fixtures.seed(rest_host, args.password)

        if args.repeat > 0:
            names = args.scenario or [name for name, _, _, _ in SCENARIOS]
            section(f"repeating {', '.join(names)} {args.repeat} times")
            return run_repeat_mode(machine, fixtures, rest_host, args.password,
                                   args.repeat, args.scenario or [])

        for location in locations:
            section(f"every context-menu action on {location.label}")
            with check(f"read the context menu of {location.label}"):
                offered = offered_actions(machine, fixtures, location)
            detail(f"offers: {', '.join(offered)}")

            run_case(f"{location.label}: every offered action is covered",
                     lambda loc=location, off=offered: run_context_menu_inventory(
                         machine, fixtures, loc, off))

            for label, action in load_actions(machine, fixtures, location):
                if label in offered:
                    run_case(f"{label} on {location.label}", action)
            for label, handler in FILE_ACTIONS:
                if label in offered:
                    run_case(
                        f"{label} on {location.label}",
                        lambda h=handler, loc=location: h(
                            machine, fixtures, loc, rest_host, args.password))

        # Last on purpose: on firmware without the boot-cart name fix this one
        # takes the whole device down, which would mask every earlier result.
        # It is also not merely failed there but skipped, because the device
        # does not come back without a power cycle and every suite after it in
        # the run would report an unreachable device.
        long_name_label = "Run a PRG whose name is far longer than the boot-cart display"
        if not machine.browser.backend.machine.skip_without_fix(
                machine_lib.BOOTCART_LONG_NAME_SAFE, long_name_label):
            run_case(long_name_label,
                     lambda: run_action_run(machine, fixtures, open_long_name_prg))

        if failures:
            suite_fail("prg_context_menu_test", f"{len(failures)} of {total} actions")
            for label, message in failures:
                detail(f"{label}: {message}")
            return 1

        suite_ok("prg_context_menu_test", f"{total} actions")
        return 0
    finally:
        try:
            machine.close_menu()
        except Exception:
            pass
        try:
            machine.remove_drive_a()
        except Exception:
            pass
        if not args.keep_fixtures:
            fixtures.remove(rest_host, args.password)
            try:
                remove_leftovers_via_browser(machine, rest_host, args.password)
            except Exception:
                pass
        try:
            machine.close()
        except Exception:
            pass


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Failure as exc:
        suite_fail("prg_context_menu_test", str(exc))
        raise SystemExit(1)
