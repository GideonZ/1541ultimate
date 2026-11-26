#!/usr/bin/env python3
"""Hardware soak for #797 using a Pico 2 W as a deliberately faulty USB keyboard.

The fixture has one boot-keyboard HID interface and a narrow Wi-Fi protocol.
It never executes caller-supplied code and can only inject the small key set
needed to navigate the Ultimate menu.

The Pico must be the only USB keyboard attached to the Ultimate 64.  The
firmware passes the negotiated idle period on to the repeat only when exactly
one attached keyboard accepted it, because Keyboard_USB sees a single merged
report stream and the periodic reports of a second keyboard would keep a stale
key of the first looking fresh (software/io/usb/usb_hid_selection.h).  With a
second keyboard present the repeat bound is disabled and the fault cases below
report far more than one menu movement.

Fault design is derived from the firmware's own timing, not guessed:

- `first_delay` in keyboard_usb.cc is 16 poll ticks, measured at ~320ms of real
  held-key time before the first repeat fires (FIRST_REPEAT_DELAY_MS below).
- `USB_REPEAT_STALE_IDLE_PERIODS` (3) times the negotiated 100ms SET_IDLE
  period gives a 300ms staleness cutoff (STALE_TIMEOUT_MS below).  A held key
  whose reports go silent for longer than that has its repeat bound off by the
  fix under test.
- Those two numbers are only 20ms apart. That is deliberate margin, not
  accident (see usb_hid_selection.h), but it is also the narrowest part of the
  fix: a key held for a duration straddling 320ms decides, right at the fault
  boundary, whether zero or one genuine repeat already fired before its
  release is lost. `MID_REPEAT_HOLD_DURATIONS_MS` below sweeps exactly that
  boundary, in addition to durations long enough for several genuine repeats
  to be in flight when the release is lost.

Each fault case is graded against a baseline this fixture measures on the live
device, not a hand-derived constant: `calibrate_repeat_counts` holds the same
key for the same duration with a normal, un-faulted release, and records how
many genuine repeats that produces. A faulted run of the same hold duration
must not exceed that baseline by more than one extra step, which is the single
"a report landing mid-check can cost one repeat decision" race the firmware
comments accept as a known, bounded cost. Anything past that is the runaway
repeat this fixture exists to catch.

Detection reads the selected file's row number directly out of the menu
screen, not a fuzzy text/colour comparison. Every fault runs synchronously on
the Pico before its control call returns, so nothing is observable mid-fault;
once the call returns, `watch_bounded` polls the settling screen instead of
taking one snapshot, and raises the instant a sample exceeds the allowed step
count rather than waiting for the whole settle window -- ending the run on
the first sign of a runaway instead of only noticing it afterwards.
"""
from __future__ import annotations

import argparse
import ftplib
import io
import random
import re
import sys
import time
from pathlib import Path
from typing import Any

# The one stanza that puts the shared library on sys.path; see tests/lib/bootstrap.py.
sys.path.insert(0, str(next(p for p in Path(__file__).resolve().parents
                            if (p / "tests" / "lib").is_dir()) / "tests" / "lib"))
import bootstrap  # noqa: E402,F401
import ftp as ftp_lib  # noqa: E402
import cli  # noqa: E402
from api import MachineApi  # noqa: E402
from report import Failure, check, detail, format_exception, suite_fail, suite_ok, suite_skip  # noqa: E402
from rest import RestClient, header_value  # noqa: E402
from pico_hid import PROTOCOL_VERSION, Pico, discover_pico, setup_pico  # noqa: E402

DEFAULT_DURATION = {"stress": 120.0, "soak": 12 * 60 * 60.0}
# The root browser holds six entries, so a held key reaches its end within a
# fraction of a second and further presses stop moving the selection.  The suite
# builds its own list on the RAM disk instead, deep enough that a held key never
# reaches the end and independent of what is on the attached storage.  200 gives
# headroom for the mid-repeat holds below, which can move several rows in one
# shot, without the recentring logic having to run every iteration.
DEEP_LIST_DIRECTORY = "usbsoak"
# How many entries of the RAM disk the setup navigation steps past to find the
# list. /Temp normally holds a handful; this bounds the search on one that
# holds far more.
RAM_DISK_SEARCH_LIMIT = 32
DEEP_LIST_ENTRIES = 200
ROW_NAME_WIDTH = len(str(DEEP_LIST_ENTRIES - 1))
# The soak builds and removes this listing while the device is under load,
# so the FTP session is given the same bounded wait as everything else
# rather than the blocking default.
FTP_TIMEOUT_SECONDS = 15.0
ROW_INDEX_RE = re.compile(r"^row(\d+)\.txt\b")
SCREEN_CELLS = 1000
SELECTED_MIN = 12

# Real, elapsed-time firmware constants (see the module docstring). Used to size
# fault windows and to choose hold durations that straddle the tight boundary
# between them, not to assert timing that this fixture cannot observe directly.
FIRST_REPEAT_DELAY_MS = 320   # `first_delay` (16 ticks) in keyboard_usb.cc
REPEAT_INTERVAL_MS = 80       # `repeat_speed` (4 ticks) in keyboard_usb.cc
STALE_TIMEOUT_MS = 300        # USB_REPEAT_STALE_IDLE_PERIODS * 100ms SET_IDLE period
# Headroom added to a fault window before the suite decides a run is over. Large
# enough to catch a repeat that fires just after either boundary above.
POST_FAULT_MARGIN_MS = 400
# `silence_after_press` keeps sending idle reports of the still-held key for as
# long as the genuine part of the hold lasts (service_idle is not suppressed
# until after `duration_ms`), so the staleness clock only starts counting from
# the *last* of those idle reports, not from when the fault nominally begins.
# That last idle report can land up to one SET_IDLE period before the fault
# starts, and the key is then still legitimately "live" for STALE_TIMEOUT_MS
# after it. So a silenced hold keeps repeating for up to this much longer than
# its nominal duration before the staleness bound has to engage -- by design,
# not by bug. `drop_release_once` does not get this grace: it keeps idle
# reports flowing straight through the fault, so the real release reaches the
# firmware within one idle period of `duration_ms` regardless.
SILENCE_GRACE_MS = STALE_TIMEOUT_MS + 100

# A tap well short of the first repeat: the release is lost before any genuine
# repeat could have fired, so exactly one keystroke is the only correct outcome.
PRE_REPEAT_TAP_DURATIONS_MS = (12, 18, 25, 30, 35, 45, 60)
# Holds that straddle FIRST_REPEAT_DELAY_MS (300, 310 land just under it; 320 is
# the boundary itself; the rest are far enough past it for one or more genuine
# repeats), so the fault lands while the firmware is deciding whether a repeat
# has just started.
MID_REPEAT_HOLD_DURATIONS_MS = (300, 310, 320, 330, 340, 360, 400, 480, 560, 640, 720, 800)
# The fixture's own protocol floor is 400ms (main.py rejects anything shorter),
# which already sits above STALE_TIMEOUT_MS, so every one of these gives the
# staleness bound a genuine chance to engage before the fault ends.
FAULT_DURATIONS_MS = (400, 500, 600, 750, 900, 1050, 1200)
FAULT_KINDS = ("drop_release_once", "silence_after_press")
CALIBRATION_REPS = 3
# Keep the live selection away from the list's ends so a real menu boundary
# clamp is never mistaken for the repeat bound failing to move the selection.
RECENTRE_LOW = 30
RECENTRE_HIGH = DEEP_LIST_ENTRIES - 30


def marker(body: bytes) -> tuple[int, str]:
    if len(body) != 2000:
        raise Failure(f"menu screen has {len(body)} bytes, expected 2000")
    chars, colours = body[:SCREEN_CELLS], body[SCREEN_CELLS:]
    candidates = []
    for row in range(2, 24):
        text = "".join(chr(c & 0x7f) if 0x20 <= (c & 0x7f) <= 0x7e else " "
                       for c in chars[row * 40:(row + 1) * 40]).strip()
        row_chars, row_colours = chars[row * 40 + 1:(row + 1) * 40 - 1], colours[row * 40 + 1:(row + 1) * 40 - 1]
        backgrounds, foregrounds, reverse = {}, {}, 0
        for char, colour in zip(row_chars, row_colours):
            reverse += bool(char & 0x80)
            background, foreground = colour >> 4, colour & 15
            if background:
                backgrounds[background] = backgrounds.get(background, 0) + 1
            elif foreground != 15:
                foregrounds[foreground] = foregrounds.get(foreground, 0) + 1
        candidates.append((max(backgrounds.values(), default=0), max(foregrounds.values(), default=0), reverse, row, text))
    background = max(candidates, key=lambda x: x[0])
    reverse = max(candidates, key=lambda x: x[2])
    foreground = max(candidates, key=lambda x: x[1])
    chosen = background if background[0] >= SELECTED_MIN else reverse if reverse[2] >= SELECTED_MIN else foreground
    if max(chosen[:3]) < SELECTED_MIN:
        raise Failure("could not locate selected menu row")
    return chosen[3], chosen[4]


def parse_row_index(text: str) -> int:
    """Extract the exact list position from a selected row's filename.

    The soak list's names sort and number the same way (`row000.txt` etc), so
    this turns the fuzzy on-screen marker into an exact integer position. That
    lets callers assert precise movement counts instead of only "did the text
    change", which cannot tell one extra keystroke from several.
    """
    match = ROW_INDEX_RE.match(text)
    if not match:
        raise Failure(f"selected row does not look like a soak list entry: {text!r}")
    return int(match.group(1))


class U64:
    def __init__(self, host: str, password: str | None, timeout: float):
        self.host = host
        self.rest = RestClient(host, password, timeout)
        self.machine = MachineApi(self.rest)
    def rows(self) -> list:
        status, _headers, body = self.rest.request("GET", "/v1/machine:menu_screen")
        if status != 200:
            raise Failure(f"menu_screen failed with HTTP {status}")
        chars = body[:SCREEN_CELLS]
        return [line for line in
                ("".join(chr(c & 0x7f) if 0x20 <= (c & 0x7f) <= 0x7e else " "
                         for c in chars[row * 40:(row + 1) * 40]).strip()
                 for row in range(2, 24)) if line]
    def key(self, *names: str, settle: float = .35):
        """Press a menu key over REST.

        Menu keys injected through `machine:input` reach the same on-screen menu
        as the USB keyboard, so this drives the suite's own setup navigation.
        The Telnet remote menu is a separate 60-column user interface instance
        and does not move the on-screen selection.
        """
        self.machine.press(*names)
        time.sleep(settle)
    def screen(self) -> tuple[int, str]:
        status, headers, body = self.rest.request("GET", "/v1/machine:menu_screen")
        if status != 200 or "application/octet-stream" not in header_value(headers, "Content-Type"):
            raise Failure(f"menu_screen failed with HTTP {status}: {body[:120]!r}")
        return marker(body)
    def stable_screen(self, attempts: int = 4) -> tuple[int, str]:
        """Read the menu until two consecutive reads agree.

        A read that lands in the middle of a redraw can report the previous
        selection, which over a long run would fail a check that the firmware
        passed.  Callers use this only where the selection is expected to have
        settled, so a disagreement means a redraw, not a live repeat.
        """
        previous = self.screen()
        for _ in range(attempts):
            current = self.screen()
            if current == previous:
                return current
            previous = current
        return previous
    def position(self) -> int:
        """The selected row's exact list index, for precise movement counts."""
        _, text = self.screen()
        return parse_row_index(text)
    def stable_position(self, attempts: int = 6, gap: float = .03) -> int:
        previous = self.position()
        for _ in range(attempts):
            time.sleep(gap)
            current = self.position()
            if current == previous:
                return current
            previous = current
        return previous
    def open_menu(self):
        status, _, _ = self.rest.request("GET", "/v1/machine:menu_screen")
        if status == 200:
            self.screen()
            return
        status, _, body = self.rest.request("PUT", "/v1/machine:menu_button")
        if status != 200:
            raise Failure(f"menu button failed: {body[:120]!r}")
        time.sleep(.3)
        self.screen()
    def close_menu(self):
        self.machine.close_menu_from_anywhere()


def watch_bounded(u64: U64, anchor_index: int, key: str, allowed_delta: int,
                   window_s: float = POST_FAULT_MARGIN_MS / 1000.0,
                   poll_s: float = .03) -> tuple[int, list[int]]:
    """Poll the selected row and abort the instant an out-of-bound step appears.

    The fixture executes a whole press/fault/release sequence synchronously on
    the Pico before its control call returns, so nothing can be observed while
    it is in flight -- only once the call returns is there anything on the
    U64 to read. From that point, a single read can still miss a repeat that
    is still landing (REST racing the screen redraw), so this polls for a
    short settle window instead of reading once, and raises the moment a
    sample exceeds `allowed_delta` rather than waiting for the window to
    finish. That is what lets a run end on the first sign of a runaway rather
    than only after the whole window has elapsed.
    """
    direction = 1 if key == "down" else -1
    deadline = time.monotonic() + window_s
    last_index = anchor_index
    trail = [anchor_index]
    while time.monotonic() < deadline:
        index = u64.position()
        if index != last_index:
            trail.append(index)
            delta = (index - anchor_index) * direction
            if delta < 0 or delta > allowed_delta:
                raise Failure(f"key {key!r} moved {delta} steps from anchor {anchor_index} "
                              f"(allowed {allowed_delta}); trail={trail}")
            last_index = index
        time.sleep(poll_s)
    return (last_index - anchor_index) * direction, trail


def recentre_if_needed(pico: Pico, u64: U64, attempts: int = 6):
    for _ in range(attempts):
        index = u64.stable_position()
        if RECENTRE_LOW <= index <= RECENTRE_HIGH:
            return
        target = "down" if index < RECENTRE_LOW else "up"
        steps = abs((RECENTRE_LOW + RECENTRE_HIGH) // 2 - index)
        hold_ms = min(2000, FIRST_REPEAT_DELAY_MS + steps * REPEAT_INTERVAL_MS + 200)
        pico.call("hold", key=target, duration_ms=hold_ms)
        time.sleep(.3)
    index = u64.stable_position()
    if not (RECENTRE_LOW <= index <= RECENTRE_HIGH):
        raise Failure(f"could not recentre the selection after {attempts} attempts: now {index}")


def measure_genuine_hold(pico: Pico, u64: U64, hold_ms: int, reps: int) -> int:
    counts = []
    for _ in range(reps):
        recentre_if_needed(pico, u64)
        anchor = u64.stable_position()
        pico.call("hold", key="down", duration_ms=hold_ms)
        time.sleep(.25)
        counts.append(u64.stable_position() - anchor)
        pico.call("hold", key="up", duration_ms=hold_ms)
        time.sleep(.25)
    detail(f"calibration: {hold_ms}ms genuine hold -> {counts} repeats (max {max(counts)})")
    return max(counts)


def calibrate_repeat_counts(pico: Pico, u64: U64) -> tuple[dict[int, int], dict[int, int]]:
    """Measure genuine, un-faulted repeat counts for each mid-repeat hold.

    Used as this device's own ground truth for the mid-repeat fault scenarios:
    a faulted hold of a given duration must not produce materially more
    repeats than an unfaulted hold already does by design. A constant derived
    only from `first_delay`/`repeat_speed` would drift from what real USB
    polling and task scheduling actually produce, so it is measured directly.

    Two baselines are kept because the two faults extend the "genuine" window
    differently. `drop_release_once` keeps idle reports flowing through the
    fault, so its true release reaches the firmware within about one idle
    period of `duration_ms`: `hold_baseline[duration_ms]` covers it directly.
    `silence_after_press` stops all reports at `duration_ms`, but the key was
    still being refreshed by idle reports right up to that point, so it stays
    legitimately "live" for `SILENCE_GRACE_MS` longer (see the constant's
    comment): `silence_baseline[duration_ms]` is calibrated at that longer,
    effective duration instead of at `duration_ms` itself.
    """
    hold_baseline: dict[int, int] = {}
    silence_baseline: dict[int, int] = {}
    for duration_ms in MID_REPEAT_HOLD_DURATIONS_MS:
        hold_baseline[duration_ms] = measure_genuine_hold(pico, u64, duration_ms, CALIBRATION_REPS)
    for duration_ms in MID_REPEAT_HOLD_DURATIONS_MS:
        effective_ms = min(2000, duration_ms + SILENCE_GRACE_MS)
        silence_baseline[duration_ms] = measure_genuine_hold(pico, u64, effective_ms, CALIBRATION_REPS)
    return hold_baseline, silence_baseline


def build_deep_list(host: str) -> int:
    """Create the RAM-disk directory the suite navigates, and return its size.

    The RAM disk is volatile, so nothing is written to flash or to attached
    storage.  Files that already exist are left alone, which makes a repeated
    run cheap.
    """
    with ftp_lib.session(host, timeout=FTP_TIMEOUT_SECONDS) as ftp:
        try:
            ftp.mkd("/Temp/" + DEEP_LIST_DIRECTORY)
        except ftplib.error_perm:
            pass
        ftp.cwd("/Temp/" + DEEP_LIST_DIRECTORY)
        existing = set(ftp.nlst())
        for index in range(DEEP_LIST_ENTRIES):
            name = f"row{index:0{ROW_NAME_WIDTH}d}.txt"
            if name not in existing:
                ftp.storbinary("STOR " + name, io.BytesIO(b"soak\n"))
        return len(ftp.nlst())


def remove_deep_list(host: str):
    with ftp_lib.session(host, timeout=FTP_TIMEOUT_SECONDS) as ftp:
        ftp.cwd("/Temp/" + DEEP_LIST_DIRECTORY)
        for name in ftp.nlst():
            ftp_lib.delete_quietly(ftp, name)
        ftp.cwd("/Temp")
        try:
            ftp.rmd(DEEP_LIST_DIRECTORY)
        except ftplib.error_perm:
            pass


def enter_deep_list(u64: U64):
    """Navigate the on-screen menu from wherever it is into the deep list.

    Return on a directory opens a context menu whose first item is `Enter`, so
    each level costs two Return presses.
    """
    # Reopening the menu restores whichever directory it was left in, so back
    # out until the root browser is on screen rather than pressing a fixed
    # number of times.
    for _attempt in range(12):
        if not u64.machine.menu_open():
            u64.machine.menu_button()
            time.sleep(.8)
        try:
            rows = u64.rows()
        except Failure:
            rows = []
        if rows and rows[0].startswith("Temp") and any(line.startswith("Flash") for line in rows):
            break
        # Cursor left leaves a directory.  run_stop closes the whole menu.
        u64.key("left_shift", "cursor_left_right", settle=.35)
    else:
        raise Failure("could not return to the root browser of the Ultimate menu")
    # The root browser is six entries deep, so ten Up presses reach the top from
    # any position without needing to know where the selection started.
    for _ in range(10):
        u64.key("left_shift", "cursor_up_down", settle=.12)
    _row, text = u64.screen()
    if not text.startswith("Temp"):
        raise Failure(f"expected the RAM disk at the top of the root browser, found {text!r}")
    u64.key("return", settle=.5)
    u64.key("return", settle=.9)
    # The RAM disk also holds whatever else is in /Temp, such as the cache
    # directory runner uploads create, so the list is found by name rather
    # than by position.
    seen = []
    for _ in range(RAM_DISK_SEARCH_LIMIT):
        _row, text = u64.screen()
        if text.startswith(DEEP_LIST_DIRECTORY):
            break
        seen.append(text)
        u64.key("cursor_up_down", settle=.15)
    else:
        raise Failure(f"expected {DEEP_LIST_DIRECTORY!r} inside the RAM disk, found {seen!r}")
    u64.key("return", settle=.5)
    u64.key("return", settle=.9)
    _row, text = u64.screen()
    if not text.startswith("row"):
        raise Failure(f"expected the generated list, found {text!r}")


def require_fixture(status: dict[str, Any]):
    if status.get("protocol_version") != PROTOCOL_VERSION or not status.get("hid_open"):
        raise Failure(f"Pico HID not enumerated/open: {status}")
    if status.get("idle_rate") != 25:
        raise Failure(f"Pico idle_rate is {status.get('idle_rate')!r}, expected 25 (U64 SET_IDLE/GET_IDLE path is absent)")
    if status.get("currently_held_keys"):
        raise Failure(f"Pico has keys held: {status}")


def navigate(pico: Pico, u64: U64, command: str, key: str, **kwargs) -> tuple[int, str]:
    pico.call(command, key=key, **kwargs)
    time.sleep(.18)
    return u64.stable_screen()


def preflight(pico: Pico, u64: U64) -> tuple[dict[int, int], dict[int, int]]:
    with check("Pico HID is enumerated and U64 negotiated SET_IDLE(25)"):
        require_fixture(pico.call("status"))
    with check("open the Ultimate menu on a list deep enough to hold a repeat"):
        u64.open_menu()
        entries = build_deep_list(u64.host)
        if entries < DEEP_LIST_ENTRIES:
            raise Failure(f"the RAM disk list holds {entries} entries, expected {DEEP_LIST_ENTRIES}")
        enter_deep_list(u64)
        u64.screen()
    with check("normal Down tap moves exactly one step and Up returns"):
        anchor_index = u64.stable_position()
        pico.call("tap", key="down", duration_ms=30)
        delta, trail = watch_bounded(u64, anchor_index, "down", 1)
        if delta != 1:
            raise Failure(f"normal Down tap moved {delta} steps, expected 1; trail={trail}")
        pico.call("tap", key="up", duration_ms=30)
        time.sleep(.2)
        if u64.stable_position() != anchor_index:
            raise Failure("Up did not return to the anchor")
    with check("genuine held Down repeats and stops on release"):
        start = u64.stable_position()
        pico.call("hold", key="down", duration_ms=760)
        time.sleep(.15)
        after = u64.stable_position()
        if after <= start + 1:
            raise Failure(f"held key did not make more than one movement: {start} -> {after}")
        stable = u64.stable_position()
        time.sleep(.4)
        if u64.stable_position() != stable:
            raise Failure("selection continued moving after hold release")
    with check("menu still responds to a single tap after a held key"):
        # A held Down can reach the end of the menu, where further Down taps do
        # not move the selection.  Hold Up for the same period to come back, then
        # prove the menu is still moving one row at a time before the fault cases
        # rely on that.
        recentre_if_needed(pico, u64)
        recovered = u64.stable_position()
        pico.call("tap", key="down", duration_ms=30)
        delta, trail = watch_bounded(u64, recovered, "down", 1)
        if delta != 1:
            raise Failure(f"menu did not step one row after the held key: trail={trail}")
        pico.call("tap", key="up", duration_ms=30)
        time.sleep(.2)
        if u64.stable_position() != recovered:
            raise Failure("Up did not return to the recovered position")
    for fault in FAULT_KINDS:
        with check(f"{fault} produces exactly one menu movement"):
            anchor_index = u64.stable_position()
            pico.call(fault, key="down", duration_ms=30, fault_duration_ms=750)
            delta, trail = watch_bounded(u64, anchor_index, "down", 1)
            if delta != 1:
                raise Failure(f"{fault}: expected exactly one movement from {anchor_index}, got delta={delta} "
                              f"trail={trail}. Check that the Pico is the only USB keyboard attached to the "
                              "Ultimate 64: the firmware disables the idle-rate repeat bound while a second "
                              "keyboard is present.")
            pico.call("tap", key="up", duration_ms=30)
            time.sleep(.2)
            require_fixture(pico.call("status"))
    with check("calibrate genuine repeat counts for mid-repeat holds"):
        baselines = calibrate_repeat_counts(pico, u64)
    return baselines


def run_fault_iteration(pico: Pico, u64: U64, randomizer: random.Random, iteration: int,
                         baselines: tuple[dict[int, int], dict[int, int]]) -> None:
    hold_baseline, silence_baseline = baselines
    key = randomizer.choice(("down", "up"))
    inverse = "up" if key == "down" else "down"
    fault = randomizer.choice(FAULT_KINDS)
    fault_duration = randomizer.choice(FAULT_DURATIONS_MS)
    mid_repeat = randomizer.random() < 0.5
    if mid_repeat:
        duration_ms = randomizer.choice(MID_REPEAT_HOLD_DURATIONS_MS)
        base = (silence_baseline if fault == "silence_after_press" else hold_baseline)[duration_ms]
        allowed_delta = base + 1  # the one accepted "costs one repeat decision" race
        require_min = 1 if base >= 1 else 0
    else:
        duration_ms = randomizer.choice(PRE_REPEAT_TAP_DURATIONS_MS)
        allowed_delta = 1
        require_min = 1
    label = (f"iteration {iteration}: key={key} {'mid-repeat' if mid_repeat else 'pre-repeat'} "
             f"hold={duration_ms}ms fault={fault} fault_dur={fault_duration}ms allowed<={allowed_delta}")
    with check(label):
        require_fixture(pico.call("status"))
        anchor_index = u64.stable_position()
        pico.call(fault, key=key, duration_ms=duration_ms, fault_duration_ms=fault_duration)
        delta, trail = watch_bounded(u64, anchor_index, key, allowed_delta)
        if delta < require_min:
            status = pico.call("status")
            raise Failure(f"{label}: expected at least {require_min} movement(s) from {anchor_index}, "
                          f"observed delta={delta}; trail={trail} Pico={status}")
        # Return toward the anchor with a genuine, un-faulted hold sized to the
        # observed movement. This both recentres the list and is a second,
        # independent confirmation that the inverse direction still moves one
        # row at a time.
        back_hold = min(2000, FIRST_REPEAT_DELAY_MS + max(0, delta - 1) * REPEAT_INTERVAL_MS + 150) if delta > 1 else 30
        back_command = "hold" if delta > 1 else "tap"
        pico.call(back_command, key=inverse, duration_ms=back_hold)
        time.sleep(.25)
        require_fixture(pico.call("status"))


def run_soak(pico: Pico, u64: U64, duration: float, seed: int,
             baselines: tuple[dict[int, int], dict[int, int]]):
    randomizer = random.Random(seed)
    deadline = time.monotonic() + duration
    iteration = 0
    while time.monotonic() < deadline:
        iteration += 1
        recentre_if_needed(pico, u64)
        run_fault_iteration(pico, u64, randomizer, iteration, baselines)
        if iteration % 5 == 0:
            # A genuine hold is the positive control: it proves the firmware has
            # not answered the fault cases by disabling auto-repeat altogether.
            with check(f"iteration {iteration}: genuine held key still repeats"):
                key = randomizer.choice(("down", "up"))
                inverse = "up" if key == "down" else "down"
                start = u64.stable_position()
                hold = randomizer.choice((500, 700, 900))
                pico.call("hold", key=key, duration_ms=hold)
                time.sleep(.25)
                after = u64.stable_position()
                if after == start:
                    raise Failure(f"iteration {iteration}: a genuine {hold}ms hold of {key} did not move from {start}")
                stable = u64.stable_position()
                time.sleep(.5)
                if u64.stable_position() != stable:
                    raise Failure(f"iteration {iteration}: the selection kept moving after the hold was released")
                # Come back the same way so the next iterations stay in the
                # middle of the list rather than sitting against its end.
                pico.call("hold", key=inverse, duration_ms=hold)
                time.sleep(.35)
                require_fixture(pico.call("status"))


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    cli.add_device_arguments(parser, password=None, colour=False)
    parser.add_argument("--pico-host", help="fixture IP address; required on networks that do not "
                        "forward broadcast between the wired test host and the Wi-Fi client")
    parser.add_argument("--profile", choices=DEFAULT_DURATION, default="stress")
    parser.add_argument("--duration", type=cli.parse_duration)
    parser.add_argument("--seed", type=int, default=797)
    parser.add_argument("--setup-pico", action="store_true")
    parser.add_argument("--wifi-ssid")
    args = parser.parse_args()
    if args.setup_pico:
        setup_pico(args.wifi_ssid, args.pico_host)
        print("\nThen run:\n\n./run-tests -H u64 --soak -s usb-keyboard-repeat")
        return 0
    host = args.pico_host or discover_pico(optional=True)
    if host is None:
        suite_skip("usb_keyboard_repeat_soak_test",
                   "no Pico HID fixture answers on this network; name one with --pico-host")
        return 0
    pico = Pico(host)
    u64 = U64(args.host, args.password, args.timeout)
    duration = args.duration or DEFAULT_DURATION[args.profile]
    detail(f"U64 hostname={args.host} Pico={host} profile={args.profile} duration={duration}s seed={args.seed}")
    try:
        info = u64.rest.json("/v1/version").get("version", "")
        detail("U64 version=" + str(info))
    except Exception as exc:
        raise Failure("U64 is not reachable: " + str(exc)) from exc
    try:
        baselines = preflight(pico, u64)
        run_soak(pico, u64, duration, args.seed, baselines)
    finally:
        try:
            pico.call("release_all")
            if pico.call("status").get("currently_held_keys"):
                raise Failure("fixture still reports held keys after release_all")
            for _ in range(3):
                u64.key("left_shift", "cursor_left_right", settle=.25)
            u64.close_menu()
        except Exception as cleanup:
            detail("cleanup failure: " + str(cleanup))
        try:
            remove_deep_list(u64.host)
        except Exception as cleanup:
            detail("cleanup failure removing the RAM disk list: " + str(cleanup))
        pico.close()
    suite_ok("usb_keyboard_repeat_soak_test", f"duration={duration}s")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Failure as exc:
        suite_fail("usb_keyboard_repeat_soak_test", format_exception(exc))
        raise SystemExit(1)
