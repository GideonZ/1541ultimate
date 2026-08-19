#!/usr/bin/env python3
# E2E: Verifies machine-code monitor commands and memory views, driven
# through the shared ui_backend.py facade (REST/Overlay by default, Telnet
# for the few checks that are genuinely about the Telnet transport).

import argparse
import difflib
import json
import os
import re
import sys
import time
import urllib.error
import urllib.request
from pathlib import Path
from typing import Dict, List, Optional, Tuple

# tests/lib holds the reporting rules every suite shares; tests/e2e/lib
# holds the shared UI backend.
sys.path.insert(0, os.path.join(
    os.path.dirname(os.path.abspath(__file__)), "..", "..", "lib"))
sys.path.insert(0, os.path.join(
    os.path.dirname(os.path.abspath(__file__)), "..", "lib"))
import machine as machine_lib
import rest as rest_lib
import targets
from api import UltimateApi
from report import Failure, check, check_skip, detail, format_exception, section, suite_fail, suite_ok
from ui_backend import (
    Backend,
    MODE_FREEZE,
    MODE_TELNET,
    Snapshot,
    add_mode_argument,
    make_backend,
)

SNAPSHOT_FILE = Path(__file__).with_name("snapshots").joinpath("expected_snapshots.json")

# Per-request timeout for this suite's own REST calls, which are all small
# reads and writes against a device that is otherwise idle.
REST_TIMEOUT_SECONDS = 5.0

# The monitor renders the status row in two forms (format_status_line_impl in
# software/monitor/machine_monitor.cc): "CPU5 $A:..." when the browsing view
# follows the live CPU bank, and "C5O7 $A:..." when a view override is
# selected, where the first digit is the live bank and the second is the
# overridden view. Matching only the first form left find_status_line unable
# to locate the row at all whenever an override was active, which is exactly
# the state every banked-breakpoint and banked-continue scenario works in.
#
# A backend that reports neither CPU banking nor a VIC bank (the U2+L's
# U2MemoryBackend) draws a third and fourth form instead, and a backend that
# reports only one of the two draws a fifth: see the status-row branches in
# MachineMonitor::draw_status_line. find_status_line only has to locate the
# row; the assertions that read banking detail out of it are already gated on
# the target. Without these alternatives the U2+L status row matches nothing,
# so enter_monitor() cannot tell an open monitor from a closed one and falls
# through to its blind menu-navigation fallback.
STATUS_LINE_RE = re.compile(
    r"(?:CPU[0-7]|C[0-7]O[0-7]) \$A:(?:RAM|BAS) \$D:(?:RAM|CHR|I/O) "
    r"\$E:(?:RAM|KRN) VIC[0-3] \$[0-9A-F]{4}"
    r"|CPU VIEW  CPU BANK N/A  VIC N/A"
    r"|CPU VIEW  VIC[0-3] \$[0-9A-F]{4}"
    r"|(?:CPU[0-7]|C[0-7]O[0-7])  VIC N/A")
# Which hardware the suite is pointed at. The U2+L runs the same monitor but
# its memory backend reports no CPU banking and no VIC bank, so the checks
# that read those out of the status row have nothing to assert there.
TARGET = "u64"


def is_u2() -> bool:
    return TARGET == "u2"


# Skip reason shared by the checks whose subject is selecting a CPU bank. The
# U2+L reports the live bank - the freeze captures the 6510's port by running a
# stub on the CPU itself - but its monitor has no view bank to point elsewhere,
# so a check that cycles the view with O has nothing to cycle.
NO_CPU_BANKING = ("the U2+L monitor has no CPU view bank to select; it reports "
                  "the live bank the freeze captured off the 6510 and nothing "
                  "else to switch between")


# The mapping fields a U2+L shows at the BASIC prompt. Its freeze reads the
# 6510's port through an NMI stub, so the row carries real banking - but the
# monitor has no view bank, so it always renders the LIVE one. Checks that pin a
# selected view bank therefore cannot be asserted verbatim there; the mapping is
# what is true and what is worth asserting.
U2_LIVE_MAPPING = "$A:BAS $D:I/O $E:KRN"


def status_text(snapshots: Dict, key: str) -> str:
    """The expected status row for the target under test."""
    return U2_LIVE_MAPPING if is_u2() else snapshots[key]["contains"]["22"]


# How long the monitor may take to appear after the key that opens it.
MONITOR_OPEN_TIMEOUT_SECONDS = 8.0
POLL_INTERVAL_SECONDS = 0.25

MEMORY_ROW_RE = re.compile(r"^[0-9A-F]{4} ")
MEMORY_ROW_16_RE = re.compile(r"^[0-9A-F]{4} [0-9A-F]{16} [0-9A-F]{16}$")


def find_status_line(snapshot: Snapshot) -> int:
    return snapshot.find_line_matching(STATUS_LINE_RE)


VIEW_KEYS = {
    "HEX ": "M",
    "ASC ": "I",
    "ASM ": "a",
    "SCR ": "V",
    "BIN ": "B",
}


class MonitorSession:
    """Domain-level machine-monitor operations, built on any ui_backend.Backend.

    Every scenario function in this file talks to a MonitorSession, never to
    a transport directly, so the exact same scenario code runs unchanged
    against RestBackend (Overlay/Freeze, the fast default) and TelnetBackend
    (kept for the few checks that are genuinely about the Telnet transport).
    """

    def __init__(self, backend: Backend) -> None:
        self.backend = backend
        self.enter_monitor()

    def close(self) -> None:
        self.backend.close()

    def capture(self) -> Snapshot:
        return self.backend.capture()

    def send_key(self, key: str, *, settle: bool = False) -> Snapshot:
        return self.backend.send_key(key, settle=settle)

    def send_key_count(self, key: str) -> Tuple[Snapshot, int]:
        """Telnet-only: see TelnetBackend.send_key_count."""
        return self.backend.send_key_count(key)

    def send_key_repeat(self, key: str, count: int) -> Snapshot:
        return self.backend.send_key_repeat(key, count)

    def send_char(self, ch: str) -> Snapshot:
        # The U2+L's remote UI can echo a command before redrawing the monitor.
        # Its view selectors must not be followed by the next command until
        # that redraw has gone quiet.
        return self.backend.send_char(ch, settle=is_u2())

    def send_text(self, text: str, label: str) -> Snapshot:
        return self.backend.send_text(text, label)

    def _navigation_landed(self, snapshot: Snapshot, marker: str) -> bool:
        # The address is looked for in the header row alone, not anywhere on
        # screen: in Assembly view an operand such as "JSR $C6C0" contains the
        # target string, so a whole-screen match could be satisfied by a stale
        # page that merely mentions the address. The prompt check does look at
        # the whole screen, because the prompt is a box drawn over the view.
        header = next((line for line in snapshot.lines if "MONITOR" in line), "")
        return bool(header) and marker in header and "Jump" not in snapshot.text()

    def goto(self, address: str, attempts: int = 3) -> Snapshot:
        """Navigate to `address`, confirming the monitor actually got there.

        Two separate things can leave the caller reading the wrong page.

        The navigation may not have finished: the J prompt closes and the header
        updates only once the monitor has re-read that page, which on a U2+L
        means stopping the machine, and the transport's settle can return while
        the prompt is still up.

        Or a keystroke may be lost outright. Where the keyboard is injected on
        one device and scanned off the matrix by another, a character goes
        missing every so often - measured on a U2+L in a C64U host, where "3200"
        arrived as "320" and the monitor dutifully went to $0320. Retrying is
        safe because navigation is idempotent, and it is the only thing that
        recovers a dropped character; waiting longer never will.
        """
        marker = f"${address.upper()}"
        snapshot = self.capture()
        for _ in range(attempts):
            # A previous attempt may have left the prompt open with a partial
            # address in it; typing the next J into that field would compound
            # the error rather than retry it.
            if "Jump" in snapshot.text():
                snapshot = self.send_key("ESC")
            self.send_char("J")
            snapshot = self.send_text(address + "\r", f"J {address}")
            deadline = time.monotonic() + MONITOR_OPEN_TIMEOUT_SECONDS
            while True:
                if self._navigation_landed(snapshot, marker):
                    return snapshot
                if time.monotonic() >= deadline:
                    break
                time.sleep(POLL_INTERVAL_SECONDS)
                snapshot = self.capture()
        # Out of attempts: hand back what is on screen so the caller's own
        # assertion reports the mismatch.
        return snapshot

    def fill(self, expr: str) -> Snapshot:
        self.send_char("F")
        return self.send_text(expr + "\r", f"F {expr}")

    def compare(self, expr: str) -> Snapshot:
        self.send_char("C")
        return self.send_text(expr + "\r", f"C {expr}")

    def goto_run(self, address: str) -> Snapshot:
        self.send_char("G")
        try:
            return self.send_text(address + "\r", f"G {address}")
        except Failure:
            # Under Freeze, a G that actually executes unfreezes the C64 and
            # closes the whole menu as a direct side effect of this final
            # keystroke (release_host() + release_ownership() in
            # run_machine_monitor.cc); under Overlay/Telnet the C64 was never
            # paused, so the menu never closes and this path is not taken.
            # Every caller re-enters the monitor next (which reopens the menu
            # via Backend.ensure_ready()) and none of them use this return
            # value, so the transient unavailability here is expected, not a
            # failure.
            return Snapshot([], [], f"G {address} (menu closed)")

    def _await_status_line(self, snapshot: Snapshot,
                           timeout: float = MONITOR_OPEN_TIMEOUT_SECONDS) -> Optional[Snapshot]:
        """Re-read until the monitor's status row is on screen, or give up.

        Opening the monitor stops the machine, reads a screenful of memory and
        redraws, and how long that takes depends on the target: on a U2+L, where
        the C64 is running behind the remote UI, the first draw takes seconds. A
        single read can therefore catch a partly drawn screen, and treating that
        as "the monitor did not open" is what sent the caller into the blind menu
        navigation below - which presses keys on rows it has not verified and has
        wedged a U2+L in the past."""
        deadline = time.monotonic() + timeout
        while True:
            try:
                find_status_line(snapshot)
                return snapshot
            except Failure:
                pass
            if time.monotonic() >= deadline:
                return None
            time.sleep(POLL_INTERVAL_SECONDS)
            snapshot = self.capture()

    def enter_monitor(self) -> Snapshot:
        self.backend.ensure_ready()
        snapshot = self.send_key("CTRL_O")
        settled = self._await_status_line(snapshot)
        if settled is not None:
            return settled

        # A Debug scenario can intentionally leave the monitor open while it
        # restores the C64.  Its footer replaces the ordinary CPU/VIC status
        # line, so the next independent monitor suite must first leave Debug
        # through the same transport-neutral key alias it uses elsewhere.
        if "MONITOR" in snapshot.text() and "Dbg" in snapshot.text():
            snapshot = self.send_key("CTRL_D")
            find_status_line(snapshot)
            return snapshot

        # CTRL_O toggles the monitor rather than opening it, so when the monitor
        # was already up the key above closed it. Whether it was up depends on
        # the transport: a REST machine reset takes the overlay and freeze UIs
        # down with the machine, but the telnet UI is a separate remote session
        # that keeps the monitor open across the same reset. Toggling back is
        # the direct way to reopen, and it leaves a freshly opened monitor just
        # as the menu navigation below does.
        snapshot = self.send_key("CTRL_O")
        settled = self._await_status_line(snapshot)
        if settled is not None:
            return settled

        snapshot = self.send_key("F5")
        snapshot = self.send_char("D")
        snapshot = self.send_key("ENTER")
        snapshot = self.send_key("DOWN")
        snapshot = self.send_key("ENTER")
        find_status_line(snapshot)
        return snapshot


def load_snapshots() -> Dict[str, Dict[str, Dict[str, str]]]:
    with SNAPSHOT_FILE.open("r", encoding="utf-8") as handle:
        return json.load(handle)


def assert_contains(snapshot: Snapshot, line_index: int, expected: str) -> None:
    actual = snapshot.line(line_index)
    if expected not in actual:
        raise Failure(
            f"Snapshot mismatch after {snapshot.last_command}: expected line {line_index} to contain\n"
            f"  {expected!r}\n"
            f"actual:\n  {actual!r}"
        )


def view_bank_status_forms(expected: str) -> Tuple[str, ...]:
    """The status-line spellings that report the monitor view bank in `expected`.

    `O` moves the monitor view bank only and never writes `$0001`, so the footer
    reads `CPU<n>` only while the live CPU execution bank still equals the view
    bank, and `C<live>O<n>` once the two differ. doc/machine_code_monitor.md
    documents both spellings, and both report the same view bank.

    Which spelling appears depends on the transport, not on the monitor. Under
    the overlay and freeze backends the live bank the monitor reads follows the
    view bank, so the footer stays on `CPU<n>`. Over telnet the C64 keeps running
    BASIC in bank 7 while `O` cycles the view, so the footer becomes `C7O<n>`.

    Only the checks that cycle the view bank use this. The live bank is a real
    assertion elsewhere - the debug suite separately expects `CPU5 ...` for a
    machine actually executing in bank 5 and `C5O7 ...` for one executing in
    bank 5 while the view shows bank 7 - so `ensure_status` stays exact.
    """
    if not re.match(r"^CPU[0-7]", expected):
        return (expected,)
    view_bank = expected[3]
    rest = expected[4:]
    return (expected,) + tuple(f"C{live}O{view_bank}{rest}" for live in "01234567")


def assert_view_bank_status(snapshot: Snapshot, expected: str) -> None:
    line_index = find_status_line(snapshot)
    accepted = view_bank_status_forms(expected)
    if any(form in snapshot.line(line_index) for form in accepted):
        return
    raise Failure(
        f"Snapshot mismatch after {snapshot.last_command}: expected line {line_index} "
        f"to contain one of {accepted!r}\n"
        f"actual:\n  {snapshot.line(line_index)!r}"
    )


def assert_line_contains_all(snapshot: Snapshot, values: Tuple[str, ...]) -> int:
    for index, line in enumerate(snapshot.lines):
        if all(value in line for value in values):
            return index
    raise Failure(
        f"Snapshot mismatch after {snapshot.last_command}: expected one line to contain {values!r}\n"
        f"actual:\n{snapshot.text()}"
    )


def assert_equal(label: str, expected: str, actual: str, command: str) -> None:
    if expected != actual:
        diff = "\n".join(
            difflib.unified_diff(
                expected.splitlines(),
                actual.splitlines(),
                fromfile="expected",
                tofile="actual",
                lineterm="",
            )
        )
        raise Failure(f"{label} failed after {command}\n{diff}")


def assert_highlight(snapshot: Snapshot, expected_cells: List[Tuple[int, int]], command: str) -> None:
    actual = sorted(snapshot.reverse_cells)
    expected = sorted(expected_cells)
    if actual != expected:
        raise Failure(
            f"Highlight mismatch after {command}: expected {expected}, actual {actual}\n"
            f"Screen:\n{snapshot.text()}"
        )


def assert_line_lacks(snapshot: Snapshot, forbidden: str) -> None:
    for line in snapshot.lines:
        if forbidden in line:
            raise Failure(
                f"Snapshot after {snapshot.last_command} unexpectedly contained {forbidden!r}\n"
                f"{snapshot.text()}"
            )


def assert_ascii_width(snapshot: Snapshot, row: int) -> None:
    line = snapshot.line(row)
    content = line[1:39]
    payload = content[5:37]
    if len(payload) != 32:
        raise Failure(f"ASCII width mismatch after {snapshot.last_command}: expected 32, got {len(payload)}")


def find_memory_rows(snapshot: Snapshot) -> List[int]:
    rows = [index for index, line in enumerate(snapshot.lines) if MEMORY_ROW_RE.match(line[1:] if line.startswith("|") else line)]
    if not rows:
        raise Failure(f"No memory rows found after {snapshot.last_command}\n{snapshot.text()}")
    return rows


def read_rest_memory(host: str, address: int, length: int) -> bytes:
    url = rest_lib.url_for(
        host, "/v1/machine:readmem",
        {"address": f"{address:04X}", "length": length})
    # Transport and retry policy come from tests/lib/rest.py; see rest.may_retry.
    with rest_lib.retrying_urlopen(urllib.request.Request(url), 5.0) as response:
        return response.read()


_REST_CLIENTS: Dict[str, UltimateApi] = {}


def rest_api(host: str) -> UltimateApi:
    """One API client per host, at the timeout the raw calls here already use.

    This suite threads a host string rather than a client, so the clients are
    kept here instead of being rebuilt per request. No password is sent,
    because the raw calls beside this one never sent one either.
    """
    client = _REST_CLIENTS.get(host)
    if client is None:
        client = UltimateApi(host, None, REST_TIMEOUT_SECONDS)
        _REST_CLIENTS[host] = client
    return client


def write_rest_memory(host: str, address: int, data: bytes) -> None:
    """Write bytes to C64 memory, through the library so the size is routed.

    The API has two writemem forms and they are not interchangeable: PUT
    carries the bytes as a hex query string and refuses more than
    api.MAX_WRITEMEM_HEX_BYTES of them, while POST uploads them as a file
    part. This used to build the query form by hand at any length, which was
    fine only because every caller happened to be writing a handful of bytes;
    the ASCII view check below writes 608. api.MachineApi.writemem picks the
    form from the length, and its docstring records what the over-long PUT
    actually does, which is either an HTTP 400 or no answer at all depending
    on how far over it is.

    Writing the same bytes twice is the same as writing them once, so the
    transport may retry.
    """
    if not data:
        raise Failure("write_rest_memory requires at least one byte")
    rest_api(host).machine.writemem(address, data, idempotent=True)


def write_rest_memory_confirmed(host: str, address: int, data: bytes,
                                attempts: int = 8, settle: float = 0.25) -> None:
    """Write memory and confirm it is still there afterwards.

    A machine:reset hands the C64 to the KERNAL, whose start-up walks up through
    RAM and zeroes it. A fixture written into that window is wiped before the
    test ever runs, and the check then fails on a byte that was written
    correctly. Re-arm rather than guess how long the boot takes: the write is
    repeated until a read-back matches, so a boot-time wipe costs one more
    attempt instead of the check.
    """
    last = b""
    for _ in range(attempts):
        write_rest_memory(host, address, data)
        time.sleep(settle)
        last = read_rest_memory(host, address, len(data))
        if last == data:
            return
    raise Failure(
        f"${address:04X} would not hold {data.hex().upper()} after {attempts} "
        f"attempts; last read {last.hex().upper()}"
    )


def reset_rest_machine(host: str, password: Optional[str]) -> None:
    headers = {"X-Password": password} if password else {}
    for attempt in range(12):
        try:
            request = urllib.request.Request(
                rest_lib.url_for(host, "/v1/machine:menu_screen"),
                headers=headers, method="GET"
            )
            with rest_lib.retrying_urlopen(request, 5.0):
                pass
        except urllib.error.HTTPError as exc:
            if exc.code == 404:
                break
            raise
        # F8 leaves the menu from any depth; RUN/STOP covers the editors it does
        # not reach. Never RETURN: it activates the entry under the cursor.
        keys = ["left_shift", "f7"] if attempt < 8 else ["run_stop"]
        body = json.dumps({
            "events": [{"kind": "keyboard", "inputs": keys, "transition": "tap"}]
        }).encode("utf-8")
        request = urllib.request.Request(
            rest_lib.url_for(host, "/v1/machine:input"),
            data=body,
            headers={**headers, "Content-Type": "application/json"},
            method="POST",
        )
        # menu_button is a toggle, so it is never declared idempotent: sending
        # it twice puts the menu back where it started. The default policy only
        # resends when no response was seen at all, which means it was not
        # applied.
        with rest_lib.retrying_urlopen(request, 5.0):
            pass
        time.sleep(0.25)
    else:
        request = urllib.request.Request(
            rest_lib.url_for(host, "/v1/machine:menu_button"),
            data=b"", headers=headers, method="PUT"
        )
        with rest_lib.retrying_urlopen(request, 5.0):
            pass
        time.sleep(0.5)

    # A successful reset request only means the device accepted it. The shared
    # fixture waits for the fresh BASIC-ready screen before it places the next
    # program, which is required on the cartridge's reset-mediated G path.
    UltimateApi(host, password, REST_TIMEOUT_SECONDS).machine.reset()


def wait_for_rest_byte(host: str, address: int, expected: int, timeout: float = 2.0) -> None:
    deadline = time.time() + timeout
    while time.time() < deadline:
        value = read_rest_memory(host, address, 1)[0]
        if value == expected:
            return
        time.sleep(0.05)
    raise Failure(f"Memory at ${address:04X} did not become ${expected:02X}")


def assert_rest_matches_row(snapshot: Snapshot, line_index: int, address: int, rest_host: str) -> None:
    monitor_bytes = parse_memory_row(snapshot, address, line_index=line_index)
    rest_bytes = read_rest_memory(rest_host, address, len(monitor_bytes))
    if rest_bytes != monitor_bytes:
        raise Failure(
            f"REST/monitor mismatch at ${address:04X} after {snapshot.last_command}:\n"
            f"  monitor: {' '.join(f'{byte:02X}' for byte in monitor_bytes)}\n"
            f"  rest:    {' '.join(f'{byte:02X}' for byte in rest_bytes)}"
        )


def parse_memory_row(snapshot: Snapshot, address: int, line_index: Optional[int] = None) -> bytes:
    target = f"{address:04X}"
    candidate_indexes = [line_index] if line_index is not None else range(len(snapshot.lines))

    for index in candidate_indexes:
        if index is None:
            continue
        actual = snapshot.line(index).strip()
        if actual.startswith("|"):
            actual = actual[1:]
        if actual.endswith("|"):
            actual = actual[:-1]
        actual = actual.strip()
        match = re.match(rf"^{target}((?: [0-9A-F]{{2}})+)", actual, re.IGNORECASE)
        if match:
            return bytes.fromhex(match.group(1))

    raise Failure(
        f"Unable to parse monitor memory row at ${address:04X} after {snapshot.last_command!r}:\n"
        f"{snapshot.text()}"
    )


def parse_text_row(snapshot: Snapshot, address: int, line_index: Optional[int] = None) -> str:
    target = f"{address:04X} "
    candidate_indexes = [line_index] if line_index is not None else range(len(snapshot.lines))

    for index in candidate_indexes:
        if index is None:
            continue
        actual = snapshot.line(index)
        if actual.startswith("|"):
            actual = actual[1:]
        if actual.startswith(target):
            if len(actual) < 37:
                raise Failure(
                    f"Text row at ${address:04X} after {snapshot.last_command!r} was too short:\n{snapshot.text()}"
                )
            return actual[5:37]

    raise Failure(
        f"Unable to parse monitor text row at ${address:04X} after {snapshot.last_command!r}:\n"
        f"{snapshot.text()}"
    )


def ensure_status(session: MonitorSession, expected: str) -> Snapshot:
    screen = session.capture()
    for _ in range(8):
        try:
            line_index = find_status_line(screen)
        except Failure:
            line_index = -1
        if line_index >= 0 and expected in screen.line(line_index):
            return screen
        screen = session.send_char("o")
    raise Failure(
        f"Unable to reach expected CPU/VIC status {expected!r}; last status line was {screen.line(find_status_line(screen))!r}"
    )


def ensure_view_bank_status(session: MonitorSession, expected: str) -> Snapshot:
    """ensure_status for a view bank reached with `O`, accepting either spelling."""
    accepted = view_bank_status_forms(expected)
    screen = session.capture()
    for _ in range(8):
        try:
            line_index = find_status_line(screen)
        except Failure:
            line_index = -1
        if line_index >= 0 and any(form in screen.line(line_index) for form in accepted):
            return screen
        screen = session.send_char("o")
    raise Failure(
        f"Unable to reach expected monitor view bank {expected!r}; last status line was "
        f"{screen.line(find_status_line(screen))!r}"
    )


def cycle_cpu_bank_from_cpu7(session: MonitorSession, target_status: str, steps: int) -> Snapshot:
    screen = ensure_view_bank_status(session, "CPU7 $A:BAS $D:I/O $E:KRN VIC")

    for _ in range(steps):
        screen = session.send_char("o")

    assert_view_bank_status(screen, target_status)
    return screen


def ensure_view(session: MonitorSession, expected: str) -> Snapshot:
    key = VIEW_KEYS.get(expected)
    if key is None:
        raise Failure(f"Unsupported monitor view selector for {expected!r}")
    screen = session.capture()
    for _ in range(3):
        try:
            screen.find_line_containing(expected)
            return screen
        except Failure:
            pass
        screen = session.send_char(key)
    raise Failure(f"Unable to reach expected monitor view {expected!r}; screen was\n{screen.text()}")


def ensure_screen_charset(session: MonitorSession, expected: str) -> Snapshot:
    if expected not in ("U/G", "L/U"):
        raise Failure(f"Unsupported screen charset request {expected!r}")

    screen = ensure_view(session, "SCR ")
    header_row = screen.find_line_containing("MONITOR SCR")
    if expected in screen.line(header_row):
        return screen

    screen = session.send_char("U")
    header_row = screen.find_line_containing("MONITOR SCR")
    if expected not in screen.line(header_row):
        raise Failure(
            f"Unable to switch Screen view to {expected}; header was {screen.line(header_row)!r}"
        )
    return screen


def ensure_hex_width(session: MonitorSession, expected_width: int) -> Snapshot:
    if expected_width not in (8, 16):
        raise Failure(f"Unsupported hex width request {expected_width}")

    screen = ensure_view(session, "HEX ")
    rows = find_memory_rows(screen)
    row_text = screen.line(rows[0]).strip()
    if row_text.startswith("|"):
        row_text = row_text[1:]
    if row_text.endswith("|"):
        row_text = row_text[:-1]
    row_text = row_text.strip()
    is_width_16 = MEMORY_ROW_16_RE.match(row_text) is not None

    if (expected_width == 16) != is_width_16:
        screen = session.send_char("W")
    return screen


def run_character_mapping_test(session: MonitorSession, rest_host: str) -> None:
    ascii_view_addr = 0x3200
    ascii_edit_addr = 0x3220
    screen_view_addr = 0x3240
    screen_edit_ug_addr = 0x3260
    screen_edit_lu_addr = 0x3280

    write_rest_memory(rest_host, ascii_view_addr, bytes((0x41, 0x61, 0x20, 0x7E, 0x1F, 0x80) + (0x20,) * 26))
    write_rest_memory(rest_host, ascii_edit_addr, b"    ")
    write_rest_memory(rest_host, screen_view_addr, bytes((0x01, 0x1A, 0x20, 0x23, 0x41, 0x42, 0x80) + (0x20,) * 25))
    write_rest_memory(rest_host, screen_edit_ug_addr, b"    ")
    write_rest_memory(rest_host, screen_edit_lu_addr, b"    ")

    screen = ensure_view(session, "ASC ")
    screen = session.goto(f"{ascii_view_addr:04X}")
    ascii_payload = parse_text_row(screen, ascii_view_addr)
    if ascii_payload[:6] != "Aa ~..":
        raise Failure(
            f"ASCII view mapping mismatch at ${ascii_view_addr:04X}: expected 'Aa ~..', got {ascii_payload[:6]!r}"
        )

    screen = session.goto(f"{ascii_edit_addr:04X}")
    screen = session.send_char("E")
    for ch in "aA# ":
        screen = session.send_char(ch)
    screen = session.send_key("CTRL_E")
    if read_rest_memory(rest_host, ascii_edit_addr, 4) != b"aA# ":
        raise Failure(
            f"ASCII edit mapping mismatch at ${ascii_edit_addr:04X}: "
            f"got {read_rest_memory(rest_host, ascii_edit_addr, 4).hex().upper()}"
        )

    screen = ensure_screen_charset(session, "U/G")
    screen = session.goto(f"{screen_view_addr:04X}")
    header_row = screen.find_line_containing("MONITOR SCR")
    if "U/G" not in screen.line(header_row):
        raise Failure(f"Screen U/G header mismatch: {screen.line(header_row)!r}")
    screen_payload = parse_text_row(screen, screen_view_addr)
    if screen_payload[:7] != "AZ #S|@":
        raise Failure(
            f"Screen U/G view mapping mismatch at ${screen_view_addr:04X}: expected 'AZ #S|@', got {screen_payload[:7]!r}"
        )

    screen = ensure_screen_charset(session, "L/U")
    screen = session.goto(f"{screen_view_addr:04X}")
    header_row = screen.find_line_containing("MONITOR SCR")
    if "L/U" not in screen.line(header_row):
        raise Failure(f"Screen L/U header mismatch: {screen.line(header_row)!r}")
    screen_payload = parse_text_row(screen, screen_view_addr)
    if screen_payload[:7] != "az #AB@":
        raise Failure(
            f"Screen L/U view mapping mismatch at ${screen_view_addr:04X}: expected 'az #AB@', got {screen_payload[:7]!r}"
        )

    screen = ensure_view(session, "ASC ")
    screen = session.goto(f"{ascii_view_addr:04X}")
    ascii_payload = parse_text_row(screen, ascii_view_addr)
    if ascii_payload[:6] != "Aa ~..":
        raise Failure(
            f"ASCII view must stay literal after Screen charset toggles; got {ascii_payload[:6]!r}"
        )

    screen = ensure_screen_charset(session, "U/G")
    screen = session.goto(f"{screen_edit_ug_addr:04X}")
    screen = session.send_char("E")
    for ch in "aA# ":
        screen = session.send_char(ch)
    screen = session.send_key("CTRL_E")
    if read_rest_memory(rest_host, screen_edit_ug_addr, 4) != bytes((0x01, 0x01, 0x23, 0x20)):
        raise Failure(
            f"Screen U/G edit mapping mismatch at ${screen_edit_ug_addr:04X}: "
            f"got {read_rest_memory(rest_host, screen_edit_ug_addr, 4).hex().upper()}"
        )

    screen = ensure_screen_charset(session, "L/U")
    screen = session.goto(f"{screen_edit_lu_addr:04X}")
    screen = session.send_char("E")
    for ch in "aA# ":
        screen = session.send_char(ch)
    screen = session.send_key("CTRL_E")
    if read_rest_memory(rest_host, screen_edit_lu_addr, 4) != bytes((0x01, 0x41, 0x23, 0x20)):
        raise Failure(
            f"Screen L/U edit mapping mismatch at ${screen_edit_lu_addr:04X}: "
            f"got {read_rest_memory(rest_host, screen_edit_lu_addr, 4).hex().upper()}"
        )


def goto_and_read_byte(
    session: MonitorSession, address: str, address_int: int,
    expected: Optional[int] = None, retries: int = 3,
) -> int:
    """Navigate to `address` and read its first byte.

    A freshly-parked memory view can show one stale byte immediately after a
    DMA release (the same class of fetch race tracked elsewhere in this
    firmware around a fresh view's first fetch after a G that unfreezes and
    re-parks the CPU). When `expected` is given, retry with a fresh
    navigation (away and back, forcing a redraw rather than trusting a
    cached one) until it matches or the budget runs out; the caller's own
    comparison still runs on whatever this last returns, so a genuine
    mismatch still fails the check."""
    value = 0
    for attempt in range(retries):
        if attempt > 0:
            session.goto("E000")  # away, so the next goto is a fresh navigation
        screen = session.goto(address)
        value = parse_memory_row(screen, address_int)[0]
        if expected is None or value == expected:
            return value
    return value


def machine_runs_behind_the_ui(mode: str) -> bool:
    """Whether the C64 keeps executing while this mode's monitor UI is up.

    Under Overlay and Telnet it does: the menu is drawn without stopping the
    machine, so a program launched by an earlier G is still running while the
    next one is being set up. Under Freeze the menu stops the machine as it
    opens, so nothing is running and there is nothing to stop.
    """
    return mode != MODE_FREEZE


def stop_running_program(rest_host: str) -> None:
    """Halt whatever the C64 is executing, without relying on a BRK.

    A BRK placed in memory by DMA is not reliably honoured by the running
    6510. A test that launches a program and then assumes it stopped at its
    trailing BRK is therefore racing the machine: the program can still be
    executing, and whatever the test writes next is overwritten by it.
    Observed as "Memory at $2000 did not become $5A", with $2000 holding the
    value the previous iteration's program writes, in one run out of three.

    Pausing stops the machine outright, which is one of the three things that
    reliably do: pause, reset, or having the BRK in memory before the program
    is launched. Reset is not used here because the monitor holds the machine
    while its UI is up, and releasing it from under the UI is what takes the
    device off the network.

    Only call this where machine_runs_behind_the_ui() is true, and always with
    the matching resume_machine(). The rule being followed is the firmware's
    own: C64_Subsys::dma_load_raw_buffer stops the C64 only when it finds it
    running, and resumes it only if it was the one that stopped it. Under
    Freeze this pair would break that rule and resume a machine the freeze
    stopped. MENU_C64_PAUSE runs C64::stop(), which overwrites the raster and
    VIC interrupt registers C64::freeze() saved for the eventual unfreeze, and
    MENU_C64_RESUME runs C64::resume(), which sets C64_MODE back to
    MODE_NORMAL and releases the CPU while isFrozen is still set and the UI's
    own I/O is still installed.

    Where it is called, it composes with the DMA path rather than fighting it:
    dma_load_raw_buffer sees an already-stopped machine, so the writes that
    follow leave it stopped instead of resuming it between them.
    """
    request = urllib.request.Request(
        rest_lib.url_for(rest_host, "/v1/machine:pause"), data=b"", method="PUT")
    with rest_lib.retrying_urlopen(request, REST_TIMEOUT_SECONDS, idempotent=True):
        pass


def resume_machine(rest_host: str) -> None:
    """Undo stop_running_program, so G starts from the normal running state."""
    request = urllib.request.Request(
        rest_lib.url_for(rest_host, "/v1/machine:resume"), data=b"", method="PUT")
    with rest_lib.retrying_urlopen(request, REST_TIMEOUT_SECONDS, idempotent=True):
        pass


def run_go_repeat_test(session: MonitorSession, rest_host: str, mode: str) -> None:
    sentinel = 0x5A
    values = (0x42, 0x37, 0x99)
    stop_first = machine_runs_behind_the_ui(mode)
    # Where the test program is placed. The U2+L has no NMI trampoline to hand
    # the CPU a new PC with (see tests/e2e/monitor/U2_CARTRIDGE_NMI.md), so its
    # G runs the boot cartridge, which resets the C64 first. The reset takes
    # the BASIC program area with it, so a program at $0810 is gone before it
    # can run there. $1100 is above everything the reset clears and below the
    # sentinel, and behaves identically on both targets.
    program = 0x1100 if is_u2() else 0x0810

    for value in values:
        # A G program may not retire at its trailing BRK before the monitor
        # returns. A pause ordinarily stops it before the next DMA write, but
        # an already-pending stop can lose that race on an Overlay/Telnet UI.
        # Begin each independent handoff from the same reset baseline instead;
        # this is the only device-level operation that proves no old program
        # remains to overwrite the sentinel.
        if stop_first:
            reset_rest_machine(rest_host, None)
            session.enter_monitor()
        # Both fixtures are written through the confirming helper: this loop
        # runs straight after a reset, so either can land in the window where
        # the KERNAL's RAM initialisation is still walking up through memory.
        # Confirming also covers what the read-back it replaces was for:
        # reading the monitor's view first cannot tell "the write has not
        # landed" from "the view has not refreshed", and under Freeze that read
        # has come back with the previous iteration's value.
        write_rest_memory_confirmed(
            rest_host, program, bytes((0xA9, value, 0x8D, 0x00, 0x20, 0x00)))
        write_rest_memory_confirmed(rest_host, 0x2000, bytes((sentinel,)))

        ensure_view(session, "HEX ")
        before = goto_and_read_byte(session, "2000", 0x2000, expected=sentinel)
        if before != sentinel:
            # The sentinel is known to be in memory: the confirming write above
            # would have failed otherwise. Report what REST sees now as well,
            # so the message says whether memory changed under the test or the
            # monitor's view simply did not follow it.
            rest_now = read_rest_memory(rest_host, 0x2000, 1)[0]
            raise Failure(
                f"G precondition failed for ${value:02X}: expected ${sentinel:02X} at $2000, "
                f"monitor view shows ${before:02X}, REST reads ${rest_now:02X} "
                f"({'monitor view is stale' if rest_now == sentinel else 'memory changed'})"
            )

        session.goto_run(f"{program:04X}")
        wait_for_rest_byte(rest_host, 0x2000, value)

        session.enter_monitor()
        ensure_view(session, "HEX ")
        after = goto_and_read_byte(session, "2000", 0x2000, expected=value)
        if after != value:
            raise Failure(
                f"G postcondition failed for ${value:02X}: expected ${value:02X} at $2000, got ${after:02X}"
            )


def run_go_visible_state_test(session: MonitorSession, rest_host: str) -> None:
    sentinel = 0xA5
    done = 0x5C

    pre_d011 = read_rest_memory(rest_host, 0xD011, 1)[0]
    pre_d020 = read_rest_memory(rest_host, 0xD020, 1)[0]

    write_rest_memory(rest_host, 0x0810, bytes((0xEE, 0x21, 0xD0, 0xA9, done, 0x8D, 0x00, 0x20, 0x00)))
    write_rest_memory(rest_host, 0x2000, bytes((sentinel,)))
    session.goto_run("0810")
    wait_for_rest_byte(rest_host, 0x2000, done)

    post_d011 = read_rest_memory(rest_host, 0xD011, 1)[0]
    post_d020 = read_rest_memory(rest_host, 0xD020, 1)[0]

    if (post_d011 & 0x7F) != (pre_d011 & 0x7F):
        raise Failure(
            f"G visual-state check failed: $D011 changed from ${pre_d011:02X} to ${post_d011:02X}"
        )
    if post_d020 != pre_d020:
        raise Failure(
            f"G visual-state check failed: $D020 changed from ${pre_d020:02X} to ${post_d020:02X}"
        )

    session.enter_monitor()


def run_bookmark_test(session: MonitorSession) -> None:
    screen = ensure_view(session, "HEX ")

    screen = session.send_key("CTRL_B")
    screen.find_line_containing("BOOKMARKS")
    screen = session.send_key("DOWN")
    screen = session.send_key("DEL")
    assert_line_contains_all(screen, ("1 SCREEN", "$0400", "SCR 32"))
    screen = session.send_key("CTRL_B")
    screen.find_line_containing("MONITOR")

    screen = session.goto("C123")
    screen.find_line_containing("MONITOR HEX $C123")
    screen = session.send_char("W")
    screen = session.send_key("CTRL_B")
    screen.find_line_containing("BOOKMARKS")
    screen = session.send_key("DOWN")
    screen = session.send_char("S")
    assert_line_contains_all(screen, ("BM1 SCREEN $C123 HEX W16", "SET"))
    screen = session.send_key("CTRL_B")
    screen.find_line_containing("MONITOR HEX $C123")

    screen = session.goto("E000")
    screen.find_line_containing("MONITOR HEX $E000")
    screen = session.send_key("CBM_1")
    screen.find_line_containing("MONITOR HEX $C123")
    screen.find_line_containing("BM1 SCREEN $C123 HEX W16")

    screen = session.send_key("CTRL_B")
    screen.find_line_containing("BOOKMARKS")
    assert_line_contains_all(screen, ("1 SCREEN", "$C123", "HEX 16"))
    screen.find_line_containing("0-9/RET Jmp  S Set  L Label  DEL Reset")

    screen = session.send_key("DOWN")
    screen = session.send_char("L")
    screen = session.send_text("\b\b\b\b\b\bE2E\r", "bookmark label E2E")
    assert_line_contains_all(screen, ("1 E2E", "$C123", "HEX 16"))

    screen = session.send_key("CTRL_B")
    screen.find_line_containing("MONITOR HEX $C123")
    screen = session.goto("E000")
    screen.find_line_containing("MONITOR HEX $E000")
    screen = session.send_key("CBM_1")
    screen.find_line_containing("MONITOR HEX $C123")
    screen.find_line_containing("BM1 E2E $C123 HEX W16")


def run_telnet_poll_guard_test(session: MonitorSession) -> None:
    screen = ensure_view(session, "HEX ")
    screen = session.send_char("P")
    screen.find_line_containing("POLL MODE UNAVAILABLE OVE")

    screen = session.send_char("o")
    screen.find_line_containing("MONITOR HEX")
    assert_line_lacks(screen, "Poll")


def run_memory_bookmark_width_test(session: MonitorSession, rest_host: str) -> None:
    write_rest_memory(rest_host, 0x3000, bytes(range(0x10)))

    screen = session.send_key("CTRL_B")
    screen.find_line_containing("BOOKMARKS")
    screen = session.send_key("DOWN")
    screen = session.send_key("DEL")
    assert_line_contains_all(screen, ("1 SCREEN", "$0400", "SCR 32"))
    screen = session.send_key("CTRL_B")
    screen.find_line_containing("MONITOR")

    screen = ensure_hex_width(session, 8)
    screen = session.goto("3000")
    screen.find_line_containing("3000 00 01 02 03 04 05 06 07")

    screen = session.send_char("W")
    screen.find_line_containing("3000 0001020304050607 08090A0B0C0D0E0F")

    screen = session.send_key("CTRL_B")
    screen.find_line_containing("BOOKMARKS")
    screen = session.send_key("DOWN")
    screen = session.send_char("S")
    assert_line_contains_all(screen, ("BM1 SCREEN $3000 HEX W16", "SET"))
    screen = session.send_key("CTRL_B")
    screen.find_line_containing("MONITOR HEX $3000")

    screen = session.goto("E000")
    screen.find_line_containing("MONITOR HEX $E000")
    screen = session.send_key("CBM_1")
    screen.find_line_containing("MONITOR HEX $3000")
    screen.find_line_containing("BM1 SCREEN $3000 HEX W16")
    screen.find_line_containing("3000 0001020304050607 08090A0B0C0D0E0F")

    screen = session.send_key("CTRL_B")
    screen.find_line_containing("BOOKMARKS")
    assert_line_contains_all(screen, ("1 SCREEN", "$3000", "HEX 16"))
    screen = session.send_key("CTRL_B")
    screen.find_line_containing("MONITOR HEX $3000")


def run_binary_bookmark_width_test(session: MonitorSession, rest_host: str) -> None:
    # Widths 3 and 4 align their rows down to $30FF, so the row the checks below read
    # starts one byte before the sentinel. Seed that byte too: leaving it to whatever
    # an earlier suite left in RAM made this test pass or fail depending on run order.
    write_rest_memory(rest_host, 0x30FF, bytes((0x00, 0x12, 0x34, 0x56, 0x78)))

    screen = session.send_key("CTRL_B")
    screen.find_line_containing("BOOKMARKS")
    screen = session.send_key("DOWN")
    screen = session.send_key("DEL")
    assert_line_contains_all(screen, ("1 SCREEN", "$0400", "SCR 32"))
    screen = session.send_key("CTRL_B")
    screen.find_line_containing("MONITOR")

    screen = ensure_view(session, "BIN ")
    screen = session.goto("3100")
    for _ in range(5):
        try:
            screen.find_line_containing("3100 ...*..*. 12")
            break
        except Failure:
            screen = session.send_char("W")
    screen.find_line_containing("3100 ...*..*. 12")

    screen = session.send_char("W")
    screen.find_line_containing("3100 ...*..*. ..**.*.. 12 34")

    screen = session.send_char("W")
    screen.find_line_containing("30FF ........ ...*..*. ..**.*.. 001234")

    screen = session.send_char("W")
    screen.find_line_containing("30FF ...........*..*...**.*.. 00 12 34")

    screen = session.send_char("W")
    screen.find_line_containing("3100 ...*..*...**.*...*.*.**..****...")
    assert_line_lacks(screen, "12 34 56 78")

    screen = session.send_key("CTRL_B")
    screen.find_line_containing("BOOKMARKS")
    screen = session.send_key("DOWN")
    screen = session.send_char("S")
    assert_line_contains_all(screen, ("BM1 SCREEN $3100 BIN W4", "SET"))
    screen = session.send_key("CTRL_B")
    screen.find_line_containing("MONITOR BIN $3100")

    screen = session.goto("E000")
    screen.find_line_containing("MONITOR BIN $E000")
    screen = session.send_key("CBM_1")
    screen.find_line_containing("MONITOR BIN $3100")
    screen.find_line_containing("BM1 SCREEN $3100 BIN W4")
    screen.find_line_containing("3100 ...*..*...**.*...*.*.**..****...")
    assert_line_lacks(screen, "12 34 56 78")

    screen = session.send_key("CTRL_B")
    screen.find_line_containing("BOOKMARKS")
    assert_line_contains_all(screen, ("1 SCREEN", "$3100", "BIN  4"))
    screen = session.send_key("CTRL_B")
    screen.find_line_containing("MONITOR BIN $3100")


def run_follow_return_test(session: MonitorSession, rest_host: str) -> None:
    # ASM view test data:
    #   $3340: JSR $3360   20 60 33 / NOP EA
    #   $3350: BNE $3354   D0 02 / NOP EA EA / RTS 60
    #   $3360: RTS         60 / NOP EA EA
    write_rest_memory(rest_host, 0x3340, bytes((0x20, 0x60, 0x33, 0xEA)))
    write_rest_memory(rest_host, 0x3350, bytes((0xD0, 0x02, 0xEA, 0xEA, 0x60)))
    write_rest_memory(rest_host, 0x3360, bytes((0x60, 0xEA, 0xEA)))
    screen = ensure_view(session, "ASM ")
    screen = session.goto("3340")
    screen = ensure_view(session, "ASM ")
    screen.find_line_containing("JSR $3360")
    # ENTER follows JSR; ENTER at the non-followable target returns
    screen = session.send_key("ENTER")
    screen.find_line_containing("MONITOR ASM $3360")
    screen.find_line_containing("F0 JMP $3360")
    screen = session.send_key("ENTER")
    screen.find_line_containing("MONITOR ASM $3340")
    screen.find_line_containing("F0 RET $3340")

    # BNE branch follow; ENTER in HEX must not trigger Back
    screen = session.goto("3350")
    screen = ensure_view(session, "ASM ")
    screen.find_line_containing("BNE $3354")
    screen = session.send_key("ENTER")
    screen.find_line_containing("MONITOR ASM $3354")
    screen.find_line_containing("F0 JMP $3354")
    screen = ensure_view(session, "HEX ")
    screen.find_line_containing("MONITOR HEX $3354")
    screen = session.send_key("ENTER")
    screen.find_line_containing("MONITOR HEX $3354")
    screen = ensure_view(session, "ASM ")
    screen.find_line_containing("MONITOR ASM $3354")
    screen = session.send_key("ENTER")
    screen.find_line_containing("MONITOR ASM $3350")
    screen.find_line_containing("F0 RET $3350")

    # RTS is not a static follow target
    screen = session.goto("3360")
    screen = ensure_view(session, "ASM ")
    screen.find_line_containing("RTS")
    screen = session.send_key("ENTER")
    screen.find_line_containing("MONITOR ASM $3360")
    assert_line_lacks(screen, "F0 JMP $")

def run_asm_edit_validation_test(session: MonitorSession, rest_host: str) -> None:
    # Bug 2 (Return advances) test program:
    #   $3380: LDA #$01   A9 01     (2 bytes)
    #   $3382: NOP        EA        (1 byte)
    #   $3383: LDA $C000  AD 00 C0  (3 bytes)
    write_rest_memory(rest_host, 0x3380, bytes((0xA9, 0x01, 0xEA, 0xAD, 0x00, 0xC0)))
    screen = ensure_view(session, "ASM ")
    screen = session.goto("3380")
    screen = ensure_view(session, "ASM ")
    screen.find_line_containing("MONITOR ASM $3380")
    screen = session.send_char("e")  # enter assembly edit mode
    screen.find_line_containing("MONITOR ASM $3380")
    # RETURN commits the current line and advances by the instruction length.
    screen = session.send_key("ENTER")  # past LDA #$01 (2 bytes)
    screen.find_line_containing("MONITOR ASM $3382")
    screen = session.send_key("ENTER")  # past NOP (1 byte)
    screen.find_line_containing("MONITOR ASM $3383")
    screen = session.send_key("ENTER")  # past LDA $C000 (3 bytes)
    screen.find_line_containing("MONITOR ASM $3386")
    screen = session.send_key("ESC")    # leave edit mode

    # Bug 1 (invalid mnemonic rejected): edit a clean NOP-filled region so the
    # opcode picker is exercised in isolation.
    write_rest_memory(rest_host, 0x33A0, bytes([0xEA] * 8))
    screen = ensure_view(session, "ASM ")
    screen = session.goto("33A0")
    screen = ensure_view(session, "ASM ")
    screen = session.send_char("e")  # enter assembly edit mode
    # A valid prefix is accepted and the completion list stays coherent.
    screen = session.send_char("A")
    screen.find_line_containing("ADC")
    screen = session.send_char("D")
    screen.find_line_containing(" AD_")
    screen.find_line_containing("ADC")
    # An invalid third letter (ADD is not a 6502 mnemonic) is rejected: the
    # mnemonic field stays at AD and nothing bleeds into the operand area.
    screen = session.send_char("D")
    screen.find_line_containing(" AD_")
    assert_line_lacks(screen, "ADD")
    # Repeated invalid input does not overflow the field or bleed.
    for _ in range(3):
        screen = session.send_char("A")
    screen.find_line_containing(" AD_")
    assert_line_lacks(screen, "ADD")
    assert_line_lacks(screen, "ADA")
    screen = session.send_key("ESC")  # close the picker (stay in edit mode)
    # A first letter that begins no supported mnemonic (G) is rejected and never
    # joins the prefix; a following valid letter still opens the picker.
    screen = session.send_char("G")
    screen = session.send_char("L")
    screen.find_line_containing(" L_")
    assert_line_lacks(screen, "GL")
    screen.find_line_containing("LDA")
    screen = session.send_key("ESC")  # close the picker
    session.send_key("ESC")           # leave edit mode


# Per-keystroke output budget for scrolling the opcode dropdown over telnet.
# Regression guard: on a full-refresh (telnet/VT100) screen the buggy path
# redrew the WHOLE screen on every cursor up/down keystroke (measured 1727 bytes
# per keystroke). Under cursor-key autorepeat that flood wedged/aborted the
# telnet monitor connection. The fix repaints only the dropdown overlay (the
# same incremental path Freeze/Overlay already use), a few hundred bytes per
# keystroke. The threshold sits well below a full-screen redraw and well above
# an overlay-only repaint so it cleanly separates buggy from fixed.
DROPDOWN_SCROLL_BYTE_BUDGET = 1000


def run_telnet_dropdown_scroll_flood_test(session: MonitorSession, rest_host: str) -> None:
    # Stable, deterministic playground: a run of NOPs so the disassembly is fixed
    # and the opcode dropdown can be anchored mid-screen (so it is larger than the
    # visible area below it and genuinely scrolls internally).
    write_rest_memory(rest_host, 0x33C0, bytes([0xEA] * 32))
    screen = ensure_view(session, "ASM ")
    screen = session.goto("33C0")
    screen = ensure_view(session, "ASM ")
    screen.find_line_containing("MONITOR ASM $33C0")
    screen = session.send_char("e")  # enter assembly edit mode
    # Step the cursor down so the dropdown anchor sits low on the screen; with the
    # large "L" candidate list this guarantees the list exceeds the visible window
    # and scrolling pushes content past the bottom row and back.
    for _ in range(10):
        screen = session.send_key("DOWN")
    screen = session.send_char("L")  # open the large opcode dropdown
    screen.find_line_containing(" L_")
    screen.find_line_containing("LDA")

    # Bounded down/up scroll burst (scroll to the bottom and beyond, then back up,
    # repeated a few times). Every scroll keystroke is measured. This is NOT a
    # soak test: the per-keystroke output volume is identical for every scroll
    # key, so a small bounded burst reliably exposes the flood.
    max_bytes = 0
    sample = []
    cycles = 2
    down_n = 12
    up_n = 12
    for _ in range(cycles):
        for _ in range(down_n):
            _, n = session.send_key_count("DOWN")
            max_bytes = max(max_bytes, n)
            sample.append(n)
        for _ in range(up_n):
            _, n = session.send_key_count("UP")
            max_bytes = max(max_bytes, n)
            sample.append(n)

    # The dropdown must remain coherent through the whole burst.
    screen = session.capture()
    screen.find_line_containing(" L_")
    screen.find_line_containing("LDA")

    if max_bytes > DROPDOWN_SCROLL_BYTE_BUDGET:
        raise Failure(
            "Telnet opcode dropdown floods on scroll: a single cursor keystroke "
            f"emitted up to {max_bytes} bytes (budget {DROPDOWN_SCROLL_BYTE_BUDGET}). "
            "Scrolling must repaint only the dropdown overlay, not the whole "
            f"screen. First samples: {sample[:8]}"
        )

    # Connection must still be alive and the monitor must still respond after the
    # bounded scroll burst.
    screen = session.send_key("ESC")  # close the dropdown (stays in edit mode)
    screen.find_line_containing("MONITOR ASM")
    screen = session.send_key("ESC")  # leave edit mode
    find_status_line(screen)


def run_number_arithmetic_test(session: MonitorSession, rest_host: str) -> None:
    write_rest_memory(rest_host, 0x3370, bytes((0x20, 0x00, 0xC0, 0xEA)))

    screen = ensure_view(session, "ASM ")
    screen = session.goto("3370")
    screen = ensure_view(session, "ASM ")
    screen.find_line_containing("JSR $C000")
    screen = session.send_char("N")
    screen.find_line_containing("MONITOR NUM $3371 WOR")
    screen.find_line_containing("Calc with +-*/")
    screen = session.send_char("+")
    screen.find_line_containing("Expr=$C000+")
    screen = session.send_text("$28=", "number expr +$28")
    screen.find_line_containing("Hex      $C028")
    assert_line_lacks(screen, "Expr=")
    screen.find_line_containing("Calc with +-*/")

    screen = session.send_char("/")
    screen = session.send_text("0\r", "number expr div0")
    screen.find_line_containing("Hex      $C028")
    screen.find_line_containing("DIV/0")
    screen = session.send_key("ESC")
    screen.find_line_containing("Calc with +-*/")
    screen = session.send_key("ESC")
    screen.find_line_containing("MONITOR ASM $3370")


# ---------------------------------------------------------------------------
# Save / Load round-trip tests
#
# These drive the monitor's S(ave) and L(oad) commands over telnet, exercising
# the firmware file picker (a TreeBrowser in PICK mode rendered on the same
# VT100 screen). They round-trip a known memory pattern to two kinds of target,
# both under the RAM-disk /Temp folder:
#   * a plain top-level PRG file
#   * a PRG file inside a freshly created D64 disk image
# The pattern is written/verified through the existing REST memory API, and a
# unique per-run token keeps each run from colliding with leftovers (the picker
# never has to answer an "overwrite?" prompt, and the D64 is genuinely new).
# ---------------------------------------------------------------------------

def picker_path(snapshot: Snapshot) -> str:
    """Return the directory path the file picker currently shows.

    The picker prints the active path on the line directly below its bottom
    border. Anchoring on the border rather than "last non-blank line" matters
    over REST/Overlay: the on-device Overlay screen is the full 25-row
    physical screen and the monitor's box is inset within it, so the root
    browser's own footer row is still visible one row below the picker's
    path row -- an extra row Telnet's 24-row remote-session model never
    fills. The path row itself also renders differently: REST/Overlay
    appends a "-F3=HELP-" hint after the padding that Telnet's rendering
    omits, so only the first whitespace-delimited token is the path -- a
    filesystem path never contains a space."""
    border_rows = [
        index for index, line in enumerate(snapshot.lines)
        if line.strip() and set(line.strip()) <= {"+", "-"}
    ]
    if not border_rows:
        return ""
    path_row = border_rows[-1] + 1
    if path_row >= len(snapshot.lines):
        return ""
    tokens = snapshot.lines[path_row].split()
    return tokens[0] if tokens else ""


# The root listing is not in the same order on every device: a U64 lists Temp
# first, a U2+L lists Flash first. Quick-seek to the entry by name instead of
# assuming a position.
TEMP_ENTRY = "Temp"


def picker_to_root(session: MonitorSession) -> Snapshot:
    """Walk the picker up to the filesystem root ("/").

    LEFT moves one level up; at the root LEFT would close the picker, so we
    check the path before each step and stop as soon as we reach "/"."""
    snapshot = session.capture()
    for _ in range(12):
        if picker_path(snapshot) == "/":
            return snapshot
        snapshot = session.send_key("LEFT")
    raise Failure(f"Unable to reach picker root; last path was {picker_path(snapshot)!r}")


def picker_enter(session: MonitorSession, name_prefix: str) -> Snapshot:
    """Quick-seek to the entry matching name_prefix and step into it."""
    for ch in name_prefix:
        session.send_char(ch)
    return session.send_key("RIGHT")


def prompt_field_text(session: MonitorSession, title: str) -> str:
    """The current contents of a string prompt's edit field.

    UIStringBox draws the title on the box's first row and the edit field two
    window rows below it (edit.init(window, keyb, 0, 2, ...)), inside a five-row
    box. The box is a window over whatever view is behind it, so the full screen
    line also carries that view's text to the left and right of the box border:
    on a U64 the memory view shows through, and reading the whole line back
    returned "0840 FF FF" as if the field still held text. Only the columns
    between this box's own borders belong to the field, and the title row is what
    locates them, because the title is centred inside them.

    The firmware's own layout (title_row + 2) is read first and title_row + 1
    second, so a stray artefact on the blank row can never win, and both are read
    because which screen row the field lands on depends on whether the border
    occupies a row of its own on that display. Reading a single fixed offset gets an empty
    string from the other row, which reads as "the field is already empty" and
    silently skips clearing it.
    """
    snapshot = session.capture()
    row = snapshot.find_line_containing(title)
    title_line = snapshot.line(row)
    title_at = title_line.index(title)
    left = title_line.rfind("|", 0, title_at)
    right = title_line.find("|", title_at + len(title))
    for offset in (2, 1):
        field_line = snapshot.line(row + offset)
        if left < 0 or right < 0:
            text = field_line.strip("|+ ").rstrip()
        elif set(field_line[left:right + 1].strip()) <= {"+", "-", "|"}:
            continue  # the box's bottom border, not a field row
        else:
            text = field_line[left + 1:right].strip()
        if text:
            return text
    return ""


def clear_prompt_field(session: MonitorSession, title: str) -> None:
    """Empty a (non-template) string prompt by deleting what is in it.

    The monitor's "Save as" prompt is pre-filled with the last-used name and
    does not auto-clear on the first keystroke, so it has to be deleted first.
    The number of backspaces comes from the field's own contents, and the field
    is read back until it is empty: a key sent over REST is held long enough
    for the firmware's own auto-repeat to fire, so a blind burst of 40 queues
    far more than 40 deletions on a U2+L, and the keys sent after it are still
    draining several seconds later - which is how the name typed next went
    missing entirely."""
    for _ in range(6):
        text = prompt_field_text(session, title)
        if not text:
            return
        session.send_key_repeat("BACKSPACE", len(text))
    raise Failure(f"{title!r} prompt field would not clear; it still reads "
                  f"{prompt_field_text(session, title)!r}")


def rest_create_d64(host: str, path: str, diskname: str) -> None:
    url = rest_lib.url_for(host, f"/v1/files{path}:create_d64",
                           {"diskname": diskname})
    request = urllib.request.Request(url, data=b"", method="PUT")
    # Creating the same image twice leaves the same image.
    with rest_lib.retrying_urlopen(request, 15.0, idempotent=True):
        pass


def rest_file_exists(host: str, path: str) -> bool:
    try:
        with rest_lib.retrying_urlopen(
                urllib.request.Request(
                    rest_lib.url_for(host, f"/v1/files{path}:info")), 5.0) as response:
            return response.status == 200
    except urllib.error.HTTPError:
        return False


def monitor_save(session: MonitorSession, mem_range: str, enter_dirs: List[str], filename: str) -> Snapshot:
    """Save mem_range to filename, navigating from root through enter_dirs.

    enter_dirs is a list of quick-seek prefixes to step into (e.g. ["MS"] for a
    /Temp subtree reached from root, or ["MD"] then the D64). The final
    directory must offer "<< Create New File >>" as its first entry."""
    session.send_char("S")
    session.send_text(mem_range + "\r", f"save range {mem_range}")
    picker_to_root(session)
    snapshot = picker_enter(session, TEMP_ENTRY)
    for prefix in enter_dirs:
        snapshot = picker_enter(session, prefix)
    # The cursor defaults to "<< Create New File >>"; RIGHT picks it and the
    # monitor then asks for the file name.
    session.send_key("RIGHT")
    clear_prompt_field(session, "Save as")
    snapshot = session.send_text(filename + "\r", f"save as {filename}")
    snapshot.find_line_containing("SAVE")
    return session.send_key("ENTER")  # dismiss the confirmation popup


def monitor_load(session: MonitorSession, enter_dirs: List[str], filename: str) -> Snapshot:
    """Load filename back, navigating from root through enter_dirs."""
    session.send_char("L")
    picker_to_root(session)
    picker_enter(session, TEMP_ENTRY)
    for prefix in enter_dirs:
        picker_enter(session, prefix)
    for ch in filename:
        session.send_char(ch)  # quick-seek to the file
    session.send_key("ENTER")  # open the context menu ("Select" is first)
    session.send_key("ENTER")  # Select -> pick the file
    # "Load [PRG|AAAA],[Offs],[Len|AUTO]" prompt: typing the spec clears the
    # template, so PRG mode is forced regardless of the last-used value.
    session.send_text("PRG,0,AUTO\r", "load PRG")
    return session.send_key("ENTER")  # dismiss the confirmation popup


def run_save_load_topfile_test(session: MonitorSession, rest_host: str, token: str,
                               file_host: Optional[str] = None) -> None:
    # The monitor's save/load picker writes to the filesystem of the device
    # running the monitor, which in a split session is not the device whose
    # C64 memory `rest_host` reads.
    file_host = file_host or rest_host
    addr = 0xC000
    pattern = bytes((0x5A, 0xA5, 0x01, 0x02, 0xDE, 0xAD, 0xBE, 0xEF,
                     0x10, 0x20, 0x30, 0x40, 0x50, 0x60, 0x70, 0x80))
    name = f"MS{token}.PRG"

    write_rest_memory(rest_host, addr, pattern)
    monitor_save(session, f"{addr:04X}-{addr + len(pattern) - 1:04X}", [], name)
    if not rest_file_exists(file_host, f"/Temp/{name}"):
        raise Failure(f"Saved file /Temp/{name} not found via REST")

    write_rest_memory(rest_host, addr, b"\x00" * len(pattern))
    monitor_load(session, [], f"MS{token}")
    loaded = read_rest_memory(rest_host, addr, len(pattern))
    if loaded != pattern:
        raise Failure(
            f"Top-level save/load mismatch at ${addr:04X}:\n"
            f"  saved:  {pattern.hex().upper()}\n"
            f"  loaded: {loaded.hex().upper()}"
        )


def run_save_load_d64_test(session: MonitorSession, rest_host: str, token: str,
                           file_host: Optional[str] = None) -> None:
    file_host = file_host or rest_host
    addr = 0xC100
    pattern = bytes((0x11, 0x22, 0x33, 0x44, 0xAA, 0xBB, 0xCC, 0xDD,
                     0x09, 0x08, 0x07, 0x06, 0x05, 0x04, 0x03, 0x02))
    disk = f"MD{token}.D64"
    inner = f"D{token}"

    rest_create_d64(file_host, f"/Temp/{disk}", f"MD{token}")
    if not rest_file_exists(file_host, f"/Temp/{disk}"):
        raise Failure(f"D64 image /Temp/{disk} was not created")

    write_rest_memory(rest_host, addr, pattern)
    monitor_save(session, f"{addr:04X}-{addr + len(pattern) - 1:04X}", [f"MD{token}"], inner)

    write_rest_memory(rest_host, addr, b"\x00" * len(pattern))
    monitor_load(session, [f"MD{token}"], inner)
    loaded = read_rest_memory(rest_host, addr, len(pattern))
    if loaded != pattern:
        raise Failure(
            f"D64 save/load mismatch at ${addr:04X}:\n"
            f"  saved:  {pattern.hex().upper()}\n"
            f"  loaded: {loaded.hex().upper()}"
        )


def run_tests(session: MonitorSession, rest_host: str, mode: str,
              file_host: Optional[str] = None) -> None:
    file_host = file_host or rest_host
    if is_u2() and mode == MODE_TELNET:
        with check("U2+L normal monitor over telnet"):
            check_skip("the U2+L remote terminal accepts navigation keys but not "
                       "the printable monitor view selectors")
        return
    snapshots = load_snapshots()

    with check("initial CPU7/KERNAL monitor status"):
        ensure_status(session, status_text(snapshots, "status_cpu31"))

    with check("KERNAL $E000 hex view and REST match"):
        ensure_hex_width(session, 8)
        screen = session.goto("E000")
        for row, expected in snapshots["kernal_hex_e000"]["contains"].items():
            assert_contains(screen, int(row), expected)
        assert_rest_matches_row(screen, 4, 0xE000, rest_host)

    with check("paging away and back keeps memory view stable"):
        initial_snapshot = screen.text()
        session.send_key("PGDN", settle=True)
        back = session.send_key("PGUP", settle=True)
        assert_equal("Memory stability", initial_snapshot, back.text(), back.last_command)

    with check("KERNAL disassembly formatting"):
        # D enters Debug on the current machine-code monitor; A is the
        # monitor view selector for assembly/disassembly.
        screen = ensure_view(session, "ASM ")
        for row, expected in snapshots["kernal_disasm_e000"]["contains"].items():
            assert_contains(screen, int(row), expected)

        screen = session.goto("E013")
        screen = ensure_view(session, "ASM ")
        for row, expected in snapshots["kernal_disasm_e013"]["contains"].items():
            assert_contains(screen, int(row), expected)

    with check("KERNAL $E010 REST match"):
        screen = ensure_view(session, "HEX ")
        screen = session.goto("E010")
        assert_rest_matches_row(screen, 4, 0xE010, rest_host)

    with check("CPU6 RAM under BASIC write/read"):
        if is_u2():
            check_skip(NO_CPU_BANKING)
        else:
            screen = ensure_view(session, "HEX ")
            session.goto("A000")
            screen = cycle_cpu_bank_from_cpu7(session, snapshots["status_cpu30"]["contains"]["22"], 7)
            session.fill("A000-A000,AA")
            screen = session.goto("A000")
            assert_contains(screen, 4, snapshots["ram_a000"]["contains"]["4"])

    with check("CPU5 RAM under KERNAL status"):
        if is_u2():
            check_skip(NO_CPU_BANKING)
        else:
            session.goto("E000")
            screen = cycle_cpu_bank_from_cpu7(session, snapshots["status_cpu29"]["contains"]["22"], 6)

    with check("ASCII view width and scrolling"):
        # 19 rows of 0x20 bytes, each row a distinct character, so the view has
        # something identifiable on every line. Written in one go rather than
        # by 19 monitor fill commands: this is setup for the view, not a test
        # of F, which "compare reports differing rows" and three other checks
        # already cover. Typing the fills cost about 15s of the 20s this check
        # took. The write happens before the goto so the view is built from it.
        write_rest_memory(rest_host, 0xC000,
                          b"".join(bytes((0x41 + row,)) * 0x20 for row in range(19)))
        session.goto("C000")

        screen = ensure_view(session, "ASC ")
        content_rows = find_memory_rows(screen)
        first_content_row = content_rows[0]
        last_content_row = content_rows[-1]
        assert_ascii_width(screen, first_content_row)
        # Still on the view bank the previous check cycled to with O.
        assert_view_bank_status(screen, status_text(snapshots, "status_cpu29"))

        screen = session.send_key("DOWN")
        assert_highlight(screen, [(6, first_content_row + 1)], "DOWN")
        assert_contains(screen, first_content_row, snapshots["ascii_top_row"]["contains"]["4"])

        screen = session.send_key("UP")
        assert_highlight(screen, [(6, first_content_row)], "UP")

        for _ in range(last_content_row - first_content_row):
            screen = session.send_key("DOWN")
        assert_highlight(screen, [(6, last_content_row)], "DOWN to last row")
        assert_contains(screen, first_content_row, snapshots["ascii_top_row"]["contains"]["4"])

        screen = session.send_key("DOWN")
        assert_highlight(screen, [(6, last_content_row)], "DOWN past last row")
        assert_contains(screen, first_content_row, snapshots["ascii_scrolled_top_row"]["contains"]["4"])

    with check("ASCII and Screen mapping semantics"):
        run_character_mapping_test(session, rest_host)

    with check("HEX edit writes both nibbles"):
        session.goto("C000")
        session.fill("C000-C000,00")
        screen = ensure_view(session, "HEX ")
        screen = session.send_char("e")
        assert_highlight(screen, [(6, 4), (7, 4)], "e")
        screen = session.send_char("A")
        assert_contains(screen, 4, snapshots["hex_first_nibble"]["contains"]["4"])
        screen = session.send_char("B")
        assert_contains(screen, 4, snapshots["hex_second_nibble"]["contains"]["4"])
        session.send_key("ESC")

    with check("CPU bank cycling reaches CHAR and RAM mappings"):
        if is_u2():
            check_skip(NO_CPU_BANKING)
        else:
            session.goto("A000")
            screen = ensure_view_bank_status(session, snapshots["status_cpu27"]["contains"]["22"])
            assert_view_bank_status(screen, snapshots["status_cpu27"]["contains"]["22"])
            session.send_char("o")
            session.send_char("o")
            screen = session.send_char("o")
            assert_view_bank_status(screen, snapshots["status_cpu30"]["contains"]["22"])

    with check("COMPARE reports differing address"):
        screen = ensure_view(session, "HEX ")
        session.fill("C100-C103,10")
        session.fill("C200-C203,10")
        session.fill("C201-C201,91")
        session.fill("C203-C203,93")
        screen = session.compare("C100-C103,C200")
        assert_contains(screen, 4, "C101")

    with check("G executes finite loop and returns to monitor"):
        if is_u2():
            # The previous COMPARE leaves its result popup up. The U64 G
            # command dismisses it as part of opening its prompt; this target
            # does not run that command, so leave the monitor in the same view.
            session.send_key("RUNSTOP")
            check_skip("the U2+L boot-cartridge G path resets this live-screen "
                       "fixture and does not provide a SYS-like handoff")
        else:
            write_rest_memory(rest_host, 0x1000, bytes.fromhex("A9008D0004A9018D00044C0010"))
            write_rest_memory(rest_host, 0x0400, bytes([0x20]))
            session.goto("1000")
            session.goto_run("1000")
            wait_for_rest_byte(rest_host, 0x0400, 0x01)
            session.enter_monitor()

    with check("G repeated execution updates RAM sentinel"):
        if is_u2():
            check_skip("the U2+L boot-cartridge G path resets the C64 and does "
                       "not provide a SYS-like handoff")
        else:
            run_go_repeat_test(session, rest_host, mode)

    with check("G handoff preserves stable VIC state"):
        if mode != MODE_TELNET:
            # The on-device UI drives the C64's VIC for its own display while
            # it is up, and puts it back when it goes away. Measured on
            # hardware in Overlay: $D011 is $1B at the BASIC prompt, $77 while
            # the menu is open, and $1B again once it closes; Freeze does the
            # same for its frozen display, as freeze_menu_test.py documents for
            # the SID volume. A G that hands the machine back therefore
            # restores the VIC, so a before/after comparison measures the UI
            # entering and leaving rather than the test program disturbing
            # anything. The invariant this check verifies, that G must not
            # disturb the VIC, only holds where the UI never touches the C64's
            # display at all, which is the Telnet remote session.
            check_skip("the on-device UI owns the VIC while it is up; only comparable over telnet")
        else:
            run_go_visible_state_test(session, rest_host)

    with check("bookmarks recall, set, list, and label edit"):
        run_bookmark_test(session)

    with check("memory bookmark jump restores width 16"):
        run_memory_bookmark_width_test(session, rest_host)

    with check("binary width cycling and bookmark jump restores width 4"):
        run_binary_bookmark_width_test(session, rest_host)

    with check("follow and return navigation"):
        run_follow_return_test(session, rest_host)

    with check("asm edit mnemonic validation and Return advance"):
        run_asm_edit_validation_test(session, rest_host)

    with check("number popup arithmetic"):
        run_number_arithmetic_test(session, rest_host)

    save_load_token = f"{int(time.time()) % 100000:05d}"

    with check("save/load round-trip to top-level /Temp file"):
        run_save_load_topfile_test(session, rest_host, save_load_token, file_host)

    with check("save/load round-trip to file in new /Temp D64"):
        run_save_load_d64_test(session, rest_host, save_load_token, file_host)

    # These two checks are about the Telnet transport itself -- a concurrent
    # poll-mode connection, and Telnet's own per-keystroke output volume --
    # and have no REST equivalent, so they only run under --mode telnet,
    # reusing the same session rather than opening a second connection.
    section("Telnet transport checks")
    with check("telnet blocks poll mode"):
        if mode != MODE_TELNET:
            check_skip(f"requires --mode telnet, running under {mode}")
        else:
            run_telnet_poll_guard_test(session)

    with check("telnet opcode dropdown scroll does not flood the connection"):
        if mode != MODE_TELNET:
            check_skip(f"requires --mode telnet, running under {mode}")
        else:
            run_telnet_dropdown_scroll_flood_test(session, rest_host)


def main() -> int:
    parser = argparse.ArgumentParser(description="Validate the U64 machine monitor over REST/Overlay (default), REST/Freeze or Telnet")
    parser.add_argument("-H", "--host", default=os.environ.get("U64_HOST", "u64"))
    parser.add_argument("-P", "--telnet-port", "--port", dest="port", type=int,
                        default=int(os.environ.get("U64_TELNET_PORT", "23")))
    parser.add_argument("-r", "--rest-host", default=os.environ.get("U64_REST_HOST"))
    parser.add_argument("-p", "--password", default=os.environ.get("U64_PASS"))
    parser.add_argument("-t", "--timeout", type=float,
                        default=float(os.environ.get("U64_TIMEOUT", "5.0")))
    add_mode_argument(parser, default=os.environ.get("U64_MODE", "overlay"))
    args = parser.parse_args()

    # The target token, not a bare host name. `--host` carries whatever the
    # runner was given, and for a cartridge that is `u2@c64u`, which names two
    # machines. Every URL this suite builds goes through `rest_lib.url_for`,
    # which sends each path to the machine that serves it: the keyboard to the
    # computer, everything else to the cartridge. See tests/lib/targets.py.
    #
    # Both halves were measured against `u2@c64u`. Interpolating the token
    # into a URL failed all three attempts with `<urlopen error [Errno -2]
    # Name or service not known>` from `reset_rest_machine`, before a single
    # check had run. Resolving the token to the cartridge alone got past that
    # and then failed on the first keystroke with `HTTP 501: Keyboard and
    # joystick injection require Ultimate 64-class hardware`.
    rest_host = args.rest_host or args.host
    global TARGET
    TARGET = "u2" if targets.parse(rest_host).split else "u64"

    # This suite drives one revision of the monitor throughout rather than in
    # one place, so the whole of it is tagged rather than any single check.
    # `tests/lib/machine.py` records which machines have that revision, and
    # without this the suite ran against a monitor it was never written for
    # and failed on a rendering difference: on a C64 Ultimate 1.2.0 it reached
    # "ASCII view width and scrolling" and reported a highlight mismatch,
    # three attempts running, which is a suite asserting the wrong thing
    # rather than a device defect.
    info = UltimateApi(rest_host, args.password or None,
                       REST_TIMEOUT_SECONDS).info()
    device = machine_lib.identify(
        targets.device_of(rest_host),
        lambda: (info.product, info.firmware_version))
    if device.skip_without_fix(machine_lib.MONITOR_EXIT_AND_BACK_KEYS,
                               "this machine runs the monitor revision this "
                               "suite drives"):
        suite_ok("monitor_test")
        return 0

    reset_rest_machine(rest_host, args.password)

    session = None
    try:
        backend = make_backend(
            args.mode, rest_host, args.password, args.timeout,
            telnet_host=args.host, telnet_port=args.port,
        )
        session = MonitorSession(backend)
        run_tests(session, rest_host, args.mode,
                  file_host=targets.device_of(rest_host))
    except Failure as exc:
        suite_fail("monitor_test", str(exc))
        if session is not None:
            snapshot = session.capture()
            section("final screen")
            detail(snapshot.text())
        return 1
    except (OSError, TimeoutError, urllib.error.URLError) as exc:
        suite_fail("monitor_test", format_exception(exc))
        return 1
    finally:
        if session is not None:
            session.close()

    suite_ok("monitor_test")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
