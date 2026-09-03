#!/usr/bin/env python3
# Reading the menu over a Telnet session.

"""Reading the menu over a Telnet session.

The device renders to a terminal here rather than to the C64's
own screen, so `VT100Screen` keeps the emulated screen this
backend reads and the geometry is the session's, not 40x25.
"""

import sys
from pathlib import Path

# tests/lib holds the shared library; importing bootstrap adds tests/e2e/lib.
# The search walks up rather than counting directories, so this is the same in
# every entry point and a suite that moves needs no edit. See tests/lib/bootstrap.py.
sys.path.insert(0, str(next(p for p in Path(__file__).resolve().parents
                            if (p / "tests" / "lib").is_dir()) / "tests" / "lib"))
from report import Failure
from collections.abc import Sequence
import interactions
import pacing
import screens as screen_spool
import select
import socket
import time
from backend import (Backend, FRAME_CHARS, Snapshot, strip_frame)


# Screen_VT100 serves a 60x24 Telnet session, not the physical 40x25 display.
# The monitor's right-side header flags are otherwise truncated by this emulator.
WIDTH = 60


HEIGHT = 24  # Screen_VT100::get_size_y(); the 25th physical row is never used


# Where the browser draws its listing in a Telnet session, which is not where
# it draws it on the 40x25 display: one row fewer, because the session is one
# row shorter. Every suite that drives the browser over Telnet used to carry
# its own copy of these two numbers, and a suite that forgot them got the
# REST layout instead. That mattered once Browser.page_rows started reading
# the page stride off the listing height: 22 rows gives a stride of 11 and 21
# gives 10, and the firmware takes its own from the same window
# (TreeBrowser::handle_key, window->get_size_y()/2), so the wrong one puts
# every paged jump a row out per page key.
TELNET_ENTRY_ROWS = range(2, HEIGHT - 1)


TELNET_STATUS_ROW = HEIGHT - 1


ALT_CHARSET_MAP = {
    "l": "+", "k": "+", "m": "+", "j": "+", "q": "-", "x": "|",
    "t": "+", "u": "+", "v": "+", "w": "+", "n": "+",
}


TELNET_KEY_BYTES: dict[str, bytes] = {
    "UP": b"\x1b[A",
    "DOWN": b"\x1b[B",
    "RIGHT": b"\x1b[C",
    "LEFT": b"\x1b[D",
    "PGUP": b"\x1b[5~",
    "PGDN": b"\x1b[6~",
    # keyboard_vt100.cc getch() indexes its `numeric` table by the escape
    # value, so 11 is KEY_F1, 13 KEY_F3, 15 KEY_F5 and 19 KEY_F8. F1 is here
    # because a C64 Ultimate puts the task menu on it; see tests/lib/machine.py.
    "F1": b"\x1b[11~",
    "F5": b"\x1b[15~",
    "F3": b"\x1b[13~",
    "F8": b"\x1b[19~",
    "RUNSTOP": b"\x11",
    # Keyboard_C64 delivers the top-left left-arrow key as '`', and the VT100
    # driver passes a printable byte straight through, so the same monitor key
    # arrives over either transport (keyboard_c64.cc keymap_normal row 7 col 1;
    # keyboard_vt100.cc getch(), e_esc_idle case).
    "ARROW_LEFT": b"`",
    "CTRL_B": b"\x02",
    "CTRL_E": b"\x05",
    "CTRL_O": b"\x0f",
    "CBM_B": b"\x1bb",
    "CBM_1": b"\x1b1",
    "CBM_9": b"\x1b9",
    # One keystroke, the same as on a USB keyboard: Ctrl+R is the byte $12,
    # which keyboard_vt100.cc getch() maps to KEY_CTRL_R. It does not collide
    # with the cursor, because a terminal's down arrow arrives as ESC [ B and
    # is decoded separately; a bare $12 only comes from someone pressing
    # Ctrl+R.
    "CBM_R": b"\x12",
    # C=+X used to be the reset shortcut and its code, $18, is plain ASCII, so
    # the VT100 driver passes it through unchanged (getch(), e_esc_idle case).
    # No monitor handler claims it now.
    "CBM_X": b"\x18",
    # KEY_CTRL_I is $09, which the VT100 driver also passes through as itself.
    "CBM_I": b"\x09",
    # A bare ESC, as a terminal sends for the ESC key; the VT100 driver delivers
    # it after VT100_ESCAPE_ALONE_MS with nothing following (keyboard_vt100.cc
    # getch()). A trailing byte would now arrive as its own keystroke.
    "ESC": b"\x1b",
    "ENTER": b"\r",
    # DEL and BACKSPACE are the same physical key (KEY_ALIASES maps both to
    # the matrix's single "inst_del" key on REST); the VT100 driver passes
    # raw ASCII straight through (keyboard_vt100.cc getch(), e_esc_idle case)
    # rather than mapping DEL (0x7F) to that key itself, so 0x7F is simply
    # never recognised as a delete here -- confirmed live: repeated \x7f left
    # typed field content untouched, while \x08 deletes it correctly.
    "DEL": b"\x08",
    "BACKSPACE": b"\x08",
    # keyboard_vt100.cc: cursor keys, backspace and F5 as above; these four
    # follow the same VT100 conventions the browser accepts.
    "COPY": b"\x03",
    "PASTE": b"\x16",
    "SELECT_ALL": b"\x01",
    "SHIFT_DEL": b"\x1b[2~",
}


class VT100Screen:
    def __init__(self, width: int = WIDTH, height: int = HEIGHT) -> None:
        self.width = width
        self.height = height
        self.reset()

    def reset(self) -> None:
        self.lines = [[" "] * self.width for _ in range(self.height)]
        self.reverse = [[False] * self.width for _ in range(self.height)]
        # The browser marks its cursor row by colour, not by the character
        # matrix's reverse-video bit (tree_browser_state.cc), and
        # Screen_VT100::set_color emits that colour just before switching
        # reverse video off, so the reverse plane alone cannot tell which row
        # is selected. `colours` tracks the raw SGR parameter string in
        # effect when each cell was drawn, for callers that need it
        # (see selected_row()).
        self.colours = [[""] * self.width for _ in range(self.height)]
        self.sgr = ""
        self.x = 0
        self.y = 0
        self.reverse_mode = False
        self.alt_charset = False
        self._esc = False
        self._csi: str | None = None
        self._charset: str | None = None
        self._password_seen = False
        self._text_tail = ""

    def rows(self) -> list[str]:
        return ["".join(row) for row in self.lines]

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
            raw_values = [part for part in params.split(";") if part]
            if raw_values and raw_values not in (["7"], ["27"]):
                self.sgr = params
            values = [int(part) for part in raw_values] or [0]
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
                self.colours = [[""] * self.width for _ in range(self.height)]
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
        self.colours[self.y][self.x] = self.sgr
        self.x += 1
        if self.x >= self.width:
            self.x = self.width - 1


class TelnetBackend(Backend):
    def __init__(
        self, host: str, port: int, password: str | None = None, timeout: float = 5.0,
        width: int = WIDTH, height: int = HEIGHT,
    ) -> None:
        # Kept so this backend can ask the device what it is; see
        # Backend.machine. The identity is the device's, not the transport's.
        self.host = host
        self.password = password
        # The colour this machine's browser marks its cursor row with, once a
        # listing has shown it unambiguously; see _marked_row.
        self._selected_sgr: str | None = None
        self.sock = self._connect_with_retry(host, port, timeout)
        self.sock.setblocking(False)
        self.timeout = timeout
        self.screen = VT100Screen(width=width, height=height)
        self.last_command = "<connect>"
        self._last_drain_bytes = 0
        self._drain_until_idle(timeout=timeout)
        if self.screen.saw_password_prompt():
            if password is None:
                raise Failure("Telnet password prompt received but no password was provided")
            self.send_text(password + "\r", "password")

    @property
    def machine_host(self) -> str:
        return self.host

    @property
    def machine_password(self) -> str | None:
        return self.password

    @staticmethod
    def _connect_with_retry(host: str, port: int, timeout: float) -> socket.socket:
        deadline = time.monotonic() + max(timeout, 15.0)
        last_error: BaseException | None = None
        attempts = 0
        started = time.monotonic()
        while time.monotonic() < deadline:
            attempts += 1
            try:
                sock = socket.create_connection((host, port), timeout=timeout)
            except (OSError, TimeoutError) as exc:
                last_error = exc
                interactions.record(
                    "telnet", f"connect {host}:{port}",
                    ms=round((time.monotonic() - started) * 1000.0, 1),
                    attempts=attempts, fault=interactions.fault_of(exc),
                    error=str(exc), connection="new")
                time.sleep(0.5)
                continue
            # One session per suite run, so every send after this is on this
            # connection and a reader can tell a fault on a fresh connection
            # from one on a session that had been up for minutes.
            interactions.record(
                "telnet", f"connect {host}:{port}",
                ms=round((time.monotonic() - started) * 1000.0, 1),
                attempts=attempts, connection="new")
            return sock
        if last_error is not None:
            raise last_error
        raise TimeoutError(f"Timed out connecting to {host}:{port}")

    # What the last send put on the wire, held until the drain that follows
    # it completes the interaction record. See _send.
    _sent = None

    # Set by every send, cleared by the drain that follows it: it tells
    # _drain_until_idle whether a redraw is actually expected, so a bare
    # capture does not sit through the first-byte wait for one that was
    # never triggered.
    _expect_redraw = False

    # Committed prompts and selected keys have a U2+L echo burst before their
    # redraw. Require a longer quiet period for those commands only.
    _expect_settle = False

    def close(self) -> None:
        try:
            self.sock.close()
        except OSError:
            pass

    def capture(self) -> Snapshot:
        self._drain_until_idle(timeout=self.timeout)
        # A redraw that sent no bytes drew nothing, which is this transport's
        # answer to the question RestBackend._settle answers by comparing two
        # screens. Maintained on both so a caller can ask either one whether
        # the last key did anything; see Browser.go_to_top.
        self.last_key_changed = self._last_drain_bytes > 0
        return self.screen.snapshot(self.last_command)

    @property
    def selected_sgr(self) -> str | None:
        """The colour this machine marks a cursor row with, once measured.

        None until a listing has shown it. A caller that has to find the
        selection on a screen this reader does not handle - a form, where the
        marking covers one field rather than a row - asks for the colour here
        rather than pinning one machine's.
        """
        return self._selected_sgr

    def _content_sgr(self, row: int, text: str) -> str | None:
        """The colour the browser drew this row's first content cell in.

        The first cell is not always column zero: a C64 Ultimate draws its
        browser inside a frame, so column zero is the frame's own colour on
        every row and says nothing about the selection.
        """
        for column, character in enumerate(text):
            if character not in FRAME_CHARS:
                return self.screen.colours[row][column]
        return None

    def _marked_row(self, entry_rows: Sequence[int], rows: list[str]) -> int:
        """The cursor row, as the one row drawn in a colour of its own.

        The same rule the REST reader uses, for the same reason: a listing
        draws every unselected entry in one colour and the selected one in
        another, and which colours those are belongs to the machine. Pinning
        the cursor's own colour string worked on an Ultimate 64, whose browser
        marks the row `0;32;1` against `0;37;2`, and found nothing at all on a
        C64 Ultimate, which marks `0;37;1` against `0;31;2`.

        Learnt once and then kept, so a two-entry listing stays readable: the
        colours tie there and the screen alone cannot say which is the cursor.
        """
        drawn = {row: self._content_sgr(row, rows[row]) for row in entry_rows
                 if strip_frame(rows[row])}
        drawn = {row: sgr for row, sgr in drawn.items() if sgr is not None}
        tally: dict[str, int] = {}
        for sgr in drawn.values():
            tally[sgr] = tally.get(sgr, 0) + 1
        odd = [row for row, sgr in drawn.items() if tally[sgr] == 1]
        if len(odd) == 1:
            self._selected_sgr = drawn[odd[0]]
            return odd[0]
        if self._selected_sgr is not None:
            wearing = [row for row, sgr in drawn.items() if sgr == self._selected_sgr]
            if len(wearing) == 1:
                return wearing[0]
        raise Failure(
            f"Telnet: expected exactly one selected row among {list(entry_rows)}, "
            f"found {odd}; screen was:\n{chr(10).join(rows)}"
        )

    def selected_row(self, entry_rows: Sequence[int] | None = None) -> int:
        if entry_rows is None:
            raise Failure("TelnetBackend.selected_row requires entry_rows")
        self._drain_until_idle(timeout=self.timeout)
        return self._marked_row(entry_rows, self.screen.rows())

    def selected_text(self, entry_rows: Sequence[int] | None = None) -> str:
        if entry_rows is None:
            raise Failure("TelnetBackend.selected_text requires entry_rows")
        self._drain_until_idle(timeout=self.timeout)
        rows = self.screen.rows()
        return strip_frame(rows[self._marked_row(entry_rows, rows)])

    def selection_and_rows(
        self, entry_rows: Sequence[int] | None = None
    ) -> tuple[int, list[str]]:
        if entry_rows is None:
            raise Failure("TelnetBackend.selection_and_rows requires entry_rows")
        self._drain_until_idle(timeout=self.timeout)
        rows = self.screen.rows()
        return self._marked_row(entry_rows, rows), [row.rstrip() for row in rows]

    def send_key(self, key: str, *, settle: bool = False,
                 expect_redraw: bool = True) -> Snapshot:
        payload = TELNET_KEY_BYTES.get(key)
        if payload is None:
            raise Failure(f"Unknown key alias {key!r} for TelnetBackend")
        self.last_command = key
        self._expect_redraw = expect_redraw
        if settle:
            self._expect_settle = True
        self._send(payload, key)
        return self.capture()

    def send_key_count(self, key: str) -> tuple[Snapshot, int]:
        """Send a key and return (snapshot, bytes_received_during_redraw).

        Used to measure per-keystroke output volume, so a flood-on-scroll
        regression (full-screen redraw per keystroke on telnet) is observable.
        Telnet-only: REST reads are a fixed-size snapshot regardless of what
        changed, so this metric has no REST equivalent."""
        payload = TELNET_KEY_BYTES.get(key)
        if payload is None:
            raise Failure(f"Unknown key alias {key!r} for TelnetBackend")
        self.last_command = key
        self._expect_redraw = True
        self._send(payload, key)
        self._last_drain_bytes = 0
        self._drain_until_idle(timeout=self.timeout)
        return self.screen.snapshot(self.last_command), self._last_drain_bytes

    def send_char(self, ch: str, *, settle: bool = False,
                  expect_redraw: bool = True) -> Snapshot:
        # Some U2+L commands emit an echo burst, pause, then redraw. Keep the
        # longer quiet wait opt-in so ordinary keystrokes remain fast.
        self.last_command = ch
        self._expect_redraw = expect_redraw
        if settle:
            self._expect_settle = True
        self._send(ch.encode("ascii"), ch)
        return self.capture()

    def send_text(self, text: str, label: str) -> Snapshot:
        self.last_command = label
        self._expect_redraw = True
        self._expect_settle = True
        self._send(text.encode("ascii"), label)
        return self.capture()

    def send_key_then_text(self, key: str, text: str, label: str) -> Snapshot:
        payload = TELNET_KEY_BYTES.get(key)
        if payload is None:
            raise Failure(f"Unknown key alias {key!r} for TelnetBackend")
        self.last_command = label
        self._expect_redraw = True
        self._send(payload + text.encode("ascii"), label)
        return self.capture()

    def send_key_repeat(self, key: str, count: int) -> Snapshot:
        payload = TELNET_KEY_BYTES.get(key)
        if payload is None:
            raise Failure(f"Unknown key alias {key!r} for TelnetBackend")
        self.last_command = f"{key} x{count}"
        self._expect_redraw = True
        self._send(payload * count, f"{key} x{count}")
        return self.capture()

    def send_key_sequence(self, keys: Sequence[str], label: str) -> Snapshot:
        if not keys:
            return self.capture()
        payload = b""
        for key in keys:
            one = TELNET_KEY_BYTES.get(key)
            if one is None:
                raise Failure(f"Unknown key alias {key!r} for TelnetBackend")
            payload += one
        self.last_command = label
        self._expect_redraw = True
        self._send(payload, label)
        return self.capture()

    def _send(self, payload: bytes, what: str) -> None:
        """Write to the session, and remember what for the interaction log.

        One place, so a keystroke cannot leave this backend unrecorded. The
        record is completed by the drain that follows, because a Telnet
        exchange is what was sent and what came back and the two are one
        event.
        """
        self._sent = (what, payload, time.monotonic())
        self.sock.sendall(payload)

    def _record_exchange(self, drained: int) -> None:
        """One Telnet exchange: what was sent, and what the redraw returned."""
        sent = getattr(self, "_sent", None)
        self._sent = None
        if sent is None:
            interactions.record("telnet", "drain", received=drained)
            return
        what, payload, started = sent
        # `sent` and `received` are byte counts on every transport, and what
        # was sent goes in `payload`, which is where a REST request body goes
        # too. They held the payload text here, which is a second meaning for
        # a field name a reader and the recorder both take as a number: the
        # band's counters added it up, and one Telnet keystroke stopped a
        # recording 1168 frames into an 8850-frame run.
        interactions.record(
            "telnet", f"send {what}",
            payload=repr(payload)[1:] if payload else "",
            sent=len(payload), received=drained,
            connection="reused",
            ms=round((time.monotonic() - started) * 1000.0, 1))

    def _drain_until_idle(self, timeout: float) -> None:
        """Read until the redraw is over, or until it is clear none is coming.

        Two waits, not one. Before the first byte the question is "has the
        redraw started yet", and the answer has to allow for a device that is
        busy; after it, the question is "has it finished", and a redraw's own
        byte gaps are far shorter. One threshold for both was wrong in both
        directions: it returned a stale screen when a redraw took longer than
        the threshold to start, and it charged that same threshold to every
        capture once the redraw had plainly finished.

        The first-byte wait applies only where a redraw is genuinely expected.
        A send that draws nothing - a command prompt refusing an impossible
        character emits not one byte - passes expect_redraw=False and pays the
        short quiet check instead. See tests/lib/pacing.py for the measurements
        behind both numbers.
        """
        received: list[bytes] = []
        try:
            started = time.monotonic()
            end = started + timeout
            expecting = self._expect_redraw
            expecting_settle = self._expect_settle
            self._expect_redraw = False
            self._expect_settle = False
            first_wait = (pacing.TELNET_FIRST_BYTE_TIMEOUT_SECONDS if expecting
                          else pacing.TELNET_QUIET_CHECK_SECONDS)
            last_data: float | None = None
            drained = 0
            while time.monotonic() < end:
                wait = min(pacing.TELNET_IDLE_GAP_SECONDS, max(0.0, end - time.monotonic()))
                ready, _, _ = select.select([self.sock], [], [], wait)
                now = time.monotonic()
                if not ready:
                    if last_data is None:
                        if now - started >= first_wait:
                            self._last_drain_bytes = drained
                            return
                        continue
                    # A byte count cannot distinguish the echo from a redraw: both
                    # vary with screen content. For settled commands, wait for a
                    # longer quiet period, bounded by the caller's timeout.
                    idle_needed = (pacing.TELNET_SETTLE_GAP_SECONDS if expecting_settle
                                   else pacing.TELNET_IDLE_GAP_SECONDS)
                    if now - last_data >= idle_needed:
                        self._last_drain_bytes = drained
                        return
                    continue
                chunk = self.sock.recv(65536)
                if not chunk:
                    self._last_drain_bytes = drained
                    return
                drained += len(chunk)
                self.screen.feed(chunk)
                received.append(chunk)
                last_data = time.monotonic()
            # The caller's own timeout ran out. With no byte at all that is the
            # same answer the first-byte budget gives, and reporting it as a
            # failure would turn a key that legitimately drew nothing into a failed
            # suite whenever the budget outlived the timeout it sits inside. With
            # bytes received it is a redraw that never went quiet, which is a real
            # failure and says so.
            if last_data is None:
                self._last_drain_bytes = drained
                return
            self._last_drain_bytes = drained
            raise Failure(f"Timed out waiting for telnet screen to go idle after "
                          f"{self.last_command} ({drained} bytes received)")
        finally:
            # Out of the loop, not in it. Two file writes between a recv
            # and the next select would sit inside the wall-clock idle
            # test this loop decides on, and an observability component
            # may not change how a suite reaches a verdict. The bytes are
            # kept in memory and written once the redraw is over.
            # What the session shows now, so the next interaction says what
            # was on screen when it happened and two consecutive records show
            # the effect of whatever was between them.
            interactions.note_screen(str(tuple(self.screen.rows())))
            self._record_exchange(self._last_drain_bytes)
            screen_spool.publish_stream(b"".join(received))
            screen_spool.publish(
                screen_spool.TELNET, self.screen.rows(),
                cols=self.screen.width,
                key=(tuple(self.screen.rows()),
                     tuple(tuple(row) for row in self.screen.reverse)))
