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
from typing import Callable, Dict, List, Optional, Tuple

# tests/lib holds the reporting rules every suite shares; tests/e2e/lib
# holds the shared UI backend.
sys.path.insert(0, os.path.join(
    os.path.dirname(os.path.abspath(__file__)), "..", "..", "lib"))
sys.path.insert(0, os.path.join(
    os.path.dirname(os.path.abspath(__file__)), "..", "lib"))
import rest as rest_lib
import machine as machine_lib
import targets
from api import UltimateApi
from av_stream import AvStreamCapture, assert_frames_differ, assert_not_black, video_frames
from report import Failure, check, check_skip, detail, format_exception, section, suite_fail, suite_ok
from ui_backend import (
    Backend,
    MODE_FREEZE,
    MODE_TELNET,
    Snapshot,
    TelnetBackend,
    add_mode_argument,
    make_backend,
)

SNAPSHOT_FILE = Path(__file__).with_name("snapshots").joinpath("expected_snapshots.json")

# Per-request timeout for this suite's own REST calls, which are all small
# reads and writes against a device that is otherwise idle.
REST_TIMEOUT_SECONDS = 5.0

# How many times a command argument may be typed again when the field shows it
# did not all arrive, for the checks where typing it is preparation rather than
# the subject. Two spare attempts, because the loss this covers is one
# keystroke in several hundred on the one transport that has it.
PROMPT_RETYPES = 2

# The Transfer prompt states its optional fourth field, so its title is long
# enough to be worth naming once.
TRANSFER_PROMPT_TITLE = "Transfer AAAA-BBBB,CCCC[,DDDD-EEEE]"

# The normal footer reports either a live CPU bank or a CPU-view override.
STATUS_LINE_RE = re.compile(
    r"(?:CPU[0-7]|C[0-7]O[0-7]) \$A:(?:RAM|BAS) \$D:(?:RAM|CHR|I/O) "
    r"\$E:(?:RAM|KRN) VIC[0-3] \$[0-9A-F]{4}")
MEMORY_ROW_RE = re.compile(r"^[0-9A-F]{4} ")
MEMORY_ROW_16_RE = re.compile(r"^[0-9A-F]{4} [0-9A-F]{16} [0-9A-F]{16}$")

# U2 has no monitor-selected CPU bank, so it uses a VIC-only footer.
U2_STATUS_LINE_RE = re.compile(r"CPU VIEW  VIC([0-3]) \$([0-9A-F]{4})")
U2_VIC_BANK_BASES = (0x0000, 0x4000, 0x8000, 0xC000)


def find_status_line(snapshot: Snapshot) -> int:
    return snapshot.find_line_matching(STATUS_LINE_RE)


def find_u2_footer_line(snapshot: Snapshot) -> int:
    return snapshot.find_line_matching(U2_STATUS_LINE_RE)


def find_any_status_line(snapshot: Snapshot) -> int:
    """Either target footer proves that the monitor rendered."""
    try:
        return find_status_line(snapshot)
    except Failure:
        return find_u2_footer_line(snapshot)


VIEW_KEYS = {
    "HEX ": "M",
    "ASC ": "I",
    "ASM ": "A",
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

    def send_key(self, key: str, *, settle: bool = False,
                 expect_redraw: bool = True) -> Snapshot:
        return self.backend.send_key(key, settle=settle, expect_redraw=expect_redraw)

    def send_key_count(self, key: str) -> Tuple[Snapshot, int]:
        """Telnet-only: see TelnetBackend.send_key_count."""
        return self.backend.send_key_count(key)

    def send_key_repeat(self, key: str, count: int) -> Snapshot:
        return self.backend.send_key_repeat(key, count)

    def send_char(self, ch: str, *, settle: bool = False,
                  expect_redraw: bool = True) -> Snapshot:
        return self.backend.send_char(ch, settle=settle, expect_redraw=expect_redraw)

    def send_text(self, text: str, label: str) -> Snapshot:
        return self.backend.send_text(text, label)

    def empty_open_prompt(self, title: str) -> None:
        """Delete what a non-template prompt is showing, so the field is empty.

        Only for the prompts that open on what they were last given. A
        template prompt is not buffer content until the first printable key
        replaces it wholesale, so backspace does nothing there; see
        `type_into_prompt`.

        Preparation rather than a subject, so the backspaces may be repeated:
        the field is measured, that many are sent, and the result is read back,
        up to three passes.
        """
        for _ in range(3):
            current = prompt_field(self.capture(), title)
            if not current:
                return
            self.send_key_repeat("BACKSPACE", len(current) + 1)
            wait_until(self, lambda screen: prompt_field_or_none(screen, title) == "")
        remaining = prompt_field(self.capture(), title)
        if remaining:
            raise Failure(
                f"the {title} field would not empty; it reads {remaining!r}")

    def type_into_prompt(self, key: str, title: str, text: str,
                         retypes: int = 0, template: bool = True) -> None:
        """Open a command prompt and type `text` into it, proving it arrived.

        The field is read back and compared in full before RETURN is sent, so a
        character that did not reach the monitor is reported at the prompt
        rather than as whatever the command then did with a short argument. A
        missing character, a duplicated one and two characters in the wrong
        order are each caught, and each character is sent exactly once per
        attempt.

        `retypes` is 0 where the input path is itself the thing under test: the
        text is typed once and anything short of the whole of it fails. It is
        non-zero where typing an argument is only how the check reaches the
        memory or the view it is really about. That is idempotent preparation:
        a retype leaves the prompt, opens it again, empties the field and types
        the same text into it, and it says so in the run, so a device that
        needs one is visible rather than absorbed here.

        RETURN is left to the caller, because a Go that executes closes the
        whole menu as a side effect of that one keystroke.
        """
        def typed(snapshot: Snapshot) -> bool:
            try:
                return prompt_field(snapshot, title) == text
            except Failure:
                return False  # the prompt is mid-redraw

        shown = None
        for attempt in range(retypes + 1):
            self.send_char(key)
            wait_for_prompt(self, title)
            # Not every prompt opens on a template that the first printable
            # key replaces wholesale. Hunt keeps its default range and takes
            # the needle after it, so typing into it appends; that one has to
            # be emptied first for "the field reads what was typed" to mean the
            # same thing there as everywhere else. Which prompts are which is
            # the template_mode flag on their MonitorCommandInput.
            if not template:
                self.empty_open_prompt(title)
            self.send_text(text, f"{key} {text}")
            snapshot = wait_until(self, typed)
            try:
                shown = prompt_field(snapshot, title)
            except Failure:
                raise Failure(
                    f"the {title} prompt is not on screen after typing {text!r}"
                    f"\n{snapshot.text()}")
            if shown == text:
                if attempt:
                    detail(f"{title}: {text!r} had to be typed {attempt + 1} "
                           f"times before the whole of it reached the monitor")
                return
            if attempt < retypes:
                self.send_key("ARROW_LEFT")
                wait_for_monitor(self, f"retyping the {title} argument")
        raise Failure(
            f"{title}: the field reads {shown!r} after {text!r} was typed"
            + (f", {retypes + 1} times over" if retypes else "")
            + ". A character of the command did not reach the monitor as typed."
            f"\n{self.capture().text()}")

    def run_prompt_command(self, key: str, title: str, text: str) -> Snapshot:
        """Type a verified argument into a command prompt and submit it.

        Reaching an address is preparation for whatever the caller checks
        there, so a lost character is retyped rather than failing the caller's
        own subject. `run_key_input_stress_test` is where the input path itself
        is the subject, and it allows no retype at all.
        """
        self.type_into_prompt(key, title, text, retypes=PROMPT_RETYPES)
        return self.send_key("ENTER")

    def goto(self, address: str) -> Snapshot:
        self.run_prompt_command("J", "Jump AAAA", address)
        # Where the monitor actually went. The field read-back above already
        # catches a character that did not arrive; this catches the case where
        # the address was typed correctly and the jump still did not land on
        # it.
        wanted = address.upper().lstrip("$").zfill(4)
        snapshot = wait_until(
            self, lambda screen: monitor_header_address(screen) is not None)
        landed = monitor_header_address(snapshot)
        if landed is None:
            raise Failure(
                f"J {address}: the monitor header is not on screen\n{snapshot.text()}")
        if landed != wanted:
            raise Failure(
                f"J {address} landed on ${landed}\n{snapshot.text()}")
        return snapshot

    def fill(self, expr: str) -> Snapshot:
        return self.run_prompt_command("F", "Fill AAAA-BBBB,DD", expr)

    def compare(self, expr: str) -> Snapshot:
        return self.run_prompt_command("C", "Compare AAAA-BBBB,CCCC", expr)

    def transfer(self, expr: str) -> Snapshot:
        return self.run_prompt_command("T", TRANSFER_PROMPT_TITLE, expr)

    def goto_run(self, address: str) -> Snapshot:
        # The argument is verified while the prompt is still up, so the C64 is
        # never run from an address nobody typed. Reaching the address is
        # preparation for what the caller checks after the run, so a lost
        # character is retyped rather than failing that check.
        self.type_into_prompt("G", "Go AAAA", address, retypes=PROMPT_RETYPES)
        try:
            return self.send_key("ENTER")
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

    def enter_monitor(self) -> Snapshot:
        self.backend.ensure_ready()
        snapshot = self.send_key("CTRL_O")
        try:
            find_any_status_line(snapshot)
            return snapshot
        except Failure:
            pass

        snapshot = self.send_key("F5")
        snapshot = self.send_char("D")
        snapshot = self.send_key("ENTER")
        snapshot = self.send_key("DOWN")
        snapshot = self.send_key("ENTER")
        find_any_status_line(snapshot)
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



def assert_source_column_is_fixed(snapshot: Snapshot, expected_tag: str) -> None:
    """Every Assembly row's source tag is three characters at one column.

    The tag is right-aligned, so a tag whose width depended on the bank would
    move the column's left edge and the rows would appear to shift sideways
    when the cursor crossed a bank boundary.
    """
    columns = set()
    tags = set()
    for line in snapshot.lines:
        if "[" not in line or "]" not in line:
            continue
        start = line.index("[")
        end = line.index("]", start)
        if end - start != 4:
            raise Failure(
                f"an Assembly source tag is not three characters: {line!r}")
        columns.add(start)
        tags.add(line[start + 1:end])
    if not columns:
        raise Failure(
            f"no Assembly source tag on screen\n{snapshot.text()}")
    if len(columns) != 1:
        raise Failure(
            f"the Assembly source column moves between rows, columns {sorted(columns)}"
            f"\n{snapshot.text()}")
    if expected_tag not in tags:
        raise Failure(
            f"expected an {expected_tag!r} source tag, saw {sorted(tags)}"
            f"\n{snapshot.text()}")


def assert_status_contains(snapshot: Snapshot, expected: str) -> None:
    line_index = find_status_line(snapshot)
    assert_contains(snapshot, line_index, expected)


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
    url = f"http://{host}/v1/machine:readmem?address={address:04X}&length={length}"
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
                                attempts: int = 4, timeout: float = 1.0) -> None:
    """Put fixture bytes in device memory and prove the device holds them.

    Each attempt writes once and then reads the range back until it matches or
    the budget runs out. This is fixture preparation rather than behaviour
    under test: writing the same bytes twice is the same as writing them once,
    so a write that did not land may be repeated. How many attempts it took is
    reported, so a device that needs more than one is visible in the run rather
    than absorbed here.
    """
    actual = b""
    for attempt in range(1, attempts + 1):
        write_rest_memory(host, address, data)
        actual = wait_for_rest_data(host, address, data, timeout=timeout)
        if actual == data:
            if attempt > 1:
                detail(f"${address:04X} needed {attempt} REST writes to hold "
                       f"{data.hex().upper()}")
            return
    # What it holds instead is the whole diagnosis: memory that keeps changing
    # says something on the C64 owns the range, while memory that stays at one
    # wrong value says the write is not arriving.
    raise Failure(
        f"${address:04X} would not hold {data.hex().upper()} in {attempts} "
        f"writes; it reads {actual.hex().upper()}"
    )


def wait_for_rest_data(host: str, address: int, expected: bytes,
                       timeout: float = 5.0) -> bytes:
    deadline = time.time() + timeout
    actual = b""
    while time.time() < deadline:
        actual = read_rest_memory(host, address, len(expected))
        if actual == expected:
            return actual
        time.sleep(0.05)
    return actual


def close_rest_menu(control: str, password: Optional[str]) -> None:
    """Shut the on-device menu, whichever machine of `control` owns which half.

    The menu belongs to the device under test, and the keys that leave it
    belong to the C64-side computer; on a single-device target both are the
    same machine. See tests/lib/targets.py.
    """
    headers = {"X-Password": password} if password else {}
    target = targets.parse(control)
    menu_host = target.device
    input_host = target.input_host
    for attempt in range(12):
        try:
            request = urllib.request.Request(
                f"http://{menu_host}/v1/machine:menu_screen", headers=headers, method="GET"
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
            f"http://{input_host}/v1/machine:input",
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
            f"http://{menu_host}/v1/machine:menu_button", data=b"", headers=headers, method="PUT"
        )
        with rest_lib.retrying_urlopen(request, 5.0):
            pass
        time.sleep(0.5)


def reset_rest_machine(control: str, password: Optional[str]) -> None:
    close_rest_menu(control, password)

    # Let the reset supersede any program that was still executing. READY can
    # remain elsewhere in screen RAM until the new boot redraws it.
    UltimateApi(control, password, REST_TIMEOUT_SECONDS).machine.reset()
    time.sleep(0.2)


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


def cycle_cpu_bank_from_cpu7(session: MonitorSession, target_status: str, steps: int) -> Snapshot:
    screen = ensure_status(session, "CPU7 $A:BAS $D:I/O $E:KRN VIC")

    for _ in range(steps):
        screen = session.send_char("o")

    assert_status_contains(screen, target_status)
    return screen


def ensure_view(session: MonitorSession, expected: str) -> Snapshot:
    """Select a monitor view, pressing its key at most once.

    Each view has its own key, so the key is sent once when the wanted view is
    not already up and the header is then waited for. A key that does not
    arrive fails here rather than being covered by a second press.
    """
    key = VIEW_KEYS.get(expected)
    if key is None:
        raise Failure(f"Unsupported monitor view selector for {expected!r}")

    def shows_view(snapshot: Snapshot) -> bool:
        try:
            snapshot.find_line_containing(expected)
            return True
        except Failure:
            return False

    screen = session.capture()
    if shows_view(screen):
        return screen
    session.send_char(key)
    screen = wait_until(session, shows_view)
    if not shows_view(screen):
        raise Failure(
            f"{key!r} did not select the {expected!r} view; screen was\n{screen.text()}")
    return screen


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


def hex_width_is_16(screen: Snapshot) -> bool:
    rows = find_memory_rows(screen)
    row_text = screen.line(rows[0]).strip()
    if row_text.startswith("|"):
        row_text = row_text[1:]
    if row_text.endswith("|"):
        row_text = row_text[:-1]
    return MEMORY_ROW_16_RE.match(row_text.strip()) is not None


def ensure_hex_width(session: MonitorSession, expected_width: int) -> Snapshot:
    """Set the Hex row width, pressing W at most once.

    The Hex width has two states, so one press reaches the other one. The
    result is then waited for rather than assumed, so a W that did not arrive
    fails here instead of leaving the next check reading rows of the wrong
    width.
    """
    if expected_width not in (8, 16):
        raise Failure(f"Unsupported hex width request {expected_width}")

    def has_width(snapshot: Snapshot) -> bool:
        try:
            return hex_width_is_16(snapshot) == (expected_width == 16)
        except Failure:
            return False  # the view is mid-redraw

    screen = ensure_view(session, "HEX ")
    if has_width(screen):
        return screen
    session.send_char("W", settle=True)
    screen = wait_until(session, has_width)
    if not has_width(screen):
        raise Failure(
            f"W did not set the Hex view to {expected_width} bytes per row\n"
            f"{screen.text()}")
    return screen


def enter_hex_nibble(session: MonitorSession, nibble: str, expected: str) -> Snapshot:
    """Type one hex digit into the byte editor and wait for the row to show it.

    The digit is sent once. In hex edit mode a second copy of the same digit
    advances the nibble cursor and writes a different byte, so re-sending it
    would not be a retry of the same intended transition at all; a digit that
    does not arrive fails here.
    """
    session.send_char(nibble)

    def shows(snapshot: Snapshot) -> bool:
        return expected in snapshot.line(4)

    screen = wait_until(session, shows)
    assert_contains(screen, 4, expected)
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
    expected: Optional[int] = None, timeout: float = 3.0,
) -> int:
    """Navigate to `address` once and read its first byte.

    A freshly-parked memory view can show one stale byte immediately after a
    DMA release (the same class of fetch race tracked elsewhere in this
    firmware around a fresh view's first fetch after a G that unfreezes and
    re-parks the CPU). When `expected` is given, the same view is re-read
    until it shows that byte or the budget runs out. The navigation itself is
    performed exactly once, so a lost keystroke in the Jump address is
    reported by `MonitorSession.goto` rather than repeated until it works.
    The caller's own comparison still runs on whatever this last returns, so a
    genuine mismatch still fails the check.
    """
    screen = session.goto(address)
    value = parse_memory_row(screen, address_int)[0]
    if expected is None or value == expected:
        return value

    deadline = time.time() + timeout
    while time.time() < deadline:
        time.sleep(0.05)
        screen = session.capture()
        try:
            value = parse_memory_row(screen, address_int)[0]
        except Failure:
            continue  # the view is mid-redraw
        if value == expected:
            return value
    return value


def ensure_monitor_open(session: MonitorSession) -> None:
    """Fixture preparation: leave the monitor on screen, wherever we are.

    Idempotent, unlike `MonitorSession.enter_monitor`, whose C=+O would close a
    monitor that is already up.
    """
    if monitor_is_on_screen(session.capture()):
        return
    session.enter_monitor()


def leave_monitor_fully(session: MonitorSession) -> None:
    """Press Back until the monitor's own status line is gone from the screen.

    Bounded rather than a fixed count of presses: a mode the caller left open
    (Edit, a popup) costs one more Back than a plain view does, and a fixed
    count either leaves one open or spends a spare press on whatever the
    surrounding menu does with it.
    """
    for _ in range(4):
        try:
            find_any_status_line(session.capture())
        except Failure:
            return
        session.send_key("ARROW_LEFT", settle=True)
    raise Failure("the monitor was still on screen after 4 Back presses\n"
                  f"{session.capture().text()}")


def device_write_lands(device_host: str, address: int, data: bytes) -> bool:
    """Whether the device's own DMA path can put `data` at `address` right now.

    `machine:writemem` reaches memory through `C64_Subsys::executeCommand`,
    which stops the machine and calls the same `C64::dma_transfer_frozen` the
    monitor's backend calls. Asking it immediately after a monitor edit did not
    land, at the same address and in the same machine state, separates a
    monitor defect from a loss in the shared path underneath both.
    """
    write_rest_memory(device_host, address, data)
    return wait_for_rest_data(device_host, address, data, timeout=2.0) == data


# Every write that did not land on its first attempt and was not attributed to
# the monitor. Counted for the whole run and reported at the end of it, so a
# suite that passes while the shared DMA path lost writes says how many rather
# than only saying that each individual one was not the monitor's fault.
FIRST_ATTEMPT_LOSSES: List[str] = []


def note_first_attempt_loss(address: int, what: str) -> None:
    FIRST_ATTEMPT_LOSSES.append(f"${address:04X} ({what})")


def report_first_attempt_losses() -> None:
    if not FIRST_ATTEMPT_LOSSES:
        return
    detail(f"{len(FIRST_ATTEMPT_LOSSES)} write(s) in this run did not land on "
           f"the first attempt and were not the monitor's write path: "
           f"{', '.join(FIRST_ATTEMPT_LOSSES)}. Those are the intermittent in "
           f"C64::dma_transfer_frozen, which the device's own machine:writemem "
           f"shows at a comparable rate")


def assert_monitor_write_landed(device_host: str, address: int, expected: bytes,
                                what: str, timeout: float = 5.0,
                                retry_monitor_write: Optional[Callable[[], None]] = None
                                ) -> bool:
    """Require a monitor write to have landed, or the loss to be underneath it.

    Every check in this suite that proves a monitor edit reached memory goes
    through here, so they all draw the same line. Returns True when the bytes
    are there.

    When they are not, the question is which path lost them, and one retry
    through the device's own `machine:writemem` does not answer it on its own.
    `C64::dma_transfer_frozen` loses a write occasionally, so a retry through
    any path will usually succeed, and taking that as proof blames the monitor
    for a fault beneath it. Measured on `u2@c64u` over 60 writes at six
    addresses, one monitor hex edit and one `machine:writemem` per address per
    round: the monitor lost none of 50 and the device lost one of 50, once the
    ten attempts at $BFFF are set aside, where BASIC ROM is banked over the
    address and neither path can write at all.

    So the monitor is blamed only when its own write fails a second time at the
    same address, after the device has just succeeded there and the address has
    been set to something else again. That is the shape of a broken write path
    rather than of a lost write. A caller that can redo its write passes
    `retry_monitor_write`; one that cannot has its single loss reported rather
    than attributed, which is the accurate answer for one sample.
    """
    actual = wait_for_rest_data(device_host, address, expected, timeout=timeout)
    if actual == expected:
        return True
    if not device_write_lands(device_host, address, expected):
        detail(f"{what}: ${address:04X} would not take {expected.hex().upper()} "
               f"from the monitor, and would not take it from the device's own "
               f"machine:writemem either, so that loss is in the frozen DMA path "
               f"under both")
        note_first_attempt_loss(address, "the device could not write it either")
        return False
    if retry_monitor_write is None:
        detail(f"{what}: ${address:04X} did not take {expected.hex().upper()} "
               f"from the monitor and did take it from the device's own "
               f"machine:writemem. One sample, and this check cannot redo its "
               f"write, so the loss is reported rather than attributed")
        note_first_attempt_loss(address, "one sample, no retry available")
        return False

    # Put something else there, so the retry has to write the bytes itself
    # rather than find them already in place from the device's attempt.
    sentinel = bytes(byte ^ 0xFF for byte in expected)
    write_rest_memory(device_host, address, sentinel)
    if wait_for_rest_data(device_host, address, sentinel, timeout=2.0) != sentinel:
        detail(f"{what}: ${address:04X} could not be set to a sentinel before "
               f"the monitor's write was retried, so the retry was skipped")
        return False
    retry_monitor_write()
    second = wait_for_rest_data(device_host, address, expected, timeout=timeout)
    if second == expected:
        detail(f"{what}: ${address:04X} did not take {expected.hex().upper()} "
               f"from the monitor the first time and took it the second, with "
               f"the device's own machine:writemem succeeding in between, so "
               f"that loss is the intermittent in the frozen DMA path under "
               f"both rather than the monitor's write path")
        note_first_attempt_loss(address, "the monitor placed it on the retry")
        return False
    raise Failure(
        f"{what}: ${address:04X} holds {second.hex().upper()}, expected "
        f"{expected.hex().upper()}. The monitor failed to write it twice, and "
        f"the device's own machine:writemem put those bytes there in between, "
        f"so this is the monitor's write path and not the DMA path underneath "
        f"it.")


def run_main_ram_edit_persists_test(session: MonitorSession, device_host: str,
                                    frozen: bool) -> None:
    """A hex edit at an ordinary main-RAM address reaches C64 memory itself.

    $1000 is plain main RAM: no ROM overlay, no I/O, and no banking, but the
    monitor being open is not a neutral vantage point to check it from: while
    it holds the machine, a U2's freezer banks its own scratch RAM over
    exactly $1000-$3FFF (`banked_ram_reason`), so a read through the device
    would pass on freezer state that looks like a persisted edit whether or
    not the underlying write did anything. Once the monitor is closed that
    banking is gone and `device_host` reads the C64's own RAM; a C64 Ultimate
    host's own REST view of the same address does not reliably see a byte a
    cartridge wrote to it over DMA, verified live against this device, so it
    is not used as the second oracle here. `device_host` still is one: it is
    read again only after being navigated to and away from in the editor and,
    for a U2, after the monitor that was driving the edit has actually closed,
    so neither read is the same call path as the one the editor's own display
    used to draw the byte.
    """
    address = 0x1000
    if frozen:
        leave_monitor_fully(session)
    original = read_rest_memory(device_host, address, 1)
    replacement = bytes((original[0] ^ 0xFF,))
    try:
        if frozen:
            session.enter_monitor()
        session.goto(f"{address:04X}")
        screen = ensure_view(session, "HEX ")
        screen = session.send_char("e")
        assert_highlight(screen, [(6, 4), (7, 4)], "e")
        digits = f"{replacement[0]:02X}"
        screen = session.send_char(digits[0], settle=True)
        screen = session.send_char(digits[1], settle=True)
        session.send_key("ESC", settle=True)

        if frozen:
            leave_monitor_fully(session)
        else:
            # Away and back: the same forced redraw goto_and_read_byte uses,
            # so a value that only ever changed in the editor's local state
            # cannot pass this far even before the independent read below.
            session.goto("E000")
            session.goto(f"{address:04X}")

        def retry_the_edit() -> None:
            if frozen:
                session.enter_monitor()
            session.goto(f"{address:04X}")
            ensure_view(session, "HEX ")
            session.send_char("e")
            session.send_char(digits[0], settle=True)
            session.send_char(digits[1], settle=True)
            session.send_key("ESC", settle=True)
            if frozen:
                leave_monitor_fully(session)

        assert_monitor_write_landed(
            device_host, address, replacement,
            "a Hex-view edit" + (" read back after leaving the monitor"
                                 if frozen else ""),
            retry_monitor_write=retry_the_edit)
    finally:
        write_rest_memory_confirmed(device_host, address, original)
        if frozen:
            session.enter_monitor()


def hex_edit_byte_persists(session: MonitorSession, device_host: str, frozen: bool,
                           address: int) -> None:
    """Edit one byte at `address` through the Hex view and prove it reached memory.

    Same verification shape as `run_main_ram_edit_persists_test`: leave the
    monitor before either read when frozen, because neither `device_host`'s
    own banked freezer scratch RAM at $1000-$3FFF nor a running C64's open
    bus while the freezer holds it is ground truth. Shared here across a
    sweep of addresses rather than duplicated per address, with `address` in
    every failure message so one bad boundary is identifiable from the
    report.
    """
    if frozen:
        leave_monitor_fully(session)
    original = read_rest_memory(device_host, address, 1)
    replacement = bytes((original[0] ^ 0xFF,))
    try:
        if frozen:
            session.enter_monitor()
        session.goto(f"{address:04X}")
        screen = ensure_view(session, "HEX ")
        # No assert_highlight here: unlike the fixed $1000 case, this address
        # can land at any offset within its 8-byte row, so the highlighted
        # column varies with it. Persistence, not cursor position, is what
        # this sweep proves; run_main_ram_edit_persists_test already proves
        # the highlight for one fixed, known position.
        screen = session.send_char("e")
        digits = f"{replacement[0]:02X}"
        screen = session.send_char(digits[0], settle=True)
        screen = session.send_char(digits[1], settle=True)
        session.send_key("ESC", settle=True)

        if frozen:
            leave_monitor_fully(session)
        else:
            session.goto("E000")
            session.goto(f"{address:04X}")

        assert_monitor_write_landed(
            device_host, address, replacement,
            "a Hex-view edit" + (" read back after leaving the monitor"
                                 if frozen else ""))
    finally:
        write_rest_memory_confirmed(device_host, address, original)
        if frozen:
            session.enter_monitor()


# Every address here is genuine RAM with no CPU-bank dependency, no I/O side
# effect and nothing else contending for it, on both a U2 and a U64: the
# $0FFF edge of the frozen Ultimax gap this defect's fix widened to $1000,
# and the $CFFF/$7FFF edges either side of where that gap continues to $D000
# (the $8000 edge is control, already inside the gap before this fix, so it
# must keep working exactly as it did rather than newly break alongside the
# widened range). $0000/$0001 are the CPU port, not RAM in the relevant
# sense, so the low end starts one byte later at $0002. The remaining
# boundaries this defect's fix touches ($03FF/$0400, $07FF/$0800, $FFFF) are
# genuine RAM too, but a running C64 itself writes or reads them constantly;
# see CONTENDED_WHEN_RUNNING_ADDRESSES below, exercised only while frozen.
MAIN_RAM_EDGE_ADDRESSES = (
    0x0002, 0x0FFF,
    0x7FFF, 0x8000, 0xCFFF,
)

# $D7FF is the byte immediately below color RAM, but with the CPU port in its
# default state $D000-$D7FF is SID I/O, mirrored throughout that 1KB (most
# SID registers do not read back what was written), not memory; $D800-$DBFF
# is color RAM itself, wired only 4 bits wide, so its top nibble is not
# guaranteed to read back whatever a full-byte write sent. Neither is a
# meaningful "did the edit reach memory" question outside a CPU bank that
# maps them to RAM, which run_cpu_banked_ram_edit_test already covers for the
# banks this suite trusts elsewhere ($A000, $E000). Left out of the
# no-banking-needed sweep rather than asserted against hardware that is not,
# in that state, ordinary RAM.

# $0400-$07FF is visible screen RAM, and while the C64 is running (not
# frozen) it is not idle: the blinking cursor and the running program's own
# screen updates keep writing it. An edit here can race one of those and be
# overwritten before the independent read that proves it landed, which is a
# fact about a running C64 sharing the byte, not about whether the edit
# reached memory. Exercised only while frozen, when nothing is running to
# contend for it.
#
# $FFFF, the IRQ/BRK vector's high byte, is deliberately absent from this
# whole suite: verified directly with `curl -X PUT
# '.../v1/machine:writemem?address=FFFF&data=42'` followed by an immediate
# `machine:readmem` on the same host, with no monitor, no freeze and no
# cartridge involved at all, the byte does not hold there. That rules out
# this defect, the monitor's edit path and this fix as the cause; it is a
# C64 Ultimate host-level limitation at the literal top of the address
# space, outside what this repository's firmware controls.
CONTENDED_WHEN_RUNNING_ADDRESSES = (0x03FF, 0x0400, 0x07FF, 0x0800)


def run_main_ram_edge_sweep_test(session: MonitorSession, device_host: str,
                                 frozen: bool) -> None:
    """Every boundary in `MAIN_RAM_EDGE_ADDRESSES`, hex-edited and verified.

    $D000-$D7FF and $DC00-$DFFF are deliberately absent: with the CPU port in
    its default state those are VIC-II/SID and CIA I/O, and writing there is
    not a memory-persistence question, it is a register side effect that
    could disturb the rest of a running test session (CIA timers feed the
    keyboard and IRQ path this suite depends on). The equivalent RAM-backed
    edges of that same window are covered separately, under a CPU bank that
    is confirmed to map them to RAM, in `run_cpu_banked_ram_edit_test`.
    """
    addresses = MAIN_RAM_EDGE_ADDRESSES
    if frozen:
        addresses = addresses + CONTENDED_WHEN_RUNNING_ADDRESSES
    else:
        detail(
            "not frozen: $0400-$07FF (screen RAM) and $FFFF (the IRQ/BRK "
            "vector) left out of this sweep, since a running C64 itself "
            "contends for both; see CONTENDED_WHEN_RUNNING_ADDRESSES")
    for address in addresses:
        hex_edit_byte_persists(session, device_host, frozen, address)


def banked_ram_edit_via_view(session: MonitorSession, address: int) -> None:
    """Edit one byte through the Hex view and prove it stuck, by the redrawn view.

    Not `device_host`: `machine:readmem` reads through the 6510's own,
    actual current port, not the monitor's own bank-view override, so at an
    address ROM or I/O banks over by default it reads what is really banked
    in right now, never the RAM underneath -- REST has no way to ask for
    that view. `CPU6 RAM under BASIC write/read` established that the write
    itself reaches the real RAM there (over REST, after physically
    rebanking to make that RAM the live view); this proves the same
    property for an edit made through the monitor's own Hex view, verified
    the only way that is visible for a banked-over address: the monitor's
    own view, navigated away and back first so a value that only ever
    changed in the editor's local state cannot pass.
    """
    original = parse_memory_row(session.goto(f"{address:04X}"), address)[0:1]
    replacement = bytes((original[0] ^ 0xFF,))
    try:
        screen = ensure_view(session, "HEX ")
        screen = session.send_char("e")
        digits = f"{replacement[0]:02X}"
        screen = session.send_char(digits[0], settle=True)
        screen = session.send_char(digits[1], settle=True)
        session.send_key("ESC", settle=True)

        session.goto("C000")
        screen = session.goto(f"{address:04X}")
        actual = parse_memory_row(screen, address)[0:1]
        if actual != replacement:
            raise Failure(
                f"${address:04X} reads {actual.hex().upper()} in the "
                f"redrawn view after the edit, expected "
                f"{replacement.hex().upper()}"
            )
    finally:
        session.send_char("e")
        digits = f"{original[0]:02X}"
        session.send_char(digits[0], settle=True)
        session.send_char(digits[1], settle=True)
        session.send_key("ESC", settle=True)


def run_cpu_banked_ram_edit_test(session: MonitorSession, device_host: str,
                                 frozen: bool) -> None:
    """A hex edit reaches RAM at addresses ROM or I/O normally banks over.

    U64-only: a U2 has no monitor-selected CPU banking
    (`supports_cpu_banking() == false`), and its own memory map never puts
    ROM or I/O at these addresses in the first place, so there is nothing
    banked here to prove. $A000 is BASIC ROM at the default CPU7 bank; $D000
    is VIC-II/SID I/O there; $E000 is KERNAL ROM there. `device_host` is
    unused here; kept in the signature to match every other check function
    this suite's `run_tests` calls uniformly.
    """
    del device_host, frozen  # see banked_ram_edit_via_view
    # CPU6 ($A:RAM $D:I/O $E:KRN) and CPU5 ($A:RAM $D:I/O $E:RAM): the exact
    # step counts (7 and 6) this fixture's own `CPU6 RAM under BASIC
    # write/read` and `CPU5 RAM under KERNAL status` checks already use to
    # reach them from CPU7, reused rather than re-derived, since the cycle
    # this key steps through is not a simple decrement by one bank per
    # press -- it visits every distinct effective bank configuration, of
    # which there are fewer than eight raw port values.
    cycle_cpu_bank_from_cpu7(session, "CPU6 $A:RAM $D:I/O $E:KRN", 7)
    banked_ram_edit_via_view(session, 0xA000)

    # $D000 stays I/O and is left alone for the reason given in
    # run_main_ram_edge_sweep_test. $FFFF is not tested here: it is the
    # IRQ/BRK vector regardless of CPU bank, and
    # CONTENDED_WHEN_RUNNING_ADDRESSES already covers it, frozen, in
    # run_main_ram_edge_sweep_test.
    cycle_cpu_bank_from_cpu7(session, "CPU5 $A:RAM $D:I/O $E:RAM", 6)
    banked_ram_edit_via_view(session, 0xE000)

    # Restore the default bank so later checks see the CPU7 state they
    # expect. ensure_status presses 'o' itself until CPU7 is on screen, so
    # this does not depend on how many steps away CPU5 happens to be.
    ensure_status(session, "CPU7 $A:BAS $D:I/O $E:KRN")


# ---------------------------------------------------------------------------
# Reliability sweeps.
#
# The gate runs a small number of rounds so it stays inside its time budget.
# MONITOR_STRESS_ROUNDS raises that for a deliberate stress run without
# changing what any single transaction asserts.
# ---------------------------------------------------------------------------

def stress_rounds(default: int) -> int:
    raw = os.environ.get("MONITOR_STRESS_ROUNDS")
    if not raw:
        return default
    try:
        rounds = int(raw)
    except ValueError:
        raise Failure(f"MONITOR_STRESS_ROUNDS is not a number: {raw!r}")
    if rounds < 1:
        raise Failure(f"MONITOR_STRESS_ROUNDS must be at least 1, not {rounds}")
    return rounds


# Four-digit Jump addresses chosen for the shapes that a keyboard-matrix scan
# can get wrong: two identical digits in a row, alternating digits, a digit
# repeated after one other digit, and letters that need SHIFT next to digits
# that do not.
KEY_STRESS_ADDRESSES = (
    "C003",  # the case seen on hardware: a repeated digit after a shifted letter
    "1100", "2200", "0000",  # adjacent identical digits
    "1010", "A0A0", "5555",  # alternating, and one digit four times
    "1234", "ABCD", "0F0F",  # all distinct, and shifted letters beside digits
)

# Structured arguments for the prompts that take more than an address, so the
# sweep covers separators and two-part operands as well as hex digits.
KEY_STRESS_PROMPTS = (
    ("F", "Fill AAAA-BBBB,DD", "1180-1183,55", True),
    ("H", 'Hunt AAAA-BBBB,BB/"text"', "1180-11FF", False),
    ("T", TRANSFER_PROMPT_TITLE, "1180-1183,1200", True),
    ("C", "Compare AAAA-BBBB,CCCC", "1180-1183,1200", True),
)


def run_key_input_stress_test(session: MonitorSession, rounds: int) -> int:
    """Type command arguments repeatedly and require every character to arrive.

    Each argument is typed once, character by character, and the field is read
    back and compared in full before the prompt is left again. A character
    that never arrived, one that arrived twice and two that arrived in the
    wrong order are each a failure, and none of them can be absorbed, because
    nothing is re-sent.

    The prompts are left with Back rather than RETURN, so this measures the
    input path on its own without also running the commands.
    """
    ensure_view(session, "HEX ")
    verified = 0
    for round_index in range(rounds):
        cases = [("J", "Jump AAAA", address, True)
                 for address in KEY_STRESS_ADDRESSES]
        cases.extend(KEY_STRESS_PROMPTS)
        for key, title, text, template in cases:
            try:
                session.type_into_prompt(key, title, text, template=template)
            except Failure as failure:
                raise Failure(
                    f"round {round_index + 1} of {rounds}, after {verified} "
                    f"arguments arrived character for character: {failure}")
            session.send_key("ARROW_LEFT")
            wait_for_monitor(session, f"leaving the {title} prompt")
            verified += 1
    detail(f"{verified} command arguments typed and read back character for character")
    return verified


# Addresses for the hex-edit reliability sweep. $0002 is below $1000, where a
# write while frozen reaches the bus with nothing rebanked. The rest are above
# it, where C64::dma_transfer_frozen has to put the frozen C64 mode back around
# the access. Sweeping both says whether a failure belongs to the rebanked
# range or to writing in general.
HEX_EDIT_RELIABILITY_ADDRESSES = (0x0002, 0x1100, 0x7FFF, 0x8000, 0xCFFF, 0xC000)


def run_hex_edit_reliability_test(session: MonitorSession, device_host: str,
                                  rounds: int) -> int:
    """Every hex edit reaches memory, or the device cannot write there either.

    One byte is inverted at each address in turn and read straight back with
    `machine:readmem`, which reaches memory through the C64's own DMA path
    rather than through the monitor's backend, so a monitor view showing what
    it was told cannot pass. The value changes on every pass, so a stale read
    cannot pass either.

    When an edit does not land, `assert_monitor_write_landed` decides which
    path lost it, and only a repeated failure of the monitor's own write is
    treated as the monitor's fault. Anything else is counted here and reported
    in the run, so a device losing writes is visible without being blamed on
    the monitor.
    """
    originals = {address: read_rest_memory(device_host, address, 1)
                 for address in HEX_EDIT_RELIABILITY_ADDRESSES}
    wrote = 0
    shared_losses = 0
    try:
        for round_index in range(rounds):
            for address in HEX_EDIT_RELIABILITY_ADDRESSES:
                before = read_rest_memory(device_host, address, 1)
                replacement = bytes((before[0] ^ 0xFF,))
                session.goto(f"{address:04X}")
                ensure_view(session, "HEX ")
                session.send_char("e")
                digits = f"{replacement[0]:02X}"
                session.send_char(digits[0])
                session.send_char(digits[1])
                session.send_key("ESC")

                def retry_the_edit(address: int = address,
                                   digits: str = digits) -> None:
                    session.goto(f"{address:04X}")
                    ensure_view(session, "HEX ")
                    session.send_char("e")
                    session.send_char(digits[0])
                    session.send_char(digits[1])
                    session.send_key("ESC")

                if assert_monitor_write_landed(
                        device_host, address, replacement,
                        f"round {round_index + 1} of {rounds}, after {wrote} "
                        f"edits, a Hex-view edit over {before.hex().upper()}",
                        timeout=2.0, retry_monitor_write=retry_the_edit):
                    wrote += 1
                else:
                    shared_losses += 1
    finally:
        leave_monitor_fully(session)
        for address, value in originals.items():
            write_rest_memory_confirmed(device_host, address, value)
        ensure_monitor_open(session)
    detail(f"{wrote} hex edits landed, every byte read back through the C64's "
           f"own DMA path")
    if shared_losses:
        detail(f"{shared_losses} further writes did not land first time and "
               f"were not the monitor's write path: either the device's own "
               f"machine:writemem could not place them either, or the monitor "
               f"placed them on the retry. Both are the intermittent in "
               f"C64::dma_transfer_frozen under both paths")
    return wrote


def asm_commit_cases(round_index: int) -> Tuple[Tuple[str, bytes], ...]:
    """One instruction of each length, with operands that change every round.

    Changing the operand every round is what stops a stale read from passing:
    the bytes this round expects were not in memory last round.
    """
    low = (round_index * 3 + 0x11) & 0xFF
    high = (round_index * 7 + 0x40) & 0xFF
    return (
        ("NOP", bytes((0xEA,))),
        (f"LDA#${low:02X}", bytes((0xA9, low))),
        (f"JMP${high:02X}{low:02X}", bytes((0x4C, low, high))),
    )


def run_asm_commit_reliability_test(session: MonitorSession, device_host: str,
                                    frozen: bool, rounds: int) -> int:
    """A committed instruction lands whole: never its opcode without its operand.

    One-, two- and three-byte instructions are assembled in turn at one
    ordinary main-RAM address, and the device's own memory is read back after
    each commit and compared against the whole encoding. A commit that wrote
    the opcode and not the operand fails here, naming the bytes it left.

    The read-back is `machine:readmem` rather than the monitor's own redraw,
    so a view that is showing what it was told rather than what memory holds
    cannot pass. After the last round the monitor is left completely and the
    same address is read again, which is the only read that the freezer's own
    view of memory cannot influence.
    """
    address = 0x1180
    if frozen:
        leave_monitor_fully(session)
    original = read_rest_memory(device_host, address, 3)
    committed = 0
    shared_losses = 0
    last_expected = b""
    try:
        if frozen:
            session.enter_monitor()
        for round_index in range(rounds):
            for text, expected in asm_commit_cases(round_index):
                session.goto(f"{address:04X}")
                ensure_view(session, "ASM ")
                session.send_char("e")
                for ch in text:
                    session.send_char(ch)
                session.send_key("ENTER")
                session.send_key("ESC")

                # A prefix is the defect this check exists for: the opcode
                # written and the operand not. That is the monitor's write
                # path whatever the device can do, because the instruction is
                # one block and a block cannot land in part.
                landed = wait_for_rest_data(device_host, address, expected,
                                            timeout=2.0)
                if len(expected) > 1 and landed[:1] == expected[:1] and landed != expected:
                    raise Failure(
                        f"round {round_index + 1} of {rounds}, after "
                        f"{committed} commits: {text} at ${address:04X} left "
                        f"{landed.hex().upper()}, which is its opcode without "
                        f"its operand")
                def retry_the_commit(text: str = text) -> None:
                    session.goto(f"{address:04X}")
                    ensure_view(session, "ASM ")
                    session.send_char("e")
                    for ch in text:
                        session.send_char(ch)
                    session.send_key("ENTER")
                    session.send_key("ESC")

                if assert_monitor_write_landed(
                        device_host, address, expected,
                        f"round {round_index + 1} of {rounds}, after "
                        f"{committed} commits, the commit of {text}",
                        timeout=2.0, retry_monitor_write=retry_the_commit):
                    committed += 1
                    last_expected = expected
                else:
                    shared_losses += 1
                    last_expected = b""

        # The one read the freezer cannot colour: the machine is running
        # again. Skipped when the last commit was one the device could not
        # make either, because then there is nothing of the monitor's to
        # confirm.
        if frozen:
            leave_monitor_fully(session)
        if last_expected:
            settled = wait_for_rest_data(device_host, address, last_expected,
                                         timeout=2.0)
            if settled != last_expected:
                raise Failure(
                    f"${address:04X} holds {settled.hex().upper()} once the "
                    f"monitor is closed, not the {last_expected.hex().upper()} "
                    f"that was assembled into it")
    finally:
        if frozen:
            leave_monitor_fully(session)
        write_rest_memory_confirmed(device_host, address, original)
        ensure_monitor_open(session)
    detail(f"{committed} instruction commits, every byte read back from the device")
    if shared_losses:
        detail(f"{shared_losses} further commits were lost whole by the "
               f"device's own frozen DMA path as well, never as a prefix")
    return committed


def run_asm_edit_memory_persists_test(session: MonitorSession, device_host: str,
                                      frozen: bool) -> None:
    """An instruction entered in the Assembly view reaches C64 memory too.

    The Hex-view checks above prove the write path from that one view; this
    proves it is the underlying memory write that is fixed, not something
    specific to the Hex view's own edit handling. `LDA #$xx` assembles to a
    fixed two-byte encoding (opcode $A9, then the immediate operand), so the
    expected bytes are known without decoding anything the monitor drew.
    """
    address = 0x1100
    opcode = 0xA9  # LDA #immediate
    if frozen:
        leave_monitor_fully(session)
    original = read_rest_memory(device_host, address, 2)
    operand = (original[1] + 0x55) & 0xFF
    replacement = bytes((opcode, operand))
    try:
        if frozen:
            session.enter_monitor()
        session.goto(f"{address:04X}")
        screen = ensure_view(session, "ASM ")
        screen = session.send_char("e")
        screen.find_line_containing(f"MONITOR ASM ${address:04X}")
        for ch in f"LDA#${operand:02X}":
            screen = session.send_char(ch)
        session.send_key("ENTER")
        session.send_key("ESC")

        if frozen:
            leave_monitor_fully(session)
        else:
            session.goto("E000")
            session.goto(f"{address:04X}")

        # A prefix is the monitor's own defect whatever the device can do: the
        # instruction is written as one block, so it cannot land in part.
        landed = wait_for_rest_data(device_host, address, replacement)
        if landed[:1] == replacement[:1] and landed != replacement:
            raise Failure(
                f"an Assembly-view edit left ${address:04X} holding "
                f"{landed.hex().upper()}, which is the opcode of "
                f"LDA #${operand:02X} without its operand")
        assert_monitor_write_landed(
            device_host, address, replacement,
            f"an Assembly-view edit of LDA #${operand:02X}"
            + (" read back after leaving the monitor" if frozen else ""))
    finally:
        write_rest_memory_confirmed(device_host, address, original)
        if frozen:
            session.enter_monitor()


def run_go_repeat_test(session: MonitorSession, rest_host: str, frozen: bool, control: str) -> None:
    sentinel_addr = 0xC200
    sentinel = 0x5A
    values = (0x42, 0x37, 0x99)

    for value in values:
        # Close the monitor before writing each fixture to live RAM.
        session.send_key("ESC")
        if frozen:
            close_rest_menu(control, None)
        else:
            reset_rest_machine(control, None)
        write_rest_memory(rest_host, 0x0810,
                          bytes((0xA9, value, 0x8D, sentinel_addr & 0xFF, sentinel_addr >> 8, 0x00)))
        write_rest_memory(rest_host, sentinel_addr, bytes((sentinel,)))
        # Confirm before monitor entry: Freeze may otherwise show the prior iteration.
        wait_for_rest_byte(rest_host, sentinel_addr, sentinel)

        session.enter_monitor()
        ensure_view(session, "HEX ")
        before = goto_and_read_byte(session, f"{sentinel_addr:04X}", sentinel_addr, expected=sentinel)
        if before != sentinel:
            rest_now = read_rest_memory(rest_host, sentinel_addr, 1)[0]
            raise Failure(
                f"G precondition failed for ${value:02X}: expected ${sentinel:02X} at ${sentinel_addr:04X}, "
                f"monitor view shows ${before:02X}, REST reads ${rest_now:02X} "
                f"({'monitor view is stale' if rest_now == sentinel else 'memory changed'})"
            )

        session.goto_run("0810")
        wait_for_rest_byte(rest_host, sentinel_addr, value)

        session.enter_monitor()
        ensure_view(session, "HEX ")
        after = goto_and_read_byte(session, f"{sentinel_addr:04X}", sentinel_addr, expected=value)
        if after != value:
            raise Failure(
                f"G postcondition failed for ${value:02X}: expected ${value:02X} at ${sentinel_addr:04X}, got ${after:02X}"
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

    screen = session.send_key("CTRL_B", settle=True)
    screen.find_line_containing("BOOKMARKS")
    screen = session.send_key("DOWN")
    screen = session.send_key("DEL")
    assert_line_contains_all(screen, ("1 SCREEN", "$0400", "SCR 32"))
    screen = session.send_key("CTRL_B", settle=True)
    screen.find_line_containing("MONITOR")

    screen = session.goto("C123")
    screen.find_line_containing("MONITOR HEX $C123")
    screen = session.send_char("W", settle=True)
    screen = session.send_key("CTRL_B", settle=True)
    screen.find_line_containing("BOOKMARKS")
    screen = session.send_key("DOWN")
    screen = session.send_char("S")
    assert_line_contains_all(screen, ("BM1 SCREEN $C123 HEX W16", "SET"))
    screen = session.send_key("CTRL_B", settle=True)
    screen.find_line_containing("MONITOR HEX $C123")

    screen = session.goto("E000")
    screen.find_line_containing("MONITOR HEX $E000")
    screen = session.send_key("CBM_1")
    screen.find_line_containing("MONITOR HEX $C123")
    screen.find_line_containing("BM1 SCREEN $C123 HEX W16")

    screen = session.send_key("CTRL_B", settle=True)
    screen.find_line_containing("BOOKMARKS")
    assert_line_contains_all(screen, ("1 SCREEN", "$C123", "HEX 16"))
    screen.find_line_containing("0-9/RET Jmp  S Set  L Label  DEL Reset")

    screen = session.send_key("DOWN")
    screen = session.send_char("L")
    screen = session.send_text("\b\b\b\b\b\bE2E\r", "bookmark label E2E")
    assert_line_contains_all(screen, ("1 E2E", "$C123", "HEX 16"))

    screen = session.send_key("CTRL_B", settle=True)
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

    screen = session.send_key("CTRL_B", settle=True)
    screen.find_line_containing("BOOKMARKS")
    screen = session.send_key("DOWN")
    screen = session.send_key("DEL")
    assert_line_contains_all(screen, ("1 SCREEN", "$0400", "SCR 32"))
    screen = session.send_key("CTRL_B", settle=True)
    screen.find_line_containing("MONITOR")

    screen = ensure_hex_width(session, 8)
    screen = session.goto("3000")
    screen.find_line_containing("3000 00 01 02 03 04 05 06 07")

    screen = session.send_char("W", settle=True)
    screen.find_line_containing("3000 0001020304050607 08090A0B0C0D0E0F")

    screen = session.send_key("CTRL_B", settle=True)
    screen.find_line_containing("BOOKMARKS")
    screen = session.send_key("DOWN")
    screen = session.send_char("S")
    assert_line_contains_all(screen, ("BM1 SCREEN $3000 HEX W16", "SET"))
    screen = session.send_key("CTRL_B", settle=True)
    screen.find_line_containing("MONITOR HEX $3000")

    screen = session.goto("E000")
    screen.find_line_containing("MONITOR HEX $E000")
    screen = session.send_key("CBM_1")
    screen.find_line_containing("MONITOR HEX $3000")
    screen.find_line_containing("BM1 SCREEN $3000 HEX W16")
    screen.find_line_containing("3000 0001020304050607 08090A0B0C0D0E0F")

    screen = session.send_key("CTRL_B", settle=True)
    screen.find_line_containing("BOOKMARKS")
    assert_line_contains_all(screen, ("1 SCREEN", "$3000", "HEX 16"))
    screen = session.send_key("CTRL_B", settle=True)
    screen.find_line_containing("MONITOR HEX $3000")


def run_binary_bookmark_width_test(session: MonitorSession, rest_host: str) -> None:
    # Widths 3 and 4 align their rows down to $C3FF, so the row the checks below
    # read starts one byte before the sentinel; that byte is seeded too.
    #
    # $C400 rather than the $3100 this used: $3100 is inside the RAM that BASIC
    # and a loaded program share, and in a full gate run a suite that ran
    # earlier can still own it. Seen live, the fixture would not hold there,
    # with the four bytes before the last one correct. Everything from $C000 up
    # is free of that, which is why the rest of this suite works there. The
    # alignment is the same: both addresses are 4 modulo 12, so width 3 still
    # starts its row one byte below and width 4 still starts on the sentinel.
    #
    # Confirmed rather than written and hoped for: every assertion below reads
    # these five bytes back off the screen, so a fixture that has not landed
    # would otherwise fail as a mismatch in the rendering under test.
    write_rest_memory_confirmed(rest_host, 0xC3FF, bytes((0x00, 0x12, 0x34, 0x56, 0x78)))

    screen = session.send_key("CTRL_B", settle=True)
    screen.find_line_containing("BOOKMARKS")
    screen = session.send_key("DOWN")
    screen = session.send_key("DEL")
    assert_line_contains_all(screen, ("1 SCREEN", "$0400", "SCR 32"))
    screen = session.send_key("CTRL_B", settle=True)
    screen.find_line_containing("MONITOR")

    screen = ensure_view(session, "BIN ")
    screen = session.goto("C400")
    for _ in range(5):
        try:
            screen.find_line_containing("C400 ...*..*. 12")
            break
        except Failure:
            screen = session.send_char("W", settle=True)
    screen.find_line_containing("C400 ...*..*. 12")

    screen = session.send_char("W", settle=True)
    screen.find_line_containing("C400 ...*..*. ..**.*.. 12 34")

    screen = session.send_char("W", settle=True)
    screen.find_line_containing("C3FF ........ ...*..*. ..**.*.. 001234")

    screen = session.send_char("W", settle=True)
    screen.find_line_containing("C3FF ...........*..*...**.*.. 00 12 34")

    screen = session.send_char("W", settle=True)
    screen.find_line_containing("C400 ...*..*...**.*...*.*.**..****...")
    assert_line_lacks(screen, "12 34 56 78")

    screen = session.send_key("CTRL_B", settle=True)
    screen.find_line_containing("BOOKMARKS")
    screen = session.send_key("DOWN")
    screen = session.send_char("S")
    assert_line_contains_all(screen, ("BM1 SCREEN $C400 BIN W4", "SET"))
    screen = session.send_key("CTRL_B", settle=True)
    screen.find_line_containing("MONITOR BIN $C400")

    screen = session.goto("E000")
    screen.find_line_containing("MONITOR BIN $E000")
    screen = session.send_key("CBM_1")
    screen.find_line_containing("MONITOR BIN $C400")
    screen.find_line_containing("BM1 SCREEN $C400 BIN W4")
    screen.find_line_containing("C400 ...*..*...**.*...*.*.**..****...")
    assert_line_lacks(screen, "12 34 56 78")

    screen = session.send_key("CTRL_B", settle=True)
    screen.find_line_containing("BOOKMARKS")
    assert_line_contains_all(screen, ("1 SCREEN", "$C400", "BIN  4"))
    screen = session.send_key("CTRL_B", settle=True)
    screen.find_line_containing("MONITOR BIN $C400")


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

# A run of NOPs, then a deliberately mixed-length program: one, two and three
# byte instructions in turn. Disassembling these bytes from one byte out
# produces completely different text, which is what makes a re-alignment
# visible rather than plausible.
ASM_ANCHOR_FILL = 0xC4C0
ASM_ANCHOR_ADDRESS = 0xC500
ASM_ANCHOR_PROGRAM = bytes((
    0xEA,                    # NOP
    0xA9, 0x01,              # LDA #$01
    0xAD, 0x00, 0xC0,        # LDA $C000
    0xEA,                    # NOP
    0x20, 0x00, 0xC6,        # JSR $C600
    0xA2, 0xFF,              # LDX #$FF
    0x4C, 0x00, 0xC5,        # JMP $C500
))
# How far to walk away from the baseline and back.
ASM_ANCHOR_STEPS = 6


def asm_row_for(snapshot: Snapshot, address: int) -> Optional[str]:
    """The disassembly row for `address`, or None when it is off screen.

    Anchored on the row starting with the address rather than merely
    containing it: the header reads "MONITOR ASM $C500" and would match too.
    """
    target = f"{address:04X} "
    for line in snapshot.lines:
        text = line[1:] if line.startswith("|") else line
        if text.startswith(target):
            return text.rstrip().rstrip("|").rstrip()
    return None


def asm_cursor_address(snapshot: Snapshot) -> str:
    """The address the Assembly header says the cursor is on."""
    header = snapshot.line(snapshot.find_line_containing("MONITOR ASM "))
    return header.split("MONITOR ASM ", 1)[1].split()[0]


def check_anchor_survives_navigation(session: MonitorSession, address: int,
                                     what: str) -> None:
    """Walk away from a baseline and back; the disassembly must not move.

    A jump sets the address the Assembly view disassembles from, and every row
    on screen is decoded by chaining instruction lengths from it. Stepping up
    leaves that chain and has to guess where the previous instruction began,
    so a wrong guess re-aligns the whole screen: the same bytes read as a
    different program. The bytes cannot change here, so any difference in what
    is displayed is the view losing its place.
    """
    baseline = ensure_view(session, "ASM ")
    anchor_row = asm_row_for(baseline, address)
    started_at = asm_cursor_address(baseline)
    if anchor_row is None:
        raise Failure(f"{what}: ${address:04X} is not on screen to begin with\n"
                      f"{baseline.text()}")

    for up, down in (("UP", "DOWN"), ("PGUP", "PGDN")):
        for step in range(ASM_ANCHOR_STEPS):
            screen = session.send_key(up)
            moved = asm_row_for(screen, address)
            if moved is not None and moved != anchor_row:
                raise Failure(
                    f"{what}: {step + 1} press(es) of {up} changed the "
                    f"disassembly at ${address:04X}\n"
                    f"  before: {anchor_row!r}\n"
                    f"  after:  {moved!r}\n{screen.text()}"
                )
        for _ in range(ASM_ANCHOR_STEPS):
            screen = session.send_key(down)
        # Walking back the same number of rows has to land where it started.
        # It does not if a step backwards guessed a different instruction
        # boundary from the one a step forwards uses.
        landed = asm_cursor_address(screen)
        if landed != started_at:
            raise Failure(
                f"{what}: {ASM_ANCHOR_STEPS} presses of {up} and {down} left "
                f"the cursor at {landed}, not {started_at}\n{screen.text()}"
            )
        back = asm_row_for(screen, address)
        if back is not None and back != anchor_row:
            raise Failure(
                f"{what}: the disassembly at ${address:04X} did not survive "
                f"{up}/{down}\n  before: {anchor_row!r}\n  after:  {back!r}\n"
                f"{screen.text()}"
            )


def run_asm_backwards_navigation_test(session: MonitorSession, rest_host: str) -> None:
    """Every way of setting a baseline keeps it while the view is scrolled."""
    write_rest_memory_confirmed(rest_host, ASM_ANCHOR_FILL, bytes((0xEA,) * 0x40))
    write_rest_memory_confirmed(rest_host, ASM_ANCHOR_ADDRESS, ASM_ANCHOR_PROGRAM)

    # A jump sets the baseline.
    ensure_view(session, "ASM ")
    session.goto(f"{ASM_ANCHOR_ADDRESS:04X}")
    screen = ensure_view(session, "ASM ")
    screen.find_line_containing("LDA #$01")
    screen.find_line_containing("JSR $C600")
    check_anchor_survives_navigation(session, ASM_ANCHOR_ADDRESS, "a jump to RAM")

    # A bookmark sets one too: slot 9 is KERNAL $E000 in the Assembly view.
    session.send_key("CBM_9")
    screen = ensure_view(session, "ASM ")
    screen.find_line_containing("MONITOR ASM $E000")
    check_anchor_survives_navigation(session, 0xE000, "a bookmark jump to $E000")

    # And so does a jump to a ROM entry the CPU view has banked in.
    session.goto("A000")
    ensure_view(session, "ASM ")
    check_anchor_survives_navigation(session, 0xA000, "a jump to $A000")

    # Coming back to where this started has to show what it showed before.
    session.goto(f"{ASM_ANCHOR_ADDRESS:04X}")
    screen = ensure_view(session, "ASM ")
    screen.find_line_containing("LDA #$01")
    screen.find_line_containing("JSR $C600")


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



def run_reentry_test(session: MonitorSession) -> None:
    """The monitor can be left and re-entered, and comes back where it was.

    Each of the three exits is used once, so a wedge that only one of them
    produces is not hidden by the other two. The address and the view are the
    monitor's saved state, so re-entry has to bring both back; a monitor that
    reopened at its default address would still look alive to a check that only
    asserted it reopened.
    """
    ensure_view(session, "BIN ")
    session.goto("C100")
    before = session.capture()
    view_and_address = monitor_header(before).strip().strip("|").strip()

    for exit_key in ("CTRL_O", "RUNSTOP", "ESC"):
        session.send_key(exit_key, settle=True)
        snapshot = wait_until(session, lambda screen: not monitor_is_on_screen(screen))
        if monitor_is_on_screen(snapshot):
            raise Failure(
                f"{exit_key} did not leave the monitor\n{snapshot.text()}")
        session.enter_monitor()
        snapshot = wait_until(session, monitor_is_on_screen)
        if not monitor_is_on_screen(snapshot):
            raise Failure(
                f"the monitor did not reopen after {exit_key}\n{snapshot.text()}")
        again = monitor_header(snapshot).strip().strip("|").strip()
        if again != view_and_address:
            raise Failure(
                f"after leaving with {exit_key} and reopening, the monitor "
                f"header reads {again!r}, expected {view_and_address!r}")

    # Leave the suite in the view the checks after this one expect.
    ensure_view(session, "HEX ")


def run_freeze_toggle_test(session: MonitorSession, live_host: str) -> None:
    """`Z` stops and releases the C64, on a machine whose freezer it can reach.

    The monitor draws no freeze indicator, so the machine itself is the oracle:
    the KERNAL jiffy clock at $00A2 advances 60 times a second while the C64
    runs and does not advance at all while it is stopped. Where the freezer is
    not reachable the monitor says so in a popup, which is the other behaviour
    worth holding, so this check accepts either and requires the monitor to be
    usable afterwards in both cases.
    """
    def jiffy_advances() -> bool:
        first = read_rest_memory(live_host, 0x00A2, 1)[0]
        for _ in range(12):
            time.sleep(0.1)
            if read_rest_memory(live_host, 0x00A2, 1)[0] != first:
                return True
        return False

    screen = session.send_char("Z")
    text = screen.text()
    if "FREEZE" in text and "UNAVAILABLE" in text or "ONLY IN OVERLAY" in text:
        session.send_key("ENTER", settle=True)
        snapshot = wait_until(session, monitor_is_on_screen)
        if not monitor_is_on_screen(snapshot):
            raise Failure(
                "the monitor did not survive a refused freeze\n"
                f"{snapshot.text()}")
        # Which of the two paths ran is the thing a reader wants from this
        # check, and the two take very different times, so say it rather than
        # leave OK to mean either.
        detail("Z: this machine's freezer is not reachable from here, so the "
               "monitor refused and stayed. The stop-and-release path is "
               "exercised under --mode freeze, not here")
        return

    if jiffy_advances():
        raise Failure(
            "Z did not stop the C64: the jiffy clock at $00A2 kept advancing")
    session.send_char("Z")
    if not jiffy_advances():
        raise Failure(
            "Z did not release the C64: the jiffy clock at $00A2 stayed still")
    snapshot = wait_until(session, monitor_is_on_screen)
    if not monitor_is_on_screen(snapshot):
        raise Failure(
            f"the monitor did not survive a freeze and release\n{snapshot.text()}")
    detail("Z stopped the C64 and released it again, with the jiffy clock at "
           "$00A2 as the oracle")


def run_asm_entry_round_trip_test(session: MonitorSession, rest_host: str,
                                  video_host: str, control: str,
                                  verify_video: bool) -> None:
    """Enter instructions, verify their bytes, then prove that G produces video."""
    address = 0xC000
    entered = bytes((0xEE, 0x21, 0xD0, 0x4C, 0x00, 0xC0))

    def type_asm(line: str) -> Snapshot:
        if isinstance(session.backend, TelnetBackend):
            return session.send_text(line + "\r", f"ASM {line}")
        screen = session.send_char(line[0])
        for char in line[1:]:
            screen = session.send_char(char)
        return session.send_key("ENTER")

    write_rest_memory_confirmed(rest_host, address, bytes((0xEA,) * 12))
    screen = ensure_view(session, "ASM ")
    screen = session.goto(f"{address:04X}")
    screen = session.send_char("e")
    screen = type_asm("INC$D021")
    screen = type_asm("JMP$C000")
    session.send_key("ESC")

    screen = session.goto(f"{address:04X}")
    screen.find_line_containing("INC $D021")
    screen.find_line_containing("JMP $C000")
    actual = read_rest_memory(rest_host, address, len(entered))
    if actual != entered:
        raise Failure(
            f"ASM entry mismatch at ${address:04X}: expected {entered.hex().upper()}, "
            f"REST reads {actual.hex().upper()}"
        )

    # Extend the typed loop with a RAM side effect that remains observable
    # while the colour-changing loop runs.
    screen = session.goto("C003")
    screen = session.send_char("e")
    screen = type_asm("LDA#$5A")
    screen = type_asm("STA$C200")
    screen = type_asm("JMP$C000")
    session.send_key("ESC")

    expected = bytes((0xEE, 0x21, 0xD0, 0xA9, 0x5A, 0x8D, 0x00, 0xC2, 0x4C, 0x00, 0xC0))
    screen = session.goto(f"{address:04X}")
    screen.find_line_containing("LDA #$5A")
    screen.find_line_containing("STA $C200")
    screen.find_line_containing("JMP $C000")
    if read_rest_memory(rest_host, address, len(expected)) != expected:
        raise Failure(f"ASM handoff fixture mismatch at ${address:04X}")

    write_rest_memory_confirmed(rest_host, 0xC200, b"\x00")
    if verify_video:
        with AvStreamCapture(video_host) as capture:
            capture.capture(0.15)
            capture.clear()
            session.goto_run(f"{address:04X}")
            capture.clear()
            launched = time.monotonic()
            capture.capture(0.60)
            frames = [frame for frame in video_frames(capture.video_packets)
                      if frame.received_at >= launched]
            if not frames:
                raise Failure("G $C000 produced no complete C64U video frame")
            visible = [frame for frame in frames if set(frame.pixels) != {0}]
            if not visible:
                assert_not_black(frames[-1], "G $C000 video")
            assert_frames_differ(visible, "G $C000 video")
    else:
        session.goto_run(f"{address:04X}")
    wait_for_rest_byte(rest_host, 0xC200, 0x5A)

    reset_rest_machine(control, None)
    session.enter_monitor()


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
    find_any_status_line(screen)


def run_number_arithmetic_test(session: MonitorSession, rest_host: str) -> None:
    write_rest_memory(rest_host, 0x3370, bytes((0x20, 0x00, 0xC0, 0xEA)))

    screen = ensure_view(session, "ASM ")
    screen = session.goto("3370")
    screen = ensure_view(session, "ASM ")
    screen.find_line_containing("JSR $C000")
    screen = session.send_char("N", settle=True)
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


def clear_prompt_field(session: MonitorSession) -> None:
    """Empty a (non-template) string prompt by sending backspaces.

    The monitor's "Save as" prompt is pre-filled with the last-used name and
    does not auto-clear on the first keystroke, so we delete it first. The
    field is at most 35 characters, hence the generous count."""
    session.send_key_repeat("BACKSPACE", 40)


def wait_for_screen_contains(session: MonitorSession, text: str,
                             timeout: float = 5.0) -> Snapshot:
    deadline = time.time() + timeout
    snapshot = session.capture()
    while time.time() < deadline:
        if text in snapshot.text():
            return snapshot
        time.sleep(0.05)
        snapshot = session.capture()
    snapshot.find_line_containing(text)
    return snapshot


def rest_create_d64(host: str, path: str, diskname: str) -> None:
    url = f"http://{host}/v1/files{path}:create_d64?diskname={diskname}"
    request = urllib.request.Request(url, data=b"", method="PUT")
    # Creating the same image twice leaves the same image.
    with rest_lib.retrying_urlopen(request, 15.0, idempotent=True):
        pass


def rest_file_exists(host: str, path: str) -> bool:
    try:
        with rest_lib.retrying_urlopen(
                urllib.request.Request(f"http://{host}/v1/files{path}:info"), 5.0) as response:
            return response.status == 200
    except urllib.error.HTTPError:
        return False


def wait_for_rest_file(host: str, path: str, timeout: float = 60.0) -> None:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if rest_file_exists(host, path):
            return
        time.sleep(0.1)
    raise Failure(f"Saved file {path} not found via REST")


def monitor_save(session: MonitorSession, mem_range: str, enter_dirs: List[str], filename: str) -> Snapshot:
    """Save mem_range to filename, navigating from root through enter_dirs.

    enter_dirs is a list of quick-seek prefixes to step into (e.g. ["MS"] for a
    /Temp subtree reached from root, or ["MD"] then the D64). The final
    directory must offer "<< Create New File >>" as its first entry."""
    session.send_char("S", settle=True)
    assert_prompt_rejects(session, "Save AAAA-BBBB", "X")
    session.send_text(mem_range + "\r", f"save range {mem_range}")
    picker_to_root(session)
    # Root-entry order differs by target, so seek /Temp by name.
    snapshot = picker_enter(session, "Temp")
    for prefix in enter_dirs:
        snapshot = picker_enter(session, prefix)
    # The cursor defaults to "<< Create New File >>"; RIGHT picks it and the
    # monitor then asks for the file name.
    session.send_key("RIGHT")
    clear_prompt_field(session)
    session.send_text(filename + "\r", f"save as {filename}")
    snapshot = wait_for_screen_contains(session, "SAVE")
    # Dismissing this popup has the same two-burst redraw as other settled keys.
    return session.send_key("ENTER", settle=True)  # dismiss the confirmation popup


def monitor_load(session: MonitorSession, enter_dirs: List[str], filename: str,
                 params: str = "PRG,0,AUTO") -> Snapshot:
    """Load filename back, navigating from root through enter_dirs.

    `params` is what to type at the parameter prompt. Both spellings load the
    same way: "PRG,0,AUTO" names every field, and ",," leaves all three empty
    and takes their defaults.
    """
    session.send_char("L", settle=True)
    picker_to_root(session)
    picker_enter(session, "Temp")  # quick-seek by name; see monitor_save
    for prefix in enter_dirs:
        picker_enter(session, prefix)
    for ch in filename:
        session.send_char(ch)  # quick-seek to the file
    session.send_key("ENTER")  # open the context menu ("Select" is first)
    session.send_key("ENTER")  # Select -> pick the file
    # "Load [PRG|AAAA],[Offs],[Len|AUTO]" prompt: typing the spec clears the
    # template, so the load is defined by what is typed rather than by the
    # last-used value. This prompt is structured, so it also refuses a
    # character no load could contain; proved here rather than in a flow of
    # its own, because reaching it means picking a file first.
    assert_prompt_rejects(session, "Load [PRG|AAAA]", "Z")
    session.send_text(params + "\r", f"load {params}")
    wait_for_screen_contains(session, "LOAD")
    # This dismissal also has a two-burst redraw.
    return session.send_key("ENTER", settle=True)  # dismiss the confirmation popup


def run_save_load_topfile_test(session: MonitorSession, rest_host: str, files_host: str,
                               token: str) -> None:
    addr = 0xC000
    pattern = bytes((0x5A, 0xA5, 0x01, 0x02, 0xDE, 0xAD, 0xBE, 0xEF,
                     0x10, 0x20, 0x30, 0x40, 0x50, 0x60, 0x70, 0x80))
    name = f"MS{token}.PRG"

    write_rest_memory_confirmed(rest_host, addr, pattern)
    monitor_save(session, f"{addr:04X}-{addr + len(pattern) - 1:04X}", [], name)
    wait_for_rest_file(files_host, f"/Temp/{name}")

    write_rest_memory_confirmed(rest_host, addr, b"\x00" * len(pattern))
    monitor_load(session, [], f"MS{token}")
    assert_monitor_write_landed(
        rest_host, addr, pattern,
        f"a top-level save of {name} loaded back")


def run_save_load_d64_test(session: MonitorSession, rest_host: str, files_host: str,
                           token: str) -> None:
    addr = 0xC100
    pattern = bytes((0x11, 0x22, 0x33, 0x44, 0xAA, 0xBB, 0xCC, 0xDD,
                     0x09, 0x08, 0x07, 0x06, 0x05, 0x04, 0x03, 0x02))
    disk = f"MD{token}.D64"
    inner = f"D{token}"

    rest_create_d64(files_host, f"/Temp/{disk}", f"MD{token}")
    wait_for_rest_file(files_host, f"/Temp/{disk}")

    write_rest_memory_confirmed(rest_host, addr, pattern)
    monitor_save(session, f"{addr:04X}-{addr + len(pattern) - 1:04X}", [f"MD{token}"], inner)

    write_rest_memory_confirmed(rest_host, addr, b"\x00" * len(pattern))
    # ",," is the same load with all three fields left empty.
    monitor_load(session, [f"MD{token}"], inner, params=",,")
    assert_monitor_write_landed(
        rest_host, addr, pattern,
        f"a save into {disk} loaded back")


# ---------------------------------------------------------------------------
# Back semantics and command-prompt input.
#
# The lexical space of what each prompt accepts is covered exhaustively by the
# host tests in software/test/monitor; these prove the same rules hold on the
# device, through real keys, one representative case per prompt.
# ---------------------------------------------------------------------------

# A command prompt draws a five-row bordered box: the title on its first inner
# row, and the edit field two rows below it.
PROMPT_FIELD_OFFSET = 2

# The vertical run of a window border, on both transports.
PROMPT_BORDER = "|"


def prompt_field(snapshot: Snapshot, title: str) -> str:
    """What the open command prompt currently holds, as shown on screen.

    The prompt is a narrow box drawn over the memory view, so the field row
    carries the view's own text on either side of it. The box's two border
    columns are read off the title row, which has them in the same places, and
    only what lies between them is the field.
    """
    row = snapshot.find_line_containing(title)
    title_line = snapshot.line(row)
    field_line = snapshot.line(row + PROMPT_FIELD_OFFSET)
    at = title_line.find(title)
    left = title_line.rfind(PROMPT_BORDER, 0, at) if at > 0 else -1
    right = title_line.find(PROMPT_BORDER, at + len(title)) if at >= 0 else -1
    if left >= 0 and right > left:
        field_line = field_line[left + 1:right]
    return field_line.strip().strip(PROMPT_BORDER).strip()


def prompt_field_or_none(snapshot: Snapshot, title: str) -> Optional[str]:
    """What an open prompt holds, or None while it is not drawn."""
    try:
        return prompt_field(snapshot, title)
    except Failure:
        return None


def wait_until(session: MonitorSession, ready, timeout: float = 5.0) -> Snapshot:
    """Re-read the screen until `ready` accepts it, or the budget runs out.

    Waiting for the thing being waited for, rather than for a fixed quiet
    period: over Telnet a capture that expects no redraw costs one short quiet
    check, so polling is both faster than the settle gap when the screen is
    already right and more patient than it when the redraw is late.
    """
    deadline = time.time() + timeout
    snapshot = session.capture()
    while not ready(snapshot):
        if time.time() >= deadline:
            return snapshot
        time.sleep(0.05)
        snapshot = session.capture()
    return snapshot


def wait_for_prompt(session: MonitorSession, title: str) -> Snapshot:
    """Wait for a command prompt to be drawn."""
    snapshot = wait_until(session, lambda screen: title in screen.text())
    snapshot.find_line_containing(title)
    return snapshot


def wait_for_monitor(session: MonitorSession, why: str) -> Snapshot:
    """Wait for the monitor's own view to be back on screen."""
    def drawn(snapshot: Snapshot) -> bool:
        try:
            find_any_status_line(snapshot)
            return True
        except Failure:
            return False

    snapshot = wait_until(session, drawn)
    assert_monitor_visible(snapshot, why)
    return snapshot


def assert_monitor_visible(snapshot: Snapshot, why: str) -> None:
    try:
        find_any_status_line(snapshot)
    except Failure:
        raise Failure(f"{why}: the monitor is no longer on screen\n{snapshot.text()}")


def monitor_header(snapshot: Snapshot) -> str:
    """The monitor window's own header row."""
    return snapshot.line(snapshot.find_line_containing("MONITOR "))


def monitor_header_address(snapshot: Snapshot) -> Optional[str]:
    """The address the monitor header names, or None while it is not drawn."""
    try:
        header = monitor_header(snapshot)
    except Failure:
        return None
    found = re.search(r"\$([0-9A-F]{4})", header)
    return found.group(1) if found else None


# The help overlay's own section heading, and nothing else on either
# transport says it. The word HELP alone would not do: the root browser's
# footer carries an "F3=HELP" hint that the Overlay screen shows below the
# monitor box.
HELP_MARKER = "CONTROL KEYS"


def assert_help_open(snapshot: Snapshot, why: str) -> None:
    if HELP_MARKER not in snapshot.text():
        raise Failure(f"{why}: help did not open\n{snapshot.text()}")


def assert_help_closed(snapshot: Snapshot, why: str) -> None:
    if HELP_MARKER in snapshot.text():
        raise Failure(f"{why}: help is still open\n{snapshot.text()}")
    assert_monitor_visible(snapshot, why)


def close_help(session: MonitorSession, close_key: str) -> None:
    """Open help, close it with `close_key`, and stay in the monitor."""
    screen = session.send_char("?")
    assert_help_open(screen, "'?' opening help")
    screen = (session.send_char("?") if close_key == "?" else session.send_key(close_key))
    assert_help_closed(screen, f"{close_key} closing help")


def help_content_line(snapshot: Snapshot, containing: str) -> str:
    """The Help row containing `containing`, with its left border stripped.

    Content column 1 (1-based, as the layout is specified) is index 0 of the
    string this returns: the border is whatever sits before the row's own
    text, one character on Telnet's 60-column frame and Overlay's 40-column
    one alike.
    """
    index = snapshot.find_line_containing(containing)
    line = snapshot.line(index)
    stripped = line.lstrip()
    border = len(line) - len(stripped)
    if stripped.startswith("|"):
        border += 1
    return line[border:]


def assert_help_column(snapshot: Snapshot, containing: str, column: int,
                       expected: str) -> None:
    """`expected` starts at 1-based content column `column` of a Help row."""
    content = help_content_line(snapshot, containing)
    actual = content[column - 1:column - 1 + len(expected)]
    if actual != expected:
        raise Failure(
            f"Help layout: column {column} of {content!r} is {actual!r}, "
            f"expected {expected!r}"
        )


def run_help_layout_test(session: MonitorSession) -> None:
    """The Help screen's KEY-first grammar and its two fixed column grids.

    Loose substring checks would let the column anchors this layout depends
    on drift silently; every position checked here is one the task's design
    fixes explicitly, so a regression shows as this check failing rather than
    as a screen that merely still contains the right words in the wrong
    place.
    """
    screen = session.send_char("?")
    assert_help_open(screen, "opening help for the layout check")

    if "Undo" in screen.text() and "Undoc" not in screen.text():
        raise Failure(f"U is described as Undo rather than Undoc/Case\n{screen.text()}")
    if "{Q}" in screen.text() or " Q CPU Bank" in screen.text():
        raise Failure(f"CPU Bank is bound to Q rather than O\n{screen.text()}")
    if "RUN/STOP/<-" in screen.text():
        raise Failure(f"RUNSTOP/<- is spelled RUN/STOP/<-\n{screen.text()}")

    # Primary grid: columns 1, 14, 27.
    assert_help_column(screen, "Memory", 1, "M Memory")
    assert_help_column(screen, "Memory", 14, "I ASCII")
    assert_help_column(screen, "Memory", 27, "V Screen")
    assert_help_column(screen, "CPU Bank", 1, "Z Freeze")
    assert_help_column(screen, "CPU Bank", 14, "O CPU Bank")
    assert_help_column(screen, "CPU Bank", 27, "SH+O VIC")
    assert_help_column(screen, "Undoc", 27, "U Undoc/Case")

    # The Jump/Go line has only two of the grid's three columns, because X is
    # not an exit key and the help must not offer it as one.
    assert_help_column(screen, "G Go", 1, "J Jump")
    assert_help_column(screen, "G Go", 14, "G Go")
    # Columns 27 to 38 are the grid's third column. The popup's right border
    # sits past column 38 on the Overlay frame, so it is excluded by the
    # slice rather than by stripping.
    jump_line = help_content_line(screen, "G Go")
    if jump_line[26:38].strip():
        raise Failure(
            f"Help layout: the Jump/Go line has a third column: {jump_line!r}")
    if "Exit" in screen.text():
        raise Failure(f"Help still offers an Exit key\n{screen.text()}")

    for label in ("BOOKMARKS", "CONTROL KEYS"):
        line = help_content_line(screen, label)
        if not line.startswith(label):
            raise Failure(f"{label} heading is not at column 1: {line!r}")
        if line.rstrip().endswith(":"):
            raise Failure(f"{label} heading has a trailing colon: {line!r}")

    # BOOKMARKS and CONTROL KEYS share one grid: columns 1, 12, 21, 29.
    assert_help_column(screen, "List", 1, "C=+B")
    assert_help_column(screen, "List", 12, "List")
    assert_help_column(screen, "List", 21, "C=+0-9")
    assert_help_column(screen, "List", 29, "Jump")

    assert_help_column(screen, "Edit off", 1, "C=+E")
    assert_help_column(screen, "Edit off", 12, "Edit off")
    assert_help_column(screen, "Edit off", 21, "C=+C/V")
    assert_help_column(screen, "Edit off", 29, "Copy/Paste")

    assert_help_column(screen, "Follow/Ret", 1, "RUNSTOP/")
    assert_help_column(screen, "Follow/Ret", 12, "Back")
    assert_help_column(screen, "Follow/Ret", 21, "RETURN")
    assert_help_column(screen, "Follow/Ret", 29, "Follow/Ret")

    assert_help_column(screen, "Monitor", 1, "?/")
    assert_help_column(screen, "Monitor", 12, "Help")
    assert_help_column(screen, "Monitor", 21, "C=+O")
    assert_help_column(screen, "Monitor", 29, "Monitor")

    assert_help_column(screen, "Page down", 1, "F1/")
    assert_help_column(screen, "Page down", 12, "Page up")
    assert_help_column(screen, "Page down", 21, "F7/")
    assert_help_column(screen, "Page down", 29, "Page down")

    # No line inside the Help popup's own border may spill past content
    # column 38. Scoped to bordered rows only: the screen around the popup
    # (title, decorative rule, footer) is not part of what this layout
    # governs and is not held to its width.
    for line in screen.lines:
        content = line.lstrip()
        if not content.startswith("|"):
            continue
        content = content[1:].rstrip()
        if content.endswith("|"):
            content = content[:-1].rstrip()
        if len(content) > 38:
            raise Failure(f"a Help line is longer than 38 columns: {content!r}")

    screen = session.send_key("RUNSTOP")
    assert_help_closed(screen, "closing help after the layout check")


def back_out_to_the_bare_browser(session: MonitorSession) -> Snapshot:
    """Press Back until nothing is drawn over the file browser.

    Bounded and observed rather than counted: how many presses a context costs
    is a property of that context. A fixed number of presses either leaves
    something open or spends a spare press on whatever the browser does with
    it, and the next thing this suite does is send a key that means something
    different in each of those states.

    Leaving the settings screens raises "Save changes to Flash?" whenever the
    configuration in memory differs from the one in flash, which it does on any
    device where the REST backend switched `Interface Type` for the session.
    Back does not answer a Yes/No dialog, so that dialog is answered here, with
    No: this suite only visited those screens and has no configuration change
    of its own to keep, and answering Yes would write the session's temporary
    `Interface Type` into the device's flash.
    """
    for _ in range(8):
        snapshot = session.capture()
        text = snapshot.text()
        if "Save changes to Flash?" in text:
            session.send_key("RIGHT", settle=True)     # Yes -> No
            session.send_key("ENTER", settle=True)
            continue
        if not any("+--" in line for line in snapshot.lines):
            return snapshot
        session.send_key("RUNSTOP", settle=True)
    raise Failure(f"a window was still open after 8 Back presses\n"
                  f"{session.capture().text()}")


def enter_monitor_with_shortcut(session: MonitorSession, context: str) -> None:
    """Press C=+O and require that the monitor came up, then leave it again.

    The shortcut only, with no fallback: `MonitorSession.enter_monitor` falls
    back to the task menu when C=+O does not work, which is the behaviour this
    check exists to distinguish.
    """
    session.send_key("CTRL_O", settle=True)
    snapshot = wait_until(session, monitor_is_on_screen)
    if not monitor_is_on_screen(snapshot):
        raise Failure(
            f"C=+O did not open the monitor from {context}\n{snapshot.text()}")
    leave_monitor_fully(session)


# A window drawn over the file browser draws this in its border, and the bare
# browser draws it nowhere. It is what tells "the monitor did not open" apart
# from "the context that was supposed to still be open collapsed".
WINDOW_BORDER_MARKER = "+--"


def monitor_is_on_screen(snapshot: Snapshot) -> bool:
    try:
        find_any_status_line(snapshot)
        return True
    except Failure:
        return False


def assert_shortcut_ignored(session: MonitorSession, context: str) -> None:
    """Press C=+O in a non-browser context and require that nothing opened.

    The key is sent once. The screen is then re-read for a bounded period: the
    check returns as soon as a monitor appears, which is the regression, and
    otherwise spends the whole budget proving it did not. The window that was
    open before must still be open afterwards, so a context that collapsed
    cannot pass as a shortcut that was ignored.
    """
    before = session.capture()
    if WINDOW_BORDER_MARKER not in before.text():
        raise Failure(
            f"{context} was not open before C=+O was pressed\n{before.text()}")

    session.send_key("CTRL_O", expect_redraw=False)
    snapshot = wait_until(session, monitor_is_on_screen, timeout=2.0)
    if monitor_is_on_screen(snapshot):
        raise Failure(
            f"C=+O opened the monitor from {context}. The shortcut belongs to "
            f"the file browser only.\n{snapshot.text()}")
    if WINDOW_BORDER_MARKER not in snapshot.text():
        raise Failure(
            f"C=+O closed {context} instead of being ignored\n{snapshot.text()}")


def run_monitor_shortcut_scope_test(session: MonitorSession, mode: str) -> None:
    """C=+O opens the monitor from the file browser, and from nothing else.

    The shortcut is a case in `TreeBrowser::handle_key`, so the file browser
    answers it. The task menu, the settings screens and the help screen pass
    the key to their own handlers, which do not open the monitor. Inside the
    monitor the same key is one of the monitor's own exit keys.

    Each context is left the way it was entered, so the checks after this one
    meet the same monitor they would have without it.
    """
    leave_monitor_fully(session)
    session.backend.ensure_ready()
    back_out_to_the_bare_browser(session)

    # 1. The file browser opens the monitor.
    enter_monitor_with_shortcut(session, "the file browser")
    back_out_to_the_bare_browser(session)

    # 2. The task menu does not.
    task_key = session.backend.machine.task_menu_key
    session.send_key(task_key, settle=True)
    assert_shortcut_ignored(session, f"the task menu ({task_key})")
    back_out_to_the_bare_browser(session)

    # 3. The settings screens do not. F2 is Shift+F1 on a C64 keyboard, which
    #    only the REST transport sends as a combination.
    if mode != MODE_TELNET:
        session.backend.send_combo(["left_shift", "f1"])
        assert_shortcut_ignored(session, "the settings screens (F2)")
        back_out_to_the_bare_browser(session)

    # 4. A selection context menu over the browser does not. The browser is
    #    still the object underneath, so this is the case a shortcut placed
    #    one level too high would get wrong.
    session.send_key("ENTER", settle=True)
    assert_shortcut_ignored(session, "a browser context menu")
    back_out_to_the_bare_browser(session)

    # 5. The browser shortcut still works after visiting all of those, and
    #    inside the monitor C=+O keeps the meaning the monitor gives it, which
    #    is to leave rather than to open a second monitor.
    session.send_key("CTRL_O", settle=True)
    snapshot = wait_until(session, monitor_is_on_screen)
    if not monitor_is_on_screen(snapshot):
        raise Failure(
            "C=+O no longer opens the monitor from the file browser after the "
            f"other contexts were visited\n{snapshot.text()}")
    # 6. A bare X is not an exit. Every other letter on the monitor's keymap
    #    is a command, so an X that closed the monitor would let one mistyped
    #    command letter discard the view.
    session.send_char("X")
    snapshot = wait_until(session, monitor_is_on_screen)
    if not monitor_is_on_screen(snapshot):
        raise Failure(f"X left the monitor; it is not an exit\n{snapshot.text()}")

    session.send_key("CTRL_O", settle=True)
    snapshot = wait_until(session, lambda screen: not monitor_is_on_screen(screen))
    if monitor_is_on_screen(snapshot):
        raise Failure(
            f"C=+O did not leave the monitor from a memory view\n{snapshot.text()}")

    session.enter_monitor()


def run_back_navigation_test(session: MonitorSession) -> None:
    """Back removes one interaction layer, from either of its two keys."""
    ensure_view(session, "HEX ")

    # Help closes on all three of its own keys, and on none of them leaves the
    # monitor as well.
    for close_key in ("ARROW_LEFT", "RUNSTOP", "?"):
        close_help(session, close_key)

    # The mapped help key is the other way in.
    screen = session.send_key("F3")
    assert_help_open(screen, "the mapped help key opening help")
    screen = session.send_key("RUNSTOP")
    assert_help_closed(screen, "RUN/STOP closing help opened with the help key")

    # A command key still dismisses help and then runs, which Back must not
    # have taken away.
    screen = session.send_char("?")
    assert_help_open(screen, "'?' opening help before the command-key check")
    screen = session.send_char("I")
    screen.find_line_containing("MONITOR ASC")
    ensure_view(session, "HEX ")

    # The number popup, and the expression above it: one level per press.
    session.send_char("N")
    wait_for_prompt(session, "MONITOR NUM")
    screen = session.send_char("+")
    screen.find_line_containing("Expr=")
    session.send_key("ARROW_LEFT")
    screen = wait_for_prompt(session, "Calc with")
    screen.find_line_containing("MONITOR NUM")
    session.send_key("ARROW_LEFT")
    wait_for_prompt(session, "MONITOR HEX")

    # The bookmark popup.
    session.send_key("CTRL_B")
    wait_for_prompt(session, "BOOKMARKS")
    session.send_key("ARROW_LEFT")
    wait_for_prompt(session, "MONITOR HEX")

    # Edit mode, and then the monitor itself.
    screen = session.send_char("E")
    if "EDIT" not in monitor_header(screen):
        raise Failure(f"edit mode did not start\n{screen.text()}")
    screen = session.send_key("ARROW_LEFT")
    if "EDIT" in monitor_header(screen):
        raise Failure(f"the left-arrow key did not leave edit mode\n{screen.text()}")
    assert_monitor_visible(screen, "the left-arrow key leaving edit mode")
    session.send_key("ARROW_LEFT")
    session.enter_monitor()


def run_transfer_relocate_test(session: MonitorSession, rest_host: str) -> None:
    """Transfer's optional code range moves absolute operands with the copy.

    A short routine holding one absolute load, one absolute jump inside itself,
    a KERNAL call and a zero-page load. Only the first two point inside the
    copied range, so only those two follow the copy, and the command says it
    moved two.
    """
    source = 0x1180
    dest = 0x11C0
    program = bytes((
        0xAD, 0x88, 0x11,   # LDA $1188   inside the source, follows the copy
        0x4C, 0x80, 0x11,   # JMP $1180   inside the source, follows the copy
        0x20, 0xD2, 0xFF,   # JSR $FFD2   the KERNAL did not move
        0xA5, 0x11,         # LDA $11     zero page cannot name another page
        0xEA,               # NOP
    ))
    relocated = bytes((
        0xAD, 0xC8, 0x11,
        0x4C, 0xC0, 0x11,
        0x20, 0xD2, 0xFF,
        0xA5, 0x11,
        0xEA,
    ))
    last = source + 0x3F
    code_end = source + len(program) - 1

    write_rest_memory_confirmed(rest_host, source, program)
    write_rest_memory_confirmed(rest_host, dest, b"\x00" * len(program))

    session.type_into_prompt(
        "T", TRANSFER_PROMPT_TITLE,
        f"{source:04X}-{last:04X},{dest:04X},{source:04X}-{code_end:04X}",
        retypes=PROMPT_RETYPES)
    screen = session.send_key("ENTER", settle=True)

    # The count is the point of the report: a scan that lost instruction
    # alignment would move a different number of operands.
    def reports_two(snapshot: Snapshot) -> bool:
        return "2 OPERANDS RELOCATED" in snapshot.text()

    screen = wait_until(session, reports_two)
    if not reports_two(screen):
        raise Failure(
            "a relocating Transfer did not report moving two operands\n"
            f"{screen.text()}")
    session.send_key("ENTER", settle=True)
    wait_for_monitor(session, "dismissing the relocation report")

    assert_monitor_write_landed(rest_host, dest, relocated,
                                "a relocating Transfer")


def run_back_is_data_in_text_views_test(session: MonitorSession, rest_host: str) -> None:
    """Where the left-arrow key is edit data it stays data; RUN/STOP still backs out."""
    for view_key, view, address, expected in (("I", "ASC ", 0xC010, 0x60),
                                              ("V", "SCR ", 0xC011, 0x1F)):
        write_rest_memory_confirmed(rest_host, address, b"\x00")
        ensure_view(session, view)
        session.goto(f"{address:04X}")
        ensure_view(session, view)
        screen = session.send_char("E")
        if "EDIT" not in monitor_header(screen):
            raise Failure(f"{view.strip()} edit mode did not start\n{screen.text()}")
        screen = session.send_key("ARROW_LEFT")
        if "EDIT" not in monitor_header(screen):
            raise Failure(
                f"{view.strip()} edit: the left-arrow key left edit mode instead of "
                f"typing its character\n{screen.text()}")
        assert_monitor_write_landed(
            rest_host, address, bytes((expected,)),
            f"{view.strip()} edit: the left-arrow key as data")
        session.send_key("RUNSTOP")

    # The ASCII and Screen rows of the number popup take it as data too.
    ensure_view(session, "HEX ")
    for presses, preview in ((3, "Hex      $60"), (4, "Hex      $1F")):
        session.send_char("N")
        wait_for_prompt(session, "MONITOR NUM")
        for _ in range(presses):
            session.send_key("DOWN")
        session.send_key("ARROW_LEFT")
        wait_for_prompt(session, preview)
        session.send_key("RUNSTOP")
        wait_for_prompt(session, "MONITOR HEX")


# One rejected and one accepted character per structured prompt. The rejected
# one cannot appear anywhere in that command; the accepted one begins an
# ordinary use of it.
PROMPT_INPUT_CASES = (
    ("J", "Jump AAAA", "Z", "8"),
    ("F", "Fill AAAA-BBBB,DD", "G", "0"),
    ("T", TRANSFER_PROMPT_TITLE, "/", "1"),
    ("C", "Compare AAAA-BBBB,CCCC", "*", "2"),
    ("H", "Hunt AAAA-BBBB", "Z", "4"),
    ("S", "Save AAAA-BBBB", "X", "3"),
    ("G", "Go AAAA", "Q", "C"),
)


def assert_prompt_rejects(session: MonitorSession, title: str, rejected: str) -> str:
    """Type an impossible character and prove the field did not move."""
    before = prompt_field(session.capture(), title)
    if not before:
        raise Failure(f"the {title} prompt opened with an empty field")
    # Nothing is drawn for a refused key, so no redraw is waited for: over
    # Telnet that wait is what would otherwise be spent proving a negative.
    screen = session.send_char(rejected, expect_redraw=False)
    after = prompt_field(screen, title)
    if after != before:
        raise Failure(
            f"{title}: {rejected!r} changed the field from {before!r} to {after!r}; "
            "an impossible character must not reach it"
        )
    return before


def run_command_input_rejection_test(session: MonitorSession) -> None:
    """An impossible character never reaches the field, and the next one does."""
    ensure_view(session, "HEX ")
    for key, title, rejected, accepted in PROMPT_INPUT_CASES:
        session.send_char(key)
        wait_for_prompt(session, title)
        before = assert_prompt_rejects(session, title, rejected)

        def field_changed(snapshot: Snapshot, title: str = title,
                          before: str = before) -> bool:
            try:
                return prompt_field(snapshot, title) != before
            except Failure:
                return False  # the prompt is mid-redraw

        session.send_char(accepted)
        screen = wait_until(session, field_changed)
        typed = prompt_field(screen, title)
        if typed == before or accepted not in typed:
            raise Failure(
                f"{title}: {accepted!r} was not accepted; the field reads {typed!r}"
            )
        # Back leaves the prompt, which is one interaction layer, not the monitor.
        session.send_key("ARROW_LEFT")
        wait_for_monitor(session, f"leaving the {title} prompt")



# The result picker's own header, which nothing else on the screen carries.
HUNT_RESULTS_HEADER = "Hunt results"


def run_hunt_quoted_text_test(session: MonitorSession, rest_host: str) -> None:
    """A quoted needle keeps the case it was typed in, and finds only that."""
    address = 0xC300
    needle = b"MonKey"

    write_rest_memory_confirmed(rest_host, address, needle)
    ensure_view(session, "HEX ")
    session.send_char("H")
    wait_for_prompt(session, "Hunt AAAA-BBBB")
    # The prompt opens on its default range with the cursor at the end, so the
    # whole command is typed after clearing it.
    clear_prompt_field(session)
    session.send_text(f'{address:04X}-{address + len(needle) - 1:04X},"MonKey"\r',
                      "hunt quoted text")
    # Waited for by the picker's own header rather than by the address, which
    # is still sitting in the prompt field while the command is being typed.
    screen = wait_for_screen_contains(session, HUNT_RESULTS_HEADER)
    assert_line_contains_all(screen, (f"{address:04X}", "4D 6F 6E 4B 65 79"))
    # An upper-case needle must not match the same bytes, which is what proves
    # the quoted text was not normalised on the way in.
    session.send_key("ARROW_LEFT")
    wait_for_monitor(session, "closing the hunt result picker")
    session.send_char("H")
    wait_for_prompt(session, "Hunt AAAA-BBBB")
    clear_prompt_field(session)
    session.send_text(
        f'{address:04X}-{address + len(needle) - 1:04X},"MONKEY"\r',
        "hunt upper-case needle")
    wait_for_screen_contains(session, "No matches")
    session.send_key("ENTER")
    wait_for_monitor(session, "dismissing the no-matches popup")


def assert_u2_footer_consistent(snapshot: Snapshot) -> int:
    """Return the U2 VIC bank after checking that its base address agrees."""
    line_index = find_u2_footer_line(snapshot)
    match = U2_STATUS_LINE_RE.search(snapshot.line(line_index))
    bank = int(match.group(1))
    address = int(match.group(2), 16)
    if address != U2_VIC_BANK_BASES[bank]:
        raise Failure(
            f"U2 footer VIC{bank} shows ${address:04X}, expected "
            f"${U2_VIC_BANK_BASES[bank]:04X}: {snapshot.line(line_index)!r}"
        )
    return bank


def set_u2_vic_bank(session: MonitorSession, target: int) -> Snapshot:
    for _ in range(4):
        screen = session.capture()
        if assert_u2_footer_consistent(screen) == target:
            return screen
        session.send_char("O")
    raise Failure(f"U2 VIC bank did not reach VIC{target}")


def ui_freezes_machine(device_host: str, mode: str) -> bool:
    """Whether this device stops the C64 while its monitor is on screen.

    Asked of the device rather than inferred from its name. A device that
    offers the `Interface Type` setting can draw its UI as an overlay on a
    running machine, and does so unless --mode freeze says otherwise. A
    cartridge has no such setting: its UI is the freezer, whatever the flag
    says, so every check that depends on a running machine or on a
    second view of memory has to treat it as frozen.
    """
    if mode == MODE_FREEZE:
        return True
    try:
        rest_api(device_host).configs.item("User Interface Settings",
                                           "Interface Type")
    except Failure:
        return True  # no such setting, so the UI is the freezer
    return False


def banked_ram_reason(is_u2: bool, frozen: bool) -> Optional[str]:
    """Return the U2+L frozen-memory limitation for $1000-$3FFF, if applicable.

    While the cartridge's freezer holds the machine, its own REST endpoint
    reads freezer RAM banked into that range rather than the C64's; $0400 and
    $C000-and-up are unaffected.
    """
    if is_u2 and frozen:
        return ("U2+L: REST cannot reach $1000-$3FFF while the monitor holds "
                "the machine (freezer RAM banked into that range; "
                "$0400/$C000-and-up unaffected)")
    return None


def run_tests(session: MonitorSession, rest_host: str, mode: str, is_u2: bool,
              control: str, video_host: str, files_host: str, live_host: str,
              frozen: bool) -> None:
    snapshots = load_snapshots()

    with check("initial CPU7/KERNAL monitor status"):
        if is_u2:
            assert_u2_footer_consistent(session.capture())
        else:
            ensure_status(session, snapshots["status_cpu31"]["contains"]["22"])

    with check("KERNAL $E000 hex view and REST match"):
        ensure_hex_width(session, 8)
        screen = session.goto("E000")
        for row, expected in snapshots["kernal_hex_e000"]["contains"].items():
            assert_contains(screen, int(row), expected)
        assert_rest_matches_row(screen, 4, 0xE000, rest_host)

    with check("paging away and back keeps memory view stable"):
        initial_snapshot = screen.text()
        session.send_key("PGDN")
        back = session.send_key("PGUP")
        assert_equal("Memory stability", initial_snapshot, back.text(), back.last_command)

    with check("KERNAL disassembly formatting"):
        screen = session.send_char("A")
        for row, expected in snapshots["kernal_disasm_e000"]["contains"].items():
            assert_contains(screen, int(row), expected)
        # The tag identity is a property of the machine: only a backend that
        # selects the bank itself can say KRN. A cartridge reads whatever the
        # CPU sees and says CPU. The fixed column is asserted on both.
        assert_source_column_is_fixed(screen, "CPU" if is_u2 else "KRN")

        # D is reserved for a future Debug mode and opens nothing. The manual
        # deliberately does not mention the key, so this check and its host
        # counterpart are what keep the reservation. An older copy of this
        # suite pressed D for the Assembly view, so the binding has drifted
        # once already.
        #
        # Read the title row, which names the view and the address, rather than
        # the whole screen: the screen also carries a status row and an edit
        # cursor, and neither is what this asserts.
        before = monitor_header(session.capture())
        session.send_char("D")
        after = monitor_header(session.capture())
        if after != before:
            raise Failure(
                f"D is reserved for Debug mode and must change nothing: the "
                f"monitor header read {before!r} and now reads {after!r}")

        screen = session.goto("E013")
        screen = session.send_char("A")
        for row, expected in snapshots["kernal_disasm_e013"]["contains"].items():
            assert_contains(screen, int(row), expected)

    with check("KERNAL $E010 REST match"):
        screen = ensure_view(session, "HEX ")
        screen = session.goto("E010")
        assert_rest_matches_row(screen, 4, 0xE010, rest_host)

    with check("CPU6 RAM under BASIC write/read"):
        if is_u2:
            check_skip("U2 has no monitor-selected CPU banking (supports_cpu_banking()==false)")
        else:
            screen = ensure_view(session, "HEX ")
            session.goto("A000")
            screen = cycle_cpu_bank_from_cpu7(session, snapshots["status_cpu30"]["contains"]["22"], 7)
            session.fill("A000-A000,AA")
            screen = session.goto("A000")
            assert_contains(screen, 4, snapshots["ram_a000"]["contains"]["4"])

    with check("CPU5 RAM under KERNAL status"):
        if is_u2:
            check_skip("U2 has no monitor-selected CPU banking (supports_cpu_banking()==false)")
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
        if is_u2:
            assert_u2_footer_consistent(screen)
        else:
            assert_status_contains(screen, snapshots["status_cpu29"]["contains"]["22"])

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
        # This check's addresses ($3200-$3280) are in the banked range; see
        # u2_freeze_banked_ram_reason.
        skip_reason = banked_ram_reason(is_u2, frozen)
        if skip_reason:
            check_skip(skip_reason)
        else:
            run_character_mapping_test(session, rest_host)

    with check("HEX edit writes both nibbles"):
        session.goto("C000")
        write_rest_memory_confirmed(rest_host, 0xC000, b"\x00")
        screen = ensure_view(session, "HEX ")
        screen = session.send_char("e")
        assert_highlight(screen, [(6, 4), (7, 4)], "e")
        screen = enter_hex_nibble(session, "A", snapshots["hex_first_nibble"]["contains"]["4"])
        screen = enter_hex_nibble(session, "B", snapshots["hex_second_nibble"]["contains"]["4"])
        session.send_key("ESC")

    with check("a main-RAM hex edit reaches C64 memory, not just the editor"):
        run_main_ram_edit_persists_test(session, rest_host, frozen)

    with check("every RAM-window boundary edge holds a hex edit"):
        run_main_ram_edge_sweep_test(session, rest_host, frozen)

    with check("a hex edit reaches RAM under BASIC, KERNAL and top of memory"):
        if is_u2:
            check_skip("U2 has no monitor-selected CPU banking (supports_cpu_banking()==false)")
        else:
            run_cpu_banked_ram_edit_test(session, rest_host, frozen)

    with check("an Assembly-view edit reaches C64 memory, not just the editor"):
        run_asm_edit_memory_persists_test(session, rest_host, frozen)

    with check("a hex edit reaches memory at every address, every time"):
        run_hex_edit_reliability_test(session, rest_host, stress_rounds(6))

    with check("one-, two- and three-byte instructions each commit whole"):
        run_asm_commit_reliability_test(session, rest_host, frozen,
                                        stress_rounds(4))

    with check("every character of a command argument reaches the monitor"):
        run_key_input_stress_test(session, stress_rounds(3))

    with check("Help is KEY-first, and its two grids stay on their columns"):
        run_help_layout_test(session)

    with check("C=+O opens the monitor from the file browser and nowhere else"):
        run_monitor_shortcut_scope_test(session, mode)

    with check("Transfer moves absolute operands when given a code range"):
        run_transfer_relocate_test(session, rest_host)

    with check("Back leaves one interaction layer at a time"):
        run_back_navigation_test(session)

    with check("the left-arrow key stays data in ASCII and Screen editing"):
        run_back_is_data_in_text_views_test(session, rest_host)

    with check("command prompts refuse an impossible character outright"):
        run_command_input_rejection_test(session)

    with check("Hunt keeps the case of a quoted needle"):
        run_hunt_quoted_text_test(session, rest_host)

    with check("CPU bank cycling reaches CHAR and RAM mappings"):
        if is_u2:
            check_skip("U2 has no monitor-selected CPU banking (supports_cpu_banking()==false)")
        else:
            session.goto("A000")
            screen = ensure_status(session, snapshots["status_cpu27"]["contains"]["22"])
            assert_status_contains(screen, snapshots["status_cpu27"]["contains"]["22"])
            session.send_char("o")
            session.send_char("o")
            screen = session.send_char("o")
            assert_status_contains(screen, snapshots["status_cpu30"]["contains"]["22"])

    with check("U2 VIC-bank selection persists after leaving Freeze"):
        if not is_u2:
            check_skip("U2-only: exercises the VIC-only footer form (supports_cpu_banking()==false)")
        else:
            default_bank = 0
            requested = 1
            set_u2_vic_bank(session, default_bank)
            screen = set_u2_vic_bank(session, requested)
            if frozen:
                session.send_key("ESC")
                close_rest_menu(control, None)
                dd00 = read_rest_memory(live_host, 0xDD00, 1)[0]
                persisted = 3 - (dd00 & 0x03)
                if persisted != requested:
                    raise Failure(
                        f"U2 VIC{requested} selection did not survive Freeze release: "
                        f"$DD00=${dd00:02X} is VIC{persisted}"
                    )
                session.enter_monitor()
            set_u2_vic_bank(session, default_bank)
            if frozen:
                session.send_key("ESC")
                close_rest_menu(control, None)
                dd00 = read_rest_memory(live_host, 0xDD00, 1)[0]
                restored = 3 - (dd00 & 0x03)
                if restored != default_bank:
                    raise Failure(f"U2 VIC bank did not restore to VIC{default_bank}")
                session.enter_monitor()

    with check("COMPARE reports every differing address, both ends included"):
        screen = ensure_view(session, "HEX ")
        write_rest_memory_confirmed(rest_host, 0xC100, bytes((0x10,) * 4))
        write_rest_memory_confirmed(rest_host, 0xC200, bytes((0x10, 0x91, 0x10, 0x93)))
        screen = session.compare("C100-C103,C200")
        assert_contains(screen, 4, "C101")
        # $C103 is the end of the range and differs, so a Compare that stopped
        # short of it would leave this line off the picker.
        screen.find_line_containing("C103")
        session.send_key("ENTER")

    with check("TRANSFER copies the whole range, both ends included"):
        ensure_view(session, "HEX ")
        write_rest_memory_confirmed(rest_host, 0xC100,
                                    bytes((0x11, 0x22, 0x33, 0x44)))
        write_rest_memory_confirmed(rest_host, 0xC300, bytes((0x00,) * 5))
        session.transfer("C100-C103,C300")
        copied = read_rest_memory(rest_host, 0xC300, 5)
        if copied[:4] != bytes((0x11, 0x22, 0x33, 0x44)):
            raise Failure(f"transfer copied {copied[:4].hex()}, expected 11223344")
        if copied[4] != 0x00:
            raise Failure(f"transfer wrote past the end of the range: "
                          f"${0xC304:04X} is ${copied[4]:02X}")

    with check("leaving and re-entering the monitor keeps its place"):
        run_reentry_test(session)

    with check("Z freezes and releases the machine, or says it cannot"):
        run_freeze_toggle_test(session, live_host)

    with check("ASM entry reaches screen and RAM, then G executes it"):
        run_asm_entry_round_trip_test(session, rest_host, video_host, control,
                                      mode != MODE_TELNET)

    with check("G executes finite loop and returns to monitor"):
        go_address = 0xC000 if is_u2 and frozen else 0x1000
        write_rest_memory(rest_host, go_address,
                          bytes.fromhex("A9008D0004A9018D00044C") +
                          go_address.to_bytes(2, "little"))
        write_rest_memory(rest_host, 0x0400, bytes([0x20]))
        session.goto(f"{go_address:04X}")
        session.goto_run(f"{go_address:04X}")
        wait_for_rest_byte(rest_host, 0x0400, 0x01)
        session.enter_monitor()

    with check("G repeated execution updates RAM sentinel"):
        run_go_repeat_test(session, rest_host, frozen, control)

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

    # The next four checks all seed their test data at $3xxx addresses, in
    # the banked range described at u2_freeze_banked_ram_reason.
    freeze_banked_skip = banked_ram_reason(is_u2, frozen)

    with check("memory bookmark jump restores width 16"):
        if freeze_banked_skip:
            check_skip(freeze_banked_skip)
        else:
            run_memory_bookmark_width_test(session, rest_host)

    with check("binary width cycling and bookmark jump restores width 4"):
        # No banked-RAM skip here: this one seeds its fixture at $C400, which
        # the freezer leaves alone.
        run_binary_bookmark_width_test(session, rest_host)

    with check("follow and return navigation"):
        if freeze_banked_skip:
            check_skip(freeze_banked_skip)
        else:
            run_follow_return_test(session, rest_host)

    with check("assembly baselines survive scrolling up and back"):
        run_asm_backwards_navigation_test(session, rest_host)

    with check("asm edit mnemonic validation and Return advance"):
        if freeze_banked_skip:
            check_skip(freeze_banked_skip)
        else:
            run_asm_edit_validation_test(session, rest_host)

    with check("number popup arithmetic"):
        if freeze_banked_skip:
            check_skip(freeze_banked_skip)
        else:
            run_number_arithmetic_test(session, rest_host)

    save_load_token = f"{int(time.time()) % 100000:05d}"
    with check("save/load round-trip to top-level /Temp file"):
        run_save_load_topfile_test(session, rest_host, files_host, save_load_token)

    with check("save/load round-trip to file in new /Temp D64"):
        run_save_load_d64_test(session, rest_host, files_host, save_load_token)

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
    parser = argparse.ArgumentParser(description="Validate the machine monitor over REST, Freeze, or Telnet")
    parser.add_argument("-H", "--host", default=os.environ.get("U64_HOST", "u64"),
                        help="Target: a host, or cartridge@computer for a cartridge "
                             "under test (see tests/lib/targets.py).")
    parser.add_argument("-P", "--telnet-port", "--port", dest="port", type=int,
                        default=int(os.environ.get("U64_TELNET_PORT", "23")))
    parser.add_argument("-r", "--rest-host", default=os.environ.get("U64_REST_HOST"),
                        help="REST address of the device under test, when it differs "
                             "from its name in the target.")
    parser.add_argument("-p", "--password", default=os.environ.get("U64_PASS"))
    parser.add_argument("-t", "--timeout", type=float,
                        default=float(os.environ.get("U64_TIMEOUT", "5.0")))
    add_mode_argument(parser, default=os.environ.get("U64_MODE", "overlay"))
    args = parser.parse_args()

    try:
        target = targets.parse(args.host)
    except targets.TargetError as exc:
        parser.error(str(exc))

    # The monitor, its menu screen and its filesystem are the device under
    # test's. The C64 keyboard, the A/V stream and a never-frozen view of
    # memory belong to the computer, which is the same machine unless the
    # device is a cartridge.
    device_host = args.rest_host or target.device
    live_host = target.computer
    control = target.token
    info = rest_api(device_host).info()
    device = machine_lib.identify(
        device_host, lambda: (info.product, info.firmware_version))
    if device.skip_without_fix(machine_lib.MONITOR_EXIT_AND_BACK_KEYS,
                               "this machine runs the monitor revision this suite drives"):
        suite_ok("monitor_test")
        return 0
    is_u2 = info.product.startswith("Ultimate II")
    if is_u2 and not target.split:
        parser.error("an Ultimate II is a cartridge: name the computer it is "
                     "plugged into, as u2@<computer>")

    # Memory verification. While the device's UI holds the machine, the device
    # is the only one that can read the memory the monitor is showing: the
    # computer's own DMA reads open bus. Otherwise the computer's live view is
    # the independent oracle.
    frozen = ui_freezes_machine(device_host, args.mode)
    memory_host = device_host if frozen else live_host

    reset_rest_machine(control, args.password)

    session = None
    try:
        backend = make_backend(
            args.mode, target.token, args.password, args.timeout,
            telnet_host=device_host, telnet_port=args.port,
        )
        session = MonitorSession(backend)
        run_tests(session, memory_host, args.mode, is_u2, control,
                  live_host, device_host, live_host, frozen)
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

    report_first_attempt_losses()
    suite_ok("monitor_test")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
