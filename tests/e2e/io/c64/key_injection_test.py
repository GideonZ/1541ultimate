#!/usr/bin/env python3
"""How fast can keys be injected before one goes missing?

The E2E suites inject keystrokes through `machine:input` and read the result
back from a screen. When one keystroke goes missing, the suite that lost it
fails somewhere else entirely - a command argument short of a character, a
Jump that landed on the wrong address - and says nothing about the injection
rate that caused it. This measures the injection itself, so the pacing the
suites use can be a measured number rather than a guess.

Two destinations, because the firmware delivers to them by different paths
(software/api/route_input.cc, apply_keyboard_event):

  basic  With the menu closed, keys are queued into the C64's keyboard matrix
         and the KERNAL scans them on its own interrupt. BASIC echoes each
         character it receives to the cursor, so C64 screen memory at $0400 is
         the record of what arrived, in order.

  mcm    With the menu open, keys go to the menu's own keyboard state instead
         and never reach the matrix. The monitor's ASCII view is typed into,
         and the memory it edits is the record: edit mode writes each
         character to the address the cursor is on and advances, so the page
         read back is exactly the keys that arrived.

Both oracles are memory, not a screen predicate, and both compare against the
exact byte the machine should hold. That matters: an earlier version of this
compared the monitor's page against upper-case ASCII while edit mode had
written the lower-case the key actually produces, and reported 396 of 420 keys
lost when none had been.

Losses are counted rather than only reported: the run says how many of how
many arrived and where the first gap was.
"""

from __future__ import annotations

import argparse
import os
import sys
import time
from typing import List, Optional, Sequence, Tuple

sys.path.insert(0, os.path.join(
    os.path.dirname(os.path.abspath(__file__)), "..", "..", "..", "lib"))
sys.path.insert(0, os.path.join(
    os.path.dirname(os.path.abspath(__file__)), "..", "..", "lib"))

import pacing
import targets
from api import UltimateApi
from report import Failure, check, detail, section, suite_fail, suite_ok
from ui_backend import make_backend

SCREEN_RAM = 0x0400
SCREEN_COLUMNS = 40
SCREEN_ROWS = 25

# Lower-case letters and digits: every one is a single unshifted key, so a
# mismatch is a lost key rather than a shift that did not take.
ALPHABET = "abcdefghijklmnopqrstuvwxyz0123456789"

# The page the monitor destination edits. RAM the KERNAL does not use, filled
# with spaces first so an arriving character is the only thing that can change
# a byte.
EDIT_BASE = 0xC000
SPACE = 0x20

# BASIC's input line is two screen lines long and a longer one wraps into a
# third, so a BASIC cycle types less than that and clears before the next.
BASIC_KEYS_PER_CYCLE = 70

# The monitor page is 256 bytes; a cycle stays inside one.
MCM_KEYS_PER_CYCLE = 200

# How long a sent key has to appear before it counts as lost. Generous on
# purpose: this separates "late" from "gone", and only the second is a loss.
ARRIVAL_TIMEOUT_SECONDS = 25.0

# Keys typed and discarded after the destination is opened, before anything is
# measured. The first keys after a reset are not representative of a rate.
WARM_UP_KEYS = 12

# How long the monitor destination's setup keeps retrying one step.
SETUP_TIMEOUT_SECONDS = 15.0


def alphabet_at(index: int) -> str:
    return ALPHABET[index % len(ALPHABET)]


def screen_code(character: str) -> int:
    """What BASIC leaves in screen memory for one typed character."""
    if character.isdigit():
        return ord(character)
    return ord(character) - ord("a") + 1


class Destination:
    """Somewhere keys can be typed, and the memory that records them."""

    keys_per_cycle = BASIC_KEYS_PER_CYCLE

    def __init__(self, target: str, password: Optional[str], timeout: float) -> None:
        self.target = targets.parse(target)
        self.keys = UltimateApi(self.target.input_host, password, timeout=timeout)
        self.memory = UltimateApi(self.target.device, password, timeout=timeout)
        self.split = self.target.split

    def open(self) -> None:
        raise NotImplementedError

    def restart_cycle(self) -> None:
        raise NotImplementedError

    def arrived(self, typed: str) -> Tuple[int, Optional[int]]:
        raise NotImplementedError

    def close(self) -> None:
        pass

    def tap_batch(self, characters: str) -> None:
        """One request for a whole run of keys.

        The device paces the keys itself, so a request per key buys nothing and
        costs a round trip each time. Measured on both machines: a 32-key batch
        posts in 53ms and takes 3.2s to arrive, so the wire is about 1ms a key
        against the device's own pacing.
        """
        self.keys.machine.send_input(
            [{"kind": "keyboard", "inputs": [character], "transition": "tap"}
             for character in characters])

    def _await(self, read, expected: bytes) -> Tuple[int, Optional[int]]:
        deadline = time.monotonic() + ARRIVAL_TIMEOUT_SECONDS
        while True:
            got = read()
            matched = 0
            while matched < len(expected) and got[matched] == expected[matched]:
                matched += 1
            if matched == len(expected):
                return matched, None
            if time.monotonic() >= deadline:
                return matched, matched
            time.sleep(pacing.POLL_INTERVAL_SECONDS)


class BasicDestination(Destination):
    keys_per_cycle = BASIC_KEYS_PER_CYCLE

    def open(self) -> None:
        self.keys.machine.release_all()
        self.memory.machine.reset(force=True)
        self.restart_cycle()

    def restart_cycle(self) -> None:
        self.keys.machine.press("left_shift", "clr_home")
        time.sleep(pacing.KEY_SETTLE_SECONDS)

    def screen(self) -> bytes:
        return self.memory.machine.readmem(SCREEN_RAM,
                                           SCREEN_COLUMNS * SCREEN_ROWS)

    def arrived(self, typed: str) -> Tuple[int, Optional[int]]:
        """How many of `typed` are on screen in order, and the first gap.

        The characters land wherever the cursor was, so the run is found
        rather than assumed at a fixed offset.
        """
        codes = bytes(screen_code(c) for c in typed)
        deadline = time.monotonic() + ARRIVAL_TIMEOUT_SECONDS
        while True:
            screen = self.screen()
            best = 0
            for start in range(len(screen)):
                matched = 0
                while (matched < len(codes)
                       and start + matched < len(screen)
                       and screen[start + matched] == codes[matched]):
                    matched += 1
                best = max(best, matched)
                if best == len(codes):
                    return best, None
            if time.monotonic() >= deadline:
                return best, best
            time.sleep(pacing.POLL_INTERVAL_SECONDS)


class MonitorDestination(Destination):
    """The monitor's ASCII view, which is the menu's own key path."""

    keys_per_cycle = MCM_KEYS_PER_CYCLE

    def __init__(self, target: str, password: Optional[str], timeout: float,
                 mode: str) -> None:
        super().__init__(target, password, timeout)
        self.backend = make_backend(mode, target, password, timeout)

    def close(self) -> None:
        self.backend.close()

    def _screen_text(self) -> str:
        try:
            return "\n".join(self.backend.capture().lines)
        except Failure:
            return ""

    def _until(self, wanted: str, act, what: str) -> None:
        """Do `act` until `wanted` is on screen, or say what never happened.

        Every setup step is retried rather than sent once: the first key after
        the menu opens is exactly the kind of key this suite measures the loss
        of, and a setup that sent it once would report that loss as a broken
        fixture.
        """
        deadline = time.monotonic() + SETUP_TIMEOUT_SECONDS
        while time.monotonic() < deadline:
            if wanted in self._screen_text():
                return
            act()
        raise Failure(f"{what}: {wanted!r} never appeared\n{self._screen_text()}")

    def _jump(self) -> None:
        self.backend.send_char("j")
        for character in f"{EDIT_BASE:04X}":
            self.backend.send_char(character)
        self.backend.send_key("ENTER", settle=True)

    def open(self) -> None:
        self.memory.machine.writemem(EDIT_BASE, bytes([SPACE]) * 256)
        # From a closed menu every time. Backing out of whatever the last
        # cycle left open is guesswork - one Back too many lands in the file
        # browser, where the jump keys are a quick-seek and RETURN launches
        # whatever they selected - and closing the menu outright is not.
        self.backend._close_menu()
        # Closing and reopening back to back is too quick for the device: the
        # reopen times out looking for a menu the close has not taken down.
        time.sleep(pacing.MENU_TOGGLE_SETTLE_SECONDS)
        self.backend.ensure_ready()
        self._until("MONITOR",
                    lambda: self.backend.send_key("CTRL_O", settle=True),
                    "opening the monitor")
        self._until(f"${EDIT_BASE:04X}", self._jump, "jumping to the edit page")
        self._until("ASC ", lambda: self.backend.send_char("i"),
                    "selecting the ASCII view")
        self._until("EDIT", lambda: self.backend.send_char("e"),
                    "entering edit mode")

    def restart_cycle(self) -> None:
        self.open()

    def arrived(self, typed: str) -> Tuple[int, Optional[int]]:
        expected = typed.encode("ascii")
        return self._await(
            lambda: self.memory.machine.readmem(EDIT_BASE, len(expected)),
            expected)


def warm_up(destination: Destination) -> int:
    typed = "".join(alphabet_at(index) for index in range(WARM_UP_KEYS))
    destination.tap_batch(typed)
    matched, _ = destination.arrived(typed)
    return matched


def measure(destination: Destination, total: int, batch: int,
            pace: float, transition: str = "tap") -> Tuple[int, int, List[str]]:
    """Type `total` keys in runs of `batch`, checking after each run."""
    sent = 0
    arrived = 0
    notes: List[str] = []
    send = destination.tap_batch

    while sent < total:
        destination.restart_cycle()
        cycle = min(destination.keys_per_cycle, total - sent)
        typed = ""
        lost = 0
        for start in range(0, cycle, batch):
            run = "".join(alphabet_at(sent + start + offset)
                          for offset in range(min(batch, cycle - start)))
            send(run)
            typed += run
            if pace:
                time.sleep(pace * len(run))
            matched, gap = destination.arrived(typed)
            if matched != len(typed):
                lost = len(typed) - matched
                notes.append(f"after {len(typed)} keys only {matched} arrived; "
                             f"first missing at index {gap}")
                break
        sent += len(typed)
        arrived += len(typed) - lost
    return arrived, sent, notes


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Measure whether injected keys reach the machine")
    parser.add_argument("-H", "--host", default=os.environ.get("U64_HOST", "u64"))
    parser.add_argument("-p", "--password", default=os.environ.get("U64_PASS"))
    parser.add_argument("-t", "--timeout", type=float,
                        default=float(os.environ.get("U64_TIMEOUT", "15.0")))
    parser.add_argument("--where", choices=("basic", "mcm"), default="basic")
    parser.add_argument("--mode", default="overlay",
                        help="UI transport for --where mcm")
    parser.add_argument("--keys", type=int, default=10,
                        help="keys to inject (default 10, the size an E2E run uses)")
    parser.add_argument("--batch", type=int, default=10,
                        help="keys per machine:input request")
    parser.add_argument("--pace", type=float, default=0.0,
                        help="extra seconds per key on top of the device's own")
    args = parser.parse_args()

    if args.where == "mcm":
        destination: Destination = MonitorDestination(
            args.host, args.password, args.timeout, args.mode)
    else:
        destination = BasicDestination(args.host, args.password, args.timeout)

    section(f"keys injected into {args.where}")
    try:
        with check(f"the {args.where} destination is ready to type into"):
            destination.open()
            detail(f"target {args.host}, keys go to "
                   f"{destination.target.input_host}, memory read from "
                   f"{destination.target.device}")

        with check(f"every key arrives in batches of {args.batch}"):
            warmed = warm_up(destination)
            if warmed != WARM_UP_KEYS:
                detail(f"{WARM_UP_KEYS - warmed} of the {WARM_UP_KEYS} warm-up "
                       f"keys were lost, which this does not count")
            arrived, sent, notes = measure(destination, args.keys, args.batch,
                                           args.pace, "tap")
            for note in notes[:4]:
                detail(f"  {note}")
            detail(f"{arrived} of {sent} keys arrived")
            if arrived != sent:
                raise Failure(f"{sent - arrived} of {sent} keys were lost in "
                              f"batches of {args.batch}")

    finally:
        destination.close()

    suite_ok("key_injection_test")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Failure as exc:
        suite_fail("key_injection_test", str(exc))
        raise SystemExit(1)
