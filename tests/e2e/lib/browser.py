#!/usr/bin/env python3
# Driving the on-device file browser, over whichever backend.

"""Driving the on-device file browser, over whichever backend.

Navigation, selection reading, popup handling and typing, written
against `Backend` so the same steps work over REST and over Telnet.
"""

import os
import sys
from pathlib import Path

# The one stanza that puts the shared library on sys.path; see tests/lib/bootstrap.py.
sys.path.insert(0, str(next(p for p in Path(__file__).resolve().parents
                            if (p / "tests" / "lib").is_dir()) / "tests" / "lib"))
from report import Failure
from collections.abc import Sequence
from report import detail
import pacing
import re
import time
from backend import (Backend, FRAME_CHARS, Snapshot,
    char_to_combo, plan_overlay_navigation, strip_frame)


# A browser row's rendered size, as size_str.cc writes it: up to four digits
# and an optional K or M. Never a menu item, so a label that looks like this
# came from the listing an overlay was drawn over. Here rather than in
# backend.py because this is the only reader: it describes a listing row, and
# only a Browser reads listing rows.
SIZE_COLUMN_RE = re.compile(r"\d{1,4}[KM]?")


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

    def rows(self) -> list[str]:
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

    # -- moving the cursor a given number of rows --
    #
    # The browser binds a page key to half a window
    # (TreeBrowser::handle_key, state->up/down(window->get_size_y()/2)), and
    # the window is the listing area, so one page key is worth
    # len(entry_rows) // 2 single steps. Which physical key that is depends on
    # the machine: F1/F7 on an Ultimate 64 and an Ultimate II+, F3/F5 on a
    # C64 Ultimate, which Machine.page_up_key and page_down_key answer.
    #
    # For a cartridge target the keys are injected into the computer's matrix
    # but read by the cartridge's firmware, so the roles are the cartridge's.
    # backend.machine already asks the device rather than the computer, so
    # nothing here needs to allow for it.
    def page_rows(self) -> int:
        """How many rows one page key moves, for this transport's window."""
        return max(1, len(self.entry_rows) // 2)

    def move_rows(self, rows: int) -> Snapshot | None:
        """Move the cursor `rows` rows, down when positive, in one request.

        The displacement is exactly `rows` either way: the page keys carry
        whole strides and single steps carry the remainder, so this is a
        cheaper spelling of press_many rather than a different movement. On a
        cartridge target, where each injected key costs a fixed drain, a
        22-row advance falls from 22 keys to 12.

        One request for the mixed sequence, not one per kind: splitting it
        would pay a second round trip and a second settle, which is most of
        what the page keys save.
        """
        if rows == 0:
            return None
        step = "DOWN" if rows > 0 else "UP"
        page = (self.backend.machine.page_down_key if rows > 0
                else self.backend.machine.page_up_key)
        pages, singles = divmod(abs(rows), self.page_rows())
        keys = [page] * pages + [step] * singles
        return self.backend.send_key_sequence(
            keys, f"{step} x{abs(rows)} rows")

    # How many rounds of page keys go_to_top will send before giving up. Six
    # rounds of a 22-row window is 132 rows, past any listing a suite builds.
    TOP_ROUNDS = 6

    def go_to_top(self, count: int = 14) -> None:
        """Put the cursor on the first entry of the listing.

        This used to send `count` UP keys and stop, which reaches the top only
        when the cursor was already within `count` rows of it. Everything
        built on it inherited that: select_entry's scan started wherever the
        cursor happened to be rather than at the top, so an entry above that
        point was not in the part of the listing it searched.

        Page keys make the honest version affordable. Each round covers a
        whole window for two keystrokes, and the rounds stop as soon as one
        changes nothing, which is what being at the top looks like. A cursor
        already at the top costs one request, the same as before.

        `count` is the least it will move, kept so a caller that wants a known
        minimum rewind still gets one.
        """
        stride = self.page_rows()
        rows = max(stride * 2, -(-count // stride) * stride)
        # A round that changed nothing is what the top of the listing looks
        # like. Read from the send itself rather than from a screen this method
        # captures for the purpose: an extra read per rewind put 12% more
        # requests on the wire across a full run, and this device serves four
        # connections at a time.
        for _ in range(self.TOP_ROUNDS):
            self.move_rows(-rows)
            if not self.backend.last_key_changed:
                return

    def select_entry(self, prefix: str, max_steps: int = 30, timeout: float = 3.0,
                     contains: bool = False) -> None:
        """Put the cursor on the listing entry starting with `prefix`.

        `contains=True` matches anywhere in the row instead, for a listing
        whose rows carry more than the entry name: the /ftp node draws
        "Ftp  Remote FTP Servers", so a caller after the server list has a
        substring and not a prefix. Quick-seek types the string at the
        firmware, which can only match from the start, so a contains search
        skips it and walks the rows.

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
        if contains:
            while True:
                self.go_to_top()
                for _ in range(screens):
                    if self._select_visible(prefix, contains=True):
                        return
                    # The same stride and the same end-of-listing test as the
                    # prefix path below: one row of overlap so a move cannot
                    # step over a row, and a move that changed nothing means
                    # the listing has been walked to its end. Without that
                    # test a short listing was rescanned until the timeout
                    # rather than answered as soon as it ran out.
                    self.move_rows(visible - 1)
                    if not self.backend.last_key_changed:
                        break
                if time.monotonic() >= deadline:
                    raise Failure(
                        f"no entry containing {prefix!r} in {self.current_path()!r}; "
                        f"screen was:\n{self.screen()}")
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
                self.move_rows(visible - 1)
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

    def _select_visible(self, prefix: str, contains: bool = False) -> bool:
        """Move the cursor onto a matching entry on the current screen.

        Returns False when no visible row matches, or when the cursor did not
        end up where the screen said it should, which a repaint between the
        jump and the check can cause. The caller retries rather than trusting
        the move, so the result is confirmed by reading the cursor back.
        """
        def matches(text: str) -> bool:
            return prefix in text if contains else text.startswith(prefix)

        row, rows = self.backend.selection_and_rows(self.entry_rows)
        for index in self.entry_rows:
            if index < len(rows) and matches(strip_frame(rows[index])):
                # Same listing, so the page keys apply here too, and this
                # jump is up to a whole window long. The landing is read back
                # below either way, which is what makes the cheaper spelling
                # safe to use on a jump the caller depends on.
                self.move_rows(index - row)
                return matches(self.selected_text())
        return False

    def enter(self) -> None:
        self.press("RIGHT")

    # Entering a directory is a keypress, and the header only says where the
    # browser is once the firmware has read the new listing and repainted.
    # Checking the header straight after the key read the previous directory
    # on every attempt on an Ultimate II+L driven through a C64 Ultimate,
    # where the same code passed on an Ultimate 64.
    DESCEND_TIMEOUT_SECONDS = 5.0

    def descend(self, directory: str) -> None:
        for part in [p for p in directory.strip("/").split("/") if p]:
            self.select_entry(part)
            self.enter()
        expected = "/" + directory.strip("/") + "/"
        deadline = time.monotonic() + self.DESCEND_TIMEOUT_SECONDS
        while True:
            current = self.current_path()
            if current == expected:
                return
            if time.monotonic() >= deadline:
                raise Failure(f"expected {expected!r}, got {current!r}")
            time.sleep(0.15)

    def go_to_directory(self, directory: str) -> None:
        self.go_to_root()
        self.descend(directory)

    # -- overlays (context menu / task menu / popups) --
    def overlay_items(self, before: list[str]) -> list[str]:
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
        # Required to be the same length: a transport that returned a
        # different row count (a Telnet screen mid-resize, a truncated REST
        # body) would otherwise have its tail ignored, and the short label
        # list reads as a shorter menu, which is how an off-by-two
        # selection got here once already.
        if len(before) != len(rows):
            raise Failure(f"the screen had {len(before)} rows before the "
                          f"popup and {len(rows)} after it")
        for old, new in zip(before, rows, strict=True):
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

    def wait_for_overlay(self, before: list[str]) -> list[str]:
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

    def open_context_menu(self) -> list[str]:
        before = self.rows()
        self.press("ENTER")
        labels = self.wait_for_overlay(before)
        if not labels:
            raise Failure(f"no context menu appeared; screen was:\n{self.screen()}")
        return labels

    def choose_overlay_item(self, labels: list[str], label: str) -> None:
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

    def open_task_menu(self, attempts: int = 2) -> list[str]:
        """Open the task menu and return its categories, retrying a lost key.

        A keystroke injected into a cartridge target travels through the
        computer's keyboard matrix, and one of them occasionally does not
        arrive: measured on u2@c64u, where a run failed with "no task menu
        appeared" against a screen still showing the browser it had been on,
        and the same suite passed on its next attempt.

        wait_for_overlay has already waited out the overlay-draw timeout by the
        time it answers nothing, so an empty result means the menu is not
        opening rather than not open yet, and pressing the key again is safe:
        it cannot close a menu that was never drawn.
        """
        for attempt in range(attempts):
            before = self.rows()
            self.press_task_menu()
            categories = self.wait_for_overlay(before)
            if categories:
                if attempt:
                    detail("the task-menu key had to be pressed twice; the "
                           "first one did not reach the machine")
                return categories
        return []

    def invoke_task_action(self, category: str, item: str) -> None:
        categories = self.open_task_menu()
        if not categories:
            raise Failure(f"no task menu appeared; screen was:\n{self.screen()}")
        if category not in categories:
            raise Failure(f"task menu has no {category!r}; it offers {categories}")
        before = self.rows()
        # Picked the same way the item below it is, rather than by pressing
        # DOWN as many times as the category's index into the parsed labels.
        # That index is only the row offset if the parse started exactly at the
        # cursor, and overlay_items is known to prepend an entry that is really
        # the listing underneath showing through: measured on an Ultimate 64 it
        # returned ['Up', 'Assembly 64', ...], and on u2@c64u the extra entry
        # opened Configuration where Developer was asked for, whose flash
        # actions then read as a menu missing its debug-log entries.
        # choose_overlay_item plans a quick-seek and a walk from where that
        # seek lands, both measured in the same list, so an entry the parse
        # added at the front cancels out of the difference.
        self.choose_overlay_item(categories, category)
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
        """Replace a string field's contents with `text` and accept it.

        `clear_taps` says how much there could be to remove, and is only used
        where the field has to be emptied one character at a time. Where the
        transport can send KEY_CLEAR the whole buffer goes in one keystroke
        whatever its length, which is the difference between one key and up to
        64 on a machine that drains injected keys at 100ms each.
        """
        if clear_taps:
            clear = self.backend.clear_field_key
            if clear:
                self.press(clear)
            else:
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
