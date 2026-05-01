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
from dataclasses import dataclass
from pathlib import Path
from typing import Dict, List, Optional, Tuple

WIDTH = 40
HEIGHT = 24
SNAPSHOT_FILE = Path(__file__).with_name("snapshots").joinpath("expected_snapshots.json")
REPO_ROOT = Path(__file__).resolve().parents[3]
REDEPLOY_SCRIPT = REPO_ROOT / "tooling" / "build_and_deploy_u64.sh"

STATUS_LINE_RE = re.compile(r"CPU[0-7] \$A:(?:RAM|BAS) \$D:(?:RAM|CHR|I/O) \$E:(?:RAM|KRN) VIC[0-3] \$[0-9A-F]{4}")
MEMORY_ROW_RE = re.compile(r"^[0-9A-F]{4} ")

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
    "CTRL_O": b"\x0f",
    "ESC": b"\x1b",
    "ENTER": b"\r",
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
            if STATUS_LINE_RE.search(line):
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
        self.sock = self._connect_with_retry(host, port, timeout)
        self.sock.setblocking(False)
        self.timeout = timeout
        self.password = password
        self.screen = VT100Screen()
        self.last_command = "<connect>"
        self._drain_until_idle(timeout=timeout)
        if self.screen.saw_password_prompt():
            if password is None:
                raise Failure("Telnet password prompt received but no password was provided")
            self.send_text(password + "\r", "password")
            self._drain_until_idle(timeout=timeout)
        self.enter_monitor()

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
        self._drain_until_idle(timeout=self.timeout)
        return self.screen.snapshot(self.last_command)

    def send_key(self, key: str) -> Snapshot:
        payload = KEYS[key]
        self.last_command = key
        self.sock.sendall(payload)
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
        while time.time() < end:
            wait = min(0.2, max(0.0, end - time.time()))
            ready, _, _ = select.select([self.sock], [], [], wait)
            if not ready:
                if time.time() - last_data >= 0.2:
                    return
                continue
            chunk = self.sock.recv(65536)
            if not chunk:
                return
            self.screen.feed(chunk)
            last_data = time.time()
        raise Failure(f"Timed out waiting for telnet screen to go idle after {self.last_command}")


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

    url = f"http://{host}/v1/machine:writemem?address={address:04X}&data={data.hex().upper()}"
    request = urllib.request.Request(url, data=b"", method="PUT")
    with urllib.request.urlopen(request, timeout=5.0):
        pass


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


def ensure_status(session: MonitorSession, expected: str) -> Snapshot:
    screen = session.capture()
    for _ in range(7):
        try:
            line_index = screen.find_status_line()
        except Failure:
            line_index = -1
        if line_index >= 0 and expected in screen.line(line_index):
            return screen
        screen = session.send_char("O")
    raise Failure(
        f"Unable to reach expected CPU/VIC status {expected!r}; last status line was {screen.line(screen.find_status_line())!r}"
    )


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
    pre_d011 = read_rest_memory(rest_host, 0xD011, 1)[0]
    pre_d020 = read_rest_memory(rest_host, 0xD020, 1)[0]
    pre_d021 = read_rest_memory(rest_host, 0xD021, 1)[0]

    write_rest_memory(rest_host, 0x0810, bytes.fromhex("EE21D000"))
    session.goto_run("0810")
    time.sleep(0.2)

    post_d011 = read_rest_memory(rest_host, 0xD011, 1)[0]
    post_d020 = read_rest_memory(rest_host, 0xD020, 1)[0]
    post_d021 = read_rest_memory(rest_host, 0xD021, 1)[0]
    expected_d021 = (pre_d021 + 1) & 0xFF

    if post_d011 != pre_d011:
        raise Failure(
            f"G visual-state check failed: $D011 changed from ${pre_d011:02X} to ${post_d011:02X}"
        )
    if post_d020 != pre_d020:
        raise Failure(
            f"G visual-state check failed: $D020 changed from ${pre_d020:02X} to ${post_d020:02X}"
        )
    if post_d021 != expected_d021:
        raise Failure(
            f"G visual-state check failed: expected $D021 ${expected_d021:02X}, got ${post_d021:02X}"
        )

    session.enter_monitor()


def run_tests(session: MonitorSession, rest_host: str) -> None:
    snapshots = load_snapshots()

    ensure_status(session, snapshots["status_cpu31"]["contains"]["22"])
    ensure_view(session, "HEX ")
    screen = session.goto("E000")
    for row, expected in snapshots["kernal_hex_e000"]["contains"].items():
        assert_contains(screen, int(row), expected)
    assert_rest_matches_row(screen, 4, 0xE000, rest_host)

    initial_snapshot = screen.text()
    session.send_key("PGDN")
    back = session.send_key("PGUP")
    assert_equal("Memory stability", initial_snapshot, back.text(), back.last_command)

    screen = session.send_char("D")
    for row, expected in snapshots["kernal_disasm_e000"]["contains"].items():
        assert_contains(screen, int(row), expected)

    screen = session.goto("E013")
    screen = session.send_char("D")
    for row, expected in snapshots["kernal_disasm_e013"]["contains"].items():
        assert_contains(screen, int(row), expected)

    screen = session.goto("E010")
    assert_rest_matches_row(screen, 4, 0xE010, rest_host)

    screen = ensure_view(session, "HEX ")
    session.goto("A000")
    screen = ensure_status(session, snapshots["status_cpu30"]["contains"]["22"])
    for address in (0xA000, 0xBFF8):
        screen = session.goto(f"{address:04X}")
        assert_rest_matches_row(screen, 4, address, rest_host)

    session.goto("E000")
    screen = ensure_status(session, snapshots["status_cpu29"]["contains"]["22"])
    for address in (0xE000, 0xFFF8):
        screen = session.goto(f"{address:04X}")
        assert_rest_matches_row(screen, 4, address, rest_host)

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

    session.goto("A000")
    screen = ensure_status(session, snapshots["status_cpu27"]["contains"]["22"])
    assert_status_contains(screen, snapshots["status_cpu27"]["contains"]["22"])
    screen = session.send_char("O")
    assert_status_contains(screen, snapshots["status_cpu30"]["contains"]["22"])

    screen = ensure_view(session, "HEX ")
    session.fill("C100-C103,10")
    session.fill("C200-C203,10")
    session.fill("C201-C201,91")
    session.fill("C203-C203,93")
    screen = session.compare("C100-C103,C200")
    assert_contains(screen, 4, "C101")

    write_rest_memory(rest_host, 0x1000, bytes.fromhex("A9008D0004A9018D00044C0010"))
    write_rest_memory(rest_host, 0x0400, bytes([0x20]))
    session.goto("1000")
    session.goto_run("1000")
    wait_for_rest_byte(rest_host, 0x0400, 0x01)
    session.enter_monitor()
    run_go_repeat_test(session, rest_host)
    run_go_visible_state_test(session, rest_host)


def main() -> int:
    parser = argparse.ArgumentParser(description="Validate the U64 machine monitor over the standard telnet service")
    parser.add_argument("--host", default=os.environ.get("U64_MONITOR_HOST", "u64"))
    parser.add_argument("--port", type=int, default=int(os.environ.get("U64_MONITOR_PORT", "23")))
    parser.add_argument("--rest-host", default=os.environ.get("U64_MONITOR_REST_HOST"))
    parser.add_argument("--password", default=os.environ.get("U64_MONITOR_PASSWORD"))
    parser.add_argument("--timeout", type=float, default=float(os.environ.get("U64_MONITOR_TIMEOUT", "5.0")))
    args = parser.parse_args()

    rest_host = args.rest_host or args.host

    redeployed = False
    while True:
        session = None
        try:
            session = MonitorSession(args.host, args.port, args.password, args.timeout)
            run_tests(session, rest_host)
            break
        except Failure as exc:
            if (not redeployed) and device_unavailable(exc):
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
            return 1
        except (OSError, TimeoutError, urllib.error.URLError, subprocess.CalledProcessError) as exc:
            if (not redeployed) and device_unavailable(exc):
                try:
                    redeploy_u64(args.host, args.port, args.password, args.timeout)
                except (Failure, OSError, TimeoutError, urllib.error.URLError, subprocess.CalledProcessError) as redeploy_exc:
                    print(f"Connection failure: {format_exception(redeploy_exc)}", file=sys.stderr)
                    return 1
                redeployed = True
                continue
            print(f"Connection failure: {format_exception(exc)}", file=sys.stderr)
            return 1
        finally:
            if session is not None:
                session.close()

    print("monitor_test: OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
