#!/usr/bin/env python3
# Gate check: fill_edit_field reads the field back, against a lossy backend.
"""Browser.fill_edit_field, checked without a device.

A cartridge scans its keyboard from the UI task, so a redraw or a DMA stop can
let one injected key pass unseen. Measured on a U2+L under load, a rename typed
as "qmenu2.tst" arrived as "qenu2.tst", and the file was renamed to that. The
field is read back before it is accepted and typed again when it does not show
the text. A device loses a key only now and then and never on demand, so
these checks drive the method through a scripted backend that drops exactly the
keystrokes each case names.

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
import cli  # noqa: E402
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


def fill(backend: LossyBackend) -> list[str]:
    """Run fill_edit_field on `backend`, and the detail lines it reported."""
    reported: list[str] = []
    real_detail = browser.detail
    browser.detail = reported.append
    try:
        browser.Browser(backend, entry_rows=range(1, 2), status_row=3).fill_edit_field(NAME)
    finally:
        browser.detail = real_detail
    return reported


def run_checks() -> None:
    # A typing that never shows the name waits this long before it is retried;
    # a device needs the full allowance, a scripted backend answers at once.
    browser.EDIT_FIELD_ECHO_SECONDS = 0.05

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


def main() -> int:
    cli.device_free_arguments(__doc__)
    try:
        run_checks()
    except Failure as exc:
        suite_fail("browser_edit_field_test", str(exc))
        return 1
    detail("a string box read back before it is accepted, against scripted key loss")
    suite_ok("browser_edit_field_test")
    return 0


if __name__ == "__main__":
    sys.exit(main())
