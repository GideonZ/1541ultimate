#!/usr/bin/env python3
# Gate check: the screen parsers, against screens built to be read wrongly.

"""The REST menu_screen parsers, checked without a device.

`find_selected_row_rest`, `row_marks`, `find_open_window` and `VT100Screen`
decide which row a suite thinks the cursor is on. Every suite that navigates
depends on them, and until now the only thing exercising them was a device:
`telnet_drain_test.py` covers the drain timing and nothing covered the
parsing, so every change to a rule cost a device run to validate and a wrong
answer showed up as a suite selecting the wrong menu entry.

The screens here are built rather than captured, because what has to be
checked is the cases the rules were written for, and each of those is one
awkward screen. The docstrings in ui_backend.py name them, each with the
machine it was measured on:

  - a two-entry listing whose colour plane carries no background nibble, where
    counting cannot separate the selected row from the one other row;
  - a framed context menu drawn beside the browser row it was opened on, where
    the browser's own highlight has more marked cells than the menu item;
  - a form that marks a ten-cell field rather than a row;
  - a repaint that leaves no row marked at all, where a blank row used to win.

Needs no device.
"""

import sys
from pathlib import Path

# The one stanza that puts the shared library on sys.path; see tests/lib/bootstrap.py.
sys.path.insert(0, str(next(p for p in Path(__file__).resolve().parents
                            if (p / "tests" / "lib").is_dir()) / "tests" / "lib"))
import bootstrap  # noqa: E402,F401
import cli  # noqa: E402
import ui_backend  # noqa: E402
from report import Failure, check, detail, suite_fail, suite_ok  # noqa: E402
from selftest import expect  # noqa: E402

WIDTH = ui_backend.SCREEN_WIDTH
HEIGHT = ui_backend.SCREEN_HEIGHT
CELLS = ui_backend.SCREEN_CELLS

# The rows a browser listing occupies, as the suites pass them.
ENTRY_ROWS = list(range(2, 23))

BLANK = 0x20
NO_COLOUR = 0x0F        # the plane's "nothing marked here" foreground


class Screen:
    """A menu_screen char plane and colour plane, built cell by cell."""

    def __init__(self) -> None:
        self.chars = bytearray([BLANK] * CELLS)
        self.colours = bytearray([NO_COLOUR] * CELLS)

    def text(self, row: int, value: str, column: int = 1) -> "Screen":
        for offset, character in enumerate(value):
            self.chars[row * WIDTH + column + offset] = ord(character)
        return self

    def foreground(self, row: int, colour: int, column: int = 1,
                   width: int | None = None) -> "Screen":
        span = WIDTH - column - 1 if width is None else width
        for offset in range(span):
            self.colours[row * WIDTH + column + offset] = colour
        return self

    def background(self, row: int, colour: int, column: int = 1,
                   width: int | None = None) -> "Screen":
        span = WIDTH - column - 1 if width is None else width
        for offset in range(span):
            cell = row * WIDTH + column + offset
            self.colours[cell] = (colour << 4) | (self.colours[cell] & 0x0F)
        return self

    def clear_colour(self, row: int, column: int = 1,
                     width: int | None = None) -> "Screen":
        """Put a span back to "nothing marked", as a window drawn over it does."""
        span = WIDTH - column - 1 if width is None else width
        for offset in range(span):
            self.colours[row * WIDTH + column + offset] = NO_COLOUR
        return self

    def reverse(self, row: int, column: int = 1,
                width: int | None = None) -> "Screen":
        span = WIDTH - column - 1 if width is None else width
        for offset in range(span):
            cell = row * WIDTH + column + offset
            self.chars[cell] |= 0x80
        return self

    def body(self) -> bytes:
        return bytes(self.chars) + bytes(self.colours)

    def selected(self, **kwargs) -> int:
        return ui_backend.find_selected_row_rest(
            bytes(self.chars), bytes(self.colours), ENTRY_ROWS, **kwargs)


def listing(rows: dict[int, str]) -> Screen:
    screen = Screen()
    for row, name in rows.items():
        screen.text(row, name)
    return screen


def run_checks() -> None:
    with check("a row marked by background colour is the selected one"):
        screen = listing({2: "Ftp", 3: "Temp", 4: "Usb0"})
        screen.background(3, 6)
        expect("row", screen.selected(), 3)

    with check("a blank row never wins, however it is coloured"):
        # A repaint that leaves no row marked: measured live on the root
        # browser, blank row 8 carried the colour last set and beat the six
        # real drive rows, so callers compared an entry against an empty
        # string and missed one that was plainly on screen.
        # The blank row is coloured across its whole width and the real one
        # only across its name column, which is the shape that made this
        # happen: a real row splits its cells between name and status colours,
        # so a blank row wins a plain cell count outright.
        screen = listing({2: "Ftp", 3: "Temp", 4: "Usb0"})
        screen.foreground(8, 1)                          # blank, 37 cells
        screen.foreground(3, 1, column=1, width=20)      # real, 20 cells
        expect("the real row, not the blank one", screen.selected(), 3)

    with check("a screen with nothing marked says so, rather than guessing"):
        # A plain Failure, not the NoCursorDrawn subclass, which belongs to the
        # backend's own read. ui_state.Device.selected_row catches this one to
        # answer None, and used to name it without importing it.
        screen = listing({2: "Ftp", 3: "Temp", 4: "Usb0"})
        try:
            row = screen.selected()
        except Failure as exc:
            expect("the message says what it could not find",
                   "selected menu row" in str(exc), True)
        else:
            raise Failure(f"an unmarked screen returned row {row}")

    with check("two entries are separated by the machine's own cursor colour"):
        # Measured on an Ultimate II+L: with the cursor on the second of two
        # entries, both rows carry the same number of coloured cells and each
        # carries a colour no other row does, so counting has two answers.
        # The colour the machine marks a cursor with is what decides it.
        screen = listing({2: "Ftp", 3: "Temp"})
        screen.foreground(2, 7)
        screen.foreground(3, 1)
        expect("without the colour, counting cannot separate them",
               screen.selected(), 2)
        expect("with it, the second row is the cursor",
               screen.selected(cursor_colour=1), 3)

    with check("a cursor that colours one field still marks its row"):
        # An Assembly 64 query form: the cursor colours the ten-cell value
        # field of "Name:" and nothing else on screen carries that colour.
        # The minimum for a whole row would dismiss ten cells as noise.
        screen = Screen()
        screen.text(2, "Name:").text(3, "Group:").text(4, "<< Submit >>")
        screen.foreground(2, 7, column=1, width=30)
        screen.foreground(3, 7, column=1, width=30)
        screen.foreground(4, 7, column=1, width=30)
        screen.foreground(2, 1, column=8, width=10)
        expect("the field's row", screen.selected(cursor_colour=1), 2)

    with check("a framed window is read instead of what it covers"):
        # A context menu on the Ftp row: it occupies rows 5 to 7 and columns
        # 29 to 38, so the browser's own highlighted row shares its rows and
        # outweighs the menu item on every read of the full width.
        screen = listing({5: "Ftp", 6: "Temp", 7: "Usb0"})
        screen.background(5, 6)                       # the browser's highlight
        # The menu is drawn over the browser, so its cells carry its own
        # colours rather than the highlight underneath.
        frame(screen, rows=range(4, 9), first=29, last=39)
        for row, label in ((5, "Run"), (6, "Mount"), (7, "Info")):
            screen.text(row, label, column=30)
            screen.clear_colour(row, column=30, width=9)
        screen.background(6, 1, column=30, width=9)   # the menu's own cursor
        window = ui_backend.find_open_window(bytes(screen.chars), ENTRY_ROWS)
        expect("the window's columns exclude the browser text",
               window.first_column >= 29, True)
        expect("the menu's row, not the browser's", screen.selected(), 6)

    with check("a window narrows what counts as a marking"):
        window = ui_backend.Window(range(5, 8), 30, 39)
        expect("a nine-column window asks for less than a full row",
               window.min_marked_cells < ui_backend.SELECTED_ROW_MIN_MARKED_CELLS,
               True)
        expect("but never for nothing", window.min_marked_cells >= 1, True)

    with check("row_marks counts only the cells inside the window"):
        screen = listing({5: "Ftp"})
        screen.text(5, "Run", column=30)
        screen.foreground(5, 6, column=1, width=20)    # browser text, outside
        screen.foreground(5, 1, column=30, width=9)    # menu text, inside
        marks = ui_backend.row_marks(bytes(screen.chars), bytes(screen.colours),
                                     ui_backend.Window(range(5, 6), 30, 39))
        expect("one row is marked", sorted(marks), [5])
        expect("with the menu's colour", marks[5].colour, 1)
        expect("and only the menu's cells", marks[5].colour_cells, 9)

    with check("reverse video marks a row where no colour does"):
        screen = listing({2: "Ftp", 3: "Temp"})
        screen.reverse(3, column=1, width=30)
        expect("the reversed row", screen.selected(), 3)


def frame(screen: Screen, rows: range, first: int, last: int) -> None:
    """Draw the box a framed window is recognised by.

    The codes are the firmware's own: a top rule of horizontals between two
    corners, verticals down each side, and the two bottom corners the frame
    search starts from. See _find_frames in ui_backend.py for why the bottom
    corners are the ones it keys on.
    """
    top, bottom = rows[0], rows[-1]
    for column in range(first, last + 1):
        screen.chars[top * WIDTH + column] = ui_backend.BOX_HORIZONTAL
        screen.chars[bottom * WIDTH + column] = ui_backend.BOX_HORIZONTAL
    screen.chars[top * WIDTH + first] = ui_backend.BOX_TOP_LEFT[0]
    screen.chars[top * WIDTH + last] = ui_backend.BOX_TOP_RIGHT[0]
    screen.chars[bottom * WIDTH + first] = ui_backend.BOX_BOTTOM_LEFT[0]
    screen.chars[bottom * WIDTH + last] = ui_backend.BOX_BOTTOM_RIGHT[0]
    for row in rows[1:-1]:
        screen.chars[row * WIDTH + first] = ui_backend.BOX_VERTICAL
        screen.chars[row * WIDTH + last] = ui_backend.BOX_VERTICAL


def main() -> int:
    cli.device_free_arguments(__doc__)
    try:
        run_checks()
    except Failure as exc:
        suite_fail("ui_backend_parse_test", str(exc))
        return 1
    detail("the rules the parsers were written for, each as one screen")
    suite_ok("ui_backend_parse_test")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
