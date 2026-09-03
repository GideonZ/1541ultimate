#!/usr/bin/env python3
# Gate check: what the harness types at a menu under each Navigation Style.

"""Verify the keys a suite sends when the machine reads letters as cursors.

"User Interface Settings" / "Navigation Style" decides whether the on-device
menu reads 'w', 'a', 's' and 'd' as letters or as cursor keys, and a C64
Ultimate ships with the cursor reading. Under it a quick-seek for a name
starting with 's' walks the cursor down the listing, and the "All" button of a
popup moves the highlight left instead of being pressed.

Which keys go out for a given machine is decided before any of that reaches
the device, so it is checked here rather than by watching a screen. The
firmware behaviour these expectations rest on was measured on a C64 Ultimate
1.2.0 and is recorded in tests/lib/navigation.py.
"""

import sys
from collections.abc import Sequence
from pathlib import Path

# tests/lib holds the shared library; importing bootstrap adds tests/e2e/lib.
# The search walks up rather than counting directories, so this is the same in
# every entry point and a suite that moves needs no edit. See tests/lib/bootstrap.py.
sys.path.insert(0, str(next(p for p in Path(__file__).resolve().parents
                            if (p / "tests" / "lib").is_dir()) / "tests" / "lib"))
import bootstrap  # noqa: E402,F401
from selftest import expect  # noqa: E402

import navigation  # noqa: E402
from report import (  # noqa: E402
    Failure, add_colour_argument, apply_colour, check, detail, suite_fail, suite_ok)

import ui_backend  # noqa: E402


class RecordingBackend(ui_backend.Backend):
    """A Backend that records what was sent instead of driving a device.

    `selected` is what selected_text() answers, so a caller can make a seek
    look as though it landed or as though it did not.
    """

    def __init__(self, style: str, selected: str = "") -> None:
        self._navigation = navigation.classify(style)
        self.selected = selected
        self.sent: list[str] = []

    @property
    def navigation(self) -> navigation.Navigation:
        return self._navigation

    def capture(self) -> ui_backend.Snapshot:
        return ui_backend.Snapshot(lines=[""] * 25, reverse_cells=[],
                                   last_command="")

    def send_key(self, key, *, settle=False, expect_redraw=True):
        self.sent.append(f"<{key}>")
        return self.capture()

    def send_char(self, ch, *, settle=False, expect_redraw=True):
        self.sent.append(ch)
        return self.capture()

    def send_text(self, text, label):
        self.sent.extend(text)
        return self.capture()

    def send_key_repeat(self, key, count):
        self.sent.extend([f"<{key}>"] * count)
        return self.capture()

    def send_key_then_text(self, key, text, label):
        self.send_key(key)
        return self.send_text(text, label)

    def selected_text(self, entry_rows: Sequence[int] | None = None) -> str:
        return self.selected

    @property
    def typed(self) -> str:
        return "".join(self.sent)


def browser(style: str, selected: str = "") -> tuple[ui_backend.Browser, RecordingBackend]:
    backend = RecordingBackend(style, selected)
    return ui_backend.Browser(backend, range(2, 24), 24), backend


def run_setting_checks():
    with check("the two values, and an unknown one, resolve to a reading"):
        expect("quick search",
               navigation.classify(navigation.QUICK_SEARCH).wasd, False)
        expect("wasd", navigation.classify(navigation.WASD_CURSORS).wasd, True)
        # A device too old to serve the item behaves as Quick Search does.
        expect("unset", navigation.classify("").wasd, False)
        expect("unknown", navigation.classify("Something Else").wasd, False)

    with check("a letter is shifted only where the menu folds it back"):
        wasd = navigation.classify(navigation.WASD_CURSORS)
        quick = navigation.classify(navigation.QUICK_SEARCH)
        expect("wasd letters", wasd.menu_text("wasd"), "WASD")
        expect("other letters", wasd.menu_text("Temp"), "TEMP")
        # Only 'A' to 'Z' and the four cursor letters pass through the
        # keymapper, so nothing else may be touched.
        expect("digits and punctuation", wasd.menu_text("A-64_1"), "A-64_1")
        expect("quick search is untouched", quick.menu_text("wasd"), "wasd")

    with check("the menu cannot be made to read an uppercase letter under wasd"):
        expect("wasd", navigation.classify(navigation.WASD_CURSORS)
               .receives_uppercase(), False)
        expect("quick search", navigation.classify(navigation.QUICK_SEARCH)
               .receives_uppercase(), True)

    with check("the setting is read once per host"):
        navigation.forget()
        reads = []

        def fetch():
            reads.append(1)
            return navigation.WASD_CURSORS

        expect("first", navigation.identify("host.invalid", fetch).wasd, True)
        expect("second", navigation.identify("host.invalid", fetch).wasd, True)
        expect("reads", len(reads), 1)
        navigation.forget("host.invalid")
        expect("after forget", navigation.identify("host.invalid", fetch).wasd, True)
        expect("reads", len(reads), 2)
        navigation.forget()


def run_browser_checks():
    with check("a quick-seek prefix is spelt the way the machine will read it"):
        # The landing is confirmed by reading the cursor back, so the fixture
        # says the browser ended up on the entry.
        menu, backend = browser(navigation.WASD_CURSORS, "SD      SD Card")
        expect("seeked", menu._seek_entry("SD"), True)
        expect("keys", backend.typed, "<UP>SD")

        menu, backend = browser(navigation.QUICK_SEARCH, "SD      SD Card")
        expect("seeked", menu._seek_entry("SD"), True)
        expect("keys", backend.typed, "<UP>SD")

    with check("a name holding a cursor letter is still seeked for"):
        # Every character of these is one the menu reads as a cursor key under
        # WASD Cursors, which is exactly the case a seek used to give up on.
        for name, keys in (("was", "WAS"), ("dawn", "DAWN")):
            menu, backend = browser(navigation.WASD_CURSORS, name)
            expect(f"seeked {name}", menu._seek_entry(name), True)
            expect(f"keys for {name}", backend.typed, "<UP>" + keys)

    with check("a popup's button key reaches the button rather than the cursor"):
        # 'a' is the All button and one of the four cursor letters.
        menu, backend = browser(navigation.WASD_CURSORS)
        menu.press_popup_button("a")
        expect("keys", backend.typed, "A")

        menu, backend = browser(navigation.QUICK_SEARCH)
        menu.press_popup_button("a")
        expect("keys", backend.typed, "a")

    with check("an overlay item is picked by the same spelling"):
        # Two entries share a first letter and the wanted one is far enough
        # down that a two-character seek beats walking, so the plan carries a
        # lowercase letter and the two styles spell it differently.
        labels = ["Run", "Mount", "Rename", "Copy", "Delete", "Sort",
                  "Set Date", "Set Time", "Save As"]
        menu, backend = browser(navigation.WASD_CURSORS)
        menu.choose_overlay_item(labels, "Save As")
        expect("keys", backend.typed, "SA<ENTER>")

        menu, backend = browser(navigation.QUICK_SEARCH)
        menu.choose_overlay_item(labels, "Save As")
        expect("keys", backend.typed, "Sa<ENTER>")

    with check("text typed into a field is never respelt"):
        # UIStringEdit::poll does not call the keymapper, so a filename holding
        # a cursor letter has to arrive exactly as the suite wrote it.
        menu, backend = browser(navigation.WASD_CURSORS)
        menu.type_text("wasd.prg")
        expect("keys", backend.typed, "wasd.prg")


def run_duration_checks() -> None:
    """The one duration parser, which had five copies and three behaviours.

    The copy kept is the one that accepted every unit and rejected the rest;
    ftp_client_test.py's accepted no bad value at all, so `--duration 5x` ended
    the run with a bare ValueError traceback out of float().
    """
    import argparse

    import cli

    with check("a duration is read in every unit the suites use"):
        for text, seconds in (("30", 30.0), ("500ms", 0.5), ("45s", 45.0),
                              ("5m", 300.0), ("1.5h", 5400.0), (" 2M ", 120.0)):
            expect(f"{text!r}", cli.parse_duration(text), seconds)

    with check("a duration that is not one is a usage error, not a traceback"):
        for text in ("5x", "", "s", "abc"):
            try:
                cli.parse_duration(text)
            except argparse.ArgumentTypeError as exc:
                expect(f"{text!r} names itself", repr(text) in str(exc), True)
            else:
                raise Failure(f"{text!r} was accepted as a duration")

    with check("a duration has to be a length of time, so zero is refused"):
        for text in ("0", "0s", "-3s", "-1"):
            try:
                cli.parse_duration(text)
            except argparse.ArgumentTypeError as exc:
                expect(f"{text!r} says why", "greater than zero" in str(exc), True)
            else:
                raise Failure(f"{text!r} was accepted as a duration")


def main() -> int:
    import argparse

    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    add_colour_argument(parser)
    apply_colour(parser.parse_args().color)
    try:
        run_setting_checks()
        run_browser_checks()
        run_duration_checks()
    except Failure as exc:
        suite_fail("navigation_test", str(exc))
        return 1
    detail("a letter is shifted only where the firmware folds it back, so a "
           "field still receives what a suite typed")
    suite_ok("navigation_test")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
