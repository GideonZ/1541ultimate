#!/usr/bin/env python3
# E2E: Verifies the shared ui_backend.py facade itself against real hardware,
# independently of any suite built on top of it, via all three transports it
# supports: Telnet, REST/Freeze, and REST/Overlay.

"""tests/e2e/lib/ui_backend.py gives every suite one Backend interface over two
transports (Telnet and REST) and, for REST, two Interface Type UI modes
(Freeze and Overlay). A bug in that shared plumbing would surface as
confusing, unrelated-looking failures in every suite built on it, so it gets
its own direct check first: the same small scenario (root browser is visible,
a keypress moves the selection, typing a character quick-seeks, teardown
leaves the device clean) run once per transport/mode.

This is deliberately not a full UI regression suite (tests/e2e/api/input_test.py
and menu_screen_test.py already cover the REST input/menu_screen contract in
depth) -- it exists to prove the facade itself is wired correctly before any
other suite relies on it.
"""

import argparse
import os
import sys
import time
from typing import Sequence

# tests/lib holds the reporting rules every suite shares; ui_backend sits
# in this directory.
sys.path.insert(0, os.path.join(
    os.path.dirname(os.path.abspath(__file__)), "..", "..", "lib"))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import pacing
import rest as rest_lib
import targets
from report import Failure, check, format_exception, section, suite_fail, suite_ok
from ui_backend import (SCREEN_CELLS, SCREEN_WIDTH, Backend, RestBackend,
                        Snapshot, TelnetBackend, find_cursor_colour,
                        find_selected_row_rest, plan_overlay_navigation)

MIN_PRINTABLE_CELLS = 20
MAX_DISTINCT_GLYPHS = 160

# How long a quick-seek gets to show up on screen. A key does not land at the
# same speed on every target: injected into the machine it drives it is applied
# almost at once, while on a cartridge target it goes out over REST, into the
# keyboard matrix of the computer the cartridge is plugged into, and reaches
# the cartridge on its next scan of that matrix. See tests/lib/targets.py.
SEEK_TIMEOUT_SECONDS = 5.0

# Root-browser entry rows, by transport: REST's menu_screen is the full
# 25-row physical screen (row 24 is the status/help line); Telnet's remote
# session only ever fills 24 of those rows (row 23 is the status line).
ROOT_ENTRY_ROWS_REST = range(2, 24)
ROOT_ENTRY_ROWS_TELNET = range(2, 23)


def run_navigation_planner_checks() -> None:
    """The overlay route planner, checked against the firmware's own rules.

    Needs no device: it is a pure function, and it is the one place encoding
    what ContextMenu::seek_char and ::perform_quick_seek do. If it drifts from
    the firmware, every context and task menu in the tree selects the wrong
    item, so it is checked before anything talks to hardware.
    """
    menu = ["Run", "Load", "View", "Hex View", "Copy to...", "Rename",
            "Delete", "Move to..."]

    with check("a unique first letter jumps straight to the item"):
        for label in ("View", "Copy to...", "Delete", "Move to..."):
            plan = plan_overlay_navigation(menu, label)
            if plan != (label[0], 0):
                raise Failure(f"{label!r}: expected a one-character jump, got {plan}")

    with check("a shared first letter extends the prefix rather than walking"):
        # "Run" and "Rename" both start with R, and R lands on "Run".
        plan = plan_overlay_navigation(menu, "Rename")
        if plan != ("Re", 0):
            raise Failure(f"expected the prefix 'Re', got {plan}")

    with check("walking wins when the item is already next to the cursor"):
        plan = plan_overlay_navigation(menu, "Load")
        if plan != ("", 1):
            raise Failure(f"expected a single DOWN, got {plan}")

    with check("the seek lands on the first match and the walk covers the rest"):
        # The seek always lands on the *first* label matching the prefix, so a
        # later one of the same letter needs a step afterwards. Written with
        # the pair far down the list, because near the top a plain walk is
        # cheaper and the planner correctly prefers it.
        long_menu = ["Zero", "One", "Two", "Three", "Four",
                     "Alpha one", "Alpha two"]
        plan = plan_overlay_navigation(long_menu, "Alpha two")
        if plan != ("A", 1):
            raise Failure(f"expected 'A' then one DOWN, got {plan}")

    with check("a prefix never contains a space, which selects rather than seeks"):
        # ContextMenu::handle_key maps KEY_SPACE to select_item(), so a space
        # in the prefix would activate whatever sits under the cursor.
        for label in menu:
            prefix, _ = plan_overlay_navigation(menu, label)
            if " " in prefix:
                raise Failure(f"{label!r}: prefix {prefix!r} contains a space")

    with check("a plan is never longer than walking from the top"):
        for index, label in enumerate(menu):
            prefix, delta = plan_overlay_navigation(menu, label)
            if len(prefix) + abs(delta) > index:
                raise Failure(f"{label!r}: plan costs {len(prefix) + abs(delta)} keys, "
                              f"walking costs {index}")

    with check("an upward walk is planned when that is the shorter route"):
        prefix, delta = plan_overlay_navigation(menu, "Load", start=3)
        if len(prefix) + abs(delta) > 2:
            raise Failure(f"expected at most two keys, got {prefix!r} {delta:+d}")


def build_menu_planes(entries: Sequence[str], selected: int, listing_colour: int,
                      selected_colour: int, background: int = 0) -> "tuple[bytes, bytes]":
    """The char and colour planes machine:menu_screen would return for a listing."""
    chars = bytearray(b" " * SCREEN_CELLS)
    colours = bytearray(SCREEN_CELLS)
    for index, text in enumerate(entries):
        row = ROOT_ENTRY_ROWS_REST[0] + index
        padded = text.ljust(SCREEN_WIDTH)[:SCREEN_WIDTH]
        start = row * SCREEN_WIDTH
        chars[start:start + SCREEN_WIDTH] = padded.encode("ascii")
        code = selected_colour if index == selected else listing_colour
        marked = (background << 4) | code if index == selected else code
        colours[start:start + SCREEN_WIDTH] = bytes([marked]) * SCREEN_WIDTH
    return bytes(chars), bytes(colours)


def run_selected_row_checks() -> None:
    """Reading the cursor row out of a colour plane, without a device.

    The rule is that a listing draws every unselected entry in one colour and
    the selected one in another. Both halves matter: firmware that marks the
    cursor with a background colour, and firmware whose menu_screen carries no
    background nibble at all, which is what an Ultimate II+L returns.
    """
    entries = ["Flash   Flash Disk             Ready",
               "Temp    RAM Disk               Ready",
               "USB0    SanDisk 3.2Gen1        Ready",
               "Ftp     Remote FTP Servers     Ready"]

    with check("a background-marked cursor row is found"):
        chars, colours = build_menu_planes(entries, selected=2, listing_colour=12,
                                           selected_colour=1, background=6)
        row = find_selected_row_rest(chars, colours, ROOT_ENTRY_ROWS_REST)
        if row != ROOT_ENTRY_ROWS_REST[0] + 2:
            raise Failure(f"expected row {ROOT_ENTRY_ROWS_REST[0] + 2}, got {row}")

    with check("a cursor row marked by foreground colour alone is found"):
        # No background nibble anywhere, so every entry row has the same number
        # of coloured cells and only the colour itself tells them apart.
        chars, colours = build_menu_planes(entries, selected=1, listing_colour=12,
                                           selected_colour=1)
        row = find_selected_row_rest(chars, colours, ROOT_ENTRY_ROWS_REST)
        if row != ROOT_ENTRY_ROWS_REST[0] + 1:
            raise Failure(f"expected row {ROOT_ENTRY_ROWS_REST[0] + 1}, got {row}")

    with check("the first entry is found when it is the selected one"):
        chars, colours = build_menu_planes(entries, selected=0, listing_colour=12,
                                           selected_colour=1)
        row = find_selected_row_rest(chars, colours, ROOT_ENTRY_ROWS_REST)
        if row != ROOT_ENTRY_ROWS_REST[0]:
            raise Failure(f"expected row {ROOT_ENTRY_ROWS_REST[0]}, got {row}")

    with check("a cursor that colours only the name field is still found"):
        # A disk image listing, copied from an Ultimate II+L showing a D64 with
        # one program: the volume row is one colour across its whole width, and
        # the selected row carries the cursor colour on its name and another
        # colour on the size and type columns. The cursor colour is therefore
        # not the selected row's commonest colour, and the volume row has more
        # coloured cells than it.
        chars = bytearray(b" " * SCREEN_CELLS)
        colours = bytearray(SCREEN_CELLS)
        volume = ROOT_ENTRY_ROWS_REST[0]
        program = volume + 1
        for row, text, plan in (
                (volume, "DMATEST           64 2A       VOLUME", [(0, SCREEN_WIDTH, 6)]),
                (program, "DMATESTPROGRAM01              PRG  254",
                 [(0, 16, 1), (16, SCREEN_WIDTH, 7)])):
            start = row * SCREEN_WIDTH
            chars[start:start + SCREEN_WIDTH] = text.ljust(SCREEN_WIDTH).encode("ascii")
            for first, last, code in plan:
                for column in range(first, last):
                    colours[start + column] = code
        row = find_selected_row_rest(bytes(chars), bytes(colours),
                                     ROOT_ENTRY_ROWS_REST, cursor_colour=1)
        if row != program:
            raise Failure(f"expected the program row {program}, got {row}")

    with check("a listing of three or more entries says which colour is the cursor"):
        chars, colours = build_menu_planes(entries, selected=2, listing_colour=12,
                                           selected_colour=1)
        measured = find_cursor_colour(chars, colours, ROOT_ENTRY_ROWS_REST)
        if measured != 1:
            raise Failure(f"expected the cursor colour 1, got {measured!r}")

    with check("a background-marked screen teaches no foreground cursor colour"):
        # An Ultimate 64 marks the cursor row with a background nibble, and
        # those cells are not counted as foreground at all, so the colour a
        # foreground scan would find there belongs to some unselected row.
        # Taking it would then point every later read at the wrong row.
        chars, colours = build_menu_planes(entries, selected=2, listing_colour=12,
                                           selected_colour=1, background=6)
        measured = find_cursor_colour(chars, colours, ROOT_ENTRY_ROWS_REST)
        if measured is not None:
            raise Failure(f"expected no cursor colour, got {measured!r}")

    with check("two entries need the colour the machine was measured to mark with"):
        # The case a browser fixture directory produces: one seeded entry
        # beside the Temp cache directory. Each row's colour is then unique, so
        # the odd-colour rule has two answers and the screen alone cannot say
        # which is the cursor. Reproduced on an Ultimate II+L before the colour
        # was carried: the cursor sat on the second entry and the first was
        # reported, so a caller looking for the second never found it.
        pair = ["cache                         DIR",
                "zbfr636996653                 DIR"]
        chars, colours = build_menu_planes(pair, selected=1, listing_colour=12,
                                           selected_colour=1)
        blind = find_selected_row_rest(chars, colours, ROOT_ENTRY_ROWS_REST)
        if blind != ROOT_ENTRY_ROWS_REST[0]:
            raise Failure(f"expected the ambiguous answer {ROOT_ENTRY_ROWS_REST[0]}, "
                          f"got {blind}")
        row = find_selected_row_rest(chars, colours, ROOT_ENTRY_ROWS_REST,
                                     cursor_colour=1)
        if row != ROOT_ENTRY_ROWS_REST[0] + 1:
            raise Failure(f"expected row {ROOT_ENTRY_ROWS_REST[0] + 1}, got {row}")


def assert_looks_like_root_browser(snapshot: Snapshot) -> None:
    text = snapshot.text()
    printable = sum(1 for ch in text if ch not in (" ",))
    if printable < MIN_PRINTABLE_CELLS:
        raise Failure(f"screen looks blank after {snapshot.last_command}: only {printable} non-space cells\n{text}")
    distinct_glyphs = len(set(text))
    if distinct_glyphs > MAX_DISTINCT_GLYPHS:
        raise Failure(f"screen looks like garbage after {snapshot.last_command}: {distinct_glyphs} distinct glyphs\n{text}")
    snapshot.find_line_containing("Ultimate")
    snapshot.find_line_containing("/")


def path_row(snapshot: Snapshot) -> str:
    """The browser's own path indicator, wherever the row layout puts it."""
    for line in reversed(snapshot.lines):
        text = line.strip()
        if text.startswith("/"):
            return text.split()[0]
    raise Failure(f"no path row found after {snapshot.last_command}\n{snapshot.text()}")


def seek_to(backend: Backend, entry_rows: Sequence[int], character: str,
            label: str) -> "tuple[Snapshot, int]":
    """Quick-seek on `character` and wait until the cursor is on `label`.

    Returns the snapshot the cursor was found in and its row. A single capture
    taken straight after the key would read the screen the key has not reached
    yet on a target where it travels through another machine's keyboard matrix.
    """
    snapshot = backend.send_char(character)
    deadline = time.monotonic() + SEEK_TIMEOUT_SECONDS
    while True:
        row = backend.selected_row(entry_rows)
        if label in snapshot.line(row) or time.monotonic() >= deadline:
            return snapshot, row
        time.sleep(pacing.POLL_INTERVAL_SECONDS)
        snapshot = backend.capture()


def run_backend_smoke(backend: Backend, entry_rows: Sequence[int]) -> None:
    with check("root browser is visible on connect"):
        snapshot = backend.capture()
        assert_looks_like_root_browser(snapshot)

    with check("F5 opens the task menu and RUN/STOP restores the browser"):
        # The browser marks the selected row by colour, not by the character
        # matrix's reverse-video bit, and the two transports use different
        # colour encodings (a real C64 colour nibble over REST, an
        # ANSI-mapped approximation over Telnet's VT100 stream), so a cursor
        # key alone is not a transport-uniform text signal. Opening the task
        # menu replaces the visible text outright, which both transports
        # render identically in the character matrix.
        before = backend.capture().text()
        opened = backend.send_key("F5").text()
        if opened == before:
            raise Failure("F5 had no visible effect on the screen")
        closed = backend.send_key("RUNSTOP").text()
        if closed != before:
            raise Failure("RUN/STOP did not restore the original screen after F5")

    with check("typing a character quick-seeks to the matching entry"):
        # Proves send_char delivers the *correct* character, not merely *a*
        # character: quick-seeking on the wrong letter would step into the
        # wrong directory, which the resulting path would catch. "Temp"
        # already appears as a row label at the root, so this checks the
        # browser's own path indicator rather than screen content generally.
        seek_to(backend, entry_rows, "t", "Temp")
        entered_path = path_row(backend.send_key("RIGHT"))
        if not entered_path.startswith("/Temp"):
            raise Failure(f"quick-seek on 't' + RIGHT did not enter /Temp: path was {entered_path!r}")
        left_path = path_row(backend.send_key("LEFT"))
        if left_path != "/":
            raise Failure(f"LEFT did not return to the root path: {left_path!r}")

    with check("selected_row() reports the colour-marked cursor row"):
        # path_row() above proves the browser's own path indicator moved to
        # the right place, but every migrated suite navigates via
        # Browser.select_entry()/selected_row(), which reads the cursor row
        # by its colour marker (tree_browser_state.cc marks the selection by
        # colour, not by the reverse-video bit) -- a different code path
        # that REST and Telnet implement with different colour encodings.
        # This must be checked directly: it can regress while every check
        # above stays green, since none of them call selected_row().
        snapshot, row = seek_to(backend, entry_rows, "t", "Temp")
        if "Temp" not in snapshot.line(row):
            raise Failure(
                f"selected_row() returned row {row} ({snapshot.line(row)!r}) after quick-seeking to 't'; "
                f"expected the Temp row"
            )


def run_telnet_smoke(host: str, port: int, password: str, timeout: float) -> None:
    # Telnet is a session on the device itself, so a cartridge target connects
    # to the cartridge; only REST keyboard injection needs the companion.
    backend = TelnetBackend(targets.device_of(host), port, password or None, timeout)
    try:
        run_backend_smoke(backend, ROOT_ENTRY_ROWS_TELNET)
    finally:
        backend.close()


def run_rest_smoke(host: str, password: str, timeout: float, interface_type: str) -> None:
    backend = RestBackend(host, password or None, timeout, interface_type=interface_type)
    try:
        run_backend_smoke(backend, ROOT_ENTRY_ROWS_REST)
    finally:
        backend.close()


def main() -> int:
    parser = argparse.ArgumentParser(description="Validate the shared ui_backend.py facade over Telnet, REST/Freeze and REST/Overlay")
    parser.add_argument("-H", "--host", default=os.environ.get("U64_HOST", "u64"))
    parser.add_argument("--telnet-port", type=int, default=int(os.environ.get("U64_TELNET_PORT", "23")))
    parser.add_argument("-p", "--password", default=os.environ.get("U64_PASS", ""))
    parser.add_argument("-t", "--timeout", type=float, default=float(os.environ.get("U64_TIMEOUT", "5.0")))
    args = parser.parse_args()

    try:
        section("Overlay navigation planner")
        run_navigation_planner_checks()

        section("Cursor row from the colour plane")
        run_selected_row_checks()

        section("Telnet backend")
        with check("Telnet: connect, navigate, teardown"):
            run_telnet_smoke(args.host, args.telnet_port, args.password, args.timeout)

        section("REST backend, Interface Type = Freeze")
        with check("REST/Freeze: connect, navigate, teardown"):
            run_rest_smoke(args.host, args.password, args.timeout, "Freeze")

        section("REST backend, Interface Type = Overlay on HDMI")
        with check("REST/Overlay: connect, navigate, teardown"):
            run_rest_smoke(args.host, args.password, args.timeout, "Overlay on HDMI")
    except Failure as exc:
        suite_fail("ui_backend_smoke_test", str(exc))
        return 1
    except Exception as exc:  # noqa: BLE001 - report any transport error through the shared library
        if rest_lib.looks_unreachable(exc):
            suite_fail("ui_backend_smoke_test", f"device unavailable: {format_exception(exc)}")
        else:
            suite_fail("ui_backend_smoke_test", format_exception(exc))
        return 1

    suite_ok("ui_backend_smoke_test")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
