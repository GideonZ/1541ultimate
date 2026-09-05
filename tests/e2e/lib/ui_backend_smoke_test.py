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
import re
import sys
import time
from collections.abc import Sequence
from pathlib import Path

# The one stanza that puts the shared library on sys.path; see tests/lib/bootstrap.py.
sys.path.insert(0, str(next(p for p in Path(__file__).resolve().parents
                            if (p / "tests" / "lib").is_dir()) / "tests" / "lib"))
import bootstrap  # noqa: E402,F401
import cli  # noqa: E402
import machine
import pacing
import profiles  # noqa: E402
import rest as rest_lib
import targets
from report import Failure, check, format_exception, section, suite_fail, suite_ok
from ui_backend import (BOX_BOTTOM_LEFT, BOX_BOTTOM_RIGHT, BOX_HORIZONTAL,
                        BOX_TOP_LEFT, BOX_TOP_RIGHT, BOX_VERTICAL,
                        SCREEN_CELLS, SCREEN_WIDTH, Backend, RestBackend,
                        Snapshot, TelnetBackend, find_cursor_colour,
                        find_open_window, find_selected_row_rest,
                        measure_cursor_colour, plan_overlay_navigation,
                        whole_screen)

# The colour the firmware draws a window frame in, distinct from both the
# listing and the cursor colour; measured on an Ultimate II+L task menu.
FRAME_COLOUR = 13

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


def run_machine_checks() -> None:
    """Telling the three machines apart from what /v1/info reports.

    Needs no device: it is the rule every suite now relies on to choose a menu
    layout, and getting it wrong sends a run down the wrong path on hardware
    that is working correctly. The product strings are the ones the three
    machines were measured to return.

    The second half covers the fix table, which decides which checks a machine
    on a lagging firmware line skips. A mistake there is quiet in a way a menu
    mistake is not: it either hides a real defect behind a skip or fails a
    machine for a fix it was never going to have.
    """
    with check("each machine is recognised from its product string"):
        expected = {
            "Ultimate 64": machine.U64,
            "Ultimate 64 Elite": machine.U64,
            "Ultimate II+": machine.U2,
            "Ultimate II+L": machine.U2,
            "C64 Ultimate": machine.C64U,
        }
        for product, kind in expected.items():
            found = machine.classify(product).kind
            if found != kind:
                raise Failure(f"{product!r}: expected {kind}, got {found}")

    with check("only the C64 Ultimate opens on a launcher"):
        # Its menu button opens a launcher whose first entry is the file
        # browser; the other two open the browser itself.
        for product, launcher in (("Ultimate 64 Elite", False),
                                  ("Ultimate II+L", False),
                                  ("C64 Ultimate", True)):
            found = machine.classify(product).menu_opens_on_launcher
            if found != launcher:
                raise Failure(f"{product!r}: expected launcher={launcher}, got {found}")

    with check("the C64 Ultimate searches CommoServe, the others Assembly 64"):
        for product, service in (("Ultimate 64 Elite", "Assembly 64"),
                                 ("Ultimate II+L", "Assembly 64"),
                                 ("C64 Ultimate", "CommoServe")):
            found = machine.classify(product).search_service
            if found != service:
                raise Failure(f"{product!r}: expected {service}, got {found}")

    with check("an unrecognised product is refused rather than guessed"):
        # Guessing would send the run down one machine's menu layout on
        # another's hardware and report the mismatch as a firmware defect.
        try:
            machine.classify("Commodore PET")
        except machine.UnknownMachine:
            pass
        else:
            raise Failure("an unknown product was classified rather than refused")

    with check("the product is fetched once and then kept"):
        # It cannot change during a run, and every screen read would otherwise
        # pay for a REST round trip.
        machine.forget("fixture-host")
        calls = []

        def fetch() -> str:
            calls.append(1)
            return "C64 Ultimate"

        kinds = {machine.identify("fixture-host", fetch).kind for _ in range(3)}
        machine.forget("fixture-host")
        if kinds != {machine.C64U} or len(calls) != 1:
            raise Failure(f"expected one fetch and one answer, got {len(calls)} "
                          f"fetches and {kinds}")

    with check("the firmware version is kept when the caller has it"):
        # One /v1/info answer carries the product and the version, and the
        # version is how a skip reason names the machine someone then has to
        # go and flash. A caller that can only reach the product still gets a
        # machine, with the version left empty.
        machine.forget("fixture-host")
        found = machine.identify("fixture-host", lambda: ("C64 Ultimate", "1.2.0"))
        machine.forget("fixture-host")
        if (found.firmware, found.described) != ("1.2.0", "C64 Ultimate 1.2.0"):
            raise Failure(f"expected the version to be kept, got {found.described!r}")
        if machine.classify("Ultimate II+L").described != "Ultimate II+L":
            raise Failure("a machine with no reported version described itself with one")

    with check("every fix in the table names a behaviour and a machine that lacks it"):
        # The tag is typed on a command line as --assume-fix and read in a skip
        # reason by whoever decides the entry can go, so it has to say what the
        # firmware does. A date tag such as "2026-08-bugfixes" names nothing a
        # check depends on and goes stale without anyone noticing, which is why
        # a tag has to start with a letter rather than a year.
        for name, entry in machine.FIXES.items():
            if name != entry.name:
                raise Failure(f"{name!r} is filed under the name {entry.name!r}")
            if not re.fullmatch(r"[a-z][a-z0-9]*(-[a-z0-9]+)+", name):
                raise Failure(f"{name!r} is not a lower-case kebab-case tag "
                              f"naming a behaviour")
            if not entry.behaviour.strip() or "\n" in entry.behaviour:
                raise Failure(f"{name!r} has no one-line behaviour: {entry.behaviour!r}")
            if not entry.lacking:
                raise Failure(f"{name!r} lists no machine that lacks it, so it "
                              f"only skips checks that had no reason to skip")
            for kind in entry.lacking:
                if kind not in (machine.U64, machine.U2, machine.C64U):
                    raise Failure(f"{name!r} lists an unknown machine kind {kind!r}")

    # Every check below asserts which machine skips what, and an assumption is
    # exactly the switch that stops a machine skipping, so the ones the run was
    # started with are cleared here and put back afterwards. Without this, the
    # suite would fail on the run it is most needed on: `--assume-fix all`, the
    # sweep someone does to find out whether a backport has landed.
    asked_for = machine.assumed()
    machine.forget_assumptions()
    try:
        with check("the table decides which machine skips a tagged check"):
            # Both entries list the Ultimate II+ only.
            for name in (machine.MONITOR_D_KEY_RESERVED,
                         machine.IDENT_SWITCHES_LIVE):
                for product, expected in (("Ultimate 64 Elite", True),
                                          ("C64 Ultimate", True),
                                          ("Ultimate II+L", False)):
                    found = machine.classify(product).has_fix(name)
                    if found != expected:
                        raise Failure(f"{product!r} and {name}: expected has_fix="
                                      f"{expected}, got {found}")

        with check("a fix no entry lists is one every machine has"):
            # What makes a propagated fix a one-line deletion: with the entry
            # gone every check tagged with it runs everywhere again, and no
            # suite is edited. It also means a mistyped tag runs the check
            # rather than skipping it for good, so the mistake shows up as a
            # failure on the machine that lacks the fix instead of hiding.
            for product in ("Ultimate 64 Elite", "Ultimate II+L", "C64 Ultimate"):
                if not machine.classify(product).has_fix("no-entry-defines-this"):
                    raise Failure(f"{product!r} skipped a check for a fix the "
                                  f"table does not list as missing anywhere")

        with check("a skipped check names the fix and the machine in its reason"):
            # A bare SKIP in a log of ninety checks says nothing anyone can act
            # on. The reason carries the tag to pass to --assume-fix and the
            # machine and version to compare against the table.
            lagging = machine.classify("Ultimate II+L", "3.15")
            reason = lagging.missing_fix(machine.MONITOR_D_KEY_RESERVED)
            for needle in ("monitor-d-key-reserved", "Ultimate II+L", "3.15"):
                if reason is None or needle not in reason:
                    raise Failure(f"expected {needle!r} in the skip reason, "
                                  f"got {reason!r}")
            current = machine.classify("C64 Ultimate", "1.2.0")
            if current.missing_fix(machine.MONITOR_D_KEY_RESERVED) is not None:
                raise Failure("a machine that has the fix was given a reason to skip")

        with check("skip_without_fix answers True only where the check cannot run"):
            # The one line a tagged check needs: it reports the skipped check
            # itself, through the same check_start and check_skip pair every
            # other skip in the tree uses, and the caller returns on True.
            # Called from inside this check the line it reports is nested, so
            # the report library holds it back and only the answer is visible.
            lagging = machine.classify("Ultimate II+L", "3.15")
            current = machine.classify("C64 Ultimate", "1.2.0")
            if not lagging.skip_without_fix(machine.MONITOR_D_KEY_RESERVED, "fixture"):
                raise Failure("a machine without the fix was not skipped")
            if current.skip_without_fix(machine.MONITOR_D_KEY_RESERVED, "fixture"):
                raise Failure("a machine with the fix was skipped anyway")

        with check("an assumed fix runs the checks it gates, which is how a "
                   "backport is found"):
            machine.forget_assumptions()
            lagging = machine.classify("Ultimate II+L", "3.15")
            machine.assume(machine.MONITOR_D_KEY_RESERVED)
            if not lagging.has_fix(machine.MONITOR_D_KEY_RESERVED):
                raise Failure("the assumed fix still skipped its checks")
            if lagging.has_fix(machine.IDENT_SWITCHES_LIVE):
                raise Failure("assuming one fix ran the checks of another")
            machine.forget_assumptions()
            machine.assume(machine.ASSUME_ALL)
            missing = [name for name in machine.FIXES if not lagging.has_fix(name)]
            if missing:
                raise Failure(f"assuming every fix left {missing} skipped")

        with check("an assumption list is parsed as a list, and a typo is refused"):
            machine.forget_assumptions()
            listed = machine.parse_assumptions(
                f"{machine.MONITOR_D_KEY_RESERVED}, "
                f"{machine.IDENT_SWITCHES_LIVE}")
            if listed != {machine.MONITOR_D_KEY_RESERVED,
                          machine.IDENT_SWITCHES_LIVE}:
                raise Failure(f"expected both fixes, got {sorted(listed)}")
            # A misspelt name that was quietly ignored would leave the checks
            # skipped, which is the answer the flag was run to get past, and
            # the run would look exactly like one where the fix had not landed.
            try:
                machine.parse_assumptions("monitor-d-key-reserve")
            except machine.UnknownFix:
                pass
            else:
                raise Failure("a misspelt fix name was accepted")
    finally:
        machine.forget_assumptions()
        if asked_for:
            machine.assume(*asked_for)


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


def draw_framed_window(chars: bytearray, colours: bytearray, top: int,
                       bottom: int, left: int, right: int,
                       items: Sequence[str], selected: int,
                       listing_colour: int, selected_colour: int) -> None:
    """Draw a framed window with its own cursor row over an existing screen.

    The frame codes and geometry are those an Ultimate II+L was measured to
    return for the F5 task menu: corners at columns 5 and 36, rules on rows 7
    and 16, and the eight items between them.
    """
    for column in range(left + 1, right):
        chars[top * SCREEN_WIDTH + column] = BOX_HORIZONTAL
        chars[bottom * SCREEN_WIDTH + column] = BOX_HORIZONTAL
    # The first of each machine's two corner sets; find_open_window accepts
    # either, and a check below draws the other to prove it.
    chars[top * SCREEN_WIDTH + left] = BOX_TOP_LEFT[0]
    chars[top * SCREEN_WIDTH + right] = BOX_TOP_RIGHT[0]
    chars[bottom * SCREEN_WIDTH + left] = BOX_BOTTOM_LEFT[0]
    chars[bottom * SCREEN_WIDTH + right] = BOX_BOTTOM_RIGHT[0]
    # Every row inside the frame carries its verticals, whether or not an item
    # was drawn on it; a window taller than its listing still has sides.
    for row in range(top + 1, bottom):
        chars[row * SCREEN_WIDTH + left] = BOX_VERTICAL
        chars[row * SCREEN_WIDTH + right] = BOX_VERTICAL
    # The frame's own cells carry a colour like every other cell, and it is
    # neither the listing's nor the cursor's: on an Ultimate II+L task menu
    # the items were 28 cells of colour 1 or 12 and the frame was drawn in
    # colour 13. Leaving these at zero would make the frame rows look like
    # rows of a colour nothing else uses, which is what a cursor row looks
    # like; drawing them in the listing colour would instead make every
    # unselected row two cells wider than the row under the cursor.
    for row in range(top, bottom + 1):
        for column in (left, right):
            colours[row * SCREEN_WIDTH + column] = FRAME_COLOUR
    for column in range(left + 1, right):
        colours[top * SCREEN_WIDTH + column] = FRAME_COLOUR
        colours[bottom * SCREEN_WIDTH + column] = FRAME_COLOUR
    for index, text in enumerate(items):
        row = top + 1 + index
        code = selected_colour if index == selected else listing_colour
        for column in range(left + 1, right):
            offset = row * SCREEN_WIDTH + column
            position = column - left - 1
            chars[offset] = ord(text[position]) if position < len(text) else 0x20
            colours[offset] = code


def run_overlay_row_checks() -> None:
    """Reading the cursor out of a framed window drawn over a listing.

    Both the window and the browser underneath it draw a cursor row, so the
    answer depends on the scan being restricted to the window. Modelled on the
    Ultimate II+L F5 task menu, whose planes were captured from the device:
    browser cursor on row 2, menu cursor on row 8, both colour 1, no
    background nibble anywhere.
    """
    entries = ["Flash   Flash Disk             Ready",
               "Temp    RAM Disk               Ready",
               "USB0    SanDisk 3.2Gen1        Ready",
               "Ftp     Remote FTP Servers     Ready",
               "Net0    MAC 02:15:41:33:C4:51  Link Down",
               "WiFi    IP: 192.168.1.99       Link Up"]
    items = ["Assembly 64", "C64 Machine", "Built-in Drive A", "Built-in Drive B",
             "Software IEC", "Printer", "Configuration", "Developer"]

    with check("a plain listing reports no framed window"):
        chars, colours = build_menu_planes(entries, selected=0, listing_colour=12,
                                           selected_colour=1)
        found = find_open_window(chars, ROOT_ENTRY_ROWS_REST)
        if found != whole_screen(ROOT_ENTRY_ROWS_REST):
            raise Failure(f"expected the whole screen, got {found}")

    with check("a framed window's own cursor is read, not the browser's"):
        chars, colours = build_menu_planes(entries, selected=0, listing_colour=12,
                                           selected_colour=1)
        chars, colours = bytearray(chars), bytearray(colours)
        draw_framed_window(chars, colours, top=7, bottom=16, left=5, right=36,
                           items=items, selected=0, listing_colour=12,
                           selected_colour=1)
        found = find_open_window(bytes(chars), ROOT_ENTRY_ROWS_REST)
        if found.rows != range(8, 16):
            raise Failure(f"expected the window interior rows 8-15, got {found}")
        row = find_selected_row_rest(bytes(chars), bytes(colours),
                                     ROOT_ENTRY_ROWS_REST)
        if row != 8:
            raise Failure(f"expected the window's cursor row 8, got {row}")

    with check("a cursor further down a framed window is read"):
        chars, colours = build_menu_planes(entries, selected=0, listing_colour=12,
                                           selected_colour=1)
        chars, colours = bytearray(chars), bytearray(colours)
        draw_framed_window(chars, colours, top=7, bottom=16, left=5, right=36,
                           items=items, selected=5, listing_colour=12,
                           selected_colour=1)
        row = find_selected_row_rest(bytes(chars), bytes(colours),
                                     ROOT_ENTRY_ROWS_REST)
        if row != 13:
            raise Failure(f"expected the window's cursor row 13, got {row}")

    with check("the menu's header rule is not taken for a framed window"):
        # The browser draws a run of the same horizontal code across row 1.
        # Without corners it is a rule, not a frame, and taking it for one
        # would narrow every plain browser read to the rows below it.
        chars, colours = build_menu_planes(entries, selected=2, listing_colour=12,
                                           selected_colour=1)
        chars = bytearray(chars)
        for column in range(SCREEN_WIDTH):
            chars[SCREEN_WIDTH + column] = BOX_HORIZONTAL
        if find_open_window(bytes(chars), ROOT_ENTRY_ROWS_REST) != whole_screen(
                ROOT_ENTRY_ROWS_REST):
            raise Failure("the header rule was taken for a framed window")
        row = find_selected_row_rest(bytes(chars), colours, ROOT_ENTRY_ROWS_REST)
        if row != ROOT_ENTRY_ROWS_REST[0] + 2:
            raise Failure(f"expected row {ROOT_ENTRY_ROWS_REST[0] + 2}, got {row}")

    with check("a titled window's cursor is read, not its title"):
        # The Select Path picker showing /Temp/, captured from an Ultimate
        # II+L. The title and the cursor row each carry a colour no other row
        # does, and both run the full inside width of the frame, so no colour
        # rule and no width rule can separate them. The title is centred and
        # every listing row is left-aligned against the frame, which is what
        # tells them apart. Reproduced live before the title was excluded: the
        # picker's cursor read as the title row and the walk never moved.
        chars = bytearray(b" " * SCREEN_CELLS)
        colours = bytearray(SCREEN_CELLS)
        listing = ["<< Select Current Dir >>",
                   "prgmenu46523tgt             DIR",
                   "prgmenu46523.d64            D64  171K",
                   "prgmenu46523.prg            PRG   67",
                   "prgmenu46763.d64            D64  171K",
                   "prgmenu46763.prg            PRG   67"]
        draw_framed_window(chars, colours, top=2, bottom=23, left=0, right=39,
                           items=["          Select Path", *listing],
                           selected=1, listing_colour=12, selected_colour=1)
        for column in range(1, SCREEN_WIDTH - 1):
            colours[3 * SCREEN_WIDTH + column] = 6
        found = find_open_window(bytes(chars), ROOT_ENTRY_ROWS_REST)
        if found.rows != range(4, 23):
            raise Failure(f"expected the listing rows 4-22, got {found}")
        row = find_selected_row_rest(bytes(chars), bytes(colours),
                                     ROOT_ENTRY_ROWS_REST)
        if row != 4:
            raise Failure(f"expected the picker's cursor row 4, got {row}")

    with check("a window's blank rows never win the cursor colour"):
        # The Select Path picker over an empty directory, captured from an
        # Ultimate II+L: the window paints every row it is not using in the
        # same colour it marks the cursor with. The one entry and all the
        # blank rows below it then carry the same number of cursor-coloured
        # cells, the count ties, and the tie breaks on the row number, so the
        # cursor read as the last blank row. Reproduced live: the picker's
        # only entry was on screen and could not be selected.
        chars = bytearray(b" " * SCREEN_CELLS)
        colours = bytearray(SCREEN_CELLS)
        draw_framed_window(chars, colours, top=2, bottom=23, left=0, right=39,
                           items=["          Select Path", "<< Select Current Dir >>"],
                           selected=1, listing_colour=12, selected_colour=1)
        for column in range(1, SCREEN_WIDTH - 1):
            colours[3 * SCREEN_WIDTH + column] = 6
        for row in range(5, 23):
            for column in range(1, SCREEN_WIDTH - 1):
                colours[row * SCREEN_WIDTH + column] = 1
        row = find_selected_row_rest(bytes(chars), bytes(colours),
                                     ROOT_ENTRY_ROWS_REST, cursor_colour=1)
        if row != 4:
            raise Failure(f"expected the picker's only entry on row 4, got {row}")

    with check("a dialog inside a picker is read, not the picker"):
        # What the screen looks like after a copy completes: the Select Path
        # picker is still drawn and an Ok dialog sits inside it. The dialog
        # has the keyboard, so it is the window a caller is asking about.
        chars = bytearray(b" " * SCREEN_CELLS)
        colours = bytearray(SCREEN_CELLS)
        draw_framed_window(chars, colours, top=2, bottom=23, left=0, right=39,
                           items=["          Select Path", "<< Select Current Dir >>"],
                           selected=1, listing_colour=12, selected_colour=1)
        draw_framed_window(chars, colours, top=10, bottom=14, left=12, right=27,
                           items=["Copy complete.", "", "      Ok"],
                           selected=2, listing_colour=12, selected_colour=1)
        found = find_open_window(bytes(chars), ROOT_ENTRY_ROWS_REST)
        if found.rows != range(11, 14):
            raise Failure(f"expected the dialog's rows 11-13, got {found}")
        # Two drawn rows, each carrying a colour the other does not, is the
        # tie the odd-colour rule cannot break, so this is asked the way a
        # Browser asks it once the machine's cursor colour has been measured.
        row = find_selected_row_rest(bytes(chars), bytes(colours),
                                     ROOT_ENTRY_ROWS_REST, cursor_colour=1)
        if row != 13:
            raise Failure(f"expected the dialog's Ok row 13, got {row}")

    with check("an open window is what the cursor colour is measured on"):
        # Two listings on screen, the browser's and the task menu's, each with
        # a cursor and both in the machine's colour. Asked about the whole
        # screen, the colour is found twice and no colour is learnt at all;
        # asked about the open window, it is learnt. A caller that opens a
        # form straight from the task menu has no other chance to learn it,
        # and a form's marking is too narrow for the rules that work without
        # one.
        chars, colours = build_menu_planes(entries, selected=0, listing_colour=12,
                                           selected_colour=1)
        chars, colours = bytearray(chars), bytearray(colours)
        draw_framed_window(chars, colours, top=7, bottom=16, left=5, right=36,
                           items=items, selected=3, listing_colour=12,
                           selected_colour=1)
        blind = find_cursor_colour(bytes(chars), bytes(colours),
                                   whole_screen(ROOT_ENTRY_ROWS_REST))
        if blind is not None:
            raise Failure(f"expected the whole screen to teach nothing, got {blind!r}")
        measured = measure_cursor_colour(bytes(chars), bytes(colours),
                                         ROOT_ENTRY_ROWS_REST)
        if measured != 1:
            raise Failure(f"expected the cursor colour 1, got {measured!r}")

    with check("a form teaches no cursor colour at all"):
        # The Assembly 64 query form again. Thirteen field rows of one colour
        # and one button row below them of another reads exactly like a
        # listing whose cursor is on its last row, so the odd-colour rule has
        # an answer here and it is the wrong one. The answer is kept for the
        # session, so learning it here pointed every later read at the button.
        # The button's colour runs the full inside width while a field's stops
        # short, and a listing draws its cursor row like the rest, so the
        # button is rejected and nothing is learnt.
        chars = bytearray(b" " * SCREEN_CELLS)
        colours = bytearray(SCREEN_CELLS)
        fields = ["Name:", "Group:", "Handle:", "Event:", "Repo:", "Category:"]
        draw_framed_window(chars, colours, top=2, bottom=23, left=0, right=39,
                           items=["      Assembly 64 Query Form", ""]
                                 + [f.ljust(10) + "_" * 18 for f in fields]
                                 + ["", "", "            << Search >>"],
                           selected=-1, listing_colour=11, selected_colour=11)
        for index in range(len(fields)):
            row = 5 + index
            for column in range(SCREEN_WIDTH - 11, SCREEN_WIDTH - 1):
                colours[row * SCREEN_WIDTH + column] = 1 if index == 0 else 12
        for column in range(1, SCREEN_WIDTH - 1):
            colours[13 * SCREEN_WIDTH + column] = 12
        measured = measure_cursor_colour(bytes(chars), bytes(colours),
                                         ROOT_ENTRY_ROWS_REST)
        if measured is not None:
            raise Failure(f"expected the form to teach nothing, got {measured!r}")

    with check("a menu beside a highlighted row is read, not the row"):
        # A context menu opened on a browser row is drawn to the right of that
        # row's text, not over it, so it shares screen rows with the row it
        # was opened on. Captured from an Ultimate II+L with the menu on the
        # Ftp row: the menu occupied rows 5 to 7 and columns 29 to 38, the
        # browser's highlight on row 5 ran 30 cells and the menu item on row 7
        # ran 10, so narrowing to the menu's rows alone still returned row 5
        # and the walk to "New Host" never moved.
        menu = ["Enter", "Copy to...", "New Host"]
        chars, colours = build_menu_planes(entries, selected=3, listing_colour=12,
                                           selected_colour=1)
        chars, colours = bytearray(chars), bytearray(colours)
        draw_framed_window(chars, colours, top=4, bottom=8, left=28, right=39,
                           items=menu, selected=2, listing_colour=12,
                           selected_colour=1)
        found = find_open_window(bytes(chars), ROOT_ENTRY_ROWS_REST)
        if found.rows != range(5, 8) or (found.first_column, found.last_column) != (29, 39):
            raise Failure(f"expected rows 5-7 and columns 29-38, got {found}")
        row = find_selected_row_rest(bytes(chars), bytes(colours),
                                     ROOT_ENTRY_ROWS_REST, cursor_colour=1)
        if row != 7:
            raise Failure(f"expected the menu's New Host row 7, got {row}")

    with check("a window with rounded corners is a window too"):
        # screen.h defines two corner sets and BORD_*_CORNER picks one: sharp
        # on an Ultimate 64 and an Ultimate II+, rounded on a C64 Ultimate.
        # A parser that knows only the sharp set finds no frame at all on a
        # C64 Ultimate, so every window there reads as the whole screen.
        chars, colours = build_menu_planes(entries, selected=0, listing_colour=12,
                                           selected_colour=1)
        chars, colours = bytearray(chars), bytearray(colours)
        draw_framed_window(chars, colours, top=7, bottom=16, left=5, right=36,
                           items=items, selected=3, listing_colour=12,
                           selected_colour=1)
        for row, column, sharp, rounded in (
                (7, 5, BOX_TOP_LEFT, BOX_TOP_LEFT),
                (7, 36, BOX_TOP_RIGHT, BOX_TOP_RIGHT),
                (16, 5, BOX_BOTTOM_LEFT, BOX_BOTTOM_LEFT),
                (16, 36, BOX_BOTTOM_RIGHT, BOX_BOTTOM_RIGHT)):
            if chars[row * SCREEN_WIDTH + column] != sharp[0]:
                raise Failure(f"expected the sharp corner at {row},{column}")
            chars[row * SCREEN_WIDTH + column] = rounded[1]
        found = find_open_window(bytes(chars), ROOT_ENTRY_ROWS_REST)
        if found.rows != range(8, 16):
            raise Failure(f"expected the same rows 8-15 as with sharp corners, got {found}")
        row = find_selected_row_rest(bytes(chars), bytes(colours),
                                     ROOT_ENTRY_ROWS_REST, cursor_colour=1)
        if row != 11:
            raise Failure(f"expected the window's cursor row 11, got {row}")

    with check("an untitled window keeps its first row"):
        # The task menu has no title, so its first interior row is a menu item
        # and excluding it would lose the item the cursor starts on.
        chars, colours = build_menu_planes(entries, selected=0, listing_colour=12,
                                           selected_colour=1)
        chars, colours = bytearray(chars), bytearray(colours)
        draw_framed_window(chars, colours, top=7, bottom=16, left=5, right=36,
                           items=items, selected=0, listing_colour=12,
                           selected_colour=1)
        found = find_open_window(bytes(chars), ROOT_ENTRY_ROWS_REST)
        if found.rows != range(8, 16):
            raise Failure(f"expected rows 8-15 with no title dropped, got {found}")

    with check("a background-marked window is read on a background-marked browser"):
        # An Ultimate 64 marks both cursors with a background nibble, and the
        # browser's row is the wider of the two, so a plain strongest-mark
        # comparison returns the browser row while a context menu is open.
        chars, colours = build_menu_planes(entries, selected=0, listing_colour=12,
                                           selected_colour=1, background=6)
        chars, colours = bytearray(chars), bytearray(colours)
        draw_framed_window(chars, colours, top=7, bottom=16, left=5, right=36,
                           items=items, selected=3, listing_colour=12,
                           selected_colour=(6 << 4) | 1)
        row = find_selected_row_rest(bytes(chars), bytes(colours),
                                     ROOT_ENTRY_ROWS_REST)
        if row != 11:
            raise Failure(f"expected the window's cursor row 11, got {row}")


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

    with check("a volume header in reverse video is not taken for a cursor"):
        # The same disk image listing as the device draws it. The volume
        # header is in reverse video, which is styling: the browser marks its
        # cursor row by colour and never by the reverse-video bit. Measured on
        # an Ultimate II+L, cursor on the program: the volume row carried 28
        # reverse cells and none of the cursor colour, and the program row the
        # other way round. Ranking reverse video above the machine's own
        # marking returned the volume header and the program was never
        # selectable.
        chars = bytearray(b" " * SCREEN_CELLS)
        colours = bytearray(SCREEN_CELLS)
        volume = ROOT_ENTRY_ROWS_REST[0]
        program = volume + 1
        for row, text, code in (
                (volume, "DMATEST           64 2A       VOLUME", 6),
                (program, "DMATESTPROGRAM01              PRG  254", 1)):
            start = row * SCREEN_WIDTH
            padded = text.ljust(SCREEN_WIDTH).encode("ascii")
            for column in range(SCREEN_WIDTH):
                glyph = padded[column]
                # The volume header's cells carry the reverse-video bit.
                chars[start + column] = glyph | 0x80 if row == volume else glyph
                colours[start + column] = code
        row = find_selected_row_rest(bytes(chars), bytes(colours),
                                     ROOT_ENTRY_ROWS_REST, cursor_colour=1)
        if row != program:
            raise Failure(f"expected the program row {program}, got {row}")

    with check("a form marks a field, not a row, and the field is found"):
        # The Assembly 64 query form, captured from an Ultimate II+L: labelled
        # fields inside a framed window. Every row is drawn the same way, 28
        # cells of the listing colour and a ten-cell value field, so no row
        # stands out by width or by its commonest colour. Only the value field
        # of the row under the cursor carries the cursor colour, and ten cells
        # is under the minimum a row highlight has to clear. Nothing else on
        # the screen carries that colour, and that is what identifies the
        # field.
        chars = bytearray(b" " * SCREEN_CELLS)
        colours = bytearray(SCREEN_CELLS)
        fields = ["Name:", "Group:", "Handle:", "Event:", "Repo:", "Category:"]
        cursor_field = 2
        draw_framed_window(chars, colours, top=2, bottom=23, left=0, right=39,
                           items=["      Assembly 64 Query Form", ""]
                                 + [f.ljust(10) + "_" * 18 for f in fields],
                           selected=-1, listing_colour=11, selected_colour=11)
        for index in range(len(fields)):
            row = 5 + index
            for column in range(SCREEN_WIDTH - 11, SCREEN_WIDTH - 1):
                colours[row * SCREEN_WIDTH + column] = (
                    1 if index == cursor_field else 12)
        found = find_open_window(bytes(chars), ROOT_ENTRY_ROWS_REST)
        if found.rows != range(4, 23):
            raise Failure(f"expected the form's rows 4-22, got {found}")
        row = find_selected_row_rest(bytes(chars), bytes(colours),
                                     ROOT_ENTRY_ROWS_REST, cursor_colour=1)
        if row != 5 + cursor_field:
            raise Failure(f"expected the Handle: field on row {5 + cursor_field}, "
                          f"got {row}")

    with check("reverse video still marks the cursor when nothing else does"):
        # With no cursor colour measured for the machine, reverse video is the
        # only marking on the screen and stays the answer.
        chars = bytearray(b" " * SCREEN_CELLS)
        colours = bytearray(SCREEN_CELLS)
        marked = ROOT_ENTRY_ROWS_REST[0] + 1
        for index, text in enumerate(entries):
            row = ROOT_ENTRY_ROWS_REST[0] + index
            start = row * SCREEN_WIDTH
            padded = text.ljust(SCREEN_WIDTH).encode("ascii")
            for column in range(SCREEN_WIDTH):
                glyph = padded[column]
                chars[start + column] = glyph | 0x80 if row == marked else glyph
        row = find_selected_row_rest(bytes(chars), bytes(colours),
                                     ROOT_ENTRY_ROWS_REST)
        if row != marked:
            raise Failure(f"expected the reverse-video row {marked}, got {row}")

    with check("a listing of three or more entries says which colour is the cursor"):
        chars, colours = build_menu_planes(entries, selected=2, listing_colour=12,
                                           selected_colour=1)
        measured = find_cursor_colour(chars, colours,
                                      whole_screen(ROOT_ENTRY_ROWS_REST))
        if measured != 1:
            raise Failure(f"expected the cursor colour 1, got {measured!r}")

    with check("a background-marked screen teaches no foreground cursor colour"):
        # An Ultimate 64 marks the cursor row with a background nibble, and
        # those cells are not counted as foreground at all, so the colour a
        # foreground scan would find there belongs to some unselected row.
        # Taking it would then point every later read at the wrong row.
        chars, colours = build_menu_planes(entries, selected=2, listing_colour=12,
                                           selected_colour=1, background=6)
        measured = find_cursor_colour(chars, colours,
                                      whole_screen(ROOT_ENTRY_ROWS_REST))
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
    # Every machine names itself in its banner, and the case differs: an
    # Ultimate 64 writes "Ultimate 64 Elite", a C64 Ultimate "COMMODORE 64
    # ULTIMATE". What is being checked is that the banner is there at all.
    snapshot.find_line_containing("ultimate", ignore_case=True)
    # The path row, not any line holding a "/": a C64 Ultimate's launcher
    # status row reads "WASD=NAV F1=MENU F3/F5=PGUP/DN F7=HELP", which holds
    # two of them, so a substring test accepts the launcher as the browser.
    path_row(snapshot)


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

    The character goes out the way the browser's own quick-seek sends it, so
    this exercises the respelling a machine set to WASD Cursors needs rather
    than working only for the letters that transform leaves alone. See
    tests/lib/navigation.py.
    """
    snapshot = backend.send_char(backend.navigation.menu_char(character))
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

    with check("the task-menu key opens it and RUN/STOP restores the browser"):
        # The browser marks the selected row by colour, not by the character
        # matrix's reverse-video bit, and the two transports use different
        # colour encodings (a real C64 colour nibble over REST, an
        # ANSI-mapped approximation over Telnet's VT100 stream), so a cursor
        # key alone is not a transport-uniform text signal. Opening the task
        # menu replaces the visible text outright, which both transports
        # render identically in the character matrix.
        #
        # Which key that is depends on the machine, and asking the wrong one
        # looks exactly like a broken transport: a C64 Ultimate puts paging on
        # F5, so pressing it over a listing that fits on one screen changes
        # nothing at all. See tests/lib/machine.py.
        key = backend.machine.task_menu_key
        before = backend.capture().text()
        opened = backend.send_key(key).text()
        if opened == before:
            raise Failure(f"{key} had no visible effect on the screen; it was\n{before}")
        closed = backend.send_key("RUNSTOP").text()
        if closed != before:
            raise Failure(f"RUN/STOP did not restore the original screen after "
                          f"{key}; expected\n{before}\nactual\n{closed}")

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
    cli.add_device_arguments(parser, timeout=5.0, colour=False)
    parser.add_argument("--telnet-port", type=int, default=int(os.environ.get("U64_TELNET_PORT", "23")))
    args = parser.parse_args()

    try:
        section("Machine identification")
        run_machine_checks()

        section("Overlay navigation planner")
        run_navigation_planner_checks()

        section("Cursor row from the colour plane")
        run_selected_row_checks()

        section("Cursor row under a framed window")
        run_overlay_row_checks()

        # Overlay first, and unconditionally: it is the transport every
        # profile sweeps, so it is the one a run is certain to depend on.
        section("REST backend, Interface Type = Overlay on HDMI")
        with check("REST/Overlay: connect, navigate, teardown"):
            run_rest_smoke(args.host, args.password, args.timeout, "Overlay on HDMI")

        # The other two cost about 2.5s each, which was most of this suite, and
        # neither transport is swept by the smoke profile. They still run from
        # quick up, so the facade is proved on all three before anything built
        # on it does: what smoke drops is proving transports it will not use.
        section("Telnet backend")
        if not profiles.skip_below(profiles.QUICK,
                                   "Telnet: connect, navigate, teardown"):
            with check("Telnet: connect, navigate, teardown"):
                run_telnet_smoke(args.host, args.telnet_port, args.password,
                                 args.timeout)

        section("REST backend, Interface Type = Freeze")
        freeze_label = "REST/Freeze: connect, navigate, teardown"
        if not profiles.skip_below(profiles.QUICK, freeze_label):
            with check(freeze_label):
                run_rest_smoke(args.host, args.password, args.timeout, "Freeze")
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
