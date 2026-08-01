#!/usr/bin/env python3
# E2E helper: transport-agnostic keyboard injection + screen reading for the
# on-device UI (menu, browser, editors, machine monitor).

"""Two transports reach the same firmware UserInterface: a raw Telnet VT100
session, and REST (machine:input for keyboard injection, machine:menu_screen
for the rendered 40x25 screen matrix). Both funnel through the same
Keyboard/Screen objects in firmware (software/userinterface/userinterface.cc),
so a suite that only presses keys and reads the resulting screen can run
against either transport through this one Backend interface, instead of
hand-rolling a transport of its own.

REST is normally the right default: one HTTP round trip reads the whole
screen as a flat byte matrix, against Telnet's per-byte VT100 stream and a
fixed quiet-window wait after every keystroke. Keep Telnet only for checks
that are genuinely about the Telnet transport itself.

The two transports render the same UI content from the same row downward
(confirmed on hardware: the first content row is identical byte-for-byte),
but they are not pixel-identical: the on-device Overlay/Freeze screen is the
full 25-row physical screen, while the Telnet remote session only ever fills
24 of those rows, so a REST capture can show one extra content row at the
bottom of a box. Callers should locate content by searching (find_line_*)
rather than assuming a fixed row count, the way the existing monitor suite
already does almost everywhere.
"""

import json
import re
import select
import socket
import time
import urllib.error
import urllib.parse
import urllib.request
from dataclasses import dataclass
from typing import Dict, List, Optional, Sequence, Tuple

from report import Failure
from menu import wait_screen_changes, wait_screen_settled

SCREEN_WIDTH = 40
SCREEN_HEIGHT = 25
SCREEN_CELLS = SCREEN_WIDTH * SCREEN_HEIGHT
SCREEN_BYTES = SCREEN_CELLS * 2

MENU_SCREEN_PATH = "/v1/machine:menu_screen"
MENU_BUTTON_PATH = "/v1/machine:menu_button"
INPUT_PATH = "/v1/machine:input"
CONFIGS_PATH = "/v1/configs"
UI_STORE = "User Interface Settings"
UI_ITEM = "Interface Type"
OVERLAY_MODE = "Overlay on HDMI"

INPUT_MAX_EVENTS = 60  # software/api/route_input.cc rejects a larger batch with HTTP 400.
POLL_INTERVAL_SECONDS = 0.05
SETTLE_TIMEOUT_SECONDS = 6.0
TRANSPORT_RETRIES = 3
TRANSPORT_RETRY_PAUSE_SECONDS = 0.5

# The menu endpoint returns raw Screen_MemMappedCharMatrix bytes: text is
# stored as literal printable characters, low values are firmware UI glyphs
# (box art, the help/mail icons). See tools/api/menu_screen_tool.py.
MENU_GLYPHS = {
    0x00: " ", 0x01: "+", 0x02: "-", 0x03: "+", 0x04: "|", 0x05: "+", 0x06: "+",
    0x07: "+", 0x08: "+", 0x09: "+", 0x0A: "+", 0x0B: "#", 0x0C: "+", 0x0D: "+",
    0x0E: "+", 0x0F: "+", 0x10: "a", 0x11: "b", 0x12: "^", 0x13: "*",
}


@dataclass
class Snapshot:
    """One rendered UI screen, independent of which transport produced it."""

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
            f"  {expected!r}\nactual:\n{self.text()}"
        )

    def find_line_matching(self, pattern: "re.Pattern") -> int:
        for index, line in enumerate(self.lines):
            if pattern.search(line):
                return index
        raise Failure(
            f"Snapshot mismatch after {self.last_command}: no line matched {pattern.pattern!r}\n{self.text()}"
        )


class Backend:
    """Keyboard injection + screen reading for one on-device UI session.

    Subclasses implement capture/send_key/send_char; send_text and
    send_key_repeat have batching default implementations that subclasses may
    override for a faster transport-specific path (REST batches into one HTTP
    request; Telnet writes one socket buffer).
    """

    def capture(self) -> Snapshot:
        raise NotImplementedError

    def send_key(self, key: str) -> Snapshot:
        raise NotImplementedError

    def send_char(self, ch: str) -> Snapshot:
        raise NotImplementedError

    def send_text(self, text: str, label: str) -> Snapshot:
        snapshot = self.capture()
        for ch in text:
            snapshot = self.send_char(ch)
        snapshot.last_command = label
        return snapshot

    def send_key_repeat(self, key: str, count: int) -> Snapshot:
        snapshot = self.capture()
        for _ in range(count):
            snapshot = self.send_key(key)
        return snapshot

    def close(self) -> None:
        pass


# ---------------------------------------------------------------------------
# REST backend: drives the on-device Overlay/Freeze UI.
# ---------------------------------------------------------------------------

# Symbolic action -> C64 keyboard matrix combo (software/api/input_api.h
# INPUT_API_KEYBOARD_MAP is the authoritative name list). Two physical keys
# (cursor_up_down, cursor_left_right) carry both directions, reversed by
# shift; PGUP/PGDN reuse the F1/F7 remap every UI context applies
# (UserInterface::keymapper in software/userinterface/userinterface.cc);
# ESC reuses the CBM-X "cancel" convention the monitor and menu share.
KEY_ALIASES: Dict[str, List[str]] = {
    "UP": ["left_shift", "cursor_up_down"],
    "DOWN": ["cursor_up_down"],
    "LEFT": ["left_shift", "cursor_left_right"],
    "RIGHT": ["cursor_left_right"],
    "PGUP": ["f1"],
    "PGDN": ["f7"],
    "F5": ["f5"],
    "ENTER": ["return"],
    # The monitor's edit modes accept either KEY_ESCAPE or KEY_BREAK to leave
    # (software/monitor/machine_monitor.cc). KEY_ESCAPE only exists on a real
    # USB keyboard's Escape key (software/io/usb/keyboard_usb.cc), which the
    # C64 matrix REST injects has no equivalent for; KEY_BREAK is RUN/STOP,
    # which the matrix does have (software/io/c64/keyboard.h; keymap_normal
    # row 7 col 7).
    "ESC": ["run_stop"],
    "DEL": ["inst_del"],
    "BACKSPACE": ["inst_del"],
    "RUNSTOP": ["run_stop"],
    "CTRL_B": ["ctrl", "b"],
    "CTRL_E": ["ctrl", "e"],
    "CTRL_O": ["ctrl", "o"],
    "CBM_B": ["commodore", "b"],
    "CBM_1": ["commodore", "1"],
}

# A letter key alone types uppercase in the firmware's default character set;
# shift produces lowercase (software/io/usb/keyboard_usb.cc keymap_normal /
# keymap_shifted). Punctuation follows the same tables: unshifted symbol keys
# map directly, the shifted digit row gives the usual '!"#$%&'()'.
_DIRECT_CHARS: Dict[str, List[str]] = {
    " ": ["space"], ":": ["colon"], ",": ["comma"], ".": ["period"],
    "+": ["plus"], "-": ["minus"], "=": ["equals"], "/": ["slash"],
    "@": ["at"], ";": ["semicolon"], "*": ["star"], "\\": ["arrow_up"],
    "\r": ["return"], "\b": ["inst_del"],
}
_SHIFTED_DIGIT_CHARS: Dict[str, str] = {
    "!": "1", '"': "2", "#": "3", "$": "4", "%": "5", "&": "6",
    "'": "7", "(": "8", ")": "9",
}


def char_to_combo(ch: str) -> List[str]:
    if ch in _DIRECT_CHARS:
        return _DIRECT_CHARS[ch]
    if ch in _SHIFTED_DIGIT_CHARS:
        return ["left_shift", _SHIFTED_DIGIT_CHARS[ch]]
    if ch.isalpha() and ch.isascii():
        return ["left_shift", ch.lower()] if ch.isupper() else [ch.lower()]
    if ch.isdigit() and ch.isascii():
        return [ch]
    raise Failure(f"No REST keyboard mapping for character {ch!r}")


class RestBackend(Backend):
    """Drives the physical/HDMI UI over REST: machine:input + machine:menu_screen.

    Opens the on-device menu if it is not already open and switches Interface
    Type to Overlay for the duration (restored on close), matching how
    tests/e2e/io/c64/freeze_menu_test.py captures and restores the same
    setting for Freeze.
    """

    def __init__(
        self,
        host: str,
        password: Optional[str] = None,
        timeout: float = 5.0,
        interface_type: Optional[str] = OVERLAY_MODE,
    ) -> None:
        self.host = host
        self.password = password
        self.timeout = timeout
        self.last_command = "<connect>"
        self._original_interface_type: Optional[str] = None
        if interface_type is not None:
            current = self.get_config(UI_STORE, UI_ITEM)
            if current != interface_type:
                self._original_interface_type = current
                self.set_config(UI_STORE, UI_ITEM, interface_type)
                # Matches MENU_TOGGLE_SETTLE_SECONDS in freeze_menu_test.py: a
                # config change needs a moment to take effect before the menu
                # is opened, or the very next interaction can land mid-switch.
                time.sleep(0.25)
        self._open_menu()

    # -- transport --
    def _url(self, path: str, params: Optional[Dict[str, object]] = None) -> str:
        query = "?" + urllib.parse.urlencode(params) if params else ""
        return f"http://{self.host}{path}{query}"

    def _request(
        self, method: str, path: str,
        params: Optional[Dict[str, object]] = None,
        payload: Optional[Dict[str, object]] = None,
    ) -> Tuple[int, bytes]:
        body = None if payload is None else json.dumps(payload).encode("utf-8")
        headers: Dict[str, str] = {}
        if self.password:
            headers["X-Password"] = self.password
        if body is not None:
            headers["Content-Type"] = "application/json"
        request = urllib.request.Request(self._url(path, params), data=body, headers=headers, method=method)
        # The device serves few concurrent HTTP connections. GET is retried
        # since an HTTP status is a real answer either way; POST/PUT are not,
        # since resending keyboard input could apply it twice.
        attempts = TRANSPORT_RETRIES if method == "GET" else 1
        last_exc: Optional[BaseException] = None
        for attempt in range(attempts):
            try:
                with urllib.request.urlopen(request, timeout=self.timeout) as response:
                    return response.status, response.read()
            except urllib.error.HTTPError as exc:
                return exc.code, exc.read()
            except (OSError, TimeoutError, urllib.error.URLError) as exc:
                last_exc = exc
                if attempt + 1 < attempts:
                    time.sleep(TRANSPORT_RETRY_PAUSE_SECONDS)
        raise Failure(f"{method} {self._url(path, params)} failed: {last_exc}")

    # -- config --
    def get_config(self, store: str, item: str) -> str:
        status, body = self._request("GET", f"{CONFIGS_PATH}/{urllib.parse.quote(store)}/{urllib.parse.quote(item)}")
        if status != 200:
            raise Failure(f"reading '{item}' failed with HTTP {status}: {body[:160]!r}")
        data = json.loads(body)
        entry = data.get(store, {})
        if isinstance(entry, dict):
            entry = entry.get(item, {})
        current = entry.get("current") if isinstance(entry, dict) else None
        if not isinstance(current, str):
            raise Failure(f"config '{item}' has no string 'current': {data!r}")
        return current

    def set_config(self, store: str, item: str, value: str) -> None:
        status, body = self._request(
            "PUT", f"{CONFIGS_PATH}/{urllib.parse.quote(store)}/{urllib.parse.quote(item)}",
            params={"value": value},
        )
        if status != 200:
            raise Failure(f"setting '{item}' to '{value}' failed with HTTP {status}: {body[:160]!r}")

    # -- menu open/close --
    def _menu_screen_body(self) -> Optional[bytes]:
        status, body = self._request("GET", MENU_SCREEN_PATH)
        if status == 404:
            return None
        if status != 200:
            raise Failure(f"menu_screen failed with HTTP {status}: {body[:160]!r}")
        if len(body) != SCREEN_BYTES:
            raise Failure(f"menu_screen returned {len(body)} bytes, expected {SCREEN_BYTES}")
        return body

    def _menu_open(self) -> bool:
        return self._menu_screen_body() is not None

    def _open_menu(self) -> None:
        if self._menu_open():
            return
        status, body = self._request("PUT", MENU_BUTTON_PATH)
        if status != 200:
            raise Failure(f"menu_button failed with HTTP {status}: {body[:160]!r}")
        deadline = time.monotonic() + SETTLE_TIMEOUT_SECONDS
        while time.monotonic() < deadline:
            if self._menu_open():
                return
            time.sleep(POLL_INTERVAL_SECONDS)
        raise Failure("the on-device menu did not open")

    def _close_menu(self) -> None:
        if not self._menu_open():
            return
        self._request("PUT", MENU_BUTTON_PATH)
        deadline = time.monotonic() + SETTLE_TIMEOUT_SECONDS
        while time.monotonic() < deadline:
            if not self._menu_open():
                return
            time.sleep(POLL_INTERVAL_SECONDS)
        # Best effort: run-e2e-tests' own ui_state gate recovers a dirty menu
        # before the next suite starts.

    # -- decode --
    def _decode(self, body: bytes) -> Snapshot:
        chars = body[:SCREEN_CELLS]
        lines = []
        reverse_cells: List[Tuple[int, int]] = []
        for row in range(SCREEN_HEIGHT):
            cells = []
            for col in range(SCREEN_WIDTH):
                code = chars[row * SCREEN_WIDTH + col]
                base = code & 0x7F
                cells.append(chr(base) if 0x20 <= base <= 0x7E else MENU_GLYPHS.get(base, "?"))
                if code & 0x80:
                    reverse_cells.append((col, row))
            lines.append("".join(cells))
        return Snapshot(lines, reverse_cells, self.last_command)

    def capture(self) -> Snapshot:
        body = self._menu_screen_body()
        if body is None:
            raise Failure(f"menu screen unavailable after {self.last_command}")
        return self._decode(body)

    # -- input --
    def _post_events(self, events: List[dict]) -> None:
        for start in range(0, len(events), INPUT_MAX_EVENTS):
            batch = events[start:start + INPUT_MAX_EVENTS]
            status, body = self._request("POST", INPUT_PATH, payload={"events": batch})
            if status != 200:
                raise Failure(f"machine:input failed with HTTP {status}: {body[:160]!r}")

    def _settle(self, before: Optional[bytes]) -> Snapshot:
        # A batch is accepted by REST immediately but drains through the C64
        # matrix over time (see tests/e2e/lib/menu.py), so the first poll or
        # two can land before the firmware has started applying it -- reading
        # the still-unchanged "before" screen as already "stable" and
        # returning before the keypress took visible effect. Waiting for a
        # change first (best-effort: a genuine no-op keypress never changes
        # the screen, so this can legitimately time out) avoids that false
        # settle; wait_screen_settled below still catches multi-frame
        # redraws once a change has started.
        wait_screen_changes(self._menu_screen_body, before, timeout=SETTLE_TIMEOUT_SECONDS)
        wait_screen_settled(self._menu_screen_body, timeout=SETTLE_TIMEOUT_SECONDS)
        return self.capture()

    def send_combo(self, matrix_keys: Sequence[str]) -> Snapshot:
        before = self._menu_screen_body()
        self._post_events([{"kind": "keyboard", "inputs": list(matrix_keys), "transition": "tap"}])
        return self._settle(before)

    def send_key(self, key: str) -> Snapshot:
        combo = KEY_ALIASES.get(key)
        if combo is None:
            raise Failure(f"Unknown key alias {key!r} for RestBackend")
        self.last_command = key
        return self.send_combo(combo)

    def send_char(self, ch: str) -> Snapshot:
        self.last_command = ch
        return self.send_combo(char_to_combo(ch))

    def send_text(self, text: str, label: str) -> Snapshot:
        self.last_command = label
        before = self._menu_screen_body()
        events = [{"kind": "keyboard", "inputs": char_to_combo(ch), "transition": "tap"} for ch in text]
        self._post_events(events)
        return self._settle(before)

    def send_key_repeat(self, key: str, count: int) -> Snapshot:
        combo = KEY_ALIASES.get(key)
        if combo is None:
            raise Failure(f"Unknown key alias {key!r} for RestBackend")
        self.last_command = f"{key} x{count}"
        before = self._menu_screen_body()
        events = [{"kind": "keyboard", "inputs": list(combo), "transition": "tap"} for _ in range(count)]
        self._post_events(events)
        return self._settle(before)

    def close(self) -> None:
        try:
            self._close_menu()
        except Failure:
            pass
        if self._original_interface_type is not None:
            try:
                self.set_config(UI_STORE, UI_ITEM, self._original_interface_type)
            except Failure:
                pass


# ---------------------------------------------------------------------------
# Telnet backend: raw VT100 remote-menu session.
# ---------------------------------------------------------------------------

WIDTH = 40
HEIGHT = 24  # the Telnet remote session only ever fills 24 of the 25 physical rows

ALT_CHARSET_MAP = {
    "l": "+", "k": "+", "m": "+", "j": "+", "q": "-", "x": "|",
    "t": "+", "u": "+", "v": "+", "w": "+", "n": "+",
}

TELNET_KEY_BYTES: Dict[str, bytes] = {
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


class TelnetBackend(Backend):
    def __init__(self, host: str, port: int, password: Optional[str] = None, timeout: float = 5.0) -> None:
        self.sock = self._connect_with_retry(host, port, timeout)
        self.sock.setblocking(False)
        self.timeout = timeout
        self.screen = VT100Screen()
        self.last_command = "<connect>"
        self._last_drain_bytes = 0
        self._drain_until_idle(timeout=timeout)
        if self.screen.saw_password_prompt():
            if password is None:
                raise Failure("Telnet password prompt received but no password was provided")
            self.send_text(password + "\r", "password")

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

    def close(self) -> None:
        try:
            self.sock.close()
        except OSError:
            pass

    def capture(self) -> Snapshot:
        self._drain_until_idle(timeout=self.timeout)
        return self.screen.snapshot(self.last_command)

    def send_key(self, key: str) -> Snapshot:
        payload = TELNET_KEY_BYTES.get(key)
        if payload is None:
            raise Failure(f"Unknown key alias {key!r} for TelnetBackend")
        self.last_command = key
        self.sock.sendall(payload)
        return self.capture()

    def send_key_count(self, key: str) -> Tuple[Snapshot, int]:
        """Send a key and return (snapshot, bytes_received_during_redraw).

        Used to measure per-keystroke output volume, so a flood-on-scroll
        regression (full-screen redraw per keystroke on telnet) is observable.
        Telnet-only: REST reads are a fixed-size snapshot regardless of what
        changed, so this metric has no REST equivalent."""
        payload = TELNET_KEY_BYTES.get(key)
        if payload is None:
            raise Failure(f"Unknown key alias {key!r} for TelnetBackend")
        self.last_command = key
        self.sock.sendall(payload)
        self._last_drain_bytes = 0
        self._drain_until_idle(timeout=self.timeout)
        return self.screen.snapshot(self.last_command), self._last_drain_bytes

    def send_char(self, ch: str) -> Snapshot:
        self.last_command = ch
        self.sock.sendall(ch.encode("ascii"))
        return self.capture()

    def send_text(self, text: str, label: str) -> Snapshot:
        self.last_command = label
        self.sock.sendall(text.encode("ascii"))
        return self.capture()

    def send_key_repeat(self, key: str, count: int) -> Snapshot:
        payload = TELNET_KEY_BYTES.get(key)
        if payload is None:
            raise Failure(f"Unknown key alias {key!r} for TelnetBackend")
        self.last_command = f"{key} x{count}"
        self.sock.sendall(payload * count)
        return self.capture()

    def _drain_until_idle(self, timeout: float) -> None:
        end = time.time() + timeout
        last_data = time.time()
        drained = 0
        while time.time() < end:
            wait = min(0.5, max(0.0, end - time.time()))
            ready, _, _ = select.select([self.sock], [], [], wait)
            if not ready:
                if time.time() - last_data >= 0.5:
                    self._last_drain_bytes = drained
                    return
                continue
            chunk = self.sock.recv(65536)
            if not chunk:
                self._last_drain_bytes = drained
                return
            drained += len(chunk)
            self.screen.feed(chunk)
            last_data = time.time()
        raise Failure(f"Timed out waiting for telnet screen to go idle after {self.last_command}")
