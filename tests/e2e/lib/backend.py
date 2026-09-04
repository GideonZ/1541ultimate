#!/usr/bin/env python3
# The screen a suite reads, however it is being read.

"""The screen a suite reads, however it is being read.

`Backend` is the interface the two transports implement and
`Browser` drives; `Snapshot`, `Window` and `RowMark` are what a read
returns; and the functions here are the parsing rules that decide
which row carries the cursor. The geometry and the paths every
transport shares are here too.

ui_backend.py was 2,871 lines holding nine classes, so a fix for one
transport had to be read against the other two in the same file.
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
from dataclasses import dataclass
import json
import machine as machine_lib
import navigation as navigation_lib
import pacing
import re
import rest as rest_lib
import urllib.request


# Read over REST whatever is driving the UI, so these two live with the base
# class rather than with the REST transport: identity and the navigation
# setting belong to the device, and a Telnet session has no way to ask for
# either. Their routes are named here for the same reason.
INFO_PATH = "/v1/info"

CONFIGS_PATH = "/v1/configs"


SCREEN_WIDTH = 40

SCREEN_HEIGHT = 25

SCREEN_CELLS = SCREEN_WIDTH * SCREEN_HEIGHT

SCREEN_BYTES = SCREEN_CELLS * 2

# Enough to climb out of the deepest settings screen the launcher leads to and
# then descend one level; a screen that is neither the browser nor the
# launcher after this many steps is reported rather than looped on.
LAUNCHER_DESCENT_STEPS = 10

# How a run of events is split into requests: see api.input_batches, which
# applies both of the device's limits, the event count and the body size.
# How fast this facade drives the UI is not decided here; see tests/lib/pacing.py.
POLL_INTERVAL_SECONDS = pacing.POLL_INTERVAL_SECONDS

SETTLE_TIMEOUT_SECONDS = pacing.SETTLE_TIMEOUT_SECONDS

TRANSPORT_RETRIES = 3

TRANSPORT_RETRY_PAUSE_SECONDS = 0.5

# The three ways a suite can drive the on-device UI. "overlay" is the default:
# fastest of the three, and it does not pause the C64 the way Freeze does.
# Suites that only make sense in one mode (e.g. freeze-menu, which tests
# Freeze-specific firmware behaviour) do not need to expose --mode at all.
MODE_TELNET = "telnet"

MODE_FREEZE = "freeze"

MODE_OVERLAY = "overlay"

MODES = (MODE_TELNET, MODE_FREEZE, MODE_OVERLAY)

DEFAULT_MODE = MODE_OVERLAY

# The menu endpoint returns raw Screen_MemMappedCharMatrix bytes: text is
# stored as literal printable characters, low values are firmware UI glyphs
# (box art, the help/mail icons). See tools/api/menu_screen_tool.py.
MENU_GLYPHS = {
    0x00: " ", 0x01: "+", 0x02: "-", 0x03: "+", 0x04: "|", 0x05: "+", 0x06: "+",
    0x07: "+", 0x08: "+", 0x09: "+", 0x0A: "+", 0x0B: "#", 0x0C: "+", 0x0D: "+",
    0x0E: "+", 0x0F: "+", 0x10: "a", 0x11: "b", 0x12: "^", 0x13: "*",
}

# The box/window frame characters MENU_GLYPHS decodes low control codes to.
# Which colour Screen_VT100::set_color emits for the cursor row is not fixed
# here: it belongs to the machine, and TelnetBackend._marked_row measures it.
FRAME_CHARS = " |+-"

# The rows a launcher's own entries can occupy: everything between its title
# and its status row. Used to read the cursor there, where the browser's own
# entry rows do not apply.
LAUNCHER_ENTRY_ROWS = range(2, SCREEN_HEIGHT - 1)

# find_selected_row's minimum marked-cell count before trusting a candidate
# row: below this, a row that merely borrows the previous row's background
# for a couple of cells is indistinguishable from noise.
SELECTED_ROW_MIN_MARKED_CELLS = 12


def fetch_product(host: str, password: str | None,
                  timeout: float) -> tuple[str, str]:
    """The `product` and `firmware_version` of a device, over plain REST.

    Free of any backend, because both transports need it and a Telnet session
    has no way to ask: identity is the device's, whatever is being driven.
    """
    headers = {"X-Password": password} if password else {}
    request = urllib.request.Request(
        rest_lib.url_for(host, INFO_PATH), headers=headers)
    try:
        with rest_lib.retrying_urlopen(request, timeout) as response:
            payload = json.loads(response.read().decode("utf-8"))
    except (OSError, ValueError, urllib.error.URLError) as exc:
        raise Failure(f"{INFO_PATH} on {host} failed: {exc}")
    # The firmware version comes back with it, and a skip reason names the
    # machine by both: "C64 Ultimate 1.2.0" says which release lacks a fix,
    # where the product alone would not.
    return str(payload.get("product", "")), str(payload.get("firmware_version", ""))


def fetch_navigation_style(host: str, password: str | None,
                           timeout: float) -> str:
    """The device's "Navigation Style" setting, over plain REST.

    Free of any backend for the same reason fetch_product is: the setting
    decides what a typed letter means on both transports, and a Telnet session
    has no way to ask for it.

    Answers "" where the device does not serve the item, which is a device
    old enough to predate the setting and behaves as Quick Search does. A
    device that cannot be reached at all raises, because guessing wrong here
    turns every typed letter in the menu into a cursor movement.
    """
    path = f"{CONFIGS_PATH}/{urllib.parse.quote(navigation_lib.CONFIG_CATEGORY)}" \
           f"/{urllib.parse.quote(navigation_lib.CONFIG_ITEM)}"
    headers = {"X-Password": password} if password else {}
    request = urllib.request.Request(rest_lib.url_for(host, path), headers=headers)
    try:
        with rest_lib.retrying_urlopen(request, timeout, idempotent=True) as response:
            payload = json.loads(response.read().decode("utf-8"))
    except urllib.error.HTTPError as exc:
        if exc.code == 404:
            return ""
        raise Failure(f"{path} on {host} failed: {exc}")
    except (OSError, ValueError, urllib.error.URLError) as exc:
        raise Failure(f"{path} on {host} failed: {exc}")
    entry = payload.get(navigation_lib.CONFIG_CATEGORY)
    if isinstance(entry, dict):
        entry = entry.get(navigation_lib.CONFIG_ITEM)
    if not isinstance(entry, dict):
        return ""
    current = entry.get("current")
    return current if isinstance(current, str) else ""


class NoCursorDrawn(Failure):
    """The screen carries no cursor marker, which a repaint can leave briefly."""


def strip_frame(text: str) -> str:
    return text.strip(FRAME_CHARS)


@dataclass
class Snapshot:
    """One rendered UI screen, independent of which transport produced it."""

    lines: list[str]
    reverse_cells: list[tuple[int, int]]
    last_command: str

    def line(self, index: int) -> str:
        return self.lines[index]

    def text(self) -> str:
        return "\n".join(self.lines)

    def find_line_containing(self, expected: str, ignore_case: bool = False) -> int:
        needle = expected.lower() if ignore_case else expected
        for index, line in enumerate(self.lines):
            if needle in (line.lower() if ignore_case else line):
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

    # Whether the last key sent changed the screen. A caller walking a listing
    # uses this to notice it has reached the end instead of pressing into a
    # wall for the rest of its step budget. It defaults to True so a transport
    # that cannot tell (Telnet reads a stream, not a frame) keeps the previous
    # behaviour of walking the full budget rather than stopping early.
    last_key_changed: bool = True

    def capture(self) -> Snapshot:
        raise NotImplementedError

    # `expect_redraw=False` says this key is known to change nothing on
    # screen. Only Telnet can tell the difference: it decides a redraw has
    # finished by watching the stream go quiet, so a key that draws nothing
    # would otherwise cost the whole first-byte budget waiting for one. REST
    # reads a whole screen either way and ignores it.
    def send_key(self, key: str, *, settle: bool = False,
                 expect_redraw: bool = True) -> Snapshot:
        raise NotImplementedError

    def send_char(self, ch: str, *, settle: bool = False,
                  expect_redraw: bool = True) -> Snapshot:
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

    # The key that empties a string edit field in one keystroke, or None where
    # the transport cannot produce one. Read by Browser.fill_edit_field, which
    # falls back to counted BACKSPACE taps.
    clear_field_key: str | None = None

    def send_key_sequence(self, keys: Sequence[str], label: str) -> Snapshot:
        """Several different keys as one batch, where the transport can.

        send_key_repeat already batches, but only one key repeated. Moving a
        cursor a given number of rows takes a mix -- page keys for the bulk of
        the distance and single steps for the remainder -- and sending that as
        two batches costs two round trips and two settles, which is most of
        what the page keys just saved.
        """
        snapshot = self.capture()
        for key in keys:
            snapshot = self.send_key(key)
        snapshot.last_command = label
        return snapshot

    def send_key_then_text(self, key: str, text: str, label: str) -> Snapshot:
        """One key followed by a string, as a single batch where possible.

        Used for the browser's quick-seek, where the leading key is what
        clears any search string left from a previous seek. Sending the two
        separately would settle twice and, worse, leave a window in which the
        reset has landed but the search has not.
        """
        self.send_key(key)
        return self.send_text(text, label)

    @property
    def machine(self) -> machine_lib.Machine:
        """Which machine this backend drives, asked once of the device.

        Three machines answer this API and their menus and keymaps differ, so
        a suite that has to allow for that asks here rather than being told on
        the command line. See tests/lib/machine.py.
        """
        return machine_lib.identify(self.machine_host, self._fetch_product)

    @property
    def navigation(self) -> navigation_lib.Navigation:
        """How this device's menu reads a typed letter, asked once of it.

        A setting rather than a property of the model, so it is read from the
        device's configuration and not derived from the product name. See
        tests/lib/navigation.py.
        """
        return navigation_lib.identify(self.machine_host, self._fetch_navigation_style)

    @property
    def machine_host(self) -> str:
        """The host that answers for the device under test."""
        raise NotImplementedError

    def _fetch_product(self) -> str:
        """The product string of `machine_host`, over REST on either transport."""
        return fetch_product(self.machine_host, self.machine_password, self.timeout)

    def _fetch_navigation_style(self) -> str:
        """The "Navigation Style" of `machine_host`, over REST on either transport."""
        return fetch_navigation_style(self.machine_host, self.machine_password,
                                      self.timeout)

    @property
    def machine_password(self) -> str | None:
        return None

    def enter_file_browser(self) -> None:
        """Land on the file browser if the UI is showing something above it.

        A no-op where the browser is the top of the UI stack, which is every
        machine but the C64 Ultimate. There the launcher sits above it, so
        backing out of the root directory leaves the browser entirely rather
        than doing nothing. See RestBackend.enter_file_browser.
        """

    def ensure_ready(self) -> None:
        """Make the UI reachable again if the last action tore it down.

        Under Freeze, a program that runs to completion (or forever, without
        hitting a BRK back into the monitor) unfreezes the C64 and closes the
        whole on-device menu (release_host() + release_ownership() in
        run_machine_monitor.cc), unlike Overlay where the C64 was never
        paused so the UI object stack survives. Callers that may need to
        re-enter the UI after such an action call this first; it is a no-op
        when nothing needs doing.
        """
        pass

    def selected_row(self, entry_rows: Sequence[int] | None = None) -> int:
        """Row index the on-device UI currently marks as selected/highlighted.

        The browser marks its cursor row by colour, not by the character
        matrix's reverse-video bit (tree_browser_state.cc), so this needs
        colour data capture()/Snapshot does not carry. Subclasses implement
        it from their own transport-native colour tracking. `entry_rows`
        restricts the scan to a known listing range when the caller knows
        the current screen's layout (required for TelnetBackend; optional,
        narrowing, for RestBackend).
        """
        raise NotImplementedError

    def selected_text(self, entry_rows: Sequence[int] | None = None) -> str:
        """Text of the selected row, read from a single screen capture.

        Both halves have to come from the same capture. Reading the rows and
        the cursor row from two separate fetches lets an asynchronous repaint
        land in between, so the text returned belongs to a screen other than
        the one the row index was measured on. Observed live on the root
        browser: selected_row() correctly reported row 2 while the separately
        fetched row 2 came back empty mid-redraw, which made a caller scanning
        for that entry miss an entry that was plainly present.
        """
        raise NotImplementedError

    def selection_and_rows(
        self, entry_rows: Sequence[int] | None = None
    ) -> tuple[int, list[str]]:
        """The cursor row and the whole screen, from one capture.

        Same rule as selected_text: a caller that wants to find an entry on
        screen and then work out how far the cursor is from it needs both
        halves to describe the same screen, or the distance it computes is
        against a screen that no longer exists.
        """
        raise NotImplementedError

    def close(self) -> None:
        pass


# Symbolic action -> C64 keyboard matrix combo (software/api/input_api.h
# INPUT_API_KEYBOARD_MAP is the authoritative name list). Two physical keys
# (cursor_up_down, cursor_left_right) carry both directions, reversed by
# shift; PGUP/PGDN reuse the F1/F7 remap every UI context applies
# (UserInterface::keymapper in software/userinterface/userinterface.cc).
KEY_ALIASES: dict[str, list[str]] = {
    "UP": ["left_shift", "cursor_up_down"],
    "DOWN": ["cursor_up_down"],
    "LEFT": ["left_shift", "cursor_left_right"],
    "RIGHT": ["cursor_left_right"],
    # The physical key, named for itself because what it does depends on the
    # machine: PGUP on an Ultimate 64 and an Ultimate II+, the task menu on a
    # C64 Ultimate. Callers ask Machine which key plays which role.
    "F1": ["f1"],
    "PGUP": ["f1"],
    "PGDN": ["f7"],
    "F5": ["f5"],
    # The C64 keyboard has no F8 key; the KERNAL produces KEY_F8 from Shift+F7
    # instead (software/io/c64/keyboard.h). Telnet's VT100 handler accepts the
    # xterm F8 escape code directly as the same discrete KEY_F8
    # (software/io/stream/keyboard_vt100.cc numeric[19]), so both transports
    # reach the identical firmware event under one alias.
    "F8": ["left_shift", "f7"],
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
    # Shift+CLR/HOME, which the C64 shifted keymap turns into KEY_CLEAR
    # (software/io/c64/keyboard_c64.cc, keymap_shifted row 6 col 3). A string
    # field empties its whole buffer on it in one keystroke
    # (software/userinterface/ui_elements.cc, case KEY_CLEAR), so it replaces
    # a run of BACKSPACE taps. Telnet has no way to send it: the VT100 decoder
    # has no sequence that produces KEY_CLEAR, which is why clearing a field
    # still has a BACKSPACE path.
    "CLEAR": ["left_shift", "clr_home"],
    "RUNSTOP": ["run_stop"],
    # The C64's top-left left-arrow key, which the monitor treats as Back
    # except where it is edit data. Deliberately its own action rather than an
    # alias for RUN/STOP: the two are the same Back only where the monitor
    # says so, and a test that cannot tell them apart cannot prove that.
    "ARROW_LEFT": ["arrow_left"],
    # The key the application key mapper turns into KEY_HELP.
    "F3": ["f3"],
    "COPY": ["ctrl", "c"],
    "PASTE": ["ctrl", "v"],
    "SELECT_ALL": ["ctrl", "a"],
    "SHIFT_DEL": ["left_shift", "inst_del"],
    "CTRL_B": ["ctrl", "b"],
    "CTRL_E": ["ctrl", "e"],
    "CTRL_O": ["ctrl", "o"],
    "CBM_B": ["commodore", "b"],
    "CBM_1": ["commodore", "1"],
    "CBM_9": ["commodore", "9"],
    # The monitor's reset shortcut, and the code the shortcut used to have.
    # Both resolve through Keyboard_C64's keymap_control
    # (software/io/c64/keyboard_c64.cc), which the REST menu route reads with
    # matrixToKeyCode: C=+R gives KEY_CTRL_R and C=+X gives $18, which is now
    # bound to nothing.
    "CBM_R": ["commodore", "r"],
    "CBM_X": ["commodore", "x"],
    # C=+I swaps the interface between the freeze menu and the HDMI overlay.
    # Unlike C=+R it is not the monitor's alone: the file browser and the
    # settings menu answer it too.
    "CBM_I": ["commodore", "i"],
}


# A letter key alone types uppercase in the firmware's default character set;
# shift produces lowercase (software/io/usb/keyboard_usb.cc keymap_normal /
# keymap_shifted). Punctuation follows the same tables: unshifted symbol keys
# map directly, the shifted digit row gives the usual '!"#$%&'()'.
_DIRECT_CHARS: dict[str, list[str]] = {
    " ": ["space"], ":": ["colon"], ",": ["comma"], ".": ["period"],
    "+": ["plus"], "-": ["minus"], "=": ["equals"], "/": ["slash"],
    "@": ["at"], ";": ["semicolon"], "*": ["star"], "\\": ["arrow_up"],
    "\r": ["return"], "\b": ["inst_del"],
}


_SHIFTED_DIGIT_CHARS: dict[str, str] = {
    "!": "1", '"': "2", "#": "3", "$": "4", "%": "5", "&": "6",
    "'": "7", "(": "8", ")": "9",
}


# Punctuation the C64 keyboard reaches by shifting a symbol key rather than a
# digit (software/io/c64/keyboard_c64.cc keymap_shifted).
_SHIFTED_SYMBOL_CHARS: dict[str, str] = {"?": "slash", "_": "pound", "^": "arrow_up"}


# Characters that are safe to type into an overlay to move its cursor.
# ContextMenu::handle_key sends anything from '!' upwards to seek_char, with two
# exceptions that matter here: KEY_SPACE selects the current item rather than
# seeking, and pattern_match treats '*' and '?' as wildcards. Restricting the
# seek prefix to letters and digits avoids all three.
_SEEKABLE_RE = re.compile(r"[A-Za-z0-9]")


def _seek_landing(labels: Sequence[str], prefix: str) -> int | None:
    """Where ContextMenu::perform_quick_seek puts the cursor for `prefix`.

    It appends '*' and scans from index 0, taking the first label that matches
    case-insensitively, so this is a plain case-insensitive prefix match on the
    first hit rather than a search forward from the current position.
    """
    lowered = prefix.lower()
    for index, label in enumerate(labels):
        if label.lower().startswith(lowered):
            return index
    return None


def plan_overlay_navigation(labels: Sequence[str], target: str,
                            start: int = 0) -> tuple[str, int]:
    """Fewest keystrokes to move an overlay's cursor from `start` to `target`.

    Returns (prefix, delta): type `prefix` one character at a time, then press
    DOWN `delta` times, or UP `-delta` times.

    The firmware accumulates typed characters into a seek string and jumps to
    the first label matching it, while any cursor key resets that string
    (ContextMenu::down, ::up). A plan is therefore always a run of characters
    followed by a run of cursor keys, never interleaved.

    Every prefix length is costed, including zero, and the cheapest wins. Zero
    is walking from where the cursor already is, which is what this replaces
    and still wins for a target that is one row away. A single character
    usually lands exactly, and where several items share a first letter the
    jump lands on the first of them and the walk covers the rest, which is
    still shorter than walking from the top.
    """
    index = list(labels).index(target)
    best = ("", index - start)
    best_cost = abs(index - start)
    prefix = ""
    for ch in target:
        if not _SEEKABLE_RE.fullmatch(ch):
            break
        prefix += ch
        landing = _seek_landing(labels, prefix)
        if landing is None:
            # Unreachable in practice: a label always matches its own prefix.
            break
        cost = len(prefix) + abs(index - landing)
        if cost < best_cost:
            best, best_cost = (prefix, index - landing), cost
    return best


def char_to_combo(ch: str) -> list[str]:
    if ch in _DIRECT_CHARS:
        return _DIRECT_CHARS[ch]
    if ch in _SHIFTED_DIGIT_CHARS:
        return ["left_shift", _SHIFTED_DIGIT_CHARS[ch]]
    if ch in _SHIFTED_SYMBOL_CHARS:
        return ["left_shift", _SHIFTED_SYMBOL_CHARS[ch]]
    if ch.isalpha() and ch.isascii():
        return ["left_shift", ch.lower()] if ch.isupper() else [ch.lower()]
    if ch.isdigit() and ch.isascii():
        return [ch]
    raise Failure(f"No REST keyboard mapping for character {ch!r}")


# The screen codes Window::draw_border writes for a frame (io/c64/screen.cc).
# They are screen codes, not the ASCII line-drawing characters a text-mode
# frame might suggest, so a frame is recognised by these rather than by
# punctuation.
#
# screen.h defines two sets of corners, sharp and rounded, and which one a
# machine uses is a matter of style: BORD_*_CORNER aliases the sharp set on an
# Ultimate 64 and an Ultimate II+, and the rounded set on a C64 Ultimate.
# Both are accepted, because the alternative is a frame parser that silently
# finds nothing on one of the three. The horizontal and vertical rules are the
# same everywhere.
BOX_HORIZONTAL = 0x02                    # CHR_HORIZONTAL_LINE


BOX_VERTICAL = 0x04                      # CHR_VERTICAL_LINE


BOX_TOP_LEFT = (0x01, 0x07)              # CHR_[ROUNDED_]LOWER_RIGHT_CORNER


BOX_TOP_RIGHT = (0x03, 0x09)             # CHR_[ROUNDED_]LOWER_LEFT_CORNER


BOX_BOTTOM_LEFT = (0x05, 0x0E)           # CHR_[ROUNDED_]UPPER_RIGHT_CORNER


BOX_BOTTOM_RIGHT = (0x06, 0x0F)          # CHR_[ROUNDED_]UPPER_LEFT_CORNER


@dataclass
class Window:
    """The area of the screen a caller's question is really about.

    A plain browser listing is the whole width; an open window is the area
    inside its frame. Columns matter as much as rows, because a window is not
    always drawn over the thing it covers: a context menu opened on a browser
    row is drawn beside that row's text, sharing screen rows with it.
    """

    rows: range
    first_column: int
    last_column: int          # exclusive

    @property
    def width(self) -> int:
        return self.last_column - self.first_column

    @property
    def min_marked_cells(self) -> int:
        """Cells a marking must cover before it counts as a highlight.

        Scaled to the window, because the fixed minimum is a statement about
        the full width of the screen. A context menu can be ten columns wide,
        so a highlight covering the whole of one of its items still falls
        short of that minimum and would be dismissed as noise.
        """
        return min(SELECTED_ROW_MIN_MARKED_CELLS, max(1, self.width // 2))


def whole_screen(rows: Sequence[int]) -> Window:
    """The window a screen with nothing open presents: every column of `rows`."""
    ordered = list(rows)
    return Window(range(ordered[0], ordered[-1] + 1) if ordered else range(0),
                  1, SCREEN_WIDTH - 1)


def find_open_window(chars: bytes, rows: Sequence[int]) -> Window:
    """The frontmost framed window on screen, or the whole screen if none.

    A context menu, the F5 task menu, a target picker and the New Host form
    are all drawn as a framed window over whatever was on screen already. Both
    the window and the browser underneath draw a cursor, so a scan of the
    whole screen has two answers and returns whichever it meets first.
    Restricting the scan to the window removes the ambiguity rather than
    trying to rank the two cursors against each other, and it is the right
    answer besides: while a window is open, what is behind it is not what a
    caller is asking about.

    Both dimensions are needed. Measured on an Ultimate II+L with a context
    menu opened on the Ftp row of the root browser: the menu occupied rows
    5 to 7 and columns 29 to 38, so the browser's own highlighted row was
    inside the menu's rows, and its 30 marked cells outweighed the 10 of the
    menu item on every read. Narrowing the columns as well leaves only the
    menu's own cells in view.

    A window that carries a title spends its first interior row on it, and a
    title is no more selectable than the frame around it. It is left out,
    because it cannot be told from a cursor row by colour: measured on an
    Ultimate II+L Select Path picker, the title "Select Path" was 38 cells of
    colour 6 and the cursor row "<< Select Current Dir >>" was 38 cells of
    colour 1, both spanning the whole inside width and each carrying a colour
    no other row had. What separates them is where the text starts. The
    firmware centres a title and left-aligns every listing row against the
    frame, so a first interior row not starting at the frame's first inside
    column is a title.

    The header rule the menu draws across row 1 is a run of BOX_HORIZONTAL
    with no corners, so it is not mistaken for a frame.
    """
    frames = _find_frames(chars)
    if not frames:
        return whole_screen(rows)
    # The smallest frame is the one drawn last and the one with the keyboard:
    # a dialog opened from a picker sits inside the picker's own frame, and
    # the picker is no longer what a key would move.
    top_row, bottom_row, left, right = min(
        frames, key=lambda frame: (frame[1] - frame[0]) * (frame[3] - frame[2]))
    interior = [r for r in rows if top_row < r < bottom_row]
    if interior and interior[0] == top_row + 1:
        first_inside = chars[interior[0] * SCREEN_WIDTH + left + 1]
        if (first_inside & 0x7F) in (0x00, 0x20):
            interior = interior[1:]
    if not interior:
        # A frame with nothing selectable between its rules carries no cursor,
        # so leave the caller reading the screen it was reading before.
        return whole_screen(rows)
    return Window(range(interior[0], interior[-1] + 1), left + 1, right)


def _find_frames(chars: bytes) -> list[tuple[int, int, int, int]]:
    """Every complete window frame on screen, as (top, bottom, left, right).

    Found from the bottom rule upward. The bottom corners are the two codes a
    frame always carries: measured on an Ultimate II+L, the context menu for a
    configured FTP host was drawn with BOX_HORIZONTAL where its top-left
    corner should be, so a search that began at the top-left corner found no
    frame there at all and the menu was invisible to every caller. The top
    rule is therefore only required to be a run of horizontals, with whatever
    the firmware chose to end it with.
    """
    frames = []
    for bottom_row in range(2, SCREEN_HEIGHT):
        line = chars[bottom_row * SCREEN_WIDTH:(bottom_row + 1) * SCREEN_WIDTH]
        for left in range(SCREEN_WIDTH - 2):
            if line[left] not in BOX_BOTTOM_LEFT:
                continue
            for right in range(left + 2, SCREEN_WIDTH):
                if line[right] not in BOX_BOTTOM_RIGHT:
                    continue
                if not all(code == BOX_HORIZONTAL for code in line[left + 1:right]):
                    break
                top_row = _frame_top(chars, bottom_row, left, right)
                if top_row is not None:
                    frames.append((top_row, bottom_row, left, right))
                break
    return frames


def _frame_top(chars: bytes, bottom_row: int, left: int,
               right: int) -> int | None:
    """The row carrying the top rule of the frame closed at `bottom_row`.

    The nearest row above whose whole span is frame characters. Walking up
    while the border cells are verticals is not enough: measured on an
    Ultimate II+L, a context menu drew BOX_TOP_RIGHT where the left border of
    its first item should have been, and a walk that insisted on a vertical
    there stopped one row early and found no rule. A row of window content
    always holds text, so a span that is entirely frame characters is the
    rule and nothing else is.
    """
    for row in range(bottom_row - 2, -1, -1):
        line = chars[row * SCREEN_WIDTH:(row + 1) * SCREEN_WIDTH]
        if all(code in BOX_TOP_LEFT or code in BOX_TOP_RIGHT
               or code == BOX_HORIZONTAL
               for code in line[left:right + 1]):
            return row
    return None


@dataclass
class RowMark:
    """How strongly one drawn row is marked, and by what."""

    background: int          # cells carrying the row's commonest background
    reverse: int             # cells with the reverse-video bit set
    colour: int              # the foreground colour most of the row's cells carry
    colour_cells: int        # how many cells carry it


def row_marks(chars: bytes, colours: bytes, window: Window) -> dict[int, RowMark]:
    """The marking of every drawn row of `window`, keyed by row index.

    Only the cells inside the window are counted. A context menu shares its
    screen rows with the browser row it was opened on, so counting the whole
    row would measure the browser's highlight rather than the menu's.

    Blank rows are left out. One can never be the selected entry, and every
    one of its cells carries whatever colour was last set, so it would
    otherwise win a cell count outright against real rows, whose name and
    status columns split their own counts between two colours. Observed live
    on the root browser: when a repaint leaves no row highlighted at all,
    blank row 8 beat the six real drive rows and was returned as the cursor
    row, so callers scanning for an entry compared against an empty string and
    missed an entry that was plainly present. TelnetBackend._marked_row
    applies the same rule.
    """
    marks: dict[int, RowMark] = {}
    for row in window.rows:
        first = row * SCREEN_WIDTH + window.first_column
        last = row * SCREEN_WIDTH + window.last_column
        row_chars = chars[first:last]
        row_colours = colours[first:last]
        if all((ch & 0x7F) in (0x00, 0x20) for ch in row_chars):
            continue
        background_counts: dict[int, int] = {}
        foreground_counts: dict[int, int] = {}
        reverse_count = 0
        for ch, colour_code in zip(row_chars, row_colours):
            if ch & 0x80:
                reverse_count += 1
            foreground = colour_code & 0x0F
            background = (colour_code >> 4) & 0x0F
            if background == 0:
                if foreground != 0x0F:
                    foreground_counts[foreground] = foreground_counts.get(foreground, 0) + 1
                continue
            background_counts[background] = background_counts.get(background, 0) + 1
        colour, colour_cells = -1, 0
        if foreground_counts:
            colour = max(foreground_counts, key=lambda code: foreground_counts[code])
            colour_cells = foreground_counts[colour]
        marks[row] = RowMark(
            background=max(background_counts.values()) if background_counts else 0,
            reverse=reverse_count,
            colour=colour,
            colour_cells=colour_cells,
        )
    return marks


def count_colour(colours: bytes, row: int, colour: int, window: Window) -> int:
    """Cells of `row` inside `window` whose foreground is `colour`."""
    start = row * SCREEN_WIDTH + window.first_column
    end = row * SCREEN_WIDTH + window.last_column
    return sum(1 for code in colours[start:end] if (code & 0x0F) == colour)


def find_cursor_colour(chars: bytes, colours: bytes,
                       window: Window) -> int | None:
    """The foreground colour this screen marks its cursor row with, if it says.

    A listing draws every unselected entry in one colour and the selected one
    in `UserInterface::color_sel` (tree_browser_state.cc draw_item), so on a
    screen showing three or more entries the cursor colour is the one exactly
    one row carries. Which colour that is depends on the configured colour
    scheme (userinterface.cc effectuate_settings), so it is measured here
    rather than assumed; a caller that keeps the answer can then read the
    cursor off a two-entry listing, where the colours tie and the screen alone
    cannot say which of them is the cursor.

    A window's title also carries a colour of its own, which would make every
    titled window ambiguous here. Titles never reach this function: they are
    left out of the rows a framed window contributes. See find_open_window.

    The odd row also has to be drawn like the rows it stands out from, because
    a screen that is not a listing at all can satisfy the rule above and teach
    a colour that is not a cursor's. A form is the case that matters, since
    the answer is kept for the session: measured on an Ultimate II+L Assembly
    64 query form, thirteen field rows carried 28 cells of one colour and the
    one button row below them 38 cells of another, which reads exactly like a
    listing whose cursor is on its last row. The colour of the button was
    learnt as the machine's cursor colour and every later read returned the
    button row. A listing draws its selected row to the same width as the
    rest, so requiring that rejects the button and leaves the colour to be
    learnt from a screen that really is a listing.

    Returns None when the screen cannot answer: when the machine marks the
    cursor with a background nibble instead (every Ultimate 64: color_sel_bg
    is only set under #if U64), and when no single row's colour is unique.
    """
    marks = row_marks(chars, colours, window)
    minimum = window.min_marked_cells
    if any(mark.background >= minimum for mark in marks.values()):
        return None
    tally: dict[int, int] = {}
    for mark in marks.values():
        tally[mark.colour] = tally.get(mark.colour, 0) + 1
    odd = [mark for mark in marks.values()
           if tally[mark.colour] == 1 and mark.colour_cells >= minimum]
    listing = [mark.colour_cells for mark in marks.values() if tally[mark.colour] > 1]
    if not listing:
        return None
    width = max(set(listing), key=listing.count)
    odd = [mark for mark in odd if mark.colour_cells == width]
    return odd[0].colour if len(odd) == 1 else None


def measure_cursor_colour(chars: bytes, colours: bytes,
                          rows: Sequence[int]) -> int | None:
    """The machine's cursor colour, measured on the rows it will be read from.

    find_cursor_colour asks which colour exactly one row carries, so it has to
    be asked about one listing at a time. A task menu drawn over the browser
    puts two listings on screen, each with a cursor and both cursors in the
    machine's colour, so asking across the whole screen finds that colour
    twice and gives up. Measured on an Ultimate II+L: a caller that opened the
    Assembly 64 form straight from the task menu never learnt a colour, and
    the form marks a ten-cell field rather than a row, which is too narrow for
    the rules that work without one, so the form's first field was never
    reachable.
    """
    return find_cursor_colour(chars, colours, find_open_window(chars, rows))


def find_selected_row_rest(chars: bytes, colours: bytes, rows: Sequence[int],
                           strict: bool = False,
                           cursor_colour: int | None = None) -> int:
    """Locate the cursor row from the raw menu_screen char/colour planes.

    The browser marks its cursor row with a distinct background colour
    (tree_browser_state.cc); some contexts instead use reverse video or a
    plain foreground colour. Try all three and trust whichever produced the
    strongest, most consistent signal across the row -- resilience over
    prescription, so this survives menu changes without pinning to exact
    colour codes.

    `cursor_colour` is the foreground colour the machine was measured to mark
    the cursor row with; see find_cursor_colour. It is what makes a two-entry
    listing readable on a machine whose menu_screen colour plane carries no
    background nibble: the selected row and the one unselected row then have
    the same number of coloured cells and each carries a colour no other row
    does, so counting cannot separate them and the odd-colour rule below has
    two answers. Measured on an Ultimate II+L: with the cursor on the second
    of two entries, this returned the first until the colour was supplied.

    When a framed window is open the scan is restricted to it; see
    find_open_window for why the two cursors on screen cannot be ranked
    against each other.
    """
    window = find_open_window(chars, rows)
    minimum = window.min_marked_cells
    marks = row_marks(chars, colours, window)
    best_background_row, best_background_count = -1, 0
    best_reverse_row, best_reverse_count = -1, 0
    best_foreground_row, best_foreground_count = -1, 0
    for row, mark in marks.items():
        if mark.background > best_background_count:
            best_background_count, best_background_row = mark.background, row
        if mark.reverse > best_reverse_count:
            best_reverse_count, best_reverse_row = mark.reverse, row
        if mark.colour_cells > best_foreground_count:
            best_foreground_count, best_foreground_row = mark.colour_cells, row

    if best_background_count >= minimum:
        return best_background_row
    if cursor_colour is not None:
        # Ahead of the reverse-video rule below, because this says what the
        # machine actually marks a cursor with and that rule only guesses.
        # The browser marks its cursor row by colour, not by the character
        # matrix's reverse-video bit (see Backend.selected_row), so a row in
        # reverse video is something else drawn that way. Measured on an
        # Ultimate II+L showing a D64: the volume header carried 28 reverse
        # cells and no cursor colour, the program row under the cursor
        # carried 28 cells of the cursor colour and no reverse cells, and the
        # reverse rule returned the volume header every time.
        #
        # How many cells carry the cursor colour, not whether it is the row's
        # commonest one. The marking does not always cover the whole row: in a
        # disk image listing the cursor colours the name field only, so the row
        # under the cursor is mostly some other colour. Measured on an Ultimate
        # II+L showing a D64 with one program, cursor on the program: the
        # volume row was 38 cells of colour 6, and the selected row was 16
        # cells of the cursor colour 1 and 22 cells of colour 7. Asking for the
        # commonest colour therefore skipped the selected row entirely and the
        # count-only fallback below picked the volume row.
        #
        # Only rows that row_marks kept, for the reason given there: a blank
        # row can never be the selected entry. It matters more here than
        # anywhere else, because a window paints its unused rows in the
        # colour the cursor uses. Measured on an Ultimate II+L Select Path
        # picker over an empty directory: the one entry and all eighteen
        # blank rows below it each carried 38 cells of the cursor colour, the
        # count tied, and the tie broke on the row number, so the cursor read
        # as the last blank row and the entry on screen was never selected.
        wearing = [(count_colour(colours, row, cursor_colour, window), row)
                   for row in window.rows if row in marks]
        wearing = [(count, row) for count, row in wearing if count]
        # One row and no other carrying the machine's cursor colour is a
        # stronger statement than any number of cells, so it is taken without
        # the minimum below. A form marks a field rather than a row: measured
        # on an Ultimate II+L Assembly 64 query form, the cursor coloured the
        # ten-cell value field of "Name:" and nothing else on the screen
        # carried that colour, which the minimum rejected as noise, so the
        # form's first field was never reachable.
        if len(wearing) == 1:
            return wearing[0][1]
        wearing = [(count, row) for count, row in wearing if count >= minimum]
        if wearing:
            return max(wearing)[1]
    if best_reverse_count >= minimum:
        return best_reverse_row
    if strict:
        # Only the foreground fallback is left, which cannot tell a real
        # selection from a screen whose cursor is not drawn yet. Say so, so the
        # caller can re-read rather than accept an arbitrary row.
        raise NoCursorDrawn("no row carries the browser's cursor colour")
    # A listing draws every unselected entry in one colour and the selected one
    # in another, so the cursor row is the odd colour out. Counting coloured
    # cells instead cannot tell them apart on a screen whose colour plane
    # carries no background nibble at all, which is what the Ultimate II+L's
    # menu_screen returns: every full-width entry row then has the same number
    # of coloured cells, the comparison above keeps the first one it saw, and
    # the answer is the top of the listing rather than the cursor.
    #
    # That is the display, not a gap in the API. Both machines draw the menu
    # into the same 40x25 char matrix, and menu_screen serves the firmware's
    # own copy of it, which has room for a background nibble either way
    # (software/io/c64/screen.cc, cell_colour_codes). On an Ultimate 64 the
    # FPGA renders that matrix as an overlay layer, which has a background per
    # cell, so userinterface.cc sets color_sel_bg. On an Ultimate II+ the same
    # bytes go to a real C64's colour RAM, which is four bits wide and carries
    # the foreground only; the background is one VIC register for the whole
    # screen. Reporting a background there would describe a colour nothing on
    # screen can show, so the II+ marks its cursor row with a distinct
    # foreground colour and this is what reads it.
    colour = find_cursor_colour(chars, colours, window)
    if colour is not None:
        for row, mark in marks.items():
            if mark.colour == colour:
                return row
    if best_foreground_count >= minimum:
        return best_foreground_row
    raise Failure("could not locate selected menu row from colour codes")
