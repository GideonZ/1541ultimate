#!/usr/bin/env python3
import argparse
import difflib
import json
import os
import re
import select
import socket
import subprocess
import sys
import time
import urllib.error
import urllib.request
from contextlib import contextmanager
from dataclasses import dataclass
from pathlib import Path
from typing import Dict, List, Optional, Tuple

WIDTH = 60
HEIGHT = 24
SNAPSHOT_FILE = Path(__file__).with_name("snapshots").joinpath("expected_snapshots.json")
REPO_ROOT = Path(__file__).resolve().parents[3]
REDEPLOY_SCRIPT = REPO_ROOT / "tooling" / "build_and_deploy_u64.sh"

STATUS_LINE_RE = re.compile(r"(?:CPU[0-7]|C[0-7]O[0-7]) \$A:(?:RAM|BAS) \$D:(?:RAM|CHR|I/O) \$E:(?:RAM|KRN) VIC[0-3] \$[0-9A-F]{4}")
# U2 cartridge backend has no monitor-side CPU banking or VIC bank selection,
# so the status line collapses to a fixed "no banking" label.
U2_STATUS_LINE_RE = re.compile(r"CPU VIEW\s+CPU BANK N/A\s+VIC N/A")
MEMORY_ROW_RE = re.compile(r"^[0-9A-F]{4} ")
MEMORY_ROW_16_RE = re.compile(r"^[0-9A-F]{4} [0-9A-F]{16} [0-9A-F]{16}$")
CHECK_COUNT = 0
ASM_CANONICAL_BYTES = bytes((
    0x85, 0x56, 0x20, 0x0F, 0xBC, 0xA5, 0x61, 0xC9,
    0x88, 0x90, 0x03, 0x20, 0xD4, 0xBA, 0x20, 0xCC,
    0xBC, 0xA5, 0x07, 0x18, 0x69, 0x81, 0xF0, 0xF3,
    0x38, 0xE9, 0x01, 0x48, 0xA2, 0x05, 0xB5, 0x69,
    0xB4, 0x61,
))
ASM_KERNAL_ROWS: Tuple[Tuple[str, str], ...] = (
    ("E000 85 56", "STA $56"),
    ("E002 20 0F BC", "JSR $BC0F"),
    ("E005 A5 61", "LDA $61"),
    ("E007 C9 88", "CMP #$88"),
    ("E009 90 03", "BCC $E00E"),
    ("E00B 20 D4 BA", "JSR $BAD4"),
    ("E00E 20 CC BC", "JSR $BCCC"),
    ("E011 A5 07", "LDA $07"),
    ("E013 18", "CLC"),
)

ALT_CHARSET_MAP = {
    "l": "+",
    "k": "+",
    "m": "+",
    "j": "+",
    "q": "-",
    "x": "|",
    "t": "+",
    "u": "+",
    "v": "+",
    "w": "+",
    "n": "+",
}

KEYS = {
    "UP": b"\x1b[A",
    "DOWN": b"\x1b[B",
    "RIGHT": b"\x1b[C",
    "LEFT": b"\x1b[D",
    "PGUP": b"\x1b[5~",
    "PGDN": b"\x1b[6~",
    "F5": b"\x1b[15~",
    "F3": b"\x1b[13~",
    "RUNSTOP": b"\x11",
    "CTRL_B": b"\x02",
    "CTRL_E": b"\x05",
    "CTRL_O": b"\x0f",
    "CBM_B": b"\x1bb",
    "CBM_1": b"\x1b1",
    "ESC": b"\x1bx",
    "ENTER": b"\r",
    "DEL": b"\x7f",
    "BACKSPACE": b"\x08",
}

VIEW_KEYS = {
    "HEX ": "M",
    "ASC ": "I",
    "ASM ": "A",
    "SCR ": "V",
    "BIN ": "B",
}


class Failure(RuntimeError):
    pass


class SkipCheck(Exception):
    pass


# Test configuration. Set by main() before run_tests() so the check() helper and
# individual tests can branch on the target hardware (u64 vs u2) and on whether
# the run should accumulate failures rather than aborting on the first one.
class TestConfig:
    target: str = "u64"
    keep_going: bool = False
    session: Optional["MonitorSession"] = None
    failures: List[Tuple[int, str, str]] = []
    skipped: List[Tuple[int, str, str]] = []


def is_u2() -> bool:
    return TestConfig.target == "u2"


def is_u64() -> bool:
    return TestConfig.target == "u64"


class _SkipCheckCtx:
    """No-op context that suppresses every exception raised in the body.

    Used when a check is gated by ``u2=False`` and the active target is u2.
    Python provides no zero-cost mechanism to suppress a `with` block from
    its context manager, so the body still runs; suppressing all exceptions
    keeps state changes intentional while preventing the run from aborting.
    The skip is announced before the body executes.
    """
    def __enter__(self):
        return self
    def __exit__(self, exc_type, exc, tb):
        return True


class _ActiveCheckCtx:
    def __init__(self, label: str, check_index: int):
        self.label = label
        self.check_index = check_index
    def __enter__(self):
        return self
    def __exit__(self, exc_type, exc, tb):
        if exc_type is None:
            print("OK", flush=True)
            return False
        if exc_type is SkipCheck:
            print(f"SKIP  ({exc})", flush=True)
            TestConfig.skipped.append((self.check_index, self.label, str(exc)))
            return True
        print("FAIL", flush=True)
        diag = _diagnostic_block(self.label, exc)
        print(diag, flush=True)
        TestConfig.failures.append((self.check_index, self.label, diag))
        return bool(TestConfig.keep_going)


def check(label: str, *, u2: bool = True, u2_reason: str = ""):
    """Return a context manager for a single check.

    Setting ``u2=False`` marks the check as U64-only; in U2 mode the check is
    skipped with a clear reason printed to the console. When
    ``TestConfig.keep_going`` is true, exceptions raised inside the check are
    captured with full screen context and logged verbosely, but the run
    continues so a remote operator can collect all failure evidence in a
    single console capture.
    """
    global CHECK_COUNT
    CHECK_COUNT += 1
    print(f"[{CHECK_COUNT:02d}] {label} ... ", end="", flush=True)
    if is_u2() and not u2:
        reason = u2_reason or "U64-only feature (CPU banking / VIC bank / ROM patch / REST API)"
        print(f"SKIP  ({reason})", flush=True)
        TestConfig.skipped.append((CHECK_COUNT, label, reason))
        return _SkipCheckCtx()
    return _ActiveCheckCtx(label, CHECK_COUNT)


def _diagnostic_block(label: str, exc: BaseException) -> str:
    """Render a verbose diagnostic block for a failed check.

    The block always includes the failing check label, the exception type, the
    full exception message, the last command sent to the device, and the final
    terminal snapshot (if any). The intent is that a remote operator can copy
    the entire console output and diagnose each failure in isolation without
    needing additional context.
    """
    lines: List[str] = []
    lines.append("    ---- FAILURE CONTEXT BEGIN ----")
    lines.append(f"    check     : {label}")
    lines.append(f"    target    : {TestConfig.target}")
    lines.append(f"    exception : {type(exc).__name__}: {format_exception(exc)}")
    session = TestConfig.session
    if session is not None:
        last_cmd = getattr(session, "last_command", "<unknown>")
        lines.append(f"    last_cmd  : {last_cmd!r}")
        try:
            snap = session.capture()
            lines.append("    screen    :")
            for row in snap.text().splitlines():
                lines.append(f"      | {row}")
        except Exception as cap_exc:  # noqa: BLE001
            lines.append(f"    screen    : <capture failed: {cap_exc!r}>")
    lines.append("    ---- FAILURE CONTEXT END ----")
    return "\n".join(lines)


def format_exception(exc: BaseException) -> str:
    if isinstance(exc, urllib.error.URLError) and getattr(exc, "reason", None) is not None:
        return f"{exc} ({exc.reason})"
    return str(exc)


def device_unavailable(exc: BaseException) -> bool:
    text = format_exception(exc).lower()
    markers = (
        "no route to host",
        "network is unreachable",
        "connection refused",
        "timed out",
        "temporary failure in name resolution",
    )
    return any(marker in text for marker in markers)


def wait_for_port(host: str, port: int, timeout: float) -> None:
    deadline = time.time() + timeout
    last_error: Optional[BaseException] = None

    while time.time() < deadline:
        try:
            with socket.create_connection((host, port), timeout=2.0):
                return
        except (OSError, TimeoutError) as exc:
            last_error = exc
            time.sleep(1.0)

    if last_error is not None:
        raise last_error
    raise TimeoutError(f"Timed out waiting for {host}:{port} to become reachable")


def redeploy_u64(host: str, port: int, password: Optional[str], timeout: float) -> None:
    if host != "u64":
        raise Failure(f"Auto-redeploy is only supported for host 'u64', not {host!r}")
    if not REDEPLOY_SCRIPT.is_file():
        raise Failure(f"Missing redeploy helper: {REDEPLOY_SCRIPT}")

    print("Device unavailable; redeploying U64 and retrying once", file=sys.stderr)
    subprocess.run([str(REDEPLOY_SCRIPT)], cwd=REPO_ROOT, check=True)
    wait_for_port(host, port, timeout=60.0)
    wait_for_monitor_ready(host, port, password, timeout)


@dataclass
class Snapshot:
    lines: List[str]
    reverse_cells: List[Tuple[int, int]]
    last_command: str

    def line(self, index: int) -> str:
        return self.lines[index]

    def text(self) -> str:
        return "\n".join(self.lines)

    def find_line_containing(self, expected: str) -> int:
        for index, line in enumerate(self.lines):
            if expected in line:
                return index
        raise Failure(
            f"Snapshot mismatch after {self.last_command}: expected any line to contain\n"
            f"  {expected!r}\n"
            f"actual:\n{self.text()}"
        )

    def find_status_line(self) -> int:
        for index, line in enumerate(self.lines):
            if STATUS_LINE_RE.search(line) or U2_STATUS_LINE_RE.search(line):
                return index
        raise Failure(
            f"Snapshot mismatch after {self.last_command}: no CPU/VIC status line found\n{self.text()}"
        )


class VT100Screen:
    def __init__(self, width: int = WIDTH, height: int = HEIGHT) -> None:
        self.width = width
        self.height = height
        self.reset()

    def reset(self) -> None:
        self.lines = [[" "] * self.width for _ in range(self.height)]
        self.reverse = [[False] * self.width for _ in range(self.height)]
        self.x = 0
        self.y = 0
        self.reverse_mode = False
        self.alt_charset = False
        self._esc = False
        self._csi: Optional[str] = None
        self._charset: Optional[str] = None
        self._password_seen = False
        self._text_tail = ""

    def feed(self, data: bytes) -> None:
        i = 0
        while i < len(data):
            byte = data[i]
            if byte == 0xFF:
                i = self._skip_telnet_iac(data, i)
                continue
            self._feed_byte(byte)
            i += 1

    def snapshot(self, last_command: str) -> Snapshot:
        reverse_cells = []
        for y in range(self.height):
            for x in range(self.width):
                if self.reverse[y][x]:
                    reverse_cells.append((x, y))
        return Snapshot(["".join(row) for row in self.lines], reverse_cells, last_command)

    def signature(self) -> Tuple[Tuple[str, ...], Tuple[Tuple[bool, ...], ...]]:
        return (tuple("".join(row) for row in self.lines),
                tuple(tuple(row) for row in self.reverse))

    def saw_password_prompt(self) -> bool:
        return self._password_seen

    def _skip_telnet_iac(self, data: bytes, index: int) -> int:
        if index + 1 >= len(data):
            return index + 1
        command = data[index + 1]
        if command in (0xFB, 0xFC, 0xFD, 0xFE):
            return min(index + 3, len(data))
        if command == 0xFA:
            end = data.find(b"\xff\xf0", index + 2)
            return len(data) if end == -1 else end + 2
        return min(index + 2, len(data))

    def _feed_byte(self, byte: int) -> None:
        ch = chr(byte)
        self._text_tail = (self._text_tail + ch)[-32:]
        if "Password:" in self._text_tail:
            self._password_seen = True

        if self._csi is not None:
            if 0x40 <= byte <= 0x7E:
                self._handle_csi(self._csi, ch)
                self._csi = None
            else:
                self._csi += ch
            return

        if self._charset is not None:
            if ch == "0":
                self.alt_charset = True
            elif ch == "B":
                self.alt_charset = False
            self._charset = None
            return

        if self._esc:
            self._esc = False
            if ch == "[":
                self._csi = ""
            elif ch == "(":
                self._charset = ""
            elif ch == "c":
                self.reset()
            return

        if byte == 0x1B:
            self._esc = True
            return
        if ch == "\r":
            self.x = 0
            return
        if ch == "\n":
            self.x = 0
            self.y = min(self.height - 1, self.y + 1)
            return
        if ch == "\b":
            self.x = max(0, self.x - 1)
            return

        if self.alt_charset:
            ch = ALT_CHARSET_MAP.get(ch, ch)
        self._put(ch)

    def _handle_csi(self, params: str, final: str) -> None:
        if final == "H":
            parts = [part for part in params.split(";") if part]
            row = int(parts[0]) if parts else 1
            col = int(parts[1]) if len(parts) > 1 else 1
            self.y = max(0, min(self.height - 1, row - 1))
            self.x = max(0, min(self.width - 1, col - 1))
            return
        if final == "m":
            values = [int(part) for part in params.split(";") if part]
            if not values:
                values = [0]
            for value in values:
                if value in (0, 27):
                    self.reverse_mode = False
                elif value == 7:
                    self.reverse_mode = True
            return
        if final == "J":
            if params in ("", "2"):
                self.lines = [[" "] * self.width for _ in range(self.height)]
                self.reverse = [[False] * self.width for _ in range(self.height)]
                self.x = 0
                self.y = 0
            return
        if final == "r":
            return

    def _put(self, ch: str) -> None:
        if not (0 <= self.x < self.width and 0 <= self.y < self.height):
            return
        self.lines[self.y][self.x] = ch
        self.reverse[self.y][self.x] = self.reverse_mode
        self.x += 1
        if self.x >= self.width:
            self.x = self.width - 1


class MonitorSession:
    def __init__(self, host: str, port: int, password: Optional[str], timeout: float) -> None:
        self.host = host
        self.port = port
        self.timeout = timeout
        self.password = password
        self.screen = VT100Screen()
        self.last_command = "<connect>"
        self._open_connection()

    def _open_connection(self) -> None:
        self.sock = self._connect_with_retry(self.host, self.port, self.timeout)
        self.sock.setblocking(False)
        self.screen = VT100Screen()
        self.last_command = "<connect>"
        self._drain_until_idle(timeout=self.timeout)
        if self.screen.saw_password_prompt():
            if self.password is None:
                raise Failure("Telnet password prompt received but no password was provided")
            self.send_text(self.password + "\r", "password")
            self._drain_until_idle(timeout=self.timeout)
        self.enter_monitor()

    def reconnect(self) -> None:
        """Drop the telnet connection and open a fresh one back in the monitor.

        After an external C64 reset the monitor's telnet state is uncertain, and
        toggling CTRL_O on the stale connection can land on a blank screen. A
        brand-new connection re-enters the monitor deterministically, which is
        how the release matrix gate recovers between ROM reset-retries."""
        try:
            self.sock.close()
        except OSError:
            pass
        self._open_connection()

    def close(self) -> None:
        try:
            self.sock.close()
        except OSError:
            pass

    @staticmethod
    def _connect_with_retry(host: str, port: int, timeout: float) -> socket.socket:
        deadline = time.time() + max(timeout, 15.0)
        last_error: Optional[BaseException] = None

        while time.time() < deadline:
            try:
                return socket.create_connection((host, port), timeout=timeout)
            except (OSError, TimeoutError) as exc:
                last_error = exc
                time.sleep(0.5)

        if last_error is not None:
            raise last_error
        raise TimeoutError(f"Timed out connecting to {host}:{port}")

    def capture(self) -> Snapshot:
        try:
            self._drain_until_idle(timeout=self.timeout)
        except Failure:
            pass
        return self.screen.snapshot(self.last_command)

    def send_key(self, key: str) -> Snapshot:
        payload = KEYS[key]
        self.last_command = key
        self.sock.sendall(payload)
        return self.capture()

    def send_key_count(self, key: str) -> Tuple[Snapshot, int]:
        """Send a key and return (snapshot, bytes_received_during_redraw).

        Used to measure per-keystroke output volume so a flood-on-scroll
        regression (full-screen redraw per keystroke on telnet) is observable."""
        payload = KEYS[key]
        self.last_command = key
        self.sock.sendall(payload)
        self._last_drain_bytes = 0
        self._drain_until_idle(timeout=self.timeout)
        return self.screen.snapshot(self.last_command), self._last_drain_bytes

    def send_key_repeat(self, key: str, count: int) -> Snapshot:
        payload = KEYS[key]
        self.last_command = f"{key} x{count}"
        self.sock.sendall(payload * count)
        return self.capture()

    def send_char(self, ch: str) -> Snapshot:
        self.last_command = ch
        self.sock.sendall(ch.encode("ascii"))
        return self.capture()

    def send_text(self, text: str, label: str) -> Snapshot:
        self.last_command = label
        self.sock.sendall(text.encode("ascii"))
        return self.capture()

    def goto(self, address: str) -> Snapshot:
        self.send_char("J")
        return self.send_text(address + "\r", f"J {address}")

    def fill(self, expr: str) -> Snapshot:
        self.send_char("F")
        return self.send_text(expr + "\r", f"F {expr}")

    def compare(self, expr: str) -> Snapshot:
        self.send_char("C")
        return self.send_text(expr + "\r", f"C {expr}")

    def transfer(self, expr: str) -> Snapshot:
        self.send_char("T")
        return self.send_text(expr + "\r", f"T {expr}")

    def goto_run(self, address: str) -> Snapshot:
        self.send_char("G")
        return self.send_text(address + "\r", f"G {address}")

    def enter_monitor(self) -> Snapshot:
        snapshot = self.send_key("CTRL_O")
        try:
            snapshot.find_status_line()
            return snapshot
        except Failure:
            pass

        snapshot = self.send_key("F5")
        snapshot = self.send_char("D")
        snapshot = self.send_key("ENTER")
        snapshot = self.send_key("DOWN")
        snapshot = self.send_key("ENTER")
        snapshot.find_status_line()
        return snapshot

    def _drain_until_idle(self, timeout: float) -> None:
        end = time.time() + timeout
        last_data = time.time()
        drained = 0
        last_change = last_data
        signature = self.screen.signature()
        while time.time() < end:
            wait = min(0.5, max(0.0, end - time.time()))
            ready, _, _ = select.select([self.sock], [], [], wait)
            if not ready:
                if time.time() - last_data >= 0.5:
                    self._last_drain_bytes = drained
                    return
                if time.time() - last_change >= 0.2:
                    self._last_drain_bytes = drained
                    return
                continue
            chunk = self.sock.recv(65536)
            if not chunk:
                self._last_drain_bytes = drained
                return
            drained += len(chunk)
            self.screen.feed(chunk)
            new_signature = self.screen.signature()
            if new_signature != signature:
                signature = new_signature
                last_change = time.time()
            last_data = time.time()
        self._last_drain_bytes = drained
        self.screen.snapshot(self.last_command).find_status_line()


def wait_for_monitor_ready(host: str, port: int, password: Optional[str], timeout: float) -> None:
    deadline = time.time() + 90.0
    last_error: Optional[BaseException] = None

    while time.time() < deadline:
        session = None
        try:
            session = MonitorSession(host, port, password, timeout)
            before_snapshot = session.capture()
            before = before_snapshot.line(before_snapshot.find_status_line())
            after_snapshot = session.send_char("O")
            after = after_snapshot.line(after_snapshot.find_status_line())
            if before != after:
                return
        except (Failure, OSError, TimeoutError, urllib.error.URLError) as exc:
            last_error = exc
        finally:
            if session is not None:
                session.close()
        time.sleep(2.0)

    if last_error is not None:
        raise last_error
    raise TimeoutError(f"Timed out waiting for {host}:{port} monitor readiness")


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


def assert_status_contains(snapshot: Snapshot, expected: str) -> None:
    line_index = snapshot.find_status_line()
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


_REST_UNAVAILABLE: Dict[str, bool] = {}


def rest_available(host: str, timeout: float = 1.0) -> bool:
    """Probe whether the REST API is reachable for `host`.

    Used by U2 (and other non-U64) targets to short-circuit dozens of
    write_rest_memory / read_rest_memory calls into a single SkipCheck so the
    test report shows one clear "REST not available" SKIP entry rather than
    a wall of identical connection errors.
    """
    cached = _REST_UNAVAILABLE.get(host)
    if cached is not None:
        return not cached
    url = f"http://{host}/v1/machine:readmem?address=0000&length=1"
    try:
        with urllib.request.urlopen(url, timeout=timeout):
            _REST_UNAVAILABLE[host] = False
            return True
    except Exception:  # noqa: BLE001 - any failure means unreachable
        _REST_UNAVAILABLE[host] = True
        return False


def read_rest_memory(host: str, address: int, length: int) -> bytes:
    # Both U64 and U2/U2+ ship the REST `v1/machine` API (route_machine.cc
    # is linked into all three target builds). Only fall back to SkipCheck
    # when the API is actually unreachable, regardless of target type.
    if not rest_available(host):
        raise SkipCheck(f"REST API not reachable on {host}")
    url = f"http://{host}/v1/machine:readmem?address={address:04X}&length={length}"
    deadline = time.time() + 15.0
    last_error: Optional[BaseException] = None

    while time.time() < deadline:
        try:
            with urllib.request.urlopen(url, timeout=5.0) as response:
                return response.read()
        except (OSError, TimeoutError, urllib.error.URLError) as exc:
            last_error = exc
            time.sleep(0.5)

    if last_error is not None:
        raise last_error
    raise TimeoutError(f"Timed out reading REST memory from {url}")


def write_rest_memory(host: str, address: int, data: bytes) -> None:
    if not data:
        raise Failure("write_rest_memory requires at least one byte")

    if not rest_available(host):
        raise SkipCheck(f"REST API not reachable on {host}")

    deadline = time.time() + 15.0
    last_error: Optional[BaseException] = None

    while time.time() < deadline:
        try:
            if len(data) <= 128:
                url = f"http://{host}/v1/machine:writemem?address={address:04X}&data={data.hex().upper()}"
                request = urllib.request.Request(url, data=b"", method="PUT")
            else:
                url = f"http://{host}/v1/machine:writemem?address={address:04X}"
                request = urllib.request.Request(
                    url,
                    data=data,
                    method="POST",
                    headers={"Content-Type": "application/octet-stream"},
                )
            with urllib.request.urlopen(request, timeout=5.0):
                return
        except (OSError, TimeoutError, urllib.error.URLError) as exc:
            last_error = exc
            time.sleep(0.5)

    if last_error is not None:
        raise last_error
    raise TimeoutError(f"Timed out writing REST memory to {host} at ${address:04X}")


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
            line_index = screen.find_status_line()
        except Failure:
            line_index = -1
        if line_index >= 0:
            line = screen.line(line_index)
            if expected in line:
                return screen
        screen = session.send_char("o")
    raise Failure(
        f"Unable to reach expected CPU/VIC status {expected!r}; last status line was {screen.line(screen.find_status_line())!r}"
    )


def cycle_cpu_bank_from_cpu7(session: MonitorSession, target_status: str, steps: int) -> Snapshot:
    screen = ensure_status(session, "CPU7 $A:BAS $D:I/O $E:KRN VIC")

    for _ in range(steps):
        screen = session.send_char("o")

    assert_status_contains(screen, target_status)
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


def run_go_repeat_test(session: MonitorSession, rest_host: str) -> None:
    sentinel = 0x5A
    values = (0x42, 0x37, 0x99)

    for value in values:
        write_rest_memory(rest_host, 0x0810, bytes((0xA9, value, 0x8D, 0x00, 0x20, 0x00)))
        write_rest_memory(rest_host, 0x2000, bytes((sentinel,)))

        screen = ensure_view(session, "HEX ")
        screen = session.goto("2000")
        before = parse_memory_row(screen, 0x2000)
        if before[0] != sentinel:
            raise Failure(
                f"G precondition failed for ${value:02X}: expected ${sentinel:02X} at $2000, got ${before[0]:02X}"
            )

        session.goto_run("0810")
        wait_for_rest_byte(rest_host, 0x2000, value)

        session.enter_monitor()
        screen = ensure_view(session, "HEX ")
        screen = session.goto("2000")
        after = parse_memory_row(screen, 0x2000)
        if after[0] != value:
            raise Failure(
                f"G postcondition failed for ${value:02X}: expected ${value:02X} at $2000, got ${after[0]:02X}"
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
    screen.find_line_containing("0-9/RET:Jmp  S:Set  L:Label  DEL:Reset")

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
    write_rest_memory(rest_host, 0x3100, bytes((0x12, 0x34, 0x56, 0x78)))

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
    screen.find_status_line()


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

    The picker prints the active path on the bottom line, below its border."""
    for line in reversed(snapshot.lines):
        text = line.strip()
        if text.endswith("-F3=HELP-"):
            text = text[:-len("-F3=HELP-")].rstrip()
        if text and not text.startswith("+"):
            return text
    return ""


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
    session.sock.sendall(KEYS["BACKSPACE"] * 40)
    session.capture()


def rest_create_d64(host: str, path: str, diskname: str) -> None:
    url = f"http://{host}/v1/files{path}:create_d64?diskname={diskname}"
    request = urllib.request.Request(url, data=b"", method="PUT")
    with urllib.request.urlopen(request, timeout=15.0):
        pass


def rest_file_exists(host: str, path: str) -> bool:
    try:
        with urllib.request.urlopen(f"http://{host}/v1/files{path}:info", timeout=5.0) as response:
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
    snapshot = session.send_key("RIGHT")  # step into /Temp (the first root entry)
    for prefix in enter_dirs:
        snapshot = picker_enter(session, prefix)
    # The cursor defaults to "<< Create New File >>"; RIGHT picks it and the
    # monitor then asks for the file name.
    session.send_key("RIGHT")
    clear_prompt_field(session)
    snapshot = session.send_text(filename + "\r", f"save as {filename}")
    snapshot.find_line_containing("SAVE")
    return session.send_key("ENTER")  # dismiss the confirmation popup


def monitor_load(session: MonitorSession, enter_dirs: List[str], filename: str) -> Snapshot:
    """Load filename back, navigating from root through enter_dirs."""
    session.send_char("L")
    picker_to_root(session)
    session.send_key("RIGHT")  # step into /Temp
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


def run_save_load_topfile_test(session: MonitorSession, rest_host: str, token: str) -> None:
    addr = 0xC000
    pattern = bytes((0x5A, 0xA5, 0x01, 0x02, 0xDE, 0xAD, 0xBE, 0xEF,
                     0x10, 0x20, 0x30, 0x40, 0x50, 0x60, 0x70, 0x80))
    name = f"MS{token}.PRG"

    write_rest_memory(rest_host, addr, pattern)
    monitor_save(session, f"{addr:04X}-{addr + len(pattern) - 1:04X}", [], name)
    if not rest_file_exists(rest_host, f"/Temp/{name}"):
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


def run_save_load_d64_test(session: MonitorSession, rest_host: str, token: str) -> None:
    addr = 0xC100
    pattern = bytes((0x11, 0x22, 0x33, 0x44, 0xAA, 0xBB, 0xCC, 0xDD,
                     0x09, 0x08, 0x07, 0x06, 0x05, 0x04, 0x03, 0x02))
    disk = f"MD{token}.D64"
    inner = f"D{token}"

    rest_create_d64(rest_host, f"/Temp/{disk}", f"MD{token}")
    if not rest_file_exists(rest_host, f"/Temp/{disk}"):
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

def highlighted_line(snapshot: Snapshot) -> str:
    if not snapshot.reverse_cells:
        raise Failure(f"No highlighted cells after {snapshot.last_command}\n{snapshot.text()}")
    row = snapshot.reverse_cells[0][1]
    return snapshot.line(row)


def highlighted_row_count(snapshot: Snapshot) -> int:
    return len({row for _, row in snapshot.reverse_cells})


def assert_highlighted_line_contains(snapshot: Snapshot, values: Tuple[str, ...]) -> None:
    count = highlighted_row_count(snapshot)
    if count != 1:
        raise Failure(
            f"Expected exactly one highlighted row after {snapshot.last_command}, got {count}\n"
            f"{snapshot.text()}"
        )
    line = highlighted_line(snapshot)
    if not all(value in line for value in values):
        raise Failure(
            f"Highlighted line after {snapshot.last_command} did not contain {values!r}\n"
            f"line: {line!r}\n{snapshot.text()}"
        )


def wait_for_highlighted_line_contains(session: MonitorSession, values: Tuple[str, ...],
                                       timeout: float = 5.0) -> Snapshot:
    deadline = time.time() + timeout
    last_snapshot: Optional[Snapshot] = None

    while time.time() < deadline:
        snapshot = session.capture()
        last_snapshot = snapshot
        try:
            assert_highlighted_line_contains(snapshot, values)
            return snapshot
        except Failure:
            time.sleep(0.1)

    if last_snapshot is None:
        raise Failure(f"Timed out waiting for highlighted line {values!r}")
    assert_highlighted_line_contains(last_snapshot, values)
    return last_snapshot


def assert_canonical_kernal_lane(snapshot: Snapshot, highlighted_values: Tuple[str, ...]) -> None:
    top_row = snapshot.find_line_containing(ASM_KERNAL_ROWS[0][0])
    for offset, (address_bytes, disasm_text) in enumerate(ASM_KERNAL_ROWS):
        row_index = top_row + offset
        if row_index >= len(snapshot.lines):
            raise Failure(f"KERNAL lane row {address_bytes} is below the captured screen\n{snapshot.text()}")
        line = snapshot.line(row_index)
        if address_bytes not in line or disasm_text not in line:
            raise Failure(
                f"KERNAL lane row mismatch after {snapshot.last_command}: expected {address_bytes!r} / {disasm_text!r} "
                f"on line {row_index}\nactual: {line!r}\n{snapshot.text()}"
            )
    assert_line_lacks(snapshot, "E001 56 20")
    assert_line_lacks(snapshot, "E010 BC A5 07")
    assert_highlighted_line_contains(snapshot, highlighted_values)


def run_asm_kernal_root_stability_test(session: MonitorSession) -> None:
    ensure_status(session, "CPU7 $A:BAS $D:I/O $E:KRN VIC")
    screen = session.goto("E000")
    screen = ensure_view(session, "ASM ")
    screen.find_line_containing("MONITOR ASM $E000")
    assert_canonical_kernal_lane(screen, ("E000 85 56", "STA $56"))

    screen = session.fill("DFFD-DFFD,4C")
    assert_canonical_kernal_lane(screen, ("E000 85 56", "STA $56"))
    screen = session.fill("DFFE-DFFE,00")
    assert_canonical_kernal_lane(screen, ("E000 85 56", "STA $56"))
    screen = session.fill("DFFF-DFFF,E0")
    screen.find_line_containing("MONITOR ASM $E000")
    assert_canonical_kernal_lane(screen, ("E000 85 56", "STA $56"))

    screen = session.fill("DFFD-DFFD,20")
    assert_canonical_kernal_lane(screen, ("E000 85 56", "STA $56"))


def run_asm_decode_offset_test(session: MonitorSession, rest_host: str) -> None:
    address = 0x3400 if is_u2() else 0xE000
    if is_u2():
        write_rest_memory(rest_host, address, ASM_CANONICAL_BYTES)

    screen = session.goto(f"{address:04X}")
    screen = ensure_view(session, "ASM ")
    assert_highlighted_line_contains(screen, (f"{address:04X} 85 56", "STA $56"))
    top_row = screen.find_line_containing(f"{address:04X} 85 56")
    screen = session.send_key("RIGHT")
    screen.find_line_containing(f"MONITOR ASM ${address + 1:04X}")
    assert_highlighted_line_contains(screen, (f"{address + 1:04X}",))
    screen = session.send_key("RIGHT")
    screen.find_line_containing(f"MONITOR ASM ${address + 2:04X}")
    assert_highlighted_line_contains(screen, (f"{address + 2:04X} 20 0F BC", "JSR $BC0F"))
    screen = session.send_key("LEFT")
    screen.find_line_containing(f"MONITOR ASM ${address + 1:04X}")
    screen = session.send_key("LEFT")
    screen.find_line_containing(f"MONITOR ASM ${address:04X}")
    screen = session.send_key("DOWN")
    screen.find_line_containing(f"MONITOR ASM ${address + 2:04X}")
    assert_highlighted_line_contains(screen, (f"{address + 2:04X} 20 0F BC", "JSR $BC0F"))
    if f"{address:04X} 85 56" not in screen.line(top_row):
        raise Failure(f"ASM Down to +2 moved the viewport top\n{screen.text()}")

    down_rows = (
        (0x0005, "A5 61", "LDA $61"),
        (0x0007, "C9 88", "CMP #$88"),
        (0x0009, "90 03", "BCC"),
        (0x000B, "20 D4 BA", "JSR $BAD4"),
        (0x000E, "20 CC BC", "JSR $BCCC"),
        (0x0011, "A5 07", "LDA $07"),
        (0x0013, "18", "CLC"),
    )
    for offset, byte_text, disasm_text in down_rows:
        screen = session.send_key("DOWN")
        screen.find_line_containing(f"MONITOR ASM ${address + offset:04X}")
        assert_highlighted_line_contains(screen, (f"{address + offset:04X} {byte_text}", disasm_text))
        if f"{address:04X} 85 56" not in screen.line(top_row):
            raise Failure(f"ASM Down to +{offset:04X} moved the viewport top\n{screen.text()}")
        assert_line_lacks(screen, f"{address + 1:04X} 56 20")

    screen = session.send_key("UP")
    screen.find_line_containing(f"MONITOR ASM ${address + 0x0011:04X}")
    assert_highlighted_line_contains(screen, (f"{address + 0x0011:04X} A5 07", "LDA $07"))
    assert_line_lacks(screen, f"{address + 0x0010:04X} BC A5 07")

    sliding_start = 0x3600
    sliding_steps = 300
    # send_key_repeat() puts all sliding_steps cursor keys into a single sendall;
    # the firmware processes them one redraw at a time over telnet, so a 300-key
    # burst drains in ~10s on hardware (the viewport still lands exactly on the
    # target). Scale the settle wait with the burst size so the poll outlasts the
    # drain instead of timing out mid-scroll (the default 5s is ~half of it).
    sliding_settle = 5.0 + sliding_steps * 0.05
    write_rest_memory(rest_host, 0x3400, bytes((0xEA,)) * 0x400)
    screen = session.goto(f"{sliding_start:04X}")
    screen = ensure_view(session, "ASM ")
    assert_highlighted_line_contains(screen, (f"{sliding_start:04X} EA", "NOP"))
    session.send_key_repeat("UP", sliding_steps)
    screen = wait_for_highlighted_line_contains(
        session, (f"{sliding_start - sliding_steps:04X} EA", "NOP"), timeout=sliding_settle)
    session.send_key_repeat("DOWN", sliding_steps)
    screen = wait_for_highlighted_line_contains(
        session, (f"{sliding_start:04X} EA", "NOP"), timeout=sliding_settle)


def run_asm_edit_navigation_test(session: MonitorSession, rest_host: str) -> None:
    write_rest_memory(rest_host, 0x3410, bytes((0xA9, 0x12, 0xEA, 0xEA)))
    screen = session.goto("3410")
    screen = ensure_view(session, "ASM ")
    screen.find_line_containing("LDA #$12")
    screen = session.send_char("e")
    screen = session.send_key("RIGHT")
    screen.find_line_containing("MONITOR ASM $3410")
    assert_highlighted_line_contains(screen, ("3410 A9 12", "LDA #$12"))
    screen = session.send_key("LEFT")
    screen.find_line_containing("MONITOR ASM $3410")
    assert_highlighted_line_contains(screen, ("3410 A9 12", "LDA #$12"))
    screen = session.send_key("DOWN")
    screen.find_line_containing("MONITOR ASM $3412")
    screen = session.send_key("UP")
    screen.find_line_containing("MONITOR ASM $3410")
    screen = session.send_key("ESC")
    screen.find_line_containing("MONITOR ASM $3410")


def run_asm_cpu0_continuous_ram_test(session: MonitorSession) -> None:
    ensure_status(session, "CPU7 $A:BAS $D:I/O $E:KRN VIC")
    screen = session.send_char("o")
    assert_status_contains(screen, "C7O0 $A:RAM $D:RAM $E:RAM VIC")
    session.fill("DFFE-DFFE,20")
    session.fill("DFFF-DFFF,00")
    session.fill("E000-E000,E0")
    screen = session.goto("DFFE")
    screen = ensure_view(session, "ASM ")
    assert_highlighted_line_contains(screen, ("DFFE 20 00 E0", "JSR $E000"))
    for _ in range(7):
        screen = session.send_char("o")
    assert_status_contains(screen, "CPU7 $A:BAS $D:I/O $E:KRN VIC")


def run_asm_self_modification_test(session: MonitorSession, rest_host: str) -> None:
    write_rest_memory(rest_host, 0x3502, bytes((0xA9, 0x00, 0xEA, 0xA5, 0x61, 0xEA, 0xEA)))
    screen = session.goto("3505")
    screen = ensure_view(session, "ASM ")
    assert_highlighted_line_contains(screen, ("3505 A5 61", "LDA $61"))

    session.fill("3502-3502,20")
    session.fill("3503-3503,05")
    screen = session.fill("3504-3504,35")
    screen.find_line_containing("MONITOR ASM $3505")
    assert_highlighted_line_contains(screen, ("3505 A5 61", "LDA $61"))

    session.fill("3505-3505,A9")
    screen = session.fill("3506-3506,42")
    assert_highlighted_line_contains(screen, ("3505 A9 42", "LDA #$42"))
    screen = session.fill("3507-3507,60")
    screen.find_line_containing("3507 60")
    screen.find_line_containing("RTS")


def run_tests(session: MonitorSession, rest_host: str) -> None:
    snapshots = load_snapshots()

    with check("initial CPU7/KERNAL monitor status", u2=False,
               u2_reason="U2 reports 'CPU VIEW CPU BANK N/A VIC N/A' instead of CPU#/$A:.../VIC#"):
        ensure_status(session, snapshots["status_cpu31"]["contains"]["22"])

    with check("initial monitor status line is present"):
        ensure_view(session, "HEX ")
        snap = session.capture()
        snap.find_status_line()

    with check("telnet blocks poll mode"):
        run_telnet_poll_guard_test(session)

    with check("KERNAL $E000 hex view and REST match", u2=False,
               u2_reason="U2 monitor does not snapshot KERNAL ROM into its own view, "
                         "so the expected text fragments differ from U64"):
        ensure_hex_width(session, 8)
        screen = session.goto("E000")
        for row, expected in snapshots["kernal_hex_e000"]["contains"].items():
            assert_contains(screen, int(row), expected)
        assert_rest_matches_row(screen, 4, 0xE000, rest_host)

    with check("paging away and back keeps memory view stable"):
        ensure_view(session, "HEX ")
        # Use a target address that is meaningful for both U64 (KERNAL view
        # already at $E000) and U2 (no ROM snapshot, but RAM at $C000 is
        # stable). Move there explicitly so the test does not depend on prior
        # state set by KERNAL-specific tests that may have been skipped.
        ref_addr = "E000" if is_u64() else "C000"
        ref = session.goto(ref_addr)
        initial_snapshot = ref.text()
        session.send_key("PGDN")
        back = session.send_key("PGUP")
        assert_equal("Memory stability", initial_snapshot, back.text(), back.last_command)

    with check("KERNAL disassembly formatting", u2=False,
               u2_reason="U2 does not snapshot KERNAL ROM into the monitor view"):
        # Reposition explicitly so this test does not depend on prior cursor
        # state. Previously this relied on the immediately-preceding check
        # leaving the view at $E000.
        screen = session.goto("E000")
        screen = session.send_char("A")
        for row, expected in snapshots["kernal_disasm_e000"]["contains"].items():
            assert_contains(screen, int(row), expected)

        screen = session.goto("E013")
        screen = session.send_char("A")
        for row, expected in snapshots["kernal_disasm_e013"]["contains"].items():
            assert_contains(screen, int(row), expected)

    with check("ASM root stable across volatile pre-root IO", u2=False,
               u2_reason="U2 backend does not expose U64 CPU7 I/O/KERNAL banking"):
        run_asm_kernal_root_stability_test(session)

    with check("ASM decode offset and instruction navigation"):
        run_asm_decode_offset_test(session, rest_host)

    with check("ASM edit navigation keeps root stable"):
        run_asm_edit_navigation_test(session, rest_host)

    with check("ASM CPU0 continuous RAM can cross DFFF/E000", u2=False,
               u2_reason="U2 backend does not expose monitor-side CPU banking"):
        run_asm_cpu0_continuous_ram_test(session)

    with check("ASM self-modification root rules"):
        run_asm_self_modification_test(session, rest_host)

    with check("KERNAL $E010 REST match", u2=False,
               u2_reason="U2 monitor does not snapshot KERNAL ROM, so monitor row text differs"):
        screen = ensure_view(session, "HEX ")
        screen = session.goto("E010")
        assert_rest_matches_row(screen, 4, 0xE010, rest_host)

    with check("CPU6 RAM under BASIC write/read", u2=False,
               u2_reason="U2 backend does not expose monitor-side CPU banking"):
        screen = ensure_view(session, "HEX ")
        session.goto("A000")
        screen = cycle_cpu_bank_from_cpu7(session, snapshots["status_cpu30"]["contains"]["22"], 7)
        session.fill("A000-A000,AA")
        screen = session.goto("A000")
        assert_contains(screen, 4, snapshots["ram_a000"]["contains"]["4"])

    with check("CPU5 RAM under KERNAL status", u2=False,
               u2_reason="U2 backend does not expose monitor-side CPU banking"):
        session.goto("E000")
        screen = cycle_cpu_bank_from_cpu7(session, snapshots["status_cpu29"]["contains"]["22"], 6)

    with check("ASCII view width and scrolling", u2=False,
               u2_reason="Reference snapshot keys reference U64-specific CPU bank status"):
        session.goto("C000")
        for row_index in range(19):
            start = 0xC000 + row_index * 0x20
            end = start + 0x1F
            session.fill(f"{start:04X}-{end:04X},{0x41 + row_index:02X}")

        screen = ensure_view(session, "ASC ")
        content_rows = find_memory_rows(screen)
        first_content_row = content_rows[0]
        last_content_row = content_rows[-1]
        assert_ascii_width(screen, first_content_row)
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

    with check("ASCII and Screen mapping semantics", u2=False,
               u2_reason="Helper writes via REST and asserts CPU/VIC status"):
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

    with check("CPU bank cycling reaches CHAR and RAM mappings", u2=False,
               u2_reason="U2 backend does not expose monitor-side CPU banking"):
        session.goto("A000")
        screen = ensure_status(session, snapshots["status_cpu27"]["contains"]["22"])
        assert_status_contains(screen, snapshots["status_cpu27"]["contains"]["22"])
        session.send_char("o")
        session.send_char("o")
        screen = session.send_char("o")
        assert_status_contains(screen, snapshots["status_cpu30"]["contains"]["22"])

    with check("COMPARE reports differing address"):
        screen = ensure_view(session, "HEX ")
        session.fill("C100-C103,10")
        session.fill("C200-C203,10")
        session.fill("C201-C201,91")
        session.fill("C203-C203,93")
        screen = session.compare("C100-C103,C200")
        assert_contains(screen, 4, "C101")
        session.send_key("ENTER")

    with check("TRANSFER relocates absolute operands", u2=False,
               u2_reason="Helper writes via REST and asserts U64 RAM content"):
        write_rest_memory(rest_host, 0xC400, bytes.fromhex("AD08C48D20D04C00C460"))
        write_rest_memory(rest_host, 0xC410, bytes.fromhex("AE08C4"))
        write_rest_memory(rest_host, 0xC500, bytes([0x00] * 0x13))
        session.transfer("C400-C40A,C500,C400-C413")
        copied = read_rest_memory(rest_host, 0xC500, 0x0A)
        outside = read_rest_memory(rest_host, 0xC410, 0x03)
        assert_equal("Relocated copied code", "AD08C58D20D04C00C560", copied.hex().upper(), "T relocate copied")
        assert_equal("Relocated external operand", "AE08C5", outside.hex().upper(), "T relocate external")

    with check("G executes finite loop and returns to monitor"):
        # REST is available on U2 firmware too (route_machine.cc is linked
        # into all three target builds); read_rest_memory/write_rest_memory
        # will raise SkipCheck if the REST endpoint is genuinely unreachable.
        write_rest_memory(rest_host, 0x1000, bytes.fromhex("A9008D0004A9018D00044C0010"))
        write_rest_memory(rest_host, 0x0400, bytes([0x20]))
        session.goto("1000")
        session.goto_run("1000")
        wait_for_rest_byte(rest_host, 0x0400, 0x01)
        session.enter_monitor()

    with check("G repeated execution updates RAM sentinel"):
        run_go_repeat_test(session, rest_host)

    with check("G handoff preserves stable VIC state"):
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

    with check("telnet opcode dropdown scroll does not flood the connection"):
        run_telnet_dropdown_scroll_flood_test(session, rest_host)

    with check("number popup arithmetic"):
        run_number_arithmetic_test(session, rest_host)

    save_load_token = f"{int(time.time()) % 100000:05d}"

    with check("save/load round-trip to top-level /Temp file"):
        run_save_load_topfile_test(session, rest_host, save_load_token)

    with check("save/load round-trip to file in new /Temp D64"):
        run_save_load_d64_test(session, rest_host, save_load_token)


def main() -> int:
    parser = argparse.ArgumentParser(description="Validate the machine monitor over the standard telnet service")
    parser.add_argument("--host", default=os.environ.get("U64_MONITOR_HOST", "u64"))
    parser.add_argument("--port", type=int, default=int(os.environ.get("U64_MONITOR_PORT", "23")))
    parser.add_argument("--rest-host", default=os.environ.get("U64_MONITOR_REST_HOST"))
    parser.add_argument("--password", default=os.environ.get("U64_MONITOR_PASSWORD"))
    parser.add_argument("--timeout", type=float, default=float(os.environ.get("U64_MONITOR_TIMEOUT", "5.0")))
    parser.add_argument("--target", choices=("u64", "u2"),
                        default=os.environ.get("U64_MONITOR_TARGET", "u64"),
                        help="Target hardware. 'u2' skips U64-only features (CPU banking, VIC bank, ROM patching, REST API).")
    parser.add_argument("--keep-going", action="store_true",
                        default=os.environ.get("U64_MONITOR_KEEP_GOING", "").lower() in ("1", "true", "yes"),
                        help="Continue running after a check fails, logging verbose context for each failure.")
    args = parser.parse_args()

    TestConfig.target = args.target
    # U2/U2+ ship the REST API like the U64, but availability is determined at
    # runtime by rest_available(); dozens of tests are REST-dependent. Default
    # to keep-going so a remote operator captures every failure in a single
    # console run instead of stopping at the first.
    TestConfig.keep_going = args.keep_going or args.target == "u2"
    TestConfig.failures = []
    TestConfig.skipped = []
    rest_host = args.rest_host or args.host
    if args.target == "u2":
        if not rest_available(rest_host):
            print(f"[info] target=u2 and REST unreachable on {rest_host}; "
                  f"REST-dependent checks will SKIP", flush=True)
        else:
            print(f"[info] target=u2 with REST reachable on {rest_host}; "
                  f"U64-only checks (CPU/VIC banking, KERNAL snapshot) still SKIP", flush=True)

    redeployed = False
    hard_error = False
    while True:
        session = None
        try:
            session = MonitorSession(args.host, args.port, args.password, args.timeout)
            TestConfig.session = session
            run_tests(session, rest_host)
            break
        except Failure as exc:
            if (not redeployed) and is_u64() and device_unavailable(exc):
                try:
                    redeploy_u64(args.host, args.port, args.password, args.timeout)
                except (Failure, OSError, TimeoutError, urllib.error.URLError, subprocess.CalledProcessError) as redeploy_exc:
                    print(f"Connection failure: {format_exception(redeploy_exc)}", file=sys.stderr)
                    return 1
                redeployed = True
                continue
            print(exc, file=sys.stderr)
            if session is not None:
                snapshot = session.capture()
                print("\nFinal screen:", file=sys.stderr)
                print(snapshot.text(), file=sys.stderr)
            break
        except (OSError, TimeoutError, urllib.error.URLError, subprocess.CalledProcessError) as exc:
            if (not redeployed) and is_u64() and device_unavailable(exc):
                try:
                    redeploy_u64(args.host, args.port, args.password, args.timeout)
                except (Failure, OSError, TimeoutError, urllib.error.URLError, subprocess.CalledProcessError) as redeploy_exc:
                    print(f"Connection failure: {format_exception(redeploy_exc)}", file=sys.stderr)
                    return 1
                redeployed = True
                continue
            print(f"Connection failure: {format_exception(exc)}", file=sys.stderr)
            hard_error = True
            break
        finally:
            if session is not None:
                session.close()
            TestConfig.session = None

    _print_summary()
    if TestConfig.failures or hard_error:
        return 1
    print(f"monitor_test: OK ({CHECK_COUNT} checks, "
          f"{len(TestConfig.skipped)} skipped)")
    return 0


def _print_summary() -> None:
    """End-of-run summary. Always prints skip+failure counts; lists every
    failure in compact form so the operator can scan the console capture
    quickly and attach each diagnostic block to a fix plan.
    """
    passed = CHECK_COUNT - len(TestConfig.failures) - len(TestConfig.skipped)
    print("")
    print("==== monitor_test summary ====")
    print(f"  target  : {TestConfig.target}")
    print(f"  passed  : {passed}")
    print(f"  skipped : {len(TestConfig.skipped)}")
    print(f"  failed  : {len(TestConfig.failures)}")
    if TestConfig.skipped:
        print("")
        print("Skipped checks:")
        for idx, label, reason in TestConfig.skipped:
            print(f"  [{idx:02d}] {label}  ({reason})")
    if TestConfig.failures:
        print("")
        print("Failed checks:")
        for idx, label, _diag in TestConfig.failures:
            print(f"  [{idx:02d}] {label}")


if __name__ == "__main__":
    raise SystemExit(main())
