#!/usr/bin/env python3
# Driving the on-device online-search query form, over whichever backend.

"""Driving the on-device online-search query form, over whichever backend.

Opening the form, discovering the fields it draws, classifying them, editing
them and submitting, written against `Backend` so the same steps work over
REST and over Telnet. The same idea as `browser.py`, for the one screen that
is not the file browser.

What the form contains is not knowledge this module holds. The firmware draws
four fixed text fields and then one field per preset the remote service
answered with (`BrowsableAssemblyRoot::getSubItems` in
software/userinterface/assembly_search.h appends one `BrowsableQueryField` per
`{type, values}` entry), so the field set follows the service, not the machine
and not the firmware. Measured with this module on 2026-09-07: Assembly 64
draws thirteen fields, and CommoServe draws fewer and not the same ones. Code
that named those labels would be asserting one service's current catalogue, so
on a machine whose service answers with a different set it fails on a missing
label instead of measuring the behaviour it is there for.

So the fields are read off the screen, and whether one is a dropdown is
settled by driving it rather than by a table: a preset field takes a value
when it is cycled and a free-text field does not. Which service a machine
searches, what the form is called and where the menu keeps it are properties
of the machine and stay in tests/lib/machine.py.

The two literals here are the firmware's own, not a service's. The submit row
is written by `BrowsableQueryField::getDisplayString` as "<<  Submit  >>" for
the field whose name is "$", and an empty field is drawn as a run of
underscores by the same function. Both are the same on every machine and for
every service.
"""

import re
import sys
from collections.abc import Callable, Sequence
from dataclasses import dataclass
from pathlib import Path

# The one stanza that puts the shared library on sys.path; see tests/lib/bootstrap.py.
sys.path.insert(0, str(next(p for p in Path(__file__).resolve().parents
                            if (p / "tests" / "lib").is_dir()) / "tests" / "lib"))
import bootstrap  # noqa: E402,F401

from backend import Backend, Snapshot, strip_frame  # noqa: E402
from menu import wait_until  # noqa: E402
from report import Failure  # noqa: E402

# The submit row, as BrowsableQueryField::getDisplayString writes it for the
# "$" field. Firmware text, identical on every machine and for every service.
SUBMIT_MARKER = "<<"
# An empty field is drawn as a run of underscores by the same function, so
# what is read back for one is not an empty string.
PLACEHOLDER_CHARS = "_ "

# A field row is "Label:" written at the first column inside the form's box,
# with the value ten columns further in. getDisplayString caps the name at
# eight characters plus the colon and capitalises its first letter, so the
# label is short and starts with a letter.
#
# Anchored on the box border rather than on the row start, and stopping at the
# next border. On REST the form is an overlay drawn over the root listing, so
# a row can carry the listing's text to the left of the box and its own status
# column to the right; on a C64 Ultimate the listing underneath is itself
# inside a framed window, so a row can hold two borders a side. Matching from
# a "|" and reading up to the next one takes the box's own text in both cases.
# The row start is accepted as well, for a Telnet session that renders the box
# flush against the left edge.
# Up to two spaces are allowed between the border and the label, because
# where the box's content starts is the window's business and not every
# machine's window draws it flush.
FIELD_RE = re.compile(r"(?:^|\| {0,2})([A-Za-z][A-Za-z0-9 ]{0,8}):([^|]*)")

# More than any form has selectable rows, so a walk always terminates.
FIELD_WALK_LIMIT = 30
# The form is fetched from the remote service, so it is far slower than a redraw.
OPEN_TIMEOUT = 25.0
# One retry, so a single slow fetch from a third-party server is not a failure.
OPEN_ATTEMPTS = 2
# More "-" presses than any preset list is long. updown() clamps at zero, so
# the extra presses are no-ops and the field lands on the first entry.
PRESET_REWIND = 40


class Unreachable(RuntimeError):
    """The form never appeared, so the service is most likely unreachable.

    A suite reports this as a skip rather than a failure: whether a
    third-party server answers is not what an E2E run of this firmware is
    measuring.
    """


@dataclass(frozen=True)
class Field:
    """One query field, as the form drew it.

    `label` carries the colon, because that is what the row shows and what
    tells a field row from prose. `row` is where it was on the screen the
    discovery read, and is re-read rather than remembered whenever the form
    is driven: an overlay redraw can move the box.
    """

    label: str
    row: int
    value: str

    @property
    def name(self) -> str:
        """The label without its colon, for a message."""
        return self.label.rstrip(":")


def label_row(rows: Sequence[str], label: str) -> int | None:
    """The screen row `label` is drawn on, or None.

    `in`, not `.startswith()`: on Telnet's one-row-shorter screen an overlay
    box renders a row higher than on REST, so a row can carry both the box's
    own label and, to its left, leftover text from whatever the root browser
    drew on that same row underneath. The label is still on the row, just no
    longer at its start.

    Here rather than in backend.py because the form is its main reader; a
    suite that has to find an entry on some other overlay uses it too rather
    than writing a third copy.
    """
    for row, text in enumerate(rows):
        if label in strip_frame(text):
            return row
    return None


class SearchForm:
    """The online-search query form, built on a Backend.

    `reach` is supplied by the caller and is the only machine-specific step:
    it navigates from wherever the UI is to having opened the form's menu
    entry. Where that entry lives differs per machine
    (`Machine.search_in_launcher`), and that is navigation, which is the
    machine's business rather than the form's.
    """

    def __init__(self, backend: Backend, mode: str, entry_rows: Sequence[int],
                 title: str, reach: Callable[[], None],
                 telnet_mode: str = "telnet") -> None:
        self.backend = backend
        self.mode = mode
        self.entry_rows = entry_rows
        self.title = title
        self.reach = reach
        self.telnet_mode = telnet_mode
        self._presets: tuple[Field, ...] | None = None
        self._texts: tuple[Field, ...] | None = None

    # ------------------------------------------------------------ screen --
    def capture(self) -> Snapshot | None:
        """The current screen, or None when the menu is closed."""
        try:
            return self.backend.capture()
        except Failure as exc:
            if str(exc).startswith("menu screen unavailable after"):
                return None
            raise

    def rows(self) -> list[str] | None:
        snapshot = self.capture()
        return None if snapshot is None else snapshot.lines

    def text(self) -> str:
        snapshot = self.capture()
        return "" if snapshot is None else snapshot.text()

    def visible(self) -> bool:
        return self.title in self.text()

    def describe_screen(self) -> str:
        """The non-blank rows, for a failure message that can be acted on."""
        rows = self.rows()
        if rows is None:
            return "    (menu screen unavailable)"
        lines = [f"    {n:02d}|{text}|" for n, text in enumerate(rows) if text.strip()]
        return "\n".join(lines) or "    (screen is blank)"

    # ------------------------------------------------------------- input --
    def press(self, key: str) -> None:
        # RUN/STOP legitimately closes the menu when it pops the last level off
        # the object stack. Backend.send_key settles by re-capturing the screen,
        # which raises "menu screen unavailable" in that case; the caller reads
        # the screen back afterwards either way, so that outcome is not a
        # failure here.
        try:
            self.backend.send_key(key)
        except Failure as exc:
            if not str(exc).startswith("menu screen unavailable after"):
                raise

    def press_many(self, key: str, count: int) -> None:
        if count <= 0:
            return
        try:
            self.backend.send_key_repeat(key, count)
        except Failure as exc:
            if not str(exc).startswith("menu screen unavailable after"):
                raise

    def type_text(self, text: str) -> None:
        self.backend.send_text(text, f"type {text}")

    # -------------------------------------------------------- opening it --
    def open(self, attempts: int = OPEN_ATTEMPTS,
             timeout: float = OPEN_TIMEOUT) -> None:
        """Reach the form and wait for its title.

        Opening it is a request to a third-party service, so it is retried
        once. A single slow or dropped fetch is a property of that server, not
        something a suite should report as a firmware defect.
        """
        for _ in range(attempts):
            self.reach()
            if wait_until(self.visible, timeout):
                # A reopened form is a new AssemblySearchForm, with every
                # preset at -1 and every text field empty, so what classify()
                # learnt describes values this one does not hold.
                self.forget_classification()
                return
        raise Unreachable(
            f"{self.title!r} did not appear within {timeout:.0f}s on "
            f"{attempts} attempts; the service is most likely unreachable "
            f"from the device. Last screen:\n{self.describe_screen()}")

    def leave(self, presses: int = 8) -> None:
        """RUN/STOP unwinds one level per press until the form is gone."""
        for _ in range(presses):
            if self.capture() is None or not self.visible():
                return
            self.press("RUNSTOP")
        raise Failure(f"{self.title!r} would not close")

    # ---------------------------------------------------------- the rows --
    def fields(self) -> list[Field]:
        """Every field the form currently draws, in the order it draws them.

        Read off the screen, so it is whatever this service answered with.
        Only the rows strictly between the title and the submit row are
        considered: below the box is the listing the form is drawn over, and
        a status row there ("/  -F3=HELP-") is prose, not a field.
        """
        rows = self.rows()
        if rows is None:
            raise Failure("the menu closed while reading the form")
        first = label_row(rows, self.title)
        last = label_row(rows, SUBMIT_MARKER)
        if first is None or last is None or last <= first:
            raise Failure(
                f"{self.title!r} is not on screen as a form: title row "
                f"{first!r}, submit row {last!r}. Screen was:\n"
                f"{self.describe_screen()}")
        found: list[Field] = []
        for row in range(first + 1, last):
            field = self._field_of(row, rows[row])
            if field is not None:
                found.append(field)
        if not found:
            raise Failure(
                f"{self.title!r} drew no query field between its title and its "
                f"submit row. Screen was:\n{self.describe_screen()}")
        return found

    def _field_of(self, row: int, text: str) -> Field | None:
        """The field on one row, or None when the row carries no field."""
        match = FIELD_RE.search(text)
        if match is None:
            return None
        return Field(label=f"{match.group(1)}:", row=row,
                     value=_clean_value(match.group(2)))

    def first_field(self) -> Field:
        """The first field the form draws, which is where a query term goes.

        Both services put a free-text field first, which `classify()` in
        scenario_dropdown_preserves_fields reports for whichever machine the
        run is aimed at; a service that put a dropdown first would show up
        there as the first field appearing in the dropdown list.
        """
        return self.fields()[0]

    def field(self, label: str) -> Field:
        """The named field as it is drawn now, re-read rather than remembered."""
        for field in self.fields():
            if field.label == label:
                return field
        raise Failure(
            f"the form has no {label!r} field, though discovery found it "
            f"before. Screen was:\n{self.describe_screen()}")

    def values(self) -> dict[str, str]:
        """What every field shows now, by label."""
        return {field.label: field.value for field in self.fields()}

    def expect_unchanged(self, expected: dict[str, str]) -> None:
        """Every field still shows what `expected` says, and none has gone."""
        current = self.values()
        for label, value in expected.items():
            if label not in current:
                raise Failure(f"the {label!r} field left the form")
            if current[label] != value:
                raise Failure(
                    f"{label} changed from {value!r} to {current[label]!r}")

    # ------------------------------------------------------ driving them --
    def cursor_row(self) -> int | None:
        """The row the selection sits on, or None when it cannot be read."""
        if self.mode == self.telnet_mode:
            return self._telnet_field_row()
        try:
            return self.backend.selected_row(self.entry_rows)
        except Failure:
            return None

    def no_cursor_reason(self) -> str:
        """Why cursor_row() answered None, which is two different faults."""
        if self.mode == self.telnet_mode and self.backend.selected_sgr is None:
            return ("the colour that marks a selection was never measured, so "
                    "no row can be read; see prime_selection_marker")
        return "the menu closed while moving the cursor"

    def focus(self, field: Field, what: str | None = None) -> Field:
        """Put the selection on a field, and answer where it actually is.

        The row is re-read first, because an overlay redraw can move the box
        between the discovery that produced `field` and this call.
        """
        what = what or f"selecting {field.label!r}"
        target = self.field(field.label)
        self._select_row(target.row, what)
        return target

    def edit(self, field: Field) -> Field:
        """Focus a field and press RETURN, which opens its editor or dropdown."""
        target = self.focus(field)
        self.press("ENTER")
        return target

    def cycle_next(self) -> None:
        """"+" moves a preset field to its next value and does nothing else.

        AssemblySearch::handle_key sends "+" to increase() at the form level,
        which BrowsableQueryField::updown ignores unless the field's target is
        a list. It is not a character the string editor can receive, so it
        cannot land in a free-text field as text.
        """
        self.type_text("+")

    def cycle_previous(self) -> None:
        self.type_text("-")

    def cycle_to_first(self) -> None:
        """Put a preset field on the first value its service listed.

        BrowsableQueryField::updown clamps at zero, so pressing "-" more times
        than the list is long lands on the first entry whatever the field was
        holding and whatever the list contains. One request, because
        send_text batches.
        """
        self.type_text("-" * PRESET_REWIND)

    def cancel(self) -> None:
        self.press("RUNSTOP")

    def confirm(self) -> None:
        self.press("ENTER")

    def clear(self, field: Field, taps: int = 40, attempts: int = 3) -> None:
        """Leave a field empty, and confirm that it is.

        KEY_CLEAR empties UIStringEdit's buffer whatever its length, so where
        the transport can spell it this is one injected key instead of forty.
        On a cartridge, where every key crosses the host's keyboard matrix,
        neither spelling is reliable on its own: the field kept the previous
        check's text, and the query that should have been refused as empty was
        sent as a real search that took 46s to come back. So the field is read
        back and the other spelling is tried, rather than a clear being assumed
        to have worked.
        """
        clear_key = getattr(self.backend, "clear_field_key", None)
        for attempt in range(attempts):
            self.edit(field)
            if clear_key and attempt == 0:
                self.press(clear_key)
            else:
                self.press_many("DEL", taps)
            self.press("ENTER")
            if not self.field(field.label).value:
                return
        raise Failure(
            f"the {field.label!r} field still holds "
            f"{self.field(field.label).value!r} after {attempts} attempts to "
            f"empty it")

    def submit(self) -> None:
        """RETURN on the submit row at the bottom of the form runs the query."""
        rows = self.rows()
        if rows is None:
            raise Failure("the menu closed before the query could be submitted")
        row = label_row(rows, SUBMIT_MARKER)
        if row is None:
            raise Failure("the form has no submit row")
        self._select_row(row, "selecting submit")
        self.press("ENTER")

    # ------------------------------------------------------ classifying --
    def classify(self) -> tuple[tuple[Field, ...], tuple[Field, ...]]:
        """(free-text fields, preset fields), settled by driving each one.

        A preset field takes a value when it is cycled and a free-text field
        does not, which is the only difference visible from outside: both are
        drawn as a label and a run of underscores. The answer is computed once
        and kept, because the probe writes into every preset field it finds
        and because repeating it would cost a round of device calls per field
        each time a caller asked.

        The probe leaves each preset field holding its first value. A caller
        that needs a known starting point takes `values()` afterwards rather
        than before.
        """
        if self._texts is not None and self._presets is not None:
            return self._texts, self._presets
        texts: list[Field] = []
        presets: list[Field] = []
        for field in self.fields():
            # `field.value` is what the same read that found the row saw, so
            # no second read is needed for the "before" half. One read after
            # the keystroke is what the classification costs per field.
            self.focus(field, f"classifying {field.label!r}")
            self.cycle_next()
            after = self.field(field.label)
            if after.value and after.value != field.value:
                presets.append(after)
            else:
                texts.append(after)
        self._texts, self._presets = tuple(texts), tuple(presets)
        return self._texts, self._presets

    def forget_classification(self) -> None:
        """Drop what classify() learnt, for a caller that reopened the form."""
        self._texts = self._presets = None

    # ------------------------------------------------------------ cursor --
    def _select_row(self, target: int, what: str) -> None:
        """Put the selection on a known row.

        The distance is read off the screen and covered in one batched run of
        keys, the way Browser.select_entry moves a file listing, rather than
        one request and one settle per row. Walking cost about a fifth of a
        second a row, which is what made moving between form fields visibly
        slow.

        The move is confirmed afterwards rather than assumed, because the
        cursor lives only in the colour half of the screen and a form that
        wraps at its ends would land somewhere else. Two batched attempts are
        made, so that a run which only partly landed is corrected in one more
        request. If the cursor is still not on the target row, the walk below
        takes over: it is slower, but it reads the position back after every
        single step.
        """
        for _ in range(2):
            current = self.cursor_row()
            if current is None:
                raise Failure(f"{what}: {self.no_cursor_reason()}")
            if current == target:
                return
            self.press_many("DOWN" if current < target else "UP",
                            abs(target - current))

        for _ in range(FIELD_WALK_LIMIT):
            current = self.cursor_row()
            if current is None:
                raise Failure(f"{what}: {self.no_cursor_reason()}")
            if current == target:
                return
            self.press("DOWN" if current < target else "UP")
        raise Failure(f"{what}: the cursor never reached row {target}; "
                      f"screen was:\n{self.text()}")

    def _telnet_field_row(self) -> int | None:
        """Telnet equivalent of Backend.selected_row(), scoped to this form.

        TelnetBackend.selected_row() only checks a row's first two columns for
        the marker colour (see ui_backend.py), which assumes the selected
        content starts at column 0 -- true for the root browser's own listing,
        false for this form's box, which REST also draws differently
        positioned but which happens to still leave REST's colour-plane
        detection intact. Column 0 is unusable here (see label_row), so this
        scans within the row's own box borders instead, and skips the title
        row (identified by its own text, not by position) since its colour is
        cosmetic, not a selection.
        """
        rows = self.rows()
        colours = self.backend.screen.colours
        marker = self.backend.selected_sgr
        if rows is None or marker is None:
            return None
        title_row = label_row(rows, self.title)
        for row in self.entry_rows:
            if row == title_row or row >= len(rows):
                continue
            bounds = _box_interior_bounds(rows[row])
            if bounds is None:
                continue
            left, right = bounds
            # The colour is the machine's, measured by the backend from a
            # listing rather than pinned here: an Ultimate 64 marks the cursor
            # 0;32;1 and a C64 Ultimate 0;37;1.
            if any(colours[row][col] == marker for col in range(left, right)):
                return row
        return None


def _clean_value(text: str) -> str:
    """What a field shows, with its empty placeholder removed."""
    return text.strip().strip(PLACEHOLDER_CHARS).strip()


def _box_interior_bounds(text: str) -> tuple[int, int] | None:
    """Column span strictly between a row's own left/right box border.

    Finding the marker anywhere on the row is not enough: the box's own title
    is drawn in the same colour as the marker (a header decoration, not a
    selection), and so, further right, is a status column belonging to
    whatever the root browser drew on that row before the box covered most of
    it. Both sit outside the box's own field-label area, so scoping the search
    to strictly between this row's two "|" border cells (the same two columns
    a field's own text is delimited by) excludes both.
    """
    left = text.find("|")
    right = text.rfind("|")
    if left == -1 or right == -1 or left == right:
        return None
    return left + 1, right
