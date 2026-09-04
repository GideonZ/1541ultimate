#!/usr/bin/env python3
# The on-device UI, as every suite reaches it.

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

# This module was 2,871 lines holding nine classes: the abstract backend and
# its two transports, the screen parsers and the browser. They are four
# modules now - backend.py, rest_backend.py, telnet_backend.py and browser.py
# - and everything they define is re-exported here, so the thirty files that
# import from ui_backend did not have to change, and moving one name later is
# a change to one line rather than to thirty.
#
# What stays is the factory: choosing a transport from --mode is the one
# thing that has to know about all three.
import sys
from pathlib import Path

# tests/lib holds the shared library; importing bootstrap adds tests/e2e/lib.
# The search walks up rather than counting directories, so this is the same in
# every entry point and a suite that moves needs no edit. See tests/lib/bootstrap.py.
sys.path.insert(0, str(next(p for p in Path(__file__).resolve().parents
                            if (p / "tests" / "lib").is_dir()) / "tests" / "lib"))
from report import Failure
from collections.abc import Sequence
import targets
from backend import (BOX_BOTTOM_LEFT, BOX_BOTTOM_RIGHT, BOX_HORIZONTAL,
    BOX_TOP_LEFT, BOX_TOP_RIGHT, BOX_VERTICAL, Backend, CONFIGS_PATH,
    DEFAULT_MODE, FRAME_CHARS, INFO_PATH, KEY_ALIASES, LAUNCHER_DESCENT_STEPS,
    LAUNCHER_ENTRY_ROWS, MENU_GLYPHS, MODES, MODE_FREEZE, MODE_OVERLAY,
    MODE_TELNET, NoCursorDrawn,
    POLL_INTERVAL_SECONDS, RowMark, SCREEN_BYTES, SCREEN_CELLS,
    SCREEN_HEIGHT, SCREEN_WIDTH, SELECTED_ROW_MIN_MARKED_CELLS,
    SETTLE_TIMEOUT_SECONDS, Snapshot, TRANSPORT_RETRIES,
    TRANSPORT_RETRY_PAUSE_SECONDS, Window, _DIRECT_CHARS,
    _SEEKABLE_RE, _SHIFTED_DIGIT_CHARS, _SHIFTED_SYMBOL_CHARS, _find_frames,
    _frame_top, _seek_landing, char_to_combo, count_colour,
    fetch_navigation_style, fetch_product, find_cursor_colour,
    find_open_window, find_selected_row_rest, measure_cursor_colour,
    plan_overlay_navigation, row_marks, strip_frame, whole_screen)
from rest_backend import (CURSOR_SETTLE_ATTEMPTS, INPUT_PATH,
    MENU_BUTTON_PATH, MENU_SCREEN_PATH, OVERLAY_MODE, RestBackend, UI_ITEM,
    UI_STORE, close_host_menu, host_menu_open)
from telnet_backend import (ALT_CHARSET_MAP, HEIGHT, TELNET_ENTRY_ROWS,
    TELNET_KEY_BYTES, TELNET_STATUS_ROW, TelnetBackend, VT100Screen, WIDTH)
from browser import (SIZE_COLUMN_RE, Browser)

# The re-exported surface. __all__ rather than a noqa on each name: it is
# what `from ui_backend import *` would take, and it is what stops the lint
# reading a re-export as an unused import.
__all__ = [
    "ALT_CHARSET_MAP",
    "BOX_BOTTOM_LEFT",
    "BOX_BOTTOM_RIGHT",
    "BOX_HORIZONTAL",
    "BOX_TOP_LEFT",
    "BOX_TOP_RIGHT",
    "BOX_VERTICAL",
    "CONFIGS_PATH",
    "CURSOR_SETTLE_ATTEMPTS",
    "DEFAULT_MODE",
    "FRAME_CHARS",
    "HEIGHT",
    "INFO_PATH",
    "INPUT_PATH",
    "KEY_ALIASES",
    "LAUNCHER_DESCENT_STEPS",
    "LAUNCHER_ENTRY_ROWS",
    "MENU_BUTTON_PATH",
    "MENU_GLYPHS",
    "MENU_SCREEN_PATH",
    "MODES",
    "MODE_FREEZE",
    "MODE_OVERLAY",
    "MODE_TELNET",
    "OVERLAY_MODE",
    "POLL_INTERVAL_SECONDS",
    "SCREEN_BYTES",
    "SCREEN_CELLS",
    "SCREEN_HEIGHT",
    "SCREEN_WIDTH",
    "SELECTED_ROW_MIN_MARKED_CELLS",
    "SETTLE_TIMEOUT_SECONDS",
    "SIZE_COLUMN_RE",
    "TELNET_ENTRY_ROWS",
    "TELNET_KEY_BYTES",
    "TELNET_STATUS_ROW",
    "TRANSPORT_RETRIES",
    "TRANSPORT_RETRY_PAUSE_SECONDS",
    "UI_ITEM",
    "UI_STORE",
    "WIDTH",
    "_DIRECT_CHARS",
    "_SEEKABLE_RE",
    "_SHIFTED_DIGIT_CHARS",
    "_SHIFTED_SYMBOL_CHARS",
    "Backend",
    "Browser",
    "NoCursorDrawn",
    "RestBackend",
    "RowMark",
    "Snapshot",
    "TelnetBackend",
    "VT100Screen",
    "Window",
    "_find_frames",
    "_frame_top",
    "_seek_landing",
    "add_mode_argument",
    "char_to_combo",
    "close_host_menu",
    "count_colour",
    "fetch_navigation_style",
    "fetch_product",
    "find_cursor_colour",
    "find_open_window",
    "find_selected_row_rest",
    "host_menu_open",
    "make_backend",
    "make_browser",
    "measure_cursor_colour",
    "plan_overlay_navigation",
    "row_marks",
    "strip_frame",
    "whole_screen",
]


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
    password: str | None = None,
    timeout: float = 5.0,
    telnet_host: str | None = None,
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


def make_browser(
    mode: str,
    host: str,
    password: str | None = None,
    timeout: float = 5.0,
    entry_rows: Sequence[int] = range(2, 24),
    status_row: int = 24,
    telnet_host: str | None = None,
    telnet_port: int = 23,
    telnet_width: int = WIDTH,
    telnet_height: int = HEIGHT,
    telnet_entry_rows: Sequence[int] | None = None,
    telnet_status_row: int | None = None,
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
        # Falling back to the Telnet layout rather than the 40x25 one, so a
        # caller that does not name it still gets the geometry its session
        # actually has. See TELNET_ENTRY_ROWS.
        rows = telnet_entry_rows if telnet_entry_rows is not None else TELNET_ENTRY_ROWS
        status = telnet_status_row if telnet_status_row is not None else TELNET_STATUS_ROW
        return Browser(backend, rows, status)
    return Browser(backend, entry_rows, status_row)
