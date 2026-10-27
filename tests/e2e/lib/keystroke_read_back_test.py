#!/usr/bin/env python3
# Gate check: the UI facades read their keystrokes back, against lossy backends.
"""The browser's and the search form's keystrokes, read back, checked without a device.

An injected key can be lost on its way to the menu. Measured on a U2+L under
load, a rename typed as "qmenu2.tst" arrived as "qenu2.tst", and the file was
renamed to that. The field is read back before it is accepted and typed again
when it does not show the text, a popup key that leaves the screen unchanged is
pressed again, an overlay item is confirmed highlighted before ENTER runs it,
and a search term is read back before the form keeps it. Each retry is
reported as a warning. A device loses a key only now and then and never on
demand, so these checks drive both methods through scripted backends that drop
exactly the keystrokes each case names.

Needs no device.
"""

import sys
from dataclasses import dataclass
from pathlib import Path

# The one stanza that puts the shared library on sys.path; see tests/lib/bootstrap.py.
sys.path.insert(0, str(next(p for p in Path(__file__).resolve().parents
                            if (p / "tests" / "lib").is_dir()) / "tests" / "lib"))

import bootstrap  # noqa: E402,F401
import browser  # noqa: E402
import search_form  # noqa: E402
import cli  # noqa: E402
import navigation  # noqa: E402
from report import Failure, check, detail, suite_fail, suite_ok  # noqa: E402

NAME = "qmenu2.tst"
PROMPT = "Give a new name.."


@dataclass
class Capture:
    lines: list[str]


class LossyBackend:
    """A string box that drops the keystroke at a chosen index of each typing.

    `drops` holds one entry per call to send_text: the index of the character
    that never arrives, or None for a typing that arrives whole.
    """

    clear_field_key = None

    def __init__(self, drops: list[int | None], listing: str = "") -> None:
        self.drops = list(drops)
        self.listing = listing
        self.field = ""
        self.typings = 0
        self.accepted: str | None = None

    def capture(self) -> Capture:
        return Capture(["*** Ultimate ***", self.listing, PROMPT, self.field])

    def send_text(self, text: str, _label: str) -> None:
        self.typings += 1
        drop = self.drops.pop(0) if self.drops else None
        self.field += text if drop is None else text[:drop] + text[drop + 1:]

    def send_key(self, key: str) -> None:
        if key == "ENTER":
            self.accepted = self.field

    def send_key_repeat(self, key: str, count: int) -> None:
        if key == "BACKSPACE":
            self.field = self.field[:max(0, len(self.field) - count)]


class PopupBackend:
    """A popup that answers the keys it is scripted to receive.

    `lost` holds one entry per key press: True for a press that never arrives.
    A press that arrives replaces the screen with `answers`, popped in order,
    so a case can make an answer open a second popup.
    """

    navigation = navigation.classify(navigation.QUICK_SEARCH)

    def __init__(self, lost: list[bool], answers: list[list[str]]) -> None:
        self.lost = list(lost)
        self.answers = list(answers)
        self.lines = ["Are you sure?", "Yes  No"]
        self.presses = 0

    def capture(self) -> Capture:
        return Capture(list(self.lines))

    def send_char(self, _character: str) -> None:
        self.presses += 1
        if self.lost.pop(0) if self.lost else False:
            return
        if self.answers:
            self.lines = self.answers.pop(0)


def press(backend: PopupBackend) -> list[str]:
    """Run press_popup_button on `backend`, and the detail lines it reported."""
    reported: list[str] = []
    real_detail = browser.warn
    browser.warn = reported.append
    try:
        browser.Browser(backend, entry_rows=range(1, 2), status_row=3).press_popup_button("y")
    finally:
        browser.warn = real_detail
    return reported


class OverlayBackend:
    """A context menu that loses the key events a case names, in order.

    `lost` holds one entry per key event sent (quick-seek letter, cursor run,
    ENTER): True for one that never arrives. `readable` says whether the
    highlight can be read back: True, False for an empty answer, or "raises"
    for a transport that raises as Telnet does.
    """

    navigation = navigation.classify(navigation.QUICK_SEARCH)
    ITEMS = ("View", "Hex View", "Copy to...", "Move to...", "Rename", "Delete")

    def __init__(self, lost: list[bool], readable: bool | str = True) -> None:
        self.lost = list(lost)
        self.readable = readable
        self.cursor = 0
        self.activated: list[str] = []

    def _arrives(self) -> bool:
        return not (self.lost.pop(0) if self.lost else False)

    def capture(self) -> Capture:
        rows = [(">" if index == self.cursor else " ") + item
                for index, item in enumerate(self.ITEMS)]
        return Capture([*rows, f"ran {self.activated}"])

    def selected_text(self, _entry_rows=None) -> str:
        if self.readable == "raises":
            raise Failure("expected exactly one selected row")
        return self.ITEMS[self.cursor] if self.readable else ""

    def send_char(self, character: str) -> None:
        if self._arrives():
            self.cursor = next(index for index, item in enumerate(self.ITEMS)
                               if item.lower().startswith(character.lower()))

    def send_key_repeat(self, key: str, count: int) -> None:
        if self._arrives():
            step = count if key == "DOWN" else -count
            self.cursor = max(0, min(len(self.ITEMS) - 1, self.cursor + step))

    def send_key(self, key: str) -> None:
        if key == "ENTER" and self._arrives():
            self.activated.append(self.ITEMS[self.cursor])


def choose(backend: OverlayBackend, label: str) -> list[str]:
    """Run choose_overlay_item on `backend`, and the detail lines it reported."""
    reported: list[str] = []
    real_detail = browser.warn
    browser.warn = reported.append
    try:
        menu = browser.Browser(backend, entry_rows=range(0, 6), status_row=6)
        menu.choose_overlay_item(list(OverlayBackend.ITEMS), label)
    finally:
        browser.warn = real_detail
    return reported


def fill(backend: LossyBackend) -> list[str]:
    """Run fill_edit_field on `backend`, and the detail lines it reported."""
    reported: list[str] = []
    real_detail = browser.warn
    browser.warn = reported.append
    try:
        browser.Browser(backend, entry_rows=range(1, 2), status_row=3).fill_edit_field(NAME)
    finally:
        browser.warn = real_detail
    return reported


def run_checks() -> None:
    # A key that changes nothing waits this long before it is retried; a device
    # needs the full allowance, a scripted backend answers at once.
    browser.EDIT_FIELD_ECHO_SECONDS = 0.05
    browser.ACTION_ECHO_SECONDS = 0.05

    with check("a field that shows the text is accepted after one typing"):
        backend = LossyBackend([None])
        reported = fill(backend)
        if (backend.accepted, backend.typings, reported) != (NAME, 1, []):
            raise Failure(f"accepted {backend.accepted!r} after {backend.typings} typings, "
                          f"reporting {reported}")

    with check("a lost keystroke is typed again, and the retype is reported"):
        backend = LossyBackend([1, None])
        reported = fill(backend)
        if (backend.accepted, backend.typings) != (NAME, 2):
            raise Failure(f"accepted {backend.accepted!r} after {backend.typings} typings")
        if len(reported) != 1 or "keystroke was lost" not in reported[0]:
            raise Failure(f"the retype was not reported as a lost keystroke: {reported}")

    with check("a field that never shows the text fails without being accepted"):
        backend = LossyBackend([1] * browser.EDIT_FIELD_ATTEMPTS)
        try:
            fill(backend)
        except Failure:
            pass
        else:
            raise Failure("a field that never showed the text was reported as filled")
        if backend.accepted is not None:
            raise Failure(f"ENTER was pressed on {backend.accepted!r}")

    with check("the same name already in the listing does not stand in for the field"):
        backend = LossyBackend([1, None], listing=NAME)
        fill(backend)
        if (backend.accepted, backend.typings) != (NAME, 2):
            raise Failure(f"a listing row that already showed {NAME!r} was taken for the "
                          f"field: accepted {backend.accepted!r} after {backend.typings} typings")


class FormBackend(LossyBackend):
    """The search form's edit field, losing the keystrokes a case names."""

    def capture(self) -> Capture:
        return Capture(["Assembly 64 Search", f"Name: {self.field}"])


def enter(backend: FormBackend, text: str) -> list[str]:
    """Run SearchForm.enter_text on `backend`, and the detail lines it reported."""
    reported: list[str] = []
    real_detail = search_form.warn
    search_form.warn = reported.append
    try:
        form = search_form.SearchForm(backend, "overlay", range(0, 2), "Assembly 64 Search",
                                      reach=lambda: None)
        form.enter_text(text)
    finally:
        search_form.warn = real_detail
    return reported


class EntryBackend(LossyBackend):
    """A browser row whose ENTER is lost, arrives, or opens something else."""

    def __init__(self, lost: list[bool], opens: str = "menu") -> None:
        super().__init__([])
        self.enter_lost = list(lost)
        self.opens = opens
        self.enters = 0
        self.shown = ["*** Ultimate ***", "> entry.prg"]

    def capture(self) -> Capture:
        return Capture(list(self.shown))

    def send_key(self, key: str) -> None:
        if key != "ENTER":
            return
        self.enters += 1
        if self.enter_lost.pop(0) if self.enter_lost else False:
            return
        self.shown = self.shown + ([self.opens] if self.opens != "menu" else ["|Run |"])


def open_menu(backend: EntryBackend) -> tuple[list[str] | None, list[str]]:
    """Run open_context_menu, and what it returned and reported."""
    reported: list[str] = []
    real_detail = browser.warn
    browser.warn = reported.append
    menu = browser.Browser(backend, entry_rows=range(1, 2), status_row=3)
    menu.wait_for_overlay = lambda before: (["Run"] if menu.rows() != before and
                                            "|Run |" in menu.rows() else [])
    menu.overlay_items = menu.wait_for_overlay
    try:
        return menu.open_context_menu(), reported
    except Failure:
        return None, reported
    finally:
        browser.warn = real_detail


def run_context_menu_checks() -> None:
    with check("a context menu that opens is opened with one ENTER"):
        backend = EntryBackend([False])
        labels, reported = open_menu(backend)
        if (labels, backend.enters, reported) != (["Run"], 1, []):
            raise Failure(f"labels {labels}, {backend.enters} ENTERs, reporting {reported}")

    with check("an ENTER the browser never saw is pressed again, and reported"):
        backend = EntryBackend([True, False])
        labels, reported = open_menu(backend)
        if labels != ["Run"] or backend.enters != 2 or len(reported) != 1:
            raise Failure(f"labels {labels}, {backend.enters} ENTERs, reporting {reported}")

    with check("a screen that changed into something else gets no second ENTER"):
        # A second ENTER on whatever appeared could choose from it.
        backend = EntryBackend([False], opens="Loading...")
        labels, _reported = open_menu(backend)
        if labels is not None or backend.enters != 1:
            raise Failure(f"labels {labels} after {backend.enters} ENTERs")


def run_form_checks() -> None:
    search_form.ENTER_ECHO_SECONDS = 0.05

    with check("a search term that arrives whole is typed once"):
        backend = FormBackend([None])
        reported = enter(backend, "turrican")
        if (backend.field, backend.typings, reported) != ("turrican", 1, []):
            raise Failure(f"field {backend.field!r} after {backend.typings} typings, "
                          f"reporting {reported}")

    with check("a doubled letter lost from a search term is typed again and reported"):
        # "turrican" arrived as "turican" on a U2+L: one r of the pair went unseen.
        backend = FormBackend([3, None])
        reported = enter(backend, "turrican")
        if backend.field != "turrican" or len(reported) != 1:
            raise Failure(f"field {backend.field!r}, reporting {reported}")

    with check("a term the field never shows fails"):
        backend = FormBackend([3] * search_form.ENTER_ATTEMPTS)
        try:
            enter(backend, "turrican")
        except Failure:
            pass
        else:
            raise Failure("a term the field never showed was reported as entered")


def run_popup_checks() -> None:
    with check("a popup key that changes the screen is pressed once"):
        backend = PopupBackend([False], [["listing"]])
        reported = press(backend)
        if (backend.presses, reported) != (1, []):
            raise Failure(f"{backend.presses} presses, reporting {reported}")

    with check("a popup key that changed nothing is pressed again, and the repeat is reported"):
        backend = PopupBackend([True, False], [["listing"]])
        reported = press(backend)
        if backend.presses != 2 or len(reported) != 1 or "was lost" not in reported[0]:
            raise Failure(f"{backend.presses} presses, reporting {reported}")

    with check("a popup key that never changes the screen fails"):
        backend = PopupBackend([True] * browser.EDIT_FIELD_ATTEMPTS, [])
        try:
            press(backend)
        except Failure:
            pass
        else:
            raise Failure("a key that never reached the popup was reported as pressed")

    with check("an answer that opens a second popup with the same buttons is not repeated"):
        # The second popup still shows "Yes  No", so a retry keyed on the
        # buttons would answer it too. Only an unchanged screen may cause one.
        backend = PopupBackend([False], [["Delete all its contents?", "Yes  No"]])
        press(backend)
        if backend.presses != 1:
            raise Failure(f"the key was pressed {backend.presses} times, answering the "
                          "second popup nobody asked about")


def run_overlay_checks() -> None:
    with check("a lost quick-seek key is corrected before ENTER, so the named item runs"):
        # Without the correction the cursor stays on View and ENTER runs View.
        backend = OverlayBackend([True])
        reported = choose(backend, "Move to...")
        if backend.activated != ["Move to..."]:
            raise Failure(f"ran {backend.activated}")
        if not any("key was lost" in line for line in reported):
            raise Failure(f"the correction was not reported: {reported}")

    with check("a lost ENTER is pressed again, and runs the item once"):
        backend = OverlayBackend([False, True])
        reported = choose(backend, "Move to...")
        if backend.activated != ["Move to..."] or len(reported) != 1:
            raise Failure(f"ran {backend.activated}, reporting {reported}")

    with check("a highlight that cannot be read leaves the navigation as it was sent"):
        backend = OverlayBackend([], readable=False)
        reported = choose(backend, "Move to...")
        if backend.activated != ["Move to..."] or reported:
            raise Failure(f"ran {backend.activated}, reporting {reported}")

    with check("a transport that raises instead of reading the highlight still runs the item"):
        # Telnet raises when it cannot find one marked row in a framed overlay.
        backend = OverlayBackend([], readable="raises")
        reported = choose(backend, "Move to...")
        if backend.activated != ["Move to..."] or reported:
            raise Failure(f"ran {backend.activated}, reporting {reported}")


def main() -> int:
    cli.device_free_arguments(__doc__)
    try:
        run_checks()
        run_popup_checks()
        run_overlay_checks()
        run_context_menu_checks()
        run_form_checks()
    except Failure as exc:
        suite_fail("keystroke_read_back_test", str(exc))
        return 1
    detail("string boxes, popup keys, overlay items and search terms read back, "
           "against scripted key loss")
    suite_ok("keystroke_read_back_test")
    return 0


if __name__ == "__main__":
    sys.exit(main())
