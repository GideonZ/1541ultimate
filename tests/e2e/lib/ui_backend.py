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
import os
import re
import select
import socket
import sys
import time
import urllib.error
import urllib.parse
import urllib.request
from dataclasses import dataclass
from typing import Dict, List, Optional, Sequence, Tuple

# tests/lib holds the reporting library. Finding it here rather than
# relying on the caller keeps this module importable from a suite that
# only put tests/e2e/lib on sys.path.
sys.path.insert(0, os.path.join(
    os.path.dirname(os.path.abspath(__file__)), "..", "..", "lib"))

import api as api_lib
import interactions
import machine as machine_lib
import navigation as navigation_lib
import pacing
import rest as rest_lib
import screens as screen_spool
import targets
from report import Failure
from menu import wait_screen_changes, wait_screen_settled

SCREEN_WIDTH = 40
SCREEN_HEIGHT = 25
SCREEN_CELLS = SCREEN_WIDTH * SCREEN_HEIGHT
SCREEN_BYTES = SCREEN_CELLS * 2

MENU_SCREEN_PATH = "/v1/machine:menu_screen"
MENU_BUTTON_PATH = "/v1/machine:menu_button"
# Names the product, which is how a run discovers which machine it is aimed
# at; see tests/lib/machine.py.
INFO_PATH = "/v1/info"

# Enough to climb out of the deepest settings screen the launcher leads to and
# then descend one level; a screen that is neither the browser nor the
# launcher after this many steps is reported rather than looped on.
LAUNCHER_DESCENT_STEPS = 10
INPUT_PATH = "/v1/machine:input"
CONFIGS_PATH = "/v1/configs"
UI_STORE = "User Interface Settings"
UI_ITEM = "Interface Type"
OVERLAY_MODE = "Overlay on HDMI"

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

# A browser row's rendered size, as size_str.cc writes it: up to four digits
# and an optional K or M. Never a menu item, so a label that looks like this
# came from the listing an overlay was drawn over.
SIZE_COLUMN_RE = re.compile(r"\d{1,4}[KM]?")

# find_selected_row's minimum marked-cell count before trusting a candidate
# row: below this, a row that merely borrows the previous row's background
# for a couple of cells is indistinguishable from noise.
SELECTED_ROW_MIN_MARKED_CELLS = 12
# How many times to re-read the screen while no cursor marker is drawn at all.
CURSOR_SETTLE_ATTEMPTS = 4


def host_menu_open(host: str, password: Optional[str], timeout: float) -> bool:
    """Whether the on-device menu of one machine, named directly, is open.

    Takes a host rather than a target because the caller that needs it is
    asking about the *computer* half of a cartridge target, which the target's
    own routing would never send it to. A closed menu answers 404.
    """
    headers = {"X-Password": password} if password else {}
    request = urllib.request.Request(
        rest_lib.url_for(host, MENU_SCREEN_PATH), headers=headers)
    try:
        with rest_lib.retrying_urlopen(request, timeout) as response:
            response.read()
        return True
    except urllib.error.HTTPError as exc:
        if exc.code == 404:
            return False
        raise Failure(f"{MENU_SCREEN_PATH} on {host} failed: {exc}")
    except (OSError, urllib.error.URLError) as exc:
        raise Failure(f"{MENU_SCREEN_PATH} on {host} failed: {exc}")


def close_host_menu(host: str, password: Optional[str], timeout: float) -> None:
    """Shut the on-device menu of one machine, named directly.

    A cartridge target injects its keys into the computer, and the firmware
    decides what such an event means from whether the keyboard matrix is
    enabled (software/api/route_input.cc, isMatrixEnabled). While the
    computer's own menu is up the matrix is disabled, so the keys are pushed
    into that menu's queue instead of onto the matrix the cartridge taps. Every
    request still answers HTTP 200 and the cartridge never sees a keystroke, so
    this has to be checked rather than hoped for.
    """
    if not host_menu_open(host, password, timeout):
        return
    headers = {"X-Password": password} if password else {}
    request = urllib.request.Request(
        rest_lib.url_for(host, MENU_BUTTON_PATH), headers=headers,
        method="PUT")
    try:
        with rest_lib.retrying_urlopen(request, timeout) as response:
            response.read()
    except (OSError, urllib.error.URLError) as exc:
        raise Failure(f"{MENU_BUTTON_PATH} on {host} failed: {exc}")
    deadline = time.monotonic() + SETTLE_TIMEOUT_SECONDS
    while time.monotonic() < deadline:
        if not host_menu_open(host, password, timeout):
            return
        time.sleep(POLL_INTERVAL_SECONDS)
    raise Failure(
        f"the menu on {host} would not close, so keys sent to it would be "
        "consumed by that menu instead of reaching the cartridge")


def fetch_product(host: str, password: Optional[str],
                  timeout: float) -> Tuple[str, str]:
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


def fetch_navigation_style(host: str, password: Optional[str],
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

    lines: List[str]
    reverse_cells: List[Tuple[int, int]]
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
    def machine_password(self) -> Optional[str]:
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

    def selected_row(self, entry_rows: Optional[Sequence[int]] = None) -> int:
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

    def selected_text(self, entry_rows: Optional[Sequence[int]] = None) -> str:
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
        self, entry_rows: Optional[Sequence[int]] = None
    ) -> Tuple[int, List[str]]:
        """The cursor row and the whole screen, from one capture.

        Same rule as selected_text: a caller that wants to find an entry on
        screen and then work out how far the cursor is from it needs both
        halves to describe the same screen, or the distance it computes is
        against a screen that no longer exists.
        """
        raise NotImplementedError

    def close(self) -> None:
        pass


# ---------------------------------------------------------------------------
# REST backend: drives the on-device Overlay/Freeze UI.
# ---------------------------------------------------------------------------

# Symbolic action -> C64 keyboard matrix combo (software/api/input_api.h
# INPUT_API_KEYBOARD_MAP is the authoritative name list). Two physical keys
# (cursor_up_down, cursor_left_right) carry both directions, reversed by
# shift; PGUP/PGDN reuse the F1/F7 remap every UI context applies
# (UserInterface::keymapper in software/userinterface/userinterface.cc).
KEY_ALIASES: Dict[str, List[str]] = {
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
# Punctuation the C64 keyboard reaches by shifting a symbol key rather than a
# digit (software/io/c64/keyboard_c64.cc keymap_shifted).
_SHIFTED_SYMBOL_CHARS: Dict[str, str] = {"?": "slash", "_": "pound", "^": "arrow_up"}


# Characters that are safe to type into an overlay to move its cursor.
# ContextMenu::handle_key sends anything from '!' upwards to seek_char, with two
# exceptions that matter here: KEY_SPACE selects the current item rather than
# seeking, and pattern_match treats '*' and '?' as wildcards. Restricting the
# seek prefix to letters and digits avoids all three.
_SEEKABLE_RE = re.compile(r"[A-Za-z0-9]")


def _seek_landing(labels: Sequence[str], prefix: str) -> Optional[int]:
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
                            start: int = 0) -> Tuple[str, int]:
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


def char_to_combo(ch: str) -> List[str]:
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


def _find_frames(chars: bytes) -> List[Tuple[int, int, int, int]]:
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
               right: int) -> Optional[int]:
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


def row_marks(chars: bytes, colours: bytes, window: Window) -> Dict[int, RowMark]:
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
    marks: Dict[int, RowMark] = {}
    for row in window.rows:
        first = row * SCREEN_WIDTH + window.first_column
        last = row * SCREEN_WIDTH + window.last_column
        row_chars = chars[first:last]
        row_colours = colours[first:last]
        if all((ch & 0x7F) in (0x00, 0x20) for ch in row_chars):
            continue
        background_counts: Dict[int, int] = {}
        foreground_counts: Dict[int, int] = {}
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
                       window: Window) -> Optional[int]:
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
    tally: Dict[int, int] = {}
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
                          rows: Sequence[int]) -> Optional[int]:
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
                           cursor_colour: Optional[int] = None) -> int:
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


class RestBackend(Backend):
    """Drives the physical/HDMI UI over REST: machine:input + machine:menu_screen.

    Opens the on-device menu if it is not already open and switches Interface
    Type to Overlay for the duration (restored on close), matching how
    tests/e2e/io/c64/freeze_menu_test.py captures and restores the same
    setting for Freeze.

    `host` is a target, so a cartridge target such as "u2@c64u" reads the menu
    from the cartridge and injects keys into the computer it is plugged into.
    See tests/lib/targets.py; the cartridge answers machine:input with HTTP
    501, and the keys reach it over the expansion port instead.
    """

    def __init__(
        self,
        host: str,
        password: Optional[str] = None,
        timeout: float = 5.0,
        interface_type: Optional[str] = OVERLAY_MODE,
    ) -> None:
        self.target = targets.parse(host)
        self.host = self.target.device
        self.input_host = self.target.input_host
        self.password = password
        self.timeout = timeout
        self.last_command = "<connect>"
        # What one key of a batch costs on this target. A cartridge target
        # pays the computer's matrix tap rate rather than the device's own key
        # queue; see tests/lib/pacing.py.
        self.key_drain_seconds = pacing.key_drain_seconds(self.target.split)
        # The foreground colour this machine marks the cursor row with, once a
        # screen has shown it unambiguously. See find_cursor_colour.
        self._cursor_colour: Optional[int] = None
        self._original_interface_type: Optional[str] = None
        if interface_type is not None:
            # Asked of the device rather than assumed from its name: a
            # cartridge has one way of drawing its UI and does not offer the
            # setting at all, so there is nothing to switch and nothing to
            # restore. Overlay and Freeze then mean the same transport there.
            current = self.get_config_optional(UI_STORE, UI_ITEM)
            if current is not None and current != interface_type:
                # Change it only with the menu closed. Which UserInterface owns
                # the machine is decided when the menu opens, so switching the
                # setting under an open one leaves the firmware holding a
                # client that is no longer the active interface. A machine
                # reset then tears that stale one down and takes the device off
                # the network, needing a JTAG recovery. Reproduced directly:
                # toggling the type with the menu up and then resetting killed
                # the device within a few cycles, while the same open-and-reset
                # without the toggle survived every attempt.
                #
                # _close_menu is best effort, so the result is checked here
                # rather than assumed: writing the setting anyway would be the
                # exact sequence this is meant to avoid. A session that cannot
                # get the menu shut refuses to start instead.
                self._close_menu()
                if self._menu_open():
                    raise Failure("the on-device menu would not close, so the "
                                  f"Interface Type cannot be set to {interface_type!r}")
                self._original_interface_type = current
                self.set_config(UI_STORE, UI_ITEM, interface_type)
                # Matches MENU_TOGGLE_SETTLE_SECONDS in freeze_menu_test.py: a
                # config change needs a moment to take effect before the menu
                # is opened, or the very next interaction can land mid-switch.
                time.sleep(0.25)
        if self.target.split:
            # The computer's menu, not this cartridge's: see close_host_menu.
            # Done before the cartridge's menu is opened, because from here on
            # every key this session sends goes to the computer.
            close_host_menu(self.input_host, password, timeout)
        self._open_menu()

    # -- transport --
    def _url(self, path: str, params: Optional[Dict[str, object]] = None) -> str:
        # The handle says where the device is, ports included, so one builder
        # answers for every caller here.
        return rest_lib.url_for(self.target, path, params)

    @property
    def machine_host(self) -> str:
        return self.host

    @property
    def machine_password(self) -> Optional[str]:
        return self.password

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
        # Transport and retry policy come from tests/lib/rest.py; see
        # rest.may_retry for the rule and why there is only one copy of it.
        #
        # Retrying cannot hide a double application here: the callers read the
        # cursor or the resulting name back, so a duplicated keystroke fails
        # that check rather than passing unnoticed.
        try:
            with rest_lib.retrying_urlopen(request, self.timeout) as response:
                return response.status, response.read()
        except urllib.error.HTTPError as exc:
            return exc.code, exc.read()
        except (OSError, TimeoutError, urllib.error.URLError) as exc:
            raise Failure(f"{method} {self._url(path, params)} failed: {exc}")

    # -- config --
    def get_config_optional(self, store: str, item: str) -> Optional[str]:
        """The setting's value, or None when this device does not have it."""
        status, body = self._request(
            "GET", f"{CONFIGS_PATH}/{urllib.parse.quote(store)}/{urllib.parse.quote(item)}")
        if status != 200:
            raise Failure(f"reading '{item}' failed with HTTP {status}: {body[:160]!r}")
        entry = json.loads(body).get(store, {})
        if isinstance(entry, dict):
            entry = entry.get(item, {})
        current = entry.get("current") if isinstance(entry, dict) else None
        return current if isinstance(current, str) else None

    def get_config(self, store: str, item: str) -> str:
        current = self.get_config_optional(store, item)
        if current is None:
            raise Failure(f"config '{item}' has no string 'current' on {self.host}")
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
        # The screens this suite already fetched, spooled for whoever reads
        # the run afterwards. It costs the device nothing and is the only
        # record of what was on screen when a check failed. The decode is
        # behind the guard because it is the only part of this that costs
        # anything when nothing is spooling.
        if screen_spool.enabled():
            # The raw payload is what "a different screen" means: bit 7 of a
            # character byte is how the selected row is marked, and that does
            # not survive into the text.
            screen_spool.publish(screen_spool.MENU, self._decode(body).lines,
                                 body, cols=SCREEN_WIDTH, key=body)
        return body

    def _menu_open(self) -> bool:
        return self._menu_screen_body() is not None

    def ensure_ready(self) -> None:
        self._open_menu()

    def _open_menu(self) -> None:
        if self._menu_open():
            self.enter_file_browser()
            return
        status, body = self._request("PUT", MENU_BUTTON_PATH)
        if status != 200:
            raise Failure(f"menu_button failed with HTTP {status}: {body[:160]!r}")
        deadline = time.monotonic() + SETTLE_TIMEOUT_SECONDS
        while time.monotonic() < deadline:
            if self._menu_open():
                self.enter_file_browser()
                return
            time.sleep(POLL_INTERVAL_SECONDS)
        raise Failure("the on-device menu did not open")

    def _in_file_browser(self) -> bool:
        """Whether the screen showing is the file browser.

        The browser puts the directory it is showing on the status row and
        nothing else does, so a leading "/" is what identifies it. True on
        every machine; the rest of this method only ever runs where it can be
        false.
        """
        rows = self._decode(self._body()).lines
        return rows[SCREEN_HEIGHT - 1].lstrip().startswith("/")

    def enter_file_browser(self) -> None:
        """Leave the menu showing the file browser, whatever it opened on.

        A C64 Ultimate does not put the file browser behind the menu button.
        The button opens a launcher whose entries are the browser, the online
        search and the settings screens, and it reopens wherever it was last
        left, which can be several levels into those settings. Every suite
        expects the browser, so the descent belongs here rather than in each
        of them.

        The other two machines open the browser directly and this returns at
        once. Measured on a C64 Ultimate: RUN/STOP on the launcher closes the
        whole menu, so the way back up is the Back key.

        The cursor is moved onto the entry by reading which row it is on and
        which row the cursor is on, not by pressing Back further than the list
        is long. The launcher lists hardware actions, so a burst that
        under-delivers would leave RETURN to fire whichever of them the cursor
        stopped on.
        """
        entry = self.machine.launcher_browser_entry
        if entry is None:
            return
        for _ in range(LAUNCHER_DESCENT_STEPS):
            if self._in_file_browser():
                return
            cursor, rows = self.selection_and_rows(LAUNCHER_ENTRY_ROWS)
            row = next((n for n, text in enumerate(rows) if entry in text), None)
            if row is None:
                self.send_key("LEFT")
                continue
            if row != cursor:
                self.send_key_repeat("UP" if row < cursor else "DOWN",
                                     abs(row - cursor))
            landed, rows = self.selection_and_rows(LAUNCHER_ENTRY_ROWS)
            if landed != row:
                # A repaint between the two reads, or a key that did not
                # arrive. Neither is a reason to press RETURN on a launcher.
                continue
            self.send_key("ENTER")
        raise Failure(
            f"could not reach the file browser: no {entry!r} entry and no "
            f"directory on the status row after {LAUNCHER_DESCENT_STEPS} steps")

    def _close_menu(self) -> None:
        if not self._menu_open():
            return
        self._request("PUT", MENU_BUTTON_PATH)
        deadline = time.monotonic() + SETTLE_TIMEOUT_SECONDS
        while time.monotonic() < deadline:
            if not self._menu_open():
                return
            time.sleep(POLL_INTERVAL_SECONDS)
        # Best effort: run-tests' own ui_state gate recovers a dirty menu
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

    def _body(self) -> bytes:
        body = self._menu_screen_body()
        if body is None:
            raise Failure(f"menu screen unavailable after {self.last_command}")
        self._learn_cursor_colour(body, None)
        return body

    def _learn_cursor_colour(self, body: bytes,
                             entry_rows: Optional[Sequence[int]]) -> None:
        """Take the machine's cursor colour from this screen, if it teaches one.

        Every screen the backend fetches is offered, not just the ones a
        caller asks a row of, because a caller can reach a screen that needs
        the colour without ever having asked for a row on one that teaches it.
        Measured on an Ultimate II+L: a suite that opened the Assembly 64 form
        from the task menu asked for its first row on the form itself, and a
        form marks a ten-cell field, which no rule can find without the colour.
        The task menu it passed through would have taught it.

        Learnt once and then kept. The colour scheme belongs to the machine
        (userinterface.cc effectuate_settings), not to the screen, and a later
        screen can answer confidently and wrongly: a disk image listing draws
        its volume row in one colour across the full width, which is unique
        among the rows on that screen, so re-learning there adopts the volume
        colour and every read afterwards returns the volume row. Measured on
        an Ultimate II+L showing a D64 with one program.
        """
        if self._cursor_colour is not None:
            return
        rows = entry_rows if entry_rows is not None else range(2, SCREEN_HEIGHT - 1)
        measured = measure_cursor_colour(body[:SCREEN_CELLS], body[SCREEN_CELLS:], rows)
        if measured is not None:
            self._cursor_colour = measured

    def _selected_row_from_body(self, body: bytes, entry_rows: Optional[Sequence[int]],
                                strict: bool = False) -> int:
        chars = body[:SCREEN_CELLS]
        colours = body[SCREEN_CELLS:]
        rows = entry_rows if entry_rows is not None else range(2, SCREEN_HEIGHT - 1)
        # Offered again with the caller's own row range, which is narrower and
        # more likely to hold exactly one listing than the default above.
        self._learn_cursor_colour(body, entry_rows)
        return find_selected_row_rest(chars, colours, rows, strict, self._cursor_colour)

    def _settled_selection(self, entry_rows: Optional[Sequence[int]]) -> Tuple[bytes, int]:
        """One screen and its cursor row, once a cursor is actually drawn.

        A repaint can leave the browser with no cursor marker for a moment.
        The foreground fallback cannot tell that state from a real selection,
        so it returns an arbitrary entry row, and a caller scanning a listing
        then never matches the entry it is looking for even though the entry is
        on screen. Re-read a few times before accepting the weaker signal.
        """
        for attempt in range(CURSOR_SETTLE_ATTEMPTS):
            body = self._body()
            try:
                return body, self._selected_row_from_body(body, entry_rows, strict=True)
            except NoCursorDrawn:
                if attempt + 1 == CURSOR_SETTLE_ATTEMPTS:
                    return body, self._selected_row_from_body(body, entry_rows)
                time.sleep(POLL_INTERVAL_SECONDS)
        raise Failure("unreachable: the settle loop always returns or raises")

    def capture(self) -> Snapshot:
        return self._decode(self._body())

    def selected_row(self, entry_rows: Optional[Sequence[int]] = None) -> int:
        return self._settled_selection(entry_rows)[1]

    def selected_text(self, entry_rows: Optional[Sequence[int]] = None) -> str:
        body, index = self._settled_selection(entry_rows)
        return strip_frame(self._decode(body).lines[index].rstrip())

    def selection_and_rows(
        self, entry_rows: Optional[Sequence[int]] = None
    ) -> Tuple[int, List[str]]:
        body, index = self._settled_selection(entry_rows)
        return index, [line.rstrip() for line in self._decode(body).lines]

    # -- input --
    def _post_events(self, events: List[dict]) -> None:
        for batch in api_lib.input_batches(events):
            status, body = self._request("POST", INPUT_PATH, payload={"events": batch})
            if status != 200:
                raise Failure(f"machine:input failed with HTTP {status}: {body[:160]!r}")

    def _settle(self, before: Optional[bytes],
                change_timeout: Optional[float] = None,
                min_drain: float = 0.0) -> Snapshot:
        # A batch is accepted by REST immediately but drains through the C64
        # matrix over time (see tests/e2e/lib/menu.py), so the first poll or
        # two can land before the firmware has started applying it -- reading
        # the still-unchanged "before" screen as already "stable" and
        # returning before the keypress took visible effect. Waiting for a
        # change first (best-effort: a genuine no-op keypress never changes
        # the screen, so this can legitimately time out) avoids that false
        # settle; wait_screen_settled below still catches multi-frame
        # redraws once a change has started.
        #
        # The two waits have different jobs and so different budgets. This one
        # only has to cover the delay before the first changed pixel, so it
        # uses the much shorter KEY_CHANGE_TIMEOUT_SECONDS: it is the wait that
        # runs to full length on every keypress that cannot do anything, such
        # as DOWN on the last row of a listing. Sharing the settle timeout made
        # each of those cost 6 seconds.
        if change_timeout is None:
            change_timeout = pacing.KEY_CHANGE_TIMEOUT_SECONDS
        started = time.monotonic()
        self.last_key_changed, body = wait_screen_changes(
            self._menu_screen_body, before, timeout=change_timeout,
            min_samples=pacing.KEY_CHANGE_MIN_SAMPLES,
            hard_timeout=SETTLE_TIMEOUT_SECONDS)
        _, body = wait_screen_settled(self._menu_screen_body,
                                      timeout=SETTLE_TIMEOUT_SECONDS, known=body)
        # A batch is still draining through the matrix after the screen has
        # gone quiet once: a gap between two of its keystrokes looks exactly
        # like the end of it. Give the rest of the batch the time it needs to
        # arrive, then settle whatever did.
        remaining = min_drain - (time.monotonic() - started)
        if remaining > 0:
            time.sleep(remaining)
            _, body = wait_screen_settled(self._menu_screen_body,
                                          timeout=SETTLE_TIMEOUT_SECONDS)
        # The screen the settle stopped on is the screen this returns. Reading
        # it again would cost a further round trip to be told the same thing,
        # and the settle has just proved it is not changing. A menu that closed
        # under the caller leaves nothing to decode, which capture() reports as
        # the failure it is.
        if body is None:
            return self.capture()
        self._learn_cursor_colour(body, None)
        return self._decode(body)

    def send_combo(self, matrix_keys: Sequence[str]) -> Snapshot:
        before = self._menu_screen_body()
        self._post_events([{"kind": "keyboard", "inputs": list(matrix_keys), "transition": "tap"}])
        return self._settle(before)

    def send_key(self, key: str, *, settle: bool = False,
                 expect_redraw: bool = True) -> Snapshot:
        # `settle` and `expect_redraw` are accepted for interface parity with
        # TelnetBackend only; see send_char below.
        combo = KEY_ALIASES.get(key)
        if combo is None:
            raise Failure(f"Unknown key alias {key!r} for RestBackend")
        self.last_command = key
        return self.send_combo(combo)

    def send_char(self, ch: str, *, settle: bool = False,
                  expect_redraw: bool = True) -> Snapshot:
        # REST reads are a fixed-size settle regardless of what changed, so
        # neither the two-burst race `settle` guards against nor the
        # first-byte wait `expect_redraw` avoids applies here; both are
        # accepted for interface parity with TelnetBackend only.
        self.last_command = ch
        return self.send_combo(char_to_combo(ch))

    def send_text(self, text: str, label: str) -> Snapshot:
        self.last_command = label
        before = self._menu_screen_body()
        events = [{"kind": "keyboard", "inputs": char_to_combo(ch), "transition": "tap"} for ch in text]
        self._post_events(events)
        return self._settle(before, min_drain=len(events) * self.key_drain_seconds)

    def send_key_repeat(self, key: str, count: int) -> Snapshot:
        combo = KEY_ALIASES.get(key)
        if combo is None:
            raise Failure(f"Unknown key alias {key!r} for RestBackend")
        self.last_command = f"{key} x{count}"
        before = self._menu_screen_body()
        events = [{"kind": "keyboard", "inputs": list(combo), "transition": "tap"} for _ in range(count)]
        self._post_events(events)
        return self._settle(before, min_drain=count * self.key_drain_seconds)

    def send_key_then_text(self, key: str, text: str, label: str) -> Snapshot:
        combo = KEY_ALIASES.get(key)
        if combo is None:
            raise Failure(f"Unknown key alias {key!r} for RestBackend")
        self.last_command = label
        before = self._menu_screen_body()
        events = [{"kind": "keyboard", "inputs": list(combo), "transition": "tap"}]
        events += [{"kind": "keyboard", "inputs": char_to_combo(ch), "transition": "tap"}
                   for ch in text]
        self._post_events(events)
        # The seek's own short budget: its caller confirms the result by
        # reading the cursor back, so an early "nothing changed" here is free,
        # and a seek onto the entry already under the cursor changes nothing at
        # all. See pacing.SEEK_CHANGE_TIMEOUT_SECONDS.
        return self._settle(before, change_timeout=pacing.SEEK_CHANGE_TIMEOUT_SECONDS,
                            min_drain=len(events) * self.key_drain_seconds)

    def close(self) -> None:
        # Same rule on the way out as on the way in: the menu is closed before
        # the setting is put back, never while a session still owns the machine.
        # Teardown must not raise over the failure it is cleaning up after, so
        # a menu that will not close skips the restore rather than writing the
        # setting under an open one. What that leaves behind is the Interface
        # Type this session already set, which the next session reads as
        # current and does not toggle; run-tests' ui_state gate recovers the
        # menu itself before the next suite starts.
        closed = False
        try:
            self._close_menu()
            closed = not self._menu_open()
        except Failure:
            pass
        if closed and self._original_interface_type is not None:
            try:
                self.set_config(UI_STORE, UI_ITEM, self._original_interface_type)
            except Failure:
                pass


# ---------------------------------------------------------------------------
# Telnet backend: raw VT100 remote-menu session.
# ---------------------------------------------------------------------------

# Screen_VT100 serves a 60x24 Telnet session, not the physical 40x25 display.
# The monitor's right-side header flags are otherwise truncated by this emulator.
WIDTH = 60
HEIGHT = 24  # Screen_VT100::get_size_y(); the 25th physical row is never used

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
        self._csi: Optional[str] = None
        self._charset: Optional[str] = None
        self._password_seen = False
        self._text_tail = ""

    def rows(self) -> List[str]:
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
        self, host: str, port: int, password: Optional[str] = None, timeout: float = 5.0,
        width: int = WIDTH, height: int = HEIGHT,
    ) -> None:
        # Kept so this backend can ask the device what it is; see
        # Backend.machine. The identity is the device's, not the transport's.
        self.host = host
        self.password = password
        # The colour this machine's browser marks its cursor row with, once a
        # listing has shown it unambiguously; see _marked_row.
        self._selected_sgr: Optional[str] = None
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
    def machine_password(self) -> Optional[str]:
        return self.password

    @staticmethod
    def _connect_with_retry(host: str, port: int, timeout: float) -> socket.socket:
        deadline = time.time() + max(timeout, 15.0)
        last_error: Optional[BaseException] = None
        attempts = 0
        started = time.monotonic()
        while time.time() < deadline:
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
        return self.screen.snapshot(self.last_command)

    @property
    def selected_sgr(self) -> Optional[str]:
        """The colour this machine marks a cursor row with, once measured.

        None until a listing has shown it. A caller that has to find the
        selection on a screen this reader does not handle - a form, where the
        marking covers one field rather than a row - asks for the colour here
        rather than pinning one machine's.
        """
        return self._selected_sgr

    def _content_sgr(self, row: int, text: str) -> Optional[str]:
        """The colour the browser drew this row's first content cell in.

        The first cell is not always column zero: a C64 Ultimate draws its
        browser inside a frame, so column zero is the frame's own colour on
        every row and says nothing about the selection.
        """
        for column, character in enumerate(text):
            if character not in FRAME_CHARS:
                return self.screen.colours[row][column]
        return None

    def _marked_row(self, entry_rows: Sequence[int], rows: List[str]) -> int:
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
        tally: Dict[str, int] = {}
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

    def selected_row(self, entry_rows: Optional[Sequence[int]] = None) -> int:
        if entry_rows is None:
            raise Failure("TelnetBackend.selected_row requires entry_rows")
        self._drain_until_idle(timeout=self.timeout)
        return self._marked_row(entry_rows, self.screen.rows())

    def selected_text(self, entry_rows: Optional[Sequence[int]] = None) -> str:
        if entry_rows is None:
            raise Failure("TelnetBackend.selected_text requires entry_rows")
        self._drain_until_idle(timeout=self.timeout)
        rows = self.screen.rows()
        return strip_frame(rows[self._marked_row(entry_rows, rows)])

    def selection_and_rows(
        self, entry_rows: Optional[Sequence[int]] = None
    ) -> Tuple[int, List[str]]:
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
        received: List[bytes] = []
        try:
            started = time.time()
            end = started + timeout
            expecting = self._expect_redraw
            expecting_settle = self._expect_settle
            self._expect_redraw = False
            self._expect_settle = False
            first_wait = (pacing.TELNET_FIRST_BYTE_TIMEOUT_SECONDS if expecting
                          else pacing.TELNET_QUIET_CHECK_SECONDS)
            last_data: Optional[float] = None
            drained = 0
            while time.time() < end:
                wait = min(pacing.TELNET_IDLE_GAP_SECONDS, max(0.0, end - time.time()))
                ready, _, _ = select.select([self.sock], [], [], wait)
                now = time.time()
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
                last_data = time.time()
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
# ---------------------------------------------------------------------------
# Mode selection: the standard --mode flag and Backend factory every
# mode-switchable suite uses, so run-tests --mode propagates the same way
# everywhere.
# ---------------------------------------------------------------------------

def add_mode_argument(parser, default: str = DEFAULT_MODE, choices: Sequence[str] = MODES) -> None:
    """Register the standard -m/--mode flag on an argparse parser.

    The same letter and the same word as run-tests uses, so a mode named on the
    runner's command line and one named on a suite's read identically.
    """
    parser.add_argument(
        "-m", "--mode",
        choices=list(choices),
        default=default,
        help=f"UI transport to drive the on-device UI through: {', '.join(choices)} (default: {default})",
    )


_MODE_INTERFACE_TYPE = {
    MODE_FREEZE: "Freeze",
    MODE_OVERLAY: OVERLAY_MODE,
}


def make_backend(
    mode: str,
    host: str,
    password: Optional[str] = None,
    timeout: float = 5.0,
    telnet_host: Optional[str] = None,
    telnet_port: int = 23,
    telnet_width: int = WIDTH,
    telnet_height: int = HEIGHT,
) -> Backend:
    """Construct the Backend for `mode` ("telnet", "freeze" or "overlay").

    One place to select a transport, so every suite does it the same way from
    the same --mode flag. REST-backed modes (freeze/overlay) talk to `host`;
    telnet mode connects to `telnet_host or host` on `telnet_port`. Telnet's
    remote session is not constrained to the physical 40-column display, so
    a suite whose screen needs more room (e.g. one testing long filenames)
    passes telnet_width/telnet_height to render wider than REST/Overlay.

    Either host may be a target such as "u2@c64u"; see tests/lib/targets.py.
    """
    if mode == MODE_TELNET:
        # Telnet is a session on the device itself, so a cartridge target
        # connects to the cartridge; only keyboard injection over REST needs
        # the companion computer.
        return TelnetBackend(targets.device_of(telnet_host or host), telnet_port,
                             password, timeout, width=telnet_width, height=telnet_height)
    if mode in _MODE_INTERFACE_TYPE:
        return RestBackend(host, password, timeout, interface_type=_MODE_INTERFACE_TYPE[mode])
    raise Failure(f"Unknown mode {mode!r}; expected one of {MODES}")


# ---------------------------------------------------------------------------
# Browser: generic on-device file-browser/menu navigation, shared by any
# suite that walks the file browser, context menus, or task menu, over
# either transport. Built purely on Backend primitives (send_key/send_char/
# capture/selected_row); a suite constructs one with the entry_rows and
# status_row its screen uses (see MENU_ENTRY_ROWS-style constants in a
# migrated suite) and gets the same navigation regardless of --mode.
# ---------------------------------------------------------------------------

class Browser:
    """Navigation primitives for the on-device TreeBrowser, built on a Backend.

    Quick-seek, directory descent, context/task menu selection, popups, and
    text-field editing, expressed once so a suite that walks the on-device
    browser does not reimplement it per suite or per transport.
    """

    def __init__(self, backend: Backend, entry_rows: Sequence[int], status_row: int) -> None:
        self.backend = backend
        self.entry_rows = entry_rows
        self.status_row = status_row

    def close(self) -> None:
        # run-tests hands the next suite a device that has to satisfy the
        # UI-state contract in tests/e2e/lib/ui_state.py: the menu closed, and
        # the root browser at "/" when it is next opened. The firmware keeps
        # the browser's location across a menu close, so a suite that finishes
        # inside a directory is reported as having left the device dirty and
        # is downgraded to WARN, even when every one of its checks passed.
        # Observed on hardware: prg-context-menu, browser-long-filename and
        # browser-filesystem-refresh each ended inside /Temp.
        #
        # Failures on the way out are not this method's to report: the suite
        # has already produced its verdict, and the gate re-checks the state
        # afterwards either way. backend.close() still has to run, because it
        # is what restores the device's Interface Type setting.
        # Two attempts, because one transient on the way out would otherwise
        # hand the next suite a dirty device for no better reason than timing.
        for _ in range(2):
            try:
                self.backend.ensure_ready()
                self.go_to_root()
                break
            except Exception:  # noqa: BLE001
                continue
        self.backend.close()

    # -- screen reading --
    def capture(self) -> Snapshot:
        return self.backend.capture()

    def rows(self) -> List[str]:
        return [line.rstrip() for line in self.backend.capture().lines]

    def screen(self) -> str:
        return "\n".join(self.rows())

    def current_path(self) -> str:
        fields = self.rows()[self.status_row].split()
        return fields[0] if fields else ""

    def selected_row(self) -> int:
        return self.backend.selected_row(self.entry_rows)

    def selected_text(self) -> str:
        # One capture for both the text and the cursor row; see
        # Backend.selected_text for why two fetches are not equivalent.
        return self.backend.selected_text(self.entry_rows)

    # -- input --
    def press(self, key: str) -> None:
        self.backend.send_key(key)

    def press_many(self, key: str, count: int) -> None:
        if count > 0:
            self.backend.send_key_repeat(key, count)

    def type_char(self, character: str) -> None:
        self.backend.send_char(character)

    def type_text(self, text: str) -> None:
        """Type a whole string through the transport's batched path.

        One request for the whole string, settled once, instead of a request
        and a settle per character. The firmware drains a batch through the
        same matrix path as separate requests, so the machine sees the same
        keys. A suite that needs the keys spaced out in time (key repeat,
        racing a redraw) calls type_char in its own loop and says why.
        """
        self.backend.send_text(text, f"type {text!r}")

    # -- characters the menu itself reads --
    #
    # A quick-seek prefix and a popup's button key are commands to the menu,
    # not text, so they pass through UserInterface::keymapper and have to be
    # spelled the way the machine's Navigation Style setting expects. The two
    # methods above stay verbatim: they are what a suite types into a string
    # field, which the keymapper never sees. See tests/lib/navigation.py.
    def type_menu_char(self, character: str) -> None:
        self.backend.send_char(self.backend.navigation.menu_char(character))

    # -- navigation --
    def go_to_root(self) -> None:
        for _ in range(12):
            # Backing out of the root directory does not always stay in the
            # browser: a C64 Ultimate keeps a launcher above it, so one LEFT
            # too many leaves the browser and every later read is of a screen
            # that has no path at all. Asking to be put back is a no-op on the
            # machines where the browser is the top of the stack.
            self.backend.enter_file_browser()
            if self.current_path() == "/":
                return
            try:
                self.press("LEFT")
            except Failure as exc:
                # LEFT pops one directory level and, once that reaches the
                # root, can also close the whole overlay as a side effect
                # (confirmed live under REST/Overlay) -- the same class of
                # thing RUN/STOP does at the root under Telnet elsewhere in
                # this codebase. Reopen and let the loop read the path back,
                # rather than treating the close as proof of where we are:
                # the overlay can also go away for reasons that have nothing
                # to do with having arrived, and this used to return as though
                # it had. Observed as browser-long-filename leaving the
                # browser in /Temp while reporting that it had gone home.
                if not str(exc).startswith("menu screen unavailable after"):
                    raise
                self.backend.ensure_ready()
        raise Failure(f"could not return to '/'; now at {self.current_path()!r}")

    def go_to_top(self, count: int = 14) -> None:
        # Deeper than any listing a suite builds, and than the root menu.
        self.press_many("UP", count)

    def select_entry(self, prefix: str, max_steps: int = 30, timeout: float = 3.0) -> None:
        """Put the cursor on the listing entry starting with `prefix`.

        The listing is already on the screen, so the row the entry is on is
        read rather than searched for, and the cursor is moved there with one
        batched keypress run. This is the same thing choose_overlay_item does
        for a context menu, and it is why picking a menu item always looked
        instant while picking a file did not: stepping sends one request and
        waits out one settle per row, which measured about 206ms a row, or
        roughly six rows a second.

        `max_steps` bounds how far into the listing to look, and `timeout`
        bounds how long to keep starting fresh attempts. The two are
        deliberately separate: an attempt is only abandoned between screens,
        never part-way through one, so a listing that is still being scrolled
        through is not cut off mid-pass.
        """
        deadline = time.monotonic() + timeout
        visible = len(self.entry_rows)
        # One screen is scanned per iteration, plus one to land on.
        screens = max(1, -(-max_steps // visible) + 1)
        # Quick-seek searches the whole listing, so it does not care where the
        # cursor is and needs no rewind first. Rewinding anyway cost more than
        # the seek itself: go_to_top is a 14 key burst for the firmware to
        # drain, against one key per character of the prefix.
        if self._seek_entry(prefix):
            return
        while True:
            # This also clears any quick-seek string a previous search left
            # behind, which is the one thing that can make the attempt above
            # miss: KEY_UP and KEY_DOWN both call reset_quick_seek()
            # (software/userinterface/tree_browser.cc), so the retry below
            # starts from an empty one.
            self.go_to_top()
            if self._seek_entry(prefix):
                return
            for _ in range(screens):
                if self._select_visible(prefix):
                    return
                # Not on this screen. Move a screenful further in, in one
                # request, and look again.
                self.press_many("DOWN", visible - 1)
                if not self.backend.last_key_changed:
                    # Nothing moved, so this is the end of the listing and the
                    # entry is not in it.
                    break
            if time.monotonic() >= deadline:
                raise Failure(
                    f"could not select an entry starting with {prefix!r}; screen was:\n{self.screen()}")

    # A quick-seek character is anything the browser routes to seek_char():
    # printable and above space (software/userinterface/tree_browser.cc's
    # default case tests c >= '!'). Space must never be sent, because the
    # browser binds it to select_one(), which toggles the selection mark on the
    # entry under the cursor instead of searching.
    def _seekable(self, prefix: str) -> bool:
        if not prefix:
            return False
        # A seek costs the firmware one keystroke per character, and a jump
        # within the visible listing costs at most one per row, so a prefix
        # longer than the screen can never be the cheaper of the two.
        if len(prefix) > len(self.entry_rows):
            return False
        if any(not ("!" <= ch < "\x7f") for ch in prefix):
            return False
        try:
            for ch in prefix:
                char_to_combo(ch)
        except Failure:
            return False
        return True

    def _seek_entry(self, prefix: str) -> bool:
        """Jump to `prefix` using the browser's own quick-seek, if it can.

        The browser matches the typed string against every child with a
        trailing wildcard and moves the cursor to the first hit
        (TreeBrowser::perform_quick_seek), so this is one request whatever the
        listing's size, where stepping is one per row and even a batched jump
        is one keypress per row for the firmware to drain.

        The leading UP is what makes the seek independent of what ran before
        it: a quick-seek string persists until a cursor key clears it, and
        every further character is dropped while it has no match, so a search
        started on top of a previous one silently does nothing. UP costs one
        keystroke in the same batch, against a whole failed seek and a rewind
        to recover from it.

        The firmware's match is case-insensitive and this method's contract is
        not, so the landing is always confirmed by reading the cursor back.
        Returning False just means the caller falls through to the scan, which
        is also what happens when nothing matched: the browser drops
        characters that would leave it with no hit, so the cursor stays put.
        """
        if not self._seekable(prefix):
            return False
        try:
            # Inside the try with the keys: reading the setting is a REST call
            # of its own, and a transient there must fall through to the walk
            # like any other seek failure rather than out of select_entry.
            sent = self.backend.navigation.menu_text(prefix)
            self.backend.send_key_then_text("UP", sent, f"seek {prefix!r}")
        except Failure:
            return False
        return self.selected_text().startswith(prefix)

    def _select_visible(self, prefix: str) -> bool:
        """Move the cursor onto a matching entry on the current screen.

        Returns False when no visible row matches, or when the cursor did not
        end up where the screen said it should, which a repaint between the
        jump and the check can cause. The caller retries rather than trusting
        the move, so the result is confirmed by reading the cursor back.
        """
        row, rows = self.backend.selection_and_rows(self.entry_rows)
        for index in self.entry_rows:
            if index < len(rows) and strip_frame(rows[index]).startswith(prefix):
                if index > row:
                    self.press_many("DOWN", index - row)
                elif index < row:
                    self.press_many("UP", row - index)
                return self.selected_text().startswith(prefix)
        return False

    def enter(self) -> None:
        self.press("RIGHT")

    def descend(self, directory: str) -> None:
        for part in [p for p in directory.strip("/").split("/") if p]:
            self.select_entry(part)
            self.enter()
        expected = "/" + directory.strip("/") + "/"
        if self.current_path() != expected:
            raise Failure(f"expected {expected!r}, got {self.current_path()!r}")

    def go_to_directory(self, directory: str) -> None:
        self.go_to_root()
        self.descend(directory)

    # -- overlays (context menu / task menu / popups) --
    def overlay_items(self, before: List[str]) -> List[str]:
        """Labels an overlay added on top of `before`, top to bottom.

        Both context and task menus draw straight over the browser, so on
        every row the characters that changed are the overlay's own cell.
        Rows that only gained a border strip to nothing and drop out.

        The changed run stops at the overlay's right border. Everything past
        it belongs to the row underneath, which the overlay did not cover:
        measured on a C64 Ultimate, whose task menu is narrower than the
        browser rows it sits on, every label came back with the listing's size
        column stuck to it, as "Developer                   |32"."""
        rows = self.rows()
        # Where the transport can name the window's own columns, read the
        # labels out of them. Comparing with the screen before takes part of
        # the listing underneath for the overlay whenever a row differed there
        # already: measured on a C64 Ultimate, the task menu came back as
        # ['50K', 'Create', 'Power & Reset', ...], where "50K" is the size
        # column of the row behind it, and that extra entry shifted every
        # index so selecting "Developer" opened the entry two places past it.
        labels = []
        for old, new in zip(before, rows):
            if old == new:
                continue
            common = len(os.path.commonprefix([old, new]))
            text = new[common:].lstrip(FRAME_CHARS)
            label = strip_frame(text.split("|", 1)[0])
            # A row whose text underneath already differed from its neighbours
            # shares a shorter prefix with what replaced it, so what is left
            # can begin with the tail of the listing rather than with the
            # overlay. Measured on a C64 Ultimate: the task menu came back as
            # ["50K", "Create", "Power & Reset", ...], and that extra entry
            # shifted every index, so selecting "Developer" opened the entry
            # two places past it. A menu item is never a bare file size.
            if label and not SIZE_COLUMN_RE.fullmatch(label):
                labels.append(label)
        return labels

    def wait_for_overlay(self, before: List[str]) -> List[str]:
        """Labels of an overlay drawn over `before`, waiting for it to appear.

        Reading the screen once after the key that opens the overlay cannot
        tell "not drawn yet" from "no overlay": settling only establishes that
        the screen stopped changing, and an overlay whose draw begins after
        that window reads as absent. Returns the labels as soon as there are
        any, or an empty list once the wait is out, which the callers report.
        """
        deadline = time.monotonic() + pacing.OVERLAY_DRAW_TIMEOUT_SECONDS
        while True:
            labels = self.overlay_items(before)
            if labels or time.monotonic() >= deadline:
                return labels
            time.sleep(pacing.POLL_INTERVAL_SECONDS)

    def open_context_menu(self) -> List[str]:
        before = self.rows()
        self.press("ENTER")
        labels = self.wait_for_overlay(before)
        if not labels:
            raise Failure(f"no context menu appeared; screen was:\n{self.screen()}")
        return labels

    def choose_overlay_item(self, labels: List[str], label: str) -> None:
        """Select `label` in an open overlay, by the shortest key sequence.

        Both the context menu and the task menu are ContextMenu objects, and
        both reach this, so every overlay in the tree navigates the same way.
        See plan_overlay_navigation for what the firmware does with the keys.
        """
        if label not in labels:
            # Some entries carry a right-hand value column, which overlay_items
            # renders as "Label||value" -- and the value changes at runtime, so
            # "Clear Debug Log" is an exact match only while the log is empty.
            # Match on the label side when that names exactly one entry.
            matches = [l for l in labels if l.split("||", 1)[0].strip() == label]
            if len(matches) != 1:
                raise Failure(f"overlay has no {label!r}; it offers {labels}")
            label = matches[0]
        prefix, delta = plan_overlay_navigation(labels, label)
        for character in prefix:
            self.type_menu_char(character)
        if delta:
            self.press_many("DOWN" if delta > 0 else "UP", abs(delta))
        self.press("ENTER")

    def invoke_context_action(self, label: str) -> None:
        self.choose_overlay_item(self.open_context_menu(), label)

    def press_task_menu(self) -> None:
        """Open the task menu with whichever key this machine puts it on.

        The task menu belongs to the file browser, so the browser has to be
        what is on screen. On a machine with a launcher above it that is not
        a given: measured on a C64 Ultimate, loading settings through the
        browser left the launcher showing, and the next task-menu press read
        the launcher's own entries as the menu it had just opened.
        """
        self.backend.enter_file_browser()
        self.press(self.backend.machine.task_menu_key)

    def invoke_task_action(self, category: str, item: str) -> None:
        before = self.rows()
        self.press_task_menu()
        categories = self.wait_for_overlay(before)
        if not categories:
            raise Failure(f"no task menu appeared; screen was:\n{self.screen()}")
        if category not in categories:
            raise Failure(f"task menu has no {category!r}; it offers {categories}")
        self.press_many("DOWN", categories.index(category))
        before = self.rows()
        self.press("ENTER")
        self.choose_overlay_item(self.wait_for_overlay(before), item)

    def press_popup_button(self, key: str) -> None:
        """Popups are keyed: o=Ok, y=Yes, n=No, a=All, c=Cancel.

        "All" is one of the four letters WASD Cursors binds to a cursor key,
        so the key goes through the navigation transform: typed raw on a
        machine set to WASD Cursors it moves the highlight left instead of
        pressing the button.
        """
        self.type_menu_char(key)

    def wait_for_text(self, text: str, timeout: float = 8.0) -> None:
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            if text in self.screen():
                return
            time.sleep(0.15)
        raise Failure(f"{text!r} never appeared; screen was:\n{self.screen()}")

    def wait_until_gone(self, text: str, timeout: float = 8.0) -> None:
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            if text not in self.screen():
                return
            time.sleep(0.15)
        raise Failure(f"{text!r} never went away; screen was:\n{self.screen()}")

    def fill_edit_field(self, text: str, clear_taps: int = 0) -> None:
        if clear_taps:
            self.press_many("BACKSPACE", clear_taps)
        self.type_text(text)
        self.press("ENTER")

    def recover_to(self, directory: str) -> None:
        """Dismiss whatever is open and end up in `directory`.

        Popups go first, because nothing else responds while one is up. Then
        the browser goes straight there rather than walking: LEFT only moves
        towards the root, so a target that is not an ancestor of where the
        browser stands cannot be reached that way at all, and the walk was
        guaranteed to spend its whole budget before falling back to the same
        descent that is now tried first. Measured after a "Copy to...", which
        leaves the browser in the source directory and so always hit that case:
        12.6s over REST and 27.3s over Telnet, against about two seconds to
        descend. go_to_directory returns to the root itself, which is what
        unwinds a nested view such as an opened disk image.

        The loop bound counts popups rather than directory levels now, which
        is why it is smaller than the 20 it replaces: popups are modal and
        appear one at a time, and one that will not go away should fail the
        suite rather than be spun on.
        """
        wanted = "/" + directory.strip("/") + "/"
        for _ in range(8):
            rows = [strip_frame(row) for row in self.rows()]
            # A popup ignores everything except its own button keys.
            if "Yes  No" in rows:
                self.press_popup_button("n")
                continue
            if "Ok" in rows:
                self.press_popup_button("o")
                continue
            break
        if self.current_path() == wanted:
            return
        self.go_to_directory(directory)
        if self.current_path() != wanted:
            raise Failure(f"could not be returned to {wanted!r}")

    def pick_directory(self, directory: str, picker_title: str, select_entry_label: str) -> None:
        """Drive a 'Select Path'-style picker onto `directory` and pick it."""
        self.wait_for_text(picker_title)
        if self.current_path() != "/":
            raise Failure(f"path picker opened at {self.current_path()!r}, expected the root")
        self.descend(directory)
        self.select_entry(select_entry_label)
        self.press("ENTER")


def make_browser(
    mode: str,
    host: str,
    password: Optional[str] = None,
    timeout: float = 5.0,
    entry_rows: Sequence[int] = range(2, 24),
    status_row: int = 24,
    telnet_host: Optional[str] = None,
    telnet_port: int = 23,
    telnet_width: int = WIDTH,
    telnet_height: int = HEIGHT,
    telnet_entry_rows: Optional[Sequence[int]] = None,
    telnet_status_row: Optional[int] = None,
) -> Browser:
    """Construct a Browser for `mode`, with REST and Telnet row layouts.

    REST/Overlay/Freeze render the browser at the full 40x25 (SCREEN_WIDTH x
    SCREEN_HEIGHT) geometry; Telnet's remote session can use a different
    width and row range (it is not constrained to the physical 40-column
    display), so callers that support Telnet pass telnet_width/telnet_height
    and telnet_entry_rows/telnet_status_row explicitly when they differ from
    the REST layout.
    """
    backend = make_backend(
        mode, host, password, timeout, telnet_host=telnet_host, telnet_port=telnet_port,
        telnet_width=telnet_width, telnet_height=telnet_height,
    )
    if mode == MODE_TELNET:
        rows = telnet_entry_rows if telnet_entry_rows is not None else entry_rows
        status = telnet_status_row if telnet_status_row is not None else status_row
        return Browser(backend, rows, status)
    return Browser(backend, entry_rows, status_row)
