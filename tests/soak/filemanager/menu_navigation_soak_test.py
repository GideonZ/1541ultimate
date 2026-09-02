#!/usr/bin/env python3
# SOAK: the browser's fast navigation paths, against an oracle that knows the answer.

"""Hammer page-key movement and one-keystroke field clearing, and check both.

Two shortcuts make the menu cheaper to drive, and both are worth checking hard
because both are invisible when wrong: a cursor that lands one row out reads as
a suite picking the wrong file, and a field that did not empty reads as a
rename producing a name nobody typed.

- `Browser.move_rows` spends page keys on the bulk of a jump and single steps
  on the remainder. The browser binds a page key to half a window
  (`TreeBrowser::handle_key`), so the two spellings must land on the same row.
- `Browser.fill_edit_field` empties a string field with one KEY_CLEAR where the
  transport can send it, rather than a counted run of BACKSPACE taps.

The oracle is a directory this suite builds itself, holding files named for
their own index. After moving `n` rows from the top of the listing the cursor
must be on the entry whose name says `n`, which is an answer the harness cannot
produce by accident from a screen it misread. The field checks read the field
back while it is still open, for the same reason.

Each jump is also made the old way, so a run reports whether the fast path is
faster as well as whether it is right. A shortcut that is correct but no
quicker is one to drop, not to keep.

A lost keystroke is counted, not failed. Measured on this bench over six drain
settings from 30ms to 100ms a key, a u2@c64u loses an injected key a few times
in a thousand, and the rate did not move with the drain the harness charged:
the two cleanest passes were the two fastest settings and the slowest setting
lost two. What decides whether a movement lands is therefore how many keys it
takes, not how long each is waited for, and that is what paging changes. A
check that failed on one lost key would be reporting the machine's loss rate as
a defect in the code under test, so what fails here is paging landing somewhere
stepping would not, or paging losing more often than stepping over the same
jumps.

    ./run-tests --soak -s menu-navigation u2@c64u
"""
import argparse
import os
import posixpath
import statistics
import sys
import time
from typing import List

sys.path.insert(0, os.path.join(
    os.path.dirname(os.path.abspath(__file__)), "..", "..", "lib"))
sys.path.insert(0, os.path.join(
    os.path.dirname(os.path.abspath(__file__)), "..", "..", "e2e", "lib"))

import ftp as ftp_lib  # noqa: E402
import pacing  # noqa: E402
import targets  # noqa: E402
from report import (  # noqa: E402
    Failure, add_colour_argument, apply_colour, check, check_fail, check_ok,
    check_start, check_warn, detail, section, suite_fail, suite_ok, warn)

import ui_backend  # noqa: E402

SUITE = "menu_navigation_soak_test"

# The listing this suite navigates. Long enough that a jump crosses more than
# one page key, short enough to build over FTP in a few seconds.
FIXTURE_DIR = "navsoak"
ENTRY_COUNT = 40
# Two bytes of load address is a valid PRG, and nothing here reads the payload.
ENTRY_BODY = bytes([0x01, 0x08]) + b"\x00" * 30

# The rename field checks type this, then read it back. Lower case because that
# is what an injected key produces, and mixed lengths because the bug a clear
# key can hide is a field that kept a tail of the old value.
FIELD_VALUES = ("n1.prg", "navsoak_medium_name.prg", "n2.prg")


def entry_name(index: int) -> str:
    return f"F{index:02d}.PRG"


class Fixture:
    """The directory the checks navigate, built and removed over FTP."""

    def __init__(self, host: str, root: str) -> None:
        self.host = host
        self.root = root
        self.path = posixpath.join(root, FIXTURE_DIR)

    def build(self) -> None:
        with ftp_lib.session(self.host) as client:
            # Already there from an abandoned run is the normal case; the loop
            # fills in whatever is missing and leaves the rest alone.
            ftp_lib.make_dir(client, self.path)
            present = set(ftp_lib.names(client, self.path))
            for index in range(ENTRY_COUNT):
                name = entry_name(index)
                if name not in present:
                    ftp_lib.store(client, name, ENTRY_BODY)

    def remove(self) -> None:
        with ftp_lib.session(self.host) as client:
            ftp_lib.remove_tree(client, self.path)


def interesting_distances(stride: int) -> List[int]:
    """The jump lengths whose page-key decomposition can differ from a step.

    `move_rows` splits a distance into whole page keys and a remainder. The
    split itself is arithmetic and is checked off-device, over every remainder,
    by the `move_rows decomposition` case in tests/lib/runner_policy_test.py.
    What a device has to answer is narrower: that a page key really moves a
    whole stride, that it clamps rather than overshooting, and that a mixed
    batch of page keys and single steps arrives in order. The boundaries below
    are where those can differ.

    Walking all 38 distances instead re-measured the same three things about a
    dozen times over, for four extra minutes on a cartridge target.
    """
    longest = ENTRY_COUNT - 2
    wanted = {1, longest}
    for pages in (1, 2, 3):
        for offset in (-1, 0, 1):
            wanted.add(pages * stride + offset)
    return sorted(d for d in wanted if 1 <= d <= longest)


def field_text(browser) -> str:
    """The string edit field's own line, read out of the popup frame.

    Not the whole screen: the listing behind the popup still shows the entry
    being renamed, so a screen-wide search for the old name reports a field
    that was cleared as one that was not.
    """
    framed = [row for row in browser.rows() if row.startswith("|")]
    if not framed:
        raise Failure("no popup frame on screen; the rename field is not open")
    return framed[-1].strip("|").strip()


def landed_index(browser) -> int:
    """Which fixture entry the cursor is on, read off the screen."""
    text = browser.selected_text().split()
    if not text or not text[0].upper().startswith("F"):
        raise Failure(f"the cursor is not on a fixture entry; it reads {text!r}")
    try:
        return int(text[0][1:3])
    except ValueError:
        raise Failure(f"entry {text[0]!r} does not name its own index")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("-H", "--host", default=os.environ.get("U64_HOST", "u64"))
    parser.add_argument("-p", "--password", default=os.environ.get("U64_PASS", ""))
    parser.add_argument("-t", "--timeout", type=float, default=30.0)
    parser.add_argument("--mode", default="overlay")
    parser.add_argument("--iterations", type=int, default=10,
                        help="repeats of the longest jump, and field clears")
    parser.add_argument("--root", default="/Temp",
                        help="where to build the fixture directory")
    parser.add_argument("--key-drain", type=float, default=None,
                        help="seconds to charge per injected key, overriding "
                             "pacing.SPLIT_KEY_DRAIN_SECONDS for this run. "
                             "Sweeping it is how the constant was chosen: the "
                             "checks here fail on a lost key rather than "
                             "reporting a slow one, so the boundary is where "
                             "they start failing")
    add_colour_argument(parser)
    args = parser.parse_args()
    apply_colour(args.color)

    target = targets.parse(args.host)
    if args.key_drain is not None:
        pacing.remember_key_drain(targets.device_of(target.input_host),
                                  args.key_drain)
    fixture = Fixture(targets.device_of(target), args.root)
    # make_browser gives a Telnet session its own listing geometry, which
    # page_rows needs: the stride is half the listing height, and Telnet's is
    # one row shorter than the 40x25 display's.
    browser = ui_backend.make_browser(args.mode, args.host,
                                      args.password or None, args.timeout)
    failures: List[str] = []

    def failed(label: str, reason: str) -> None:
        failures.append(f"{label}: {reason}")
        check_fail(reason)

    try:
        fixture.build()
        browser.backend.ensure_ready()
        stride = browser.page_rows()
        # Only the REST backend charges a per-key drain; Telnet writes bytes to
        # a stream and reads the redraw back, so it has no such number.
        charged = getattr(browser.backend, "key_drain_seconds", None)
        detail(f"page key moves {stride} of {len(list(browser.entry_rows))} "
               "listing rows"
               + (f"; {charged * 1000:.0f} ms charged per key"
                  if charged is not None else "; this transport charges none"))
        browser.go_to_root()
        browser.go_to_directory(fixture.path)

        section("1. Paging lands where stepping lands, and loses less")
        # Neither spelling is checked against the other: both are checked
        # against the distance asked for, and a mismatch is counted rather
        # than failed. On this bench a u2@c64u loses an injected key a few
        # times in a thousand whatever drain the harness charges, so a check
        # that failed on one lost key would be reporting the machine's loss
        # rate as a defect in the code under test. What is a defect is paging
        # landing somewhere stepping would not, or paging losing more.
        distances = interesting_distances(stride)
        detail(f"distances: {distances}")
        stepped_ms: List[float] = []
        paged_ms: List[float] = []
        stepped_lost = paged_lost = 0
        stepped_keys = paged_keys = 0
        for distance in distances:
            label = f"jump {distance} rows"
            check_start(label)
            pages, singles = divmod(distance, stride)
            browser.go_to_top()
            started = time.monotonic()
            browser.press_many("DOWN", distance)
            stepped_ms.append((time.monotonic() - started) * 1000.0)
            stepped_keys += distance
            stepped = landed_index(browser)
            browser.go_to_top()
            started = time.monotonic()
            browser.move_rows(distance)
            paged_ms.append((time.monotonic() - started) * 1000.0)
            paged_keys += pages + singles
            paged = landed_index(browser)
            if stepped != distance:
                stepped_lost += 1
            if paged != distance:
                paged_lost += 1
            if paged == distance and stepped == distance:
                check_ok(f"F{paged:02d}, {pages + singles} keys against {distance}")
            elif paged == distance:
                check_ok(f"F{paged:02d}; stepping lost a key and read F{stepped:02d}")
            else:
                check_fail(f"paging read F{paged:02d}, F{distance:02d} expected "
                           f"({pages} page keys and {singles} steps); stepping "
                           f"read F{stepped:02d}")
                failures.append(f"{label}: paging landed on F{paged:02d}")
        detail(f"median: stepping {statistics.median(stepped_ms):.0f}ms, "
               f"paging {statistics.median(paged_ms):.0f}ms")
        detail(f"keys sent: stepping {stepped_keys}, paging {paged_keys}")
        with check("paging is at least as reliable as stepping"):
            detail(f"wrong landings: stepping {stepped_lost} of {len(distances)}, "
                   f"paging {paged_lost} of {len(distances)}")
            if paged_lost > stepped_lost:
                raise Failure(
                    f"paging landed wrong {paged_lost} times against stepping's "
                    f"{stepped_lost}, over the same jumps; the cheaper spelling "
                    "is supposed to lose less, not more")

        section("2. The longest jump, repeated, to measure what a key costs")
        # One correct landing is not evidence: a lost key is intermittent by
        # nature. This is where the per-key loss rate is measured, from the
        # jump with the most keys in it.
        distance = ENTRY_COUNT - 2
        pages, singles = divmod(distance, stride)
        per_jump = pages + singles
        drifted = 0
        for iteration in range(args.iterations):
            label = f"repeat {iteration + 1} of {args.iterations}"
            check_start(label)
            browser.go_to_top()
            browser.move_rows(distance)
            landed = landed_index(browser)
            if landed == distance:
                check_ok()
            else:
                drifted += 1
                check_warn(f"landed on F{landed:02d}, F{distance:02d} expected")
        sent = per_jump * args.iterations
        detail(f"{args.iterations - drifted} of {args.iterations} landed exactly, "
               f"over {sent} injected keys")
        with check("the machine does not lose most of what it is sent"):
            # A loose bound on purpose. The point of the figure is to be
            # recorded and compared between runs and machines; what would make
            # this check fail is a machine or a path that has stopped working,
            # not the few-in-a-thousand loss this bench already has.
            if drifted > args.iterations // 2:
                raise Failure(
                    f"{drifted} of {args.iterations} jumps landed wrong, which "
                    "is not a loss rate, it is a broken path")
            rate = (drifted / sent * 100.0) if sent else 0.0
            detail(f"about {rate:.2f}% of injected keys went missing")

        section("3. Clearing a field leaves nothing of what was there")
        clear_key = browser.backend.clear_field_key
        if clear_key is None:
            warn(f"{args.mode} cannot send a clear key; the counted BACKSPACE "
                 "path is what runs here")
        for iteration in range(args.iterations):
            wanted = FIELD_VALUES[iteration % len(FIELD_VALUES)]
            label = f"rename to {wanted!r} ({iteration + 1} of {args.iterations})"
            check_start(label)
            browser.go_to_top()
            try:
                browser.invoke_context_action("Rename")
                # Read the field back while it is still open. Judging the
                # clear by what the rename produced afterwards cannot tell a
                # field that kept a tail from a rename that failed for its own
                # reasons.
                prefilled = field_text(browser)
                if clear_key:
                    browser.press(clear_key)
                else:
                    browser.press_many("BACKSPACE", 64)
                emptied = field_text(browser)
                browser.type_text(wanted)
                shown = field_text(browser)
                # RUN/STOP, so the fixture entry keeps its name and every
                # iteration starts from the same field contents.
                browser.press("RUNSTOP")
            except Failure as exc:
                failed(label, f"the rename field could not be driven: {exc}")
                continue
            if prefilled != entry_name(0):
                failed(label, f"the field opened reading {prefilled!r}, "
                              f"{entry_name(0)!r} expected")
                continue
            if emptied:
                failed(label, f"the field still reads {emptied!r} after being "
                              "cleared")
                continue
            if shown != wanted:
                failed(label, f"the field reads {shown!r} after {wanted!r} "
                              "was typed into it")
                continue
            check_ok()

        browser.go_to_root()
    except Failure as exc:
        suite_fail(SUITE, str(exc))
        return 1
    finally:
        try:
            browser.close()
        except Exception:  # noqa: BLE001
            pass
        try:
            browser.backend.close()
        except Exception:  # noqa: BLE001
            pass
        try:
            fixture.remove()
        except Exception as exc:  # noqa: BLE001
            warn(f"the fixture directory could not be removed: {exc}")

    if failures:
        suite_fail(SUITE, f"{len(failures)} of the navigation checks failed")
        for line in failures[:12]:
            detail(line)
        return 1
    suite_ok(SUITE)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
