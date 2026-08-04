#!/usr/bin/env python3
# PERF: How fast a string can be typed into a menu field, and whether it survives.

"""Compare the two ways the tree types into a menu field, on speed and on truth.

Two implementations exist and they disagree about what is safe:

- `tests/e2e/lib/ui_backend.py` batches a whole string into one
  `machine:input` request, then waits for it to drain.
- `tests/e2e/filesystem/ftp_client_test.py` sends one request per character,
  with a comment recording that batching "floods the injected-key queue past
  its drain rate and silently drops characters (verified)".

Both cannot be right about the same device, and the answer decides whether the
slower path can be dropped. Speed alone cannot settle it: a method that drops
every other character is very fast.

The oracle is a screen scan of the field itself, which is the only way to read
one back. Two things make it trustworthy where earlier attempts were not. It
reads the field while it is still open, rather than inferring from what the UI
did with the value afterwards. And it types a random lowercase needle rather
than hex digits, so a hit cannot come from a memory dump, a path, or a label
that happened to contain the same characters.

A third strategy types half the needle on purpose and has to be reported as
incomplete. Without that control a screen scan that silently matched nothing
would report both real strategies as perfect, which is exactly how the earlier
attempts at this went wrong.

Run it against a device you can spare; it renames nothing, cancelling the
dialog on every path.
"""

import argparse
import os
import random
import statistics
import string
import sys
import time
from pathlib import Path
from typing import List, Tuple

# tests/lib holds the reporting rules every suite shares; tests/e2e/lib holds
# the UI backend under measurement.
SCRIPT_DIR = Path(__file__).resolve().parent
sys.path.insert(0, str(SCRIPT_DIR.parent / "lib"))
sys.path.insert(0, str(SCRIPT_DIR.parent / "e2e" / "lib"))

import ftp as ftp_lib  # noqa: E402  (needs tests/lib on sys.path first)
from api import UltimateApi  # noqa: E402
from report import (  # noqa: E402
    Failure, check_ok, check_start, detail, section, suite_fail, suite_ok)
from ui_backend import add_mode_argument, make_browser  # noqa: E402

SUITE = "typing_speed_perf_test"
DEFAULT_REPEATS = 4
# Long enough for the per-request overhead to show against the drain, short
# enough to fit a rename field.
NEEDLE_LENGTH = 12
FIXTURE_DIR = "/Temp"
FIXTURE_NAME = "typingbench.prg"
# A minimal PRG, so the browser has a real entry to rename.
FIXTURE_BYTES = b"\x01\x08\x0b\x08\x0a\x00\x9e\x32\x30\x36\x31\x00\x00\x00"


def needle() -> str:
    """A random string that cannot appear on screen by accident.

    Letters only, and random, so a hit cannot come from a path, a label or a
    memory dump that happened to contain the same characters. That is what the
    earlier attempt at this got wrong: it searched for hex digits on a screen
    made largely of hex digits.
    """
    return "".join(random.choice(string.ascii_lowercase) for _ in range(NEEDLE_LENGTH))


def open_rename_field(browser) -> None:
    browser.go_to_directory(FIXTURE_DIR)
    browser.select_entry(FIXTURE_NAME[:8])
    browser.invoke_context_action("Rename")


def cancel_rename(browser) -> None:
    browser.press("RUNSTOP")


def type_batched(browser, text: str) -> None:
    browser.type_text(text)


def type_per_key(browser, text: str) -> None:
    for ch in text:
        browser.type_char(ch)


def type_half(browser, text: str) -> None:
    """Deliberately drop every other character. The oracle's control.

    An oracle that cannot fail proves nothing, so one strategy here is known to
    be wrong. If this reports the needle arriving whole, the screen scan is not
    reading the field and the other two results mean nothing either.
    """
    for ch in text[::2]:
        browser.type_char(ch)


def measure(browser, strategy) -> Tuple[float, bool]:
    """Type a needle into the rename field. Returns (seconds, arrived whole)."""
    # The field arrives pre-filled with the current name; the needle is typed
    # after it rather than replacing it, which needs no field-clearing key and
    # still shows a partial arrival as a short or missing needle.
    text = needle()
    open_rename_field(browser)
    started = time.perf_counter()
    strategy(browser, text)
    elapsed = time.perf_counter() - started
    # Case-insensitively: a letter key alone types uppercase in the firmware's
    # default character set, so a lowercase needle comes back uppercase.
    arrived = text.lower() in browser.screen().lower()
    cancel_rename(browser)
    return elapsed, arrived


def sweep(browser, name: str, strategy, repeats: int,
          expect_incomplete: bool = False) -> None:
    check_start(name)
    rates: List[float] = []
    lost = 0
    for _ in range(repeats):
        elapsed, arrived = measure(browser, strategy)
        rates.append(NEEDLE_LENGTH / elapsed if elapsed else 0.0)
        lost += 0 if arrived else 1
    rate = statistics.median(rates)
    if expect_incomplete:
        if lost == repeats:
            check_ok("caught every deliberately truncated needle, so the screen "
                     "scan does read the field")
        else:
            raise Failure(
                f"the control needle was reported whole {repeats - lost} of "
                f"{repeats} times; the screen scan is not reading the field, so "
                "the other measurements above mean nothing")
        return
    if lost:
        check_ok(f"{rate:.1f} chars/s, but {lost} of {repeats} arrived incomplete")
    else:
        check_ok(f"{rate:.1f} chars/s, {repeats}/{repeats} arrived whole")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("-H", "--host", default=os.environ.get("U64_HOST", "u64"))
    parser.add_argument("-p", "--password", default=os.environ.get("U64_PASS", ""))
    parser.add_argument("--repeats", type=int, default=DEFAULT_REPEATS,
                        help=f"measurements per strategy (default: {DEFAULT_REPEATS})")
    add_mode_argument(parser)
    args = parser.parse_args()

    section("Typing into a menu field")
    detail(f"host: {args.host}, mode: {args.mode}, "
           f"{NEEDLE_LENGTH}-character random needles")

    device = UltimateApi(args.host, args.password or None, 15.0)
    browser = None
    try:
        with ftp_lib.session(args.host, args.password, timeout=10) as client:
            ftp_lib.store(client, f"{FIXTURE_DIR}/{FIXTURE_NAME}", FIXTURE_BYTES)
        browser = make_browser(args.mode, args.host, args.password or None)
        sweep(browser, "batched: one request for the whole string",
              type_batched, args.repeats)
        sweep(browser, "per key: one request per character",
              type_per_key, args.repeats)
        sweep(browser, "control: half the characters, must arrive incomplete",
              type_half, args.repeats, expect_incomplete=True)
    except Failure as exc:
        suite_fail(SUITE, str(exc))
        return 1
    finally:
        if browser is not None:
            try:
                browser.close()
            except Failure:
                pass
        try:
            with ftp_lib.session(args.host, args.password, timeout=10) as client:
                ftp_lib.delete_quietly(client, f"{FIXTURE_DIR}/{FIXTURE_NAME}")
            device.machine.close_menu_from_anywhere()
        except Failure:
            pass

    detail("a strategy that arrived incomplete is not a candidate however fast "
           "it was; the tree may only adopt one that arrived whole every time")
    suite_ok(SUITE)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
