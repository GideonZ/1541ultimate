#!/usr/bin/env python3
# E2E: a USB mouse arrives on control port 1 as a 1351 mouse with a Micromys wheel.

"""Move, click and scroll a mouse in every mouse mode, and read what the C64 saw.

The mouse comes from one of two backends, chosen with --backend. `rest` (the
default) sends `mouse` events to `POST /v1/machine:input`, which the firmware
handles exactly as a USB mouse's reports, and needs no hardware. `pico` uses
the Pico 2 W fixture (`tests/lib/pico_hid.py`) as a real USB mouse and
keyboard. Where a scenario holds a key, `rest` holds it over REST and `pico` on
the fixture's USB keyboard.

The C64 runs `tools/c64/mouse-listener.asm`, which follows control port 1 as a
1351 driver with Micromys wheel support does, port 2 as a joystick routine
does, and the keyboard as a scan does, and keeps all of it in RAM.
`tests/e2e/lib/mouse.py` drives both. The listener samples once per frame, as
a driver in a raster interrupt does, so "a step" below is the change of the
position between two frames.

The suite sets Mouse Sensitivity 8 with acceleration off unless a scenario
says otherwise, which makes one HID count one POT count, and puts every mouse
setting back when it ends. The expected numbers come from the firmware's own
scaling in `HidMouseInterpreter` (software/io/usb/hid_decoder.h); each check
names the function.

Scenarios, selectable with --test:

move
    Moves land exactly, in both directions and diagonally. Every later
    "position stays" check depends on this one.
motion-speed
    Movement as a hand makes it: slow and precise moves, flicks whose speed
    rises to a peak and falls again in all eight directions, a square and a
    circle that come back to where they started, and a flick straight back.
    Up to 31 counts per report every movement lands exactly and no step goes
    against it; faster flicks, which can put two reports into one frame, land
    within one 1351 wrap (128 counts). One report beyond the 63-count clamp,
    Mouse Sensitivity 1 and 16, and adaptive acceleration land where the
    firmware's scaling says.
pacing
    At 50Hz and 60Hz: the fastest movement a mouse reports, diagonally in all
    four directions, with motion and wheel in one report (pico), and while
    other REST calls load the machine. No frame sees a step against the
    movement or beyond 63 counts (MousePotPacer), most of the movement
    arrives, and the pointer stops soon after the mouse.
settings
    Every mouse setting: what each Mouse Mode does with motion and the wheel,
    Mouse Sensitivity 1 to 16 on the pointer and on the cursor keys, Mouse
    Wheel Sensitivity 1 to 16, and both wheel directions.
path
    REST `path` events: a drawn stroke lands where its steps add up and shows
    one step per frame, a path that turns comes back, interval_ms paces the
    steps, the longest path of 256 steps arrives, a press and release around a
    path make a drag, and two paths in one request run in order.
precision
    The slowest movement a mouse makes: reports of one and two counts. Every
    report has to reach the POT lines in its own frame, in the step the mouse
    made, so the pointer follows the hand without lagging or jumping.
flood
    Thousands of reports at the rate the POT lines carry, in both directions,
    and over REST also hundreds of separate requests and button taps: every
    count, press and release arrives, and no frame steps backward.
buttons
    All eight button states and all 64 transitions between them: what is held
    and how many presses were counted, without moving the position. With
    `rest`, a `tap` of each button is one press.
wheel-micromys
    Mouse + Wheel mode. Slow and fast vertical detents at wheel sensitivities
    1, 3 and 8, a burst beyond the firmware's 16-pulse queue, a reversal in
    the middle of a burst, the horizontal wheel (which Micromys cannot
    express), and wheel detents inside motion reports. The position never
    moves (issue #909).
wheel-count
    120 slow detents in alternating directions, counted exactly, to bound the
    rate at which a pulse could be lost.
wheel-mouse
    Mouse mode: both wheels move the pointer, slowly and fast, with Mouse Wheel
    Direction Normal and Reversed.
wheel-cursor
    Mouse + Cursor mode: both wheels type exactly the cursor keys the wheel
    sensitivity asks for, at sensitivities 1 and 2, and the pointer stays where
    it is.
cursor-mode
    Cursor mode: both wheels and the motion type exactly the expected cursor
    keys. Motion far faster than keys can be typed stops typing within 1.5
    seconds of the mouse and leaves no key stuck, and turning the other way
    drops the keys still waiting.
concurrent
    Buttons held while moving, wheel detents and buttons inside motion
    reports, a USB key held while moving and while the wheel pulses, and REST
    joystick and keyboard input on both ports while the USB mouse moves.
rest-joystick
    REST joystick input on port 1 leaves the mouse position alone (issue
    #909), and a REST fire2 press on port 1 still owns the POT lines while
    the mouse is attached; its release gives them back to the mouse.
menus
    Menu Mouse Navigation Disabled leaves the menu where it is. With a Telnet
    menu session open, the C64 still gets every button and wheel
    pulse and no cursor key: that menu has its own keyboard. With the menu open
    on the machine's own screen, the wheel moves its
    selection and the C64 gets neither buttons, pulses nor keys; that part is
    skipped when the menu freezes the C64 (Interface Type Freeze, or no HDMI
    display). Once the menu closes, the mouse reaches the C64 again.

With --backend pico the suite needs the fixture on a USB port of the machine.
"""

from __future__ import annotations

import argparse
import math
import os
import socket
import sys
import threading
import time
from collections.abc import Callable, Iterator
from contextlib import contextmanager
from itertools import pairwise
from pathlib import Path

# The one stanza that puts the shared library on sys.path; see tests/lib/bootstrap.py.
sys.path.insert(0, str(next(p for p in Path(__file__).resolve().parents
                            if (p / "tests" / "lib").is_dir()) / "tests" / "lib"))
import bootstrap  # noqa: E402,F401
import cli  # noqa: E402
import profiles  # noqa: E402
from api import UltimateApi  # noqa: E402
from mouse import MouseListener, MouseState, PicoMouse, RestMouse  # noqa: E402
from pico_hid import Pico, discover_pico  # noqa: E402
from report import Failure, check, check_skip, detail, format_exception, suite_fail, suite_ok, suite_skip  # noqa: E402

SUITE = "mouse_test"
CATEGORY = "U64 Specific Settings"
DEFAULTS = {
    "Mouse Mode": "Mouse + Wheel",
    "Mouse Sensitivity": 8,
    "Mouse Acceleration": "Off",
    "Mouse Wheel Sensitivity": 1,
    "Mouse Wheel Direction": "Normal",
    "Joystick Swapper": "Normal",
    "Menu Mouse Navigation": "Enabled",
}

TESTS = ("move", "motion-speed", "pacing", "precision", "path", "flood", "settings", "buttons",
         "wheel-micromys", "wheel-count", "wheel-mouse", "wheel-cursor", "cursor-mode", "concurrent",
         "rest-joystick", "menus")
# What each profile runs, on top of the profile before it. Smoke shows the mouse
# reaches the C64 at all; quick adds the mappings a user feels first; standard,
# the merge gate, adds every setting and the REST events; deep adds the long
# sweeps and the checks that need the menu or minutes of reports.
PROFILE_TESTS = {
    profiles.SMOKE: ("move", "buttons"),
    profiles.QUICK: ("precision", "cursor-mode", "wheel-micromys"),
    profiles.STANDARD: ("pacing", "path", "settings", "wheel-mouse", "wheel-cursor", "rest-joystick"),
    profiles.DEEP: ("motion-speed", "flood", "wheel-count", "concurrent", "menus"),
    profiles.EXHAUSTIVE: (),
}


def deep_run() -> bool:
    """Whether this run sweeps the cases a shorter profile only samples."""
    return profiles.rank(profiles.current()) >= profiles.rank(profiles.DEEP)


def tests_for(profile: str) -> tuple[str, ...]:
    """The scenarios `profile` runs, in the order TESTS declares them."""
    wanted: set[str] = set()
    for name in profiles.ORDER:
        wanted |= set(PROFILE_TESTS[name])
        if name == profile:
            break
    return tuple(test for test in TESTS if test in wanted)

# The largest change a report may make to the position (`clampDelta(..., 63)`
# after the sensitivity scale), and so the largest step a monotonic movement
# may show between two frames without a 1351 driver reading it backwards.
REPORT_CLAMP = 63
# HID reports AC Pan positive for a tilt or scroll to the right. The firmware
# negates it (`normalizeHorizontalWheel`) for every mouse except the compact
# shared-wheel layout, so a positive pan moves the pointer and the cursor left.
PAN_POSITIVE_SIGN = -1
# The firmware's queue of pending Micromys pulses (USB_HID_MOUSE_WHEEL_BURST_LIMIT).
WHEEL_BURST_LIMIT = 16

ALL_BUTTONS = ("left", "middle", "right")


def require(condition: bool, message: str, state: MouseState) -> None:
    if not condition:
        raise Failure(f"{message}: {state}")


# What configure() last put in force, so a check writes only what it changes.
IN_FORCE: dict[str, object] = {}


def configure(api: UltimateApi, **settings: object) -> None:
    """Apply DEFAULTS, then `settings` given with underscores for spaces."""
    wanted = dict(DEFAULTS)
    for name, value in settings.items():
        wanted[name.replace("_", " ")] = value
    for item, value in wanted.items():
        if IN_FORCE.get(item) != value:
            api.configs.set(CATEGORY, item, value)
            IN_FORCE[item] = value


# The 7-bit value both POT lines are parked on before a check.
PARKED_POT = 24


def park(listener: MouseListener, mouse: PicoMouse | RestMouse) -> None:
    """Move the mouse so both POT lines read PARKED_POT in their low 7 bits.

    A 1351 driver reads only those 7 bits, where the released joystick value
    $80 reads as 0. A check that started from a position of 0 could not see the
    firmware write $80 over the mouse position (issue #909). A pointer count is
    two POT counts, so the move is half the distance on the lines.
    """
    state = listener.state()
    # POTX follows the mouse's x; POTY moves against it.
    dx = (PARKED_POT - (state.potx & 0x7F)) % 128
    dy = ((state.poty & 0x7F) - PARKED_POT) % 128
    if dx or dy:
        mouse.move(dx // 2, dy // 2)


# Whether the POT lines have been seen following the mouse since the listener
# started. The two moves that show it cost a check's worth of time, so they run
# once, not before every check.
FOLLOWS = {"seen": False}


@contextmanager
def fresh(listener: MouseListener, mouse: PicoMouse | RestMouse, parked: bool = True,
          prime: bool = False) -> Iterator[None]:
    """Start a check from a quiet listener with its counters at zero.

    The origin is a position the mouse itself set, parked away from the
    released joystick value (see park). `parked=False` leaves the position
    alone, for Cursor mode, where motion types keys instead, and for a caller
    that has parked the mouse itself. `prime` sends a report each way first,
    for a check whose expectation depends on the reports before it.
    """
    if parked:
        park(listener, mouse)
    if prime or not FOLLOWS["seen"]:
        mouse.move(1, 1)
        mouse.move(-1, -1)
        state = listener.quiet(hold=0.1)
        if parked and (not (state.potx & 0x7F) or not (state.poty & 0x7F)):
            raise Failure(f"the POT lines do not follow the mouse, or sit where $80 would not show: {state}")
        FOLLOWS["seen"] = True
    listener.reset()
    yield


def require_still(state: MouseState, what: str) -> None:
    require((state.x, state.y) == (0, 0), f"{what} moved the position", state)
    require(state.step_x == (0, 0) and state.step_y == (0, 0), f"{what} changed the POT lines", state)


def require_no_pulses(state: MouseState, what: str) -> None:
    require((state.wheel_up, state.wheel_down) == (0, 0), f"{what} pulsed the wheel lines", state)


def require_no_cursor_keys(state: MouseState, what: str) -> None:
    require(not any(state.cursor.values()), f"{what} typed cursor keys", state)


def require_monotonic(state: MouseState, dx: int, dy: int) -> None:
    """No step against the movement, and none a 1351 driver could read backwards."""
    for axis, direction, (low, high) in (("x", dx, state.step_x), ("y", dy, state.step_y)):
        if direction > 0:
            require(low >= 0 and high <= REPORT_CLAMP, f"{axis} stepped outside 0..{REPORT_CLAMP}", state)
        elif direction < 0:
            require(high <= 0 and low >= -REPORT_CLAMP, f"{axis} stepped outside -{REPORT_CLAMP}..0", state)
        else:
            require((low, high) == (0, 0), f"{axis} moved while the movement had no {axis} part", state)


def clamp(value: int) -> int:
    return max(-REPORT_CLAMP, min(REPORT_CLAMP, value))


# --------------------------------------------------------------------- move --

def test_move(api, listener, mouse) -> None:
    configure(api)
    for moves, expected in (([(30, 20)], (30, 20)), ([(-80, -65)], (-80, -65)),
                            ([(63, -63), (-20, 40)], (43, -23))):
        with check(f"moves {moves} land at {expected}"), fresh(listener, mouse):
            for dx, dy in moves:
                mouse.move(dx, dy)
            state = listener.quiet()
            detail(str(state))
            require((state.x, state.y) == expected, f"expected position {expected}", state)


# ------------------------------------------------------------- motion-speed --

# A hand moving the mouse: the speed rises to a peak and falls again. Each
# factor of the peak is one step of two reports at the host's 20ms poll rate.
FLICK_SHAPE = (0.2, 0.6, 1.0, 0.6, 0.2)
# The USB mouse is polled every 20ms, and now and then the firmware handles two
# reports inside one frame. Up to STEADY_PEAK counts per report, two of them
# stay within the 31 counts a 1351 driver reads the right way round, so every
# step and the landing are checked exactly. Faster flicks may lose one 1351
# wrap (64 counts) now and then, as a real 1351 moved that fast would, so they
# are checked to land within that.
STEADY_PEAK = 15
ONE_WRAP = 64


def flick(dx: int, dy: int) -> list[tuple[int, int]]:
    """The steps of a flick whose fastest report moves by dx, dy counts."""
    return [(round(dx * f), round(dy * f)) for f in FLICK_SHAPE]


def movement_case(listener, mouse, label: str, steps: list[tuple[int, int]], reports_per_step: int = 1,
                  monotonic: bool = True, expected: tuple[int, int] | None = None, tolerance: int = 0) -> None:
    """Send `steps` as a hand would and require where they land, within `tolerance` counts.

    `expected` defaults to the sum of the steps, which is where they land at
    Mouse Sensitivity 8.
    """
    with check(label), fresh(listener, mouse):
        mouse.path(steps, reports_per_step)
        state = listener.quiet()
        if expected is None:
            expected = (sum(x for x, _ in steps) * reports_per_step, sum(y for _, y in steps) * reports_per_step)
        detail(str(state))
        require(abs(state.x - expected[0]) <= tolerance and abs(state.y - expected[1]) <= tolerance,
                f"expected position {expected}" + (f" within {tolerance} counts" if tolerance else ""), state)
        if monotonic:
            require_monotonic(state, sum(x for x, _ in steps), sum(y for _, y in steps))


def _divide(value: int, divisor: int) -> int:
    """C integer division, which rounds toward zero."""
    quotient = abs(value) // abs(divisor)
    return quotient if (value >= 0) == (divisor > 0) else -quotient


def adaptive_acceleration_total(dx: int, dy: int, count: int) -> tuple[int, int]:
    """Where `count` reports of dx, dy land with Mouse Acceleration Adaptive.

    Follows usb_hid_apply_pointer_acceleration and HidMouseInterpreter's
    updateMotionEma, computeAdaptiveAccelerationScale and limitScaleStep,
    starting from the history that fresh()'s two one-count moves leave after a
    report with acceleration off has cleared it.
    """
    ema = 0
    scale = 384
    x = y = 0
    for index, (step_x, step_y) in enumerate([(1, 1), (-1, -1)] + [(dx, dy)] * count):
        ema += _divide(((abs(step_x) + abs(step_y)) << 4) - ema, 4)
        target = 384
        if ema >= 96 << 4:
            target = 576
        elif ema > 32 << 4:
            excess, span = ema - (32 << 4), (96 << 4) - (32 << 4)
            target = 384 + _divide(192 * excess * excess, span * span)
        scale = max(scale - 32, min(scale + 32, target))
        if index >= 2:
            x += clamp(_divide(step_x * scale, 256))
            y += clamp(_divide(step_y * scale, 256))
    return x, y


def test_motion_speed(api, listener, mouse) -> None:
    configure(api, Mouse_Mode="Mouse")
    movement_case(listener, mouse, "slow and precise: 1 count per report", [(1, 0)] * 30, 3)
    movement_case(listener, mouse, "slow and precise the other way, diagonally", [(-1, 1)] * 30, 3)
    for peak in (8, STEADY_PEAK, 24, 31):
        steady = peak <= STEADY_PEAK
        tolerance = 0 if steady else ONE_WRAP
        for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1), (1, 1), (-1, 1), (1, -1), (-1, -1)):
            if not steady and dx and dy:
                continue
            outcome = "lands exactly, every step forward" if steady else "lands within one wrap"
            movement_case(listener, mouse, f"a flick peaking at {peak} counts per report towards {dx},{dy}: {outcome}",
                          flick(dx * peak, dy * peak), monotonic=steady, tolerance=tolerance)
    movement_case(listener, mouse, "a square at 12 counts per report comes back to where it started",
                  [(12, 0)] * 4 + [(0, 12)] * 4 + [(-12, 0)] * 4 + [(0, -12)] * 4, monotonic=False)
    circle = [(round(20 * math.cos(math.tau * i / 16)), round(20 * math.sin(math.tau * i / 16))) for i in range(16)]
    movement_case(listener, mouse, "a circle at 20 counts per report comes back to where it started",
                  circle, monotonic=False)
    movement_case(listener, mouse, "a flick straight back comes back to where it started",
                  flick(STEADY_PEAK, 0) + flick(-STEADY_PEAK, 0), monotonic=False)
    with check("one report beyond the clamp moves by 63 counts (clampDelta)"), fresh(listener, mouse):
        # move() splits large moves; a stream of one sends the report as it is.
        mouse.stream(dx=127, count=1)
        mouse.stream(dy=-127, count=1)
        state = listener.quiet()
        detail(str(state))
        require((state.x, state.y) == (REPORT_CLAMP, -REPORT_CLAMP), "expected position (63, -63)", state)

    # scaleFixedWithRemainder: sensitivity 1 is 32/256 of a count per count,
    # with the remainder carried, so 160 single counts are exactly 20.
    configure(api, Mouse_Mode="Mouse", Mouse_Sensitivity=1)
    movement_case(listener, mouse, "sensitivity 1 carries fractions: 160 x 1 count is 20", [(1, 0)] * 80,
                  reports_per_step=2, expected=(20, 0))
    # Sensitivity 16 doubles, and the clamp still applies after it.
    configure(api, Mouse_Mode="Mouse", Mouse_Sensitivity=16)
    with check("sensitivity 16 doubles, and clamps one report of 40 counts to 63"), fresh(listener, mouse):
        mouse.path([(10, 0)] * 10, 2)
        mouse.move(0, 40)
        state = listener.quiet()
        detail(str(state))
        require((state.x, state.y) == (400, REPORT_CLAMP), "expected position (400, 63)", state)
    # Adaptive acceleration scales by 384/256 to 576/256 depending on recent
    # speed, before the clamp; adaptive_acceleration_total follows it.
    # The faster case sends its reports 40ms apart, so no two share a frame.
    for dx, dy, count, interval_ms in ((10, 10, 40, 0), (30, 30, 20, 40)):
        configure(api, Mouse_Mode="Mouse")
        # A report with acceleration off clears the speed history; park there too,
        # so no report but fresh()'s two moves comes before the check.
        mouse.move(1, 1)
        park(listener, mouse)
        configure(api, Mouse_Mode="Mouse", Mouse_Acceleration="Adaptive")
        expected = adaptive_acceleration_total(dx, dy, count)
        with check(f"adaptive acceleration: {count} x {dx},{dy} counts lands at {expected}"), \
                fresh(listener, mouse, parked=False, prime=True):
            mouse.stream(dx=dx, dy=dy, count=count, interval_ms=interval_ms)
            state = listener.quiet()
            detail(str(state))
            require((state.x, state.y) == expected, f"expected position {expected}", state)
    configure(api, Mouse_Mode="Mouse")


# ------------------------------------------------------------------- pacing --

FRAME_RATES = {"PAL": 50, "NTSC": 60}
# MousePotPacer shows what waits in changes of 63 POT counts, 24ms apart on 5ms
# timer ticks, plus a margin.
PACER_CATCH_UP_SECONDS = 0.25
# The port carries about 31 counts per 24ms, and a mouse at full speed reports
# 63 every 20ms, so about two fifths of that arrives.
FAST_ARRIVES = 0.35
FAST_REPORTS = 60


def set_system_mode(api, listener: MouseListener, mouse: PicoMouse | RestMouse, mode: str) -> None:
    """Switch the video timing and start the listener again on the new frames."""
    api.configs.set(CATEGORY, "System Mode", mode)
    listener.start()
    FOLLOWS["seen"] = False
    mouse.move(1, 1)
    mouse.move(-1, -1)


def fast_case(listener, mouse, label: str, dx: int, dy: int, frame_rate: int, sent: tuple[int, int],
              wheel: int = 0, pan: int = 0, load: Callable[[], None] | None = None) -> None:
    """FAST_REPORTS reports as fast as the host polls, moving by `sent` in all.

    No frame may see a step against the movement or beyond 63 counts, the
    listener must have seen nearly every frame, at least FAST_ARRIVES of 63
    counts per report must arrive and no more than was sent, and the pointer
    must have stopped PACER_CATCH_UP_SECONDS after the mouse.
    """
    with check(label), fresh(listener, mouse):
        stop = threading.Event()
        loader = Background(lambda: _load_until(stop, load)) if load else None
        started = time.monotonic()
        try:
            mouse.stream(dx=dx, dy=dy, count=FAST_REPORTS, wheel=wheel, pan=pan)
        finally:
            stop.set()
            if loader:
                loader.join()
        # No read until the lines have caught up: a read stops the 6510, and a
        # frame the listener misses joins two frames of movement into one step,
        # which looks like a wrap the firmware never made.
        time.sleep(PACER_CATCH_UP_SECONDS)
        first_read = time.monotonic()
        after = listener.state()
        state = listener.quiet()
        detail(str(state))
        frames = (first_read - started) * frame_rate
        require(after.frames >= 0.9 * frames, f"the listener sampled fewer than 90% of {frames:.0f} frames", after)
        require((state.x, state.y) == (after.x, after.y),
                f"the pointer still moved {PACER_CATCH_UP_SECONDS}s after the mouse stopped", state)
        for axis, landed, total in (("x", state.x, sent[0]), ("y", state.y, sent[1])):
            showable = max(-REPORT_CLAMP * FAST_REPORTS, min(REPORT_CLAMP * FAST_REPORTS, total))
            low, high = sorted((int(showable * FAST_ARRIVES), total))
            require(low <= landed <= high, f"expected {axis} in {low}..{high}", state)
        require_monotonic(state, dx, dy)


def _load_until(stop: threading.Event, load: Callable[[], None]) -> None:
    while not stop.is_set():
        load()


def pacing_at_frame_rate(api, listener, mouse, mode: str) -> None:
    rate = FRAME_RATES[mode]
    set_system_mode(api, listener, mouse, mode)
    configure(api, Mouse_Mode="Mouse")
    at = f"{rate}Hz: "
    # One diagonal covers both axes and both signs; the pacer is per axis.
    for dx, dy in ((REPORT_CLAMP, -REPORT_CLAMP), (-REPORT_CLAMP, REPORT_CLAMP)):
        fast_case(listener, mouse, at + f"{dx},{dy} as fast as the host polls never moves backward",
                  dx, dy, rate, (dx * FAST_REPORTS, dy * FAST_REPORTS))
    # Reads that do not stop the 6510 (no DMA), so the listener sees every frame.
    fast_case(listener, mouse, at + "the same under other REST calls never moves backward",
              REPORT_CLAMP, -REPORT_CLAMP, rate, (REPORT_CLAMP * FAST_REPORTS, -REPORT_CLAMP * FAST_REPORTS),
              load=lambda: api.configs.item(CATEGORY, "Mouse Mode"))
    if isinstance(mouse, PicoMouse):
        # Motion and wheel the same way in one report move more than 63 counts:
        # at wheel sensitivity 16, scaleVerticalWheelAxisDelta makes a vertical
        # value of 1 into 40 counts. REST cannot put both in one report.
        configure(api, Mouse_Mode="Mouse", Mouse_Wheel_Sensitivity=16)
        fast_case(listener, mouse, at + "motion and wheel in one report, 103 counts up, never move backward",
                  0, -REPORT_CLAMP, rate, (0, -(REPORT_CLAMP + 40) * FAST_REPORTS), wheel=1)
    configure(api, Mouse_Mode="Mouse")


def test_pacing(api, listener, mouse) -> None:
    original = api.configs.item(CATEGORY, "System Mode").get("current")
    # Both frame rates need the machine switched and settled twice, so below the
    # deep profile the check runs at the rate the machine is already in.
    if profiles.rank(profiles.current()) < profiles.rank(profiles.DEEP):
        mode = original if original in FRAME_RATES else "PAL"
        pacing_at_frame_rate(api, listener, mouse, mode)
        return
    try:
        for mode in FRAME_RATES:
            pacing_at_frame_rate(api, listener, mouse, mode)
    finally:
        if original is not None:
            set_system_mode(api, listener, mouse, original)


# ---------------------------------------------------------------- precision --

# Reports of one or two counts at the 20ms poll rate, which every frame can
# show. Nearly all of them have to move a frame of their own: a pointer that
# holds movement back for a window shows it in fewer, larger steps, which is
# both jagged and late. Showing every report as it arrives moves about 99% of
# the frames; holding each one for a 24ms window moves about 80%.
PRECISE_REPORTS = 120
PRECISE_FRAMES = 0.9


def stream_case(listener, mouse, label: str, dx: int, dy: int, count: int, moved_frames: float = 0) -> None:
    """`count` identical reports: where they land, and how many frames moved.

    `moved_frames` is how many of the reports have to move a frame of their own,
    which is what tells smooth movement from movement shown in fewer, larger
    steps. Without it only the landing and the direction of each step are checked.
    """
    with check(label), fresh(listener, mouse):
        mouse.stream(dx=dx, dy=dy, count=count)
        time.sleep(PACER_CATCH_UP_SECONDS)
        state = listener.quiet()
        detail(str(state))
        expected = (dx * count, dy * count)
        require((state.x, state.y) == expected, f"expected position {expected}", state)
        if moved_frames:
            step = max(abs(dx), abs(dy))
            require(state.step_x[1] - state.step_x[0] <= 2 * step and state.step_y[1] - state.step_y[0] <= 2 * step,
                    f"a frame moved by more than two reports of {step}", state)
            require(state.moved >= moved_frames * count,
                    f"only {state.moved} frames moved, expected at least {moved_frames * count:.0f} of {count}",
                    state)
        require_monotonic(state, dx, dy)


def test_precision(api, listener, mouse) -> None:
    configure(api, Mouse_Mode="Mouse")
    # One count each way and two counts diagonally: both axes, both signs.
    for dx, dy in ((1, -1), (-2, 2)):
        stream_case(listener, mouse, f"{PRECISE_REPORTS} reports of {dx},{dy} move a frame each",
                    dx, dy, PRECISE_REPORTS, moved_frames=PRECISE_FRAMES)


# --------------------------------------------------------------------- path --

# INPUT_API_MAX_MOUSE_PATH_STEPS, the longest path one request may hold.
PATH_MAX_STEPS = 256
# Long enough that the steps are plainly paced, short enough to stay quick.
PATH_SLOW_INTERVAL_MS = 100
PATH_SLOW_STEPS = 12
# A drawn stroke: the speed rises and falls, and the direction turns.
PATH_STROKE = [(3, 1), (5, 2), (8, 3), (11, 2), (8, -1), (5, -3), (3, -2)]


def send_path(api, steps, interval_ms: int = 20, before=(), after=()) -> None:
    """One request: `before`, the path, then `after`."""
    events = [*before, {"kind": "mouse", "path": [list(step) for step in steps], "interval_ms": interval_ms}, *after]
    api.machine.send_input(events)


def path_sum(steps) -> tuple[int, int]:
    return sum(x for x, _ in steps), sum(y for _, y in steps)


def test_path(api, listener, mouse) -> None:
    if not isinstance(mouse, RestMouse):
        with check("`path` is a REST event"):
            check_skip("run this suite with --backend rest to check paths")
        return
    configure(api, Mouse_Mode="Mouse")

    with check("a drawn stroke lands where its steps add up, one step per frame"), fresh(listener, mouse):
        steps = PATH_STROKE * 5
        send_path(api, steps)
        mouse.wait_sent()
        state = listener.quiet()
        detail(str(state))
        require((state.x, state.y) == path_sum(steps), f"expected position {path_sum(steps)}", state)
        require(state.moved >= 0.9 * len(steps), f"only {state.moved} frames moved of {len(steps)} steps", state)

    with check("a path that turns back comes back to where it started"), fresh(listener, mouse):
        steps = PATH_STROKE + [(-x, -y) for x, y in reversed(PATH_STROKE)]
        send_path(api, steps)
        mouse.wait_sent()
        state = listener.quiet()
        detail(str(state))
        require((state.x, state.y) == (0, 0), "the path did not come back", state)

    with check(f"interval_ms {PATH_SLOW_INTERVAL_MS} paces the steps"), fresh(listener, mouse):
        started = time.monotonic()
        send_path(api, [(2, 0)] * PATH_SLOW_STEPS, interval_ms=PATH_SLOW_INTERVAL_MS)
        mouse.wait_sent()
        elapsed = time.monotonic() - started
        state = listener.quiet()
        detail(f"{PATH_SLOW_STEPS} steps took {elapsed:.2f}s: {state}")
        require((state.x, state.y) == (2 * PATH_SLOW_STEPS, 0), "the steps did not all arrive", state)
        expected = (PATH_SLOW_STEPS - 1) * PATH_SLOW_INTERVAL_MS / 1000.0
        require(expected <= elapsed <= expected + 1.0, f"expected about {expected:.1f}s, not {elapsed:.2f}s", state)

    if deep_run():
        with check(f"the longest path, {PATH_MAX_STEPS} steps in one request"), fresh(listener, mouse):
            send_path(api, [(1, 0)] * PATH_MAX_STEPS)
            queued = mouse.pending()
            mouse.wait_sent()
            state = listener.quiet()
            detail(f"{queued} reports queued: {state}")
            require((state.x, state.y) == (PATH_MAX_STEPS, 0), f"expected position ({PATH_MAX_STEPS}, 0)", state)

    with check("a press, a path and a release in one request is a drag"), fresh(listener, mouse):
        steps = [(4, 0)] * PATH_SLOW_STEPS
        send_path(api, steps, interval_ms=PATH_SLOW_INTERVAL_MS,
                  before=[{"kind": "mouse", "inputs": ["left"], "transition": "press"}],
                  after=[{"kind": "mouse", "inputs": ["left"], "transition": "release"}])
        during = listener.wait_until(lambda s: s.held == {"left"} and s.x > 0)
        mouse.wait_sent()
        state = listener.quiet()
        detail(f"held while moving: {during}")
        detail(str(state))
        require((state.x, state.y) == path_sum(steps), f"expected position {path_sum(steps)}", state)
        require(state.presses["left"] == 1, "expected one left press", state)
        require(not state.held, "the button is still held", state)

    with check("two paths in one request run one after the other"), fresh(listener, mouse):
        there = [(6, 0)] * 10
        back = [(0, 6)] * 10
        api.machine.send_input([
            {"kind": "mouse", "path": [list(step) for step in there], "interval_ms": 20},
            {"kind": "mouse", "path": [list(step) for step in back], "interval_ms": 20},
        ])
        mouse.wait_sent()
        state = listener.quiet()
        detail(str(state))
        require((state.x, state.y) == (60, 60), "expected position (60, 60)", state)
        require(state.step_x[0] >= 0 and state.step_y[0] >= 0, "a step went against the paths", state)


# -------------------------------------------------------------------- flood --

# 6s of reports at the 20ms poll rate. Each moves by FLOOD_STEP, which is
# within the 31 counts per 24ms the POT lines carry, so nothing may be dropped.
FLOOD_REPORTS = 300
FLOOD_STEP = 15
# Separate requests, each one report, sent as fast as the host can post them.
FLOOD_REQUESTS = 80
FLOOD_REQUEST_STEP = 20
FLOOD_TAPS = 40


def test_flood(api, listener, mouse) -> None:
    configure(api, Mouse_Mode="Mouse")
    stream_case(listener, mouse, f"{FLOOD_REPORTS} reports of {FLOOD_STEP},-{FLOOD_STEP} land exactly",
                FLOOD_STEP, -FLOOD_STEP, FLOOD_REPORTS)

    if not isinstance(mouse, RestMouse):
        return
    with check(f"{FLOOD_REQUESTS} separate requests of one report each land exactly"), fresh(listener, mouse):
        for _ in range(FLOOD_REQUESTS):
            api.machine.send_input([{"kind": "mouse", "move": {"x": FLOOD_REQUEST_STEP}}])
        mouse.wait_sent()
        state = listener.quiet()
        detail(str(state))
        expected = FLOOD_REQUESTS * FLOOD_REQUEST_STEP
        require((state.x, state.y) == (expected, 0), f"expected position ({expected}, 0)", state)
        require_monotonic(state, FLOOD_REQUEST_STEP, 0)

    with check(f"{FLOOD_TAPS} taps in a row are {FLOOD_TAPS} presses and end released"), fresh(listener, mouse):
        for _ in range(FLOOD_TAPS):
            api.machine.send_input([{"kind": "mouse", "inputs": ["left"], "transition": "tap"}])
        mouse.wait_sent()
        state = listener.quiet()
        detail(str(state))
        require(state.presses["left"] == FLOOD_TAPS, f"expected {FLOOD_TAPS} left presses", state)
        require(not state.held, "a button is still held", state)
        require_still(state, "the taps")


# ----------------------------------------------------------------- settings --

# HidMouseInterpreter: the pointer moves counts * sensitivity / 8, a cursor key
# is CURSOR_KEY_COUNTS of that movement, and a detent is 8 wheel units.
CURSOR_KEY_COUNTS = 4
# What each Mouse Mode does with motion and with a wheel detent.
MODE_EFFECTS = {
    "Mouse": ("pointer", "pointer"),
    "Mouse + Wheel": ("pointer", "pulses"),
    "Mouse + Cursor": ("pointer", "keys"),
    "Cursor": ("keys", "keys"),
}


def pointer_counts(counts: int, sensitivity: int) -> int:
    """Where `counts` of motion land at `sensitivity`, with the remainder carried."""
    return counts * sensitivity // 8


def require_effect(state: MouseState, effect: str, what: str, pointer: int | None) -> None:
    """`what` drove `effect` and nothing else: the pointer, wheel pulses or cursor keys."""
    detail(f"{what}: {state}")
    if effect == "pointer":
        require(state.x == pointer if pointer is not None else (state.x, state.y) != (0, 0),
                f"{what} did not move the pointer", state)
    else:
        require_still(state, what)
    if effect == "keys":
        require(any(state.cursor.values()), f"{what} typed no cursor key", state)
    else:
        require_no_cursor_keys(state, what)
    if effect == "pulses":
        require(state.wheel_down == 5, "expected 5 down pulses", state)
    else:
        require_no_pulses(state, what)


def mode_case(api, listener, mouse, mode: str) -> None:
    """In `mode`, move 40 counts and turn 5 detents; each must drive only what it maps to."""
    motion, wheel = MODE_EFFECTS[mode]
    configure(api, Mouse_Mode=mode, Mouse_Wheel_Sensitivity=1)
    with check(f"{mode}: motion drives the {motion}, the wheel the {wheel}"), \
            fresh(listener, mouse, parked=mode != "Cursor"):
        mouse.path([(8, 0)] * 5, 1)
        require_effect(listener.quiet(), motion, "motion", 40)
        listener.reset()
        # As fast as the host polls: five detents fit the Micromys queue, and
        # the slow gaps are what the wheel scenarios are for.
        mouse.wheel(vertical=-5, gap_ms=0)
        require_effect(listener.quiet(), wheel, "the wheel", None)


def test_settings(api, listener, mouse) -> None:
    for mode in MODE_EFFECTS:
        mode_case(api, listener, mouse, mode)

    for sensitivity in ((1, 2, 4, 8, 16) if deep_run() else (1, 8, 16)):
        configure(api, Mouse_Mode="Mouse", Mouse_Sensitivity=sensitivity)
        expected = pointer_counts(80, sensitivity)
        # Below 8 a count is a fraction of a pointer count, and the moves that
        # start the check can leave part of one behind.
        movement_case(listener, mouse, f"Mouse Sensitivity {sensitivity}: 80 counts land at {expected}",
                      [(8, 0)] * 10, reports_per_step=1, expected=(expected, 0), monotonic=False,
                      tolerance=0 if sensitivity >= 8 else 1)

    # The setting that makes single cursor steps possible: at 1 a key needs 32
    # counts of hand movement, at 8 one every 4. The counts sent land between
    # two keys, so the fraction the earlier moves leave cannot change the total.
    for sensitivity, counts in ((1, 34), (8, 10)):
        configure(api, Mouse_Mode="Cursor", Mouse_Sensitivity=sensitivity)
        keys = pointer_counts(counts, sensitivity) // CURSOR_KEY_COUNTS
        with check(f"Cursor, Mouse Sensitivity {sensitivity}: {counts} counts type {keys} cursor keys"), \
                fresh(listener, mouse, parked=False):
            mouse.path([(1, 0)] * counts, 1)
            state = listener.quiet()
            detail(str(state))
            expected = dict.fromkeys(("up", "down", "left", "right"), 0)
            expected["right"] = keys
            require_cursor_keys(state, expected)
            require_still(state, "motion in Cursor mode")

    for sensitivity in ((1, 2, 8, 16) if deep_run() else (1, 16)):
        configure(api, Mouse_Mode="Mouse + Wheel", Mouse_Wheel_Sensitivity=sensitivity)
        with check(f"Mouse Wheel Sensitivity {sensitivity}: a detent is {sensitivity} pulses"), \
                fresh(listener, mouse):
            mouse.wheel(vertical=1, pulses_per_detent=sensitivity)
            state = listener.quiet()
            detail(str(state))
            require((state.wheel_up, state.wheel_down) == (sensitivity, 0),
                    f"expected {sensitivity} up pulses", state)
            require_still(state, "the wheel")

    for direction, first in (("Normal", "up"), ("Reversed", "down")):
        configure(api, Mouse_Mode="Mouse + Cursor", Mouse_Wheel_Sensitivity=1, Mouse_Wheel_Direction=direction)
        with check(f"Mouse Wheel Direction {direction}: a detent away from the user types {first}"), \
                fresh(listener, mouse):
            mouse.wheel(vertical=1)
            state = listener.quiet()
            detail(str(state))
            expected = dict.fromkeys(("up", "down", "left", "right"), 0)
            expected[first] = 2
            require_cursor_keys(state, expected)
    configure(api)


# ------------------------------------------------------------------ buttons --

def test_buttons(api, listener, mouse) -> None:
    configure(api)
    masks = [tuple(name for bit, name in enumerate(ALL_BUTTONS) if code & (1 << bit)) for code in range(8)]
    # Every state, and in a deep run every way of reaching one from another.
    states = [(first, second) for first in masks for second in masks] if deep_run() else list(pairwise(masks))
    label = "all 8 button states and all 64 transitions between them" if deep_run() else "all 8 button states"
    with check(label), fresh(listener, mouse):
        expected = dict.fromkeys(ALL_BUTTONS, 0)
        held: tuple[str, ...] = ()
        for first, second in states:
            for target in (first, second):
                for name in target:
                    if name not in held:
                        expected[name] += 1
                mouse.buttons(*target)
                held = target
                listener.wait_until(lambda s, target=target: s.held == set(target))
        mouse.buttons()
        state = listener.quiet()
        detail(str(state))
        require(state.presses == expected, f"expected presses {expected}", state)
        require_still(state, "the buttons")
        require_no_pulses(state, "the buttons")

    if isinstance(mouse, RestMouse):
        # `transition: tap` exists only over REST: the firmware holds the
        # buttons for two PAL frames, long enough for a read once per frame.
        with check("a REST tap of each button, and of all three, is one press each"), fresh(listener, mouse):
            expected = dict.fromkeys(ALL_BUTTONS, 0)
            for combination in (("left",), ("right",), ("middle",), ALL_BUTTONS):
                mouse.tap(*combination)
                for name in combination:
                    expected[name] += 1
            state = listener.quiet()
            detail(str(state))
            require(state.presses == expected, f"expected presses {expected}", state)
            require(not state.held, "a button is still held", state)
            require_still(state, "the taps")


# ----------------------------------------------------------- wheel-micromys --

def test_wheel_micromys(api, listener, mouse) -> None:
    # A slow detent waits out the pulse train it makes, so below deep one
    # sensitivity in the middle stands for the range the deep run sweeps.
    for sensitivity, up, down in (((1, 5, 4), (3, 2, 1), (8, 1, 1)) if deep_run() else ((3, 2, 1),)):
        configure(api, Mouse_Wheel_Sensitivity=sensitivity)
        with check(f"sensitivity {sensitivity}: {up} up and {down} down detents, slowly, "
                   f"give {sensitivity} pulses each and leave the position"), fresh(listener, mouse):
            mouse.wheel(vertical=up, pulses_per_detent=sensitivity)
            mouse.wheel(vertical=-down, pulses_per_detent=sensitivity)
            state = listener.quiet()
            detail(str(state))
            require((state.wheel_up, state.wheel_down) == (up * sensitivity, down * sensitivity),
                    f"expected {up * sensitivity} up and {down * sensitivity} down pulses", state)
            require_still(state, "the wheel")
            require_no_cursor_keys(state, "the wheel")

    configure(api)
    with check("10 detents as fast as the host polls queue and all pulse"), fresh(listener, mouse):
        mouse.wheel(vertical=10, gap_ms=0)
        state = listener.quiet()
        detail(str(state))
        require((state.wheel_up, state.wheel_down) == (10, 0), "expected all 10 pulses", state)
        require_still(state, "the wheel")

    if not deep_run():
        return
    with check(f"40 fast detents fill the {WHEEL_BURST_LIMIT}-pulse queue, then the wheel is normal again"), \
            fresh(listener, mouse):
        mouse.wheel(vertical=-40, gap_ms=0)
        burst = listener.quiet(timeout=20)
        detail(f"burst: {burst}")
        require(burst.wheel_up == 0 and WHEEL_BURST_LIMIT <= burst.wheel_down <= 40,
                f"expected between {WHEEL_BURST_LIMIT} and 40 down pulses and no up pulse", burst)
        require_still(burst, "the wheel")
        listener.reset()
        mouse.wheel(vertical=1)
        after = listener.quiet()
        detail(f"one slow detent after it: {after}")
        require((after.wheel_up, after.wheel_down) == (1, 0), "a pulse was left over from the burst", after)

    with check("a reversal inside a fast burst pulses the new direction"), fresh(listener, mouse):
        mouse.wheel(vertical=8, gap_ms=0)
        mouse.wheel(vertical=-3, gap_ms=0)
        state = listener.quiet()
        detail(str(state))
        # By design a new wheel delta is added to the last queued one when the
        # queue is full (usb_hid_publish_native_wheel_input), so the first down
        # detent can cancel an up detent still waiting there. That keeps the
        # wheel responsive when it turns the other way.
        require(2 <= state.wheel_down <= 3, "expected 2 or 3 down pulses after the reversal", state)
        require(1 <= state.wheel_up <= 8, "expected some, at most 8, up pulses before it", state)
        require_still(state, "the wheel")

    with check("the horizontal wheel, slow and fast, pulses nothing and leaves the position"), fresh(listener, mouse):
        mouse.wheel(horizontal=3)
        mouse.wheel(horizontal=-6, gap_ms=0)
        state = listener.quiet()
        detail(str(state))
        require_no_pulses(state, "the horizontal wheel")
        require_still(state, "the horizontal wheel")
        require_no_cursor_keys(state, "the horizontal wheel")

    with check("detents inside motion reports pulse and the motion lands exactly"), fresh(listener, mouse):
        mouse.stream(dx=7, dy=-4, wheel=1, count=10, interval_ms=150)
        state = listener.quiet()
        detail(str(state))
        require((state.x, state.y) == (70, -40), "expected position (70, -40)", state)
        require((state.wheel_up, state.wheel_down) == (10, 0), "expected 10 up pulses", state)
        require_monotonic(state, 7, -4)


def test_wheel_count(api, listener, mouse) -> None:
    configure(api)
    with check("60 slow detents in alternating directions are counted exactly"), fresh(listener, mouse):
        for _ in range(15):
            mouse.wheel(vertical=2)
            mouse.wheel(vertical=-2)
        state = listener.quiet()
        detail(str(state))
        require((state.wheel_up, state.wheel_down) == (30, 30), "expected 30 up and 30 down pulses", state)
        require_still(state, "the wheel")


# -------------------------------------------------------------- wheel-mouse --

def test_wheel_mouse(api, listener, mouse) -> None:
    # scaleVerticalWheelAxisDelta: a detent is 8 units, times the wheel
    # sensitivity, times 10/32; at 4 that is 10 counts, up for a detent away
    # from the user. scaleHorizontalWheelAxisDelta: one unit times sensitivity
    # times 2, so 8 counts, in the direction PAN_POSITIVE_SIGN gives.
    for direction, sign in (("Normal", 1), ("Reversed", -1)):
        configure(api, Mouse_Mode="Mouse", Mouse_Wheel_Sensitivity=4, Mouse_Wheel_Direction=direction)
        cases = [("slow vertical", 2, 0, 150, (0, -20 * sign)),
                 ("slow horizontal", 0, 2, 150, (16 * PAN_POSITIVE_SIGN * sign, 0)),
                 ("fast vertical", 12, 0, 0, (0, -120 * sign))]
        if deep_run():
            cases += [("slow vertical, other way", -3, 0, 150, (0, 30 * sign)),
                      ("slow horizontal, other way", 0, -3, 150, (-24 * PAN_POSITIVE_SIGN * sign, 0)),
                      ("fast horizontal", 0, -12, 0, (-96 * PAN_POSITIVE_SIGN * sign, 0))]
        for label, vertical, horizontal, gap_ms, expected in cases:
            with check(f"Mouse mode, {direction}: {label} moves the pointer to {expected}"), fresh(listener, mouse):
                mouse.wheel(vertical=vertical, horizontal=horizontal, gap_ms=gap_ms)
                state = listener.quiet()
                detail(str(state))
                require((state.x, state.y) == expected, f"expected position {expected}", state)
                require_no_pulses(state, "the wheel in Mouse mode")
                require_no_cursor_keys(state, "the wheel in Mouse mode")


# ------------------------------------------------------ wheel-cursor, cursor --

def cursor_expectation(vertical: int, horizontal: int, sensitivity: int) -> dict[str, int]:
    """Cursor keys for whole detents: scaleVerticalWheelKeys and scaleHorizontalWheelKeys."""
    keys = dict.fromkeys(("up", "down", "left", "right"), 0)
    if vertical:
        keys["up" if vertical > 0 else "down"] = abs(vertical) * 2 * sensitivity  # 8 units * s / 4
    if horizontal:
        pan_right = horizontal * PAN_POSITIVE_SIGN > 0
        keys["right" if pan_right else "left"] = abs(horizontal) * ((sensitivity * 3 + 1) // 2)
    return keys


def require_cursor_keys(state: MouseState, expected: dict[str, int]) -> None:
    """Exactly the expected cursor keys, each a press of its own, and none left down."""
    require(state.cursor == expected, f"expected cursor keys {expected}", state)
    require(state.keys_held == 0, "a key is still down", state)


def wheel_cursor_cases(api, listener, mouse, mode: str) -> None:
    # A deep run sweeps both sensitivities and all four turns; below that one of
    # each shows the mapping, and the other mode's scenario covers the rest.
    sensitivities = (1, 2) if deep_run() else (2,)
    turns = ((1, 0), (-2, 0), (0, 1), (0, -2)) if deep_run() else ((-2, 0), (0, 1))
    for sensitivity in sensitivities:
        configure(api, Mouse_Mode=mode, Mouse_Wheel_Sensitivity=sensitivity)
        for vertical, horizontal in turns:
            expected = cursor_expectation(vertical, horizontal, sensitivity)
            with check(f"{mode}, sensitivity {sensitivity}: wheel {vertical}/{horizontal} "
                       f"types cursor keys {expected}"), \
                    fresh(listener, mouse, parked=mode != "Cursor"):
                mouse.wheel(vertical=vertical, horizontal=horizontal, gap_ms=300)
                state = listener.quiet()
                detail(str(state))
                require_cursor_keys(state, expected)
                require_still(state, f"the wheel in {mode} mode")
                require_no_pulses(state, f"the wheel in {mode} mode")


def test_wheel_cursor(api, listener, mouse) -> None:
    wheel_cursor_cases(api, listener, mouse, "Mouse + Cursor")
    configure(api, Mouse_Mode="Mouse + Cursor")
    with check("Mouse + Cursor: motion still moves the pointer and types no keys"), fresh(listener, mouse):
        mouse.move(25, -15)
        state = listener.quiet()
        detail(str(state))
        require((state.x, state.y) == (25, -15), "expected position (25, -15)", state)
        require_no_cursor_keys(state, "motion in Mouse + Cursor mode")


# USB_HID_CURSOR_MAX_PENDING_KEYS (16) keys of about 60ms each, with a margin.
CURSOR_RUN_ON_SECONDS = 1.5


def test_cursor_mode(api, listener, mouse) -> None:
    wheel_cursor_cases(api, listener, mouse, "Cursor")
    configure(api, Mouse_Mode="Cursor")
    # scaleCursorMotionKeys: one key per CURSOR_KEY_COUNTS counts of motion,
    # carrying the rest; the vertical motion is negated first, so down is down.
    for moves, expected in (([(8, 0)], {"right": 2}), ([(-12, 0)], {"left": 3}), ([(0, 8)], {"down": 2}),
                            ([(0, -2)], {}), ([(0, -2)] * 2, {"up": 1}),
                            ([(1, 0)] * 3, {}), ([(1, 0)] * 4, {"right": 1})):
        keys = dict.fromkeys(("up", "down", "left", "right"), 0)
        keys.update(expected)
        with check(f"Cursor: {moves} types {expected or 'nothing'} and leaves the pointer"), \
                fresh(listener, mouse, parked=False):
            for dx, dy in moves:
                mouse.move(dx, dy)
            state = listener.quiet()
            detail(str(state))
            require_cursor_keys(state, keys)
            require_still(state, "motion in Cursor mode")
    with check("Cursor: motion far faster than the keys can be typed stops soon after the mouse"), \
            fresh(listener, mouse, parked=False):
        mouse.path(flick(REPORT_CLAMP, 0), 2)
        stopped = time.monotonic()
        hold = 0.3
        state = listener.quiet(timeout=30, hold=hold)
        run_on = time.monotonic() - stopped - hold
        detail(f"keys kept coming for {run_on:.2f}s after the mouse stopped: {state}")
        require(state.cursor["right"] >= 1 and not state.cursor["left"], "expected right keys only", state)
        require(run_on < CURSOR_RUN_ON_SECONDS, f"keys kept coming for more than {CURSOR_RUN_ON_SECONDS}s", state)
        require(state.keys_held == 0, "a key is still down", state)
        require_still(state, "motion in Cursor mode")
    with check("Cursor: turning the other way drops the keys still waiting"), fresh(listener, mouse, parked=False):
        mouse.path(flick(REPORT_CLAMP, 0), 2)
        mouse.move(-4, 0)
        state = listener.quiet(timeout=30)
        detail(str(state))
        require(state.cursor["left"] == 1, "expected one left key after the turn", state)
        # The flick takes about half a second, enough for a handful of keys; the
        # rest of the 16 that were waiting must not be typed.
        require(state.cursor["right"] <= 12, "right keys still waiting were typed after the turn", state)
        require(state.keys_held == 0, "a key is still down", state)


# --------------------------------------------------------------- concurrent --

class Background:
    """A fixture call on its own thread, whose failure is raised when it is joined."""

    def __init__(self, action: Callable[[], object]) -> None:
        self.error: BaseException | None = None
        self.thread = threading.Thread(target=self._run, args=(action,), daemon=True)
        self.thread.start()

    def _run(self, action: Callable[[], object]) -> None:
        try:
            action()
        except BaseException as exc:  # handed to the main thread in join()
            self.error = exc

    def join(self) -> None:
        self.thread.join(120)
        if self.thread.is_alive():
            raise Failure("the background fixture call did not finish")
        if self.error is not None:
            raise self.error


def test_concurrent(api, listener, mouse) -> None:
    configure(api)
    for code in range(1, 8):
        held = tuple(name for bit, name in enumerate(ALL_BUTTONS) if code & (1 << bit))
        with check(f"dragging with {'+'.join(held)} held moves exactly and presses once"), fresh(listener, mouse):
            mouse.stream(dx=5, dy=-3, count=20, buttons=held)
            during = listener.wait_until(lambda s, held=held: s.held == set(held))
            mouse.buttons()
            state = listener.quiet()
            detail(f"held: {during}")
            detail(f"released: {state}")
            require((state.x, state.y) == (100, -60), "expected position (100, -60)", state)
            expected = {name: int(name in held) for name in ALL_BUTTONS}
            require(state.presses == expected, f"expected presses {expected}", state)
            require_monotonic(state, 5, -3)

    with check("motion, wheel detents and a button in the same reports"), fresh(listener, mouse):
        mouse.stream(dx=3, dy=3, wheel=-1, count=8, interval_ms=150, buttons=("left",))
        mouse.buttons()
        state = listener.quiet()
        detail(str(state))
        require((state.x, state.y) == (24, 24), "expected position (24, 24)", state)
        require((state.wheel_up, state.wheel_down) == (0, 8), "expected 8 down pulses", state)
        require(state.presses["left"] == 1, "expected one left press", state)

    with check("a USB key held while the mouse moves is one press and the motion is exact"), fresh(listener, mouse):
        mouse.stream(dx=20, count=50, key="a")
        state = listener.quiet()
        detail(str(state))
        require(state.key_presses("a") == 1, "expected exactly one press of A", state)
        require((state.x, state.y) == (1000, 0), "expected position (1000, 0)", state)
        require(state.keys_held == 0, "a key is still down", state)

    with check("a USB key held while the wheel pulses is one press and every pulse counts"), fresh(listener, mouse):
        mouse.stream(wheel=1, count=5, interval_ms=200, key="space")
        state = listener.quiet()
        detail(str(state))
        require(state.key_presses("space") == 1, "expected exactly one press of SPACE", state)
        require((state.wheel_up, state.wheel_down) == (5, 0), "expected 5 up pulses", state)

    with check("REST joystick port 2 and REST keyboard while the USB mouse moves"), fresh(listener, mouse):
        mover = Background(lambda: mouse.stream(dx=10, dy=5, count=100))
        for index in range(10):
            api.machine.send_input([{"kind": "joystick", "port": 2, "inputs": ["fire"], "transition": "press"}])
            time.sleep(0.06)
            api.machine.send_input([{"kind": "joystick", "port": 2, "inputs": ["fire"], "transition": "release"}])
            time.sleep(0.06)
            if index % 2:
                api.machine.send_input([{"kind": "keyboard", "inputs": ["b"], "transition": "tap"}])
                time.sleep(0.12)
        mover.join()
        state = listener.quiet()
        detail(str(state))
        require((state.x, state.y) == (1000, 500), "expected position (1000, 500)", state)
        require(state.port2_presses["fire"] == 10, "expected 10 port 2 fire presses", state)
        require(state.key_presses("b") == 5, "expected 5 presses of B", state)
        require_monotonic(state, 10, 5)

    with check("REST joystick port 1 on the mouse's own lines while it moves"), fresh(listener, mouse):
        mover = Background(lambda: mouse.stream(dx=-10, count=100))
        for _ in range(5):
            # Joystick down is the line of the middle mouse button.
            api.machine.send_input([{"kind": "joystick", "port": 1, "inputs": ["down"], "transition": "press"}])
            time.sleep(0.08)
            api.machine.send_input([{"kind": "joystick", "port": 1, "inputs": ["down"], "transition": "release"}])
            time.sleep(0.08)
        mover.join()
        state = listener.quiet()
        detail(str(state))
        require((state.x, state.y) == (-1000, 0), "expected position (-1000, 0)", state)
        require(state.presses["middle"] == 5, "expected 5 presses on the middle button line", state)
        require_monotonic(state, -10, 0)

    # No REST key while a key is held here: while any REST key is down,
    # Keyboard_USB::applyMatrixState hides the USB keyboard's matrix, so a held
    # USB key reads as pressed again after every REST tap. REST keys with the
    # mouse moving are checked above.
    with check("mouse with a button and the wheel, a held key and REST port 2 all at once"), fresh(listener, mouse):
        mover = Background(lambda: mouse.stream(dx=4, dy=-4, wheel=1, count=12, interval_ms=150,
                                                buttons=("right",), key="a"))
        for _ in range(4):
            # Held for 100ms: the listener samples port 2 once per pass, and a
            # pass that redraws the screen can take longer than a frame.
            api.machine.send_input([{"kind": "joystick", "port": 2, "inputs": ["up"], "transition": "press"}])
            time.sleep(0.1)
            api.machine.send_input([{"kind": "joystick", "port": 2, "inputs": ["up"], "transition": "release"}])
            time.sleep(0.2)
        mover.join()
        mouse.buttons()
        state = listener.quiet()
        detail(str(state))
        require((state.x, state.y) == (48, -48), "expected position (48, -48)", state)
        require((state.wheel_up, state.wheel_down) == (12, 0), "expected 12 up pulses", state)
        require(state.presses["right"] == 1, "expected one right press", state)
        require(state.key_presses("a") == 1, "expected one press of A", state)
        require(state.port2_presses["up"] == 4, "expected 4 port 2 up presses", state)


# ------------------------------------------------------------ rest-joystick --

def test_rest_joystick(api, listener, mouse) -> None:
    configure(api)
    for line, button in (("up", "right"), ("down", "middle"), ("fire", "left")):
        with check(f"REST joystick {line} on port 1 leaves the mouse position alone"), fresh(listener, mouse):
            api.machine.send_input([{"kind": "joystick", "port": 1, "inputs": [line], "transition": "press"}])
            listener.wait_until(lambda s, button=button: s.held == {button})
            api.machine.send_input([{"kind": "joystick", "port": 1, "inputs": [line], "transition": "release"}])
            state = listener.quiet()
            detail(str(state))
            require_still(state, f"REST joystick {line}")

    with check("a REST fire2 press owns port 1's POT lines, and its release gives them back to the mouse"):
        park(listener, mouse)
        before = listener.quiet()
        listener.reset()
        api.machine.send_input([{"kind": "joystick", "port": 1, "inputs": ["fire2"], "transition": "press"}])
        held = listener.wait_until(lambda s: (s.potx, s.poty) == (0x00, 0x80))
        detail(f"held: {held}")
        api.machine.send_input([{"kind": "joystick", "port": 1, "inputs": ["fire2"], "transition": "release"}])
        released = listener.wait_until(lambda s: (s.potx & 0x7F, s.poty & 0x7F) == (PARKED_POT, PARKED_POT))
        detail(f"before: {before}; released: {released}")


# ------------------------------------------------------------------- menus --

TELNET_PORT = 23
MENU_TIMEOUT_SECONDS = 5.0
MENU_DRAW_SECONDS = 1.5
UI_CATEGORY = "User Interface Settings"
# At 50Hz a two-second menu visit is about 100 frames; far fewer means the menu
# froze the machine.
MENU_RUNNING_FRAMES = 25


def _telnet_read(connection: socket.socket, seconds: float) -> bytes:
    """Everything the server sends within `seconds`."""
    received = b""
    deadline = time.monotonic() + seconds
    connection.settimeout(0.1)
    while time.monotonic() < deadline:
        try:
            chunk = connection.recv(4096)
        except TimeoutError:
            continue
        if not chunk:
            raise Failure("the Telnet server closed the session")
        received += chunk
    return received


@contextmanager
def telnet_session(host: str, password: str | None) -> Iterator[None]:
    """Hold a Telnet menu session open on the machine for the body.

    Logs in when the machine asks for its network password, and returns only
    once the menu has drawn itself (VT100 escape sequences), so the body runs
    while the remote menu is active.
    """
    connection = socket.create_connection((host, TELNET_PORT), timeout=5)
    try:
        received = _telnet_read(connection, 1.0)
        if b"Password:" in received:
            if not password:
                raise Failure("the Telnet server asks for a password; pass it with -p")
            connection.sendall(password.encode() + b"\r\n")
            received = _telnet_read(connection, MENU_DRAW_SECONDS)
            if b"Incorrect password" in received:
                raise Failure("the Telnet server refused the password")
        if b"\x1b[" not in received:
            received += _telnet_read(connection, MENU_DRAW_SECONDS)
        if b"\x1b[" not in received:
            raise Failure("the Telnet session did not draw a menu")
        yield
    finally:
        connection.close()
        time.sleep(1.0)


def press_each_button(listener: MouseListener, mouse: PicoMouse | RestMouse) -> None:
    for button in ALL_BUTTONS:
        mouse.buttons(button)
        listener.wait_until(lambda s, button=button: s.held == {button})
        mouse.buttons()
        listener.wait_until(lambda s: not s.held)


def wait_menu(api: UltimateApi, open_: bool) -> None:
    deadline = time.monotonic() + MENU_TIMEOUT_SECONDS
    while api.machine.menu_open() != open_:
        if time.monotonic() > deadline:
            raise Failure(f"the menu did not {'open' if open_ else 'close'}")
        time.sleep(0.1)


def test_menus(api, listener, mouse) -> None:
    configure(api)
    with check("with a Telnet menu session open, the C64 still gets buttons and wheel, and no keys"), \
            fresh(listener, mouse):
        with telnet_session(api.host, api.rest.password):
            press_each_button(listener, mouse)
            mouse.wheel(vertical=2)
            mouse.wheel(vertical=-1)
            state = listener.quiet()
        detail(str(state))
        require(state.presses == dict.fromkeys(ALL_BUTTONS, 1), "expected one press of each button", state)
        require((state.wheel_up, state.wheel_down) == (2, 1), "expected 2 up and 1 down pulses", state)
        require_no_cursor_keys(state, "the mouse during a Telnet session")
        require_still(state, "the mouse during a Telnet session")

    # With Interface Type Freeze, or with no HDMI display attached, the menu
    # stops the C64 and the listener with it, so what the C64 got under the menu
    # is checked only while it is seen to keep running.
    interface_type = api.configs.item(UI_CATEGORY, "Interface Type").get("current")
    api.configs.set(UI_CATEGORY, "Interface Type", "Overlay on HDMI")
    with check("with the on-screen menu open, the wheel moves its selection"):
        listener.quiet()
        start = listener.reset()
        try:
            api.machine.menu_button()
            wait_menu(api, True)
            time.sleep(0.5)
            before = api.machine.menu_screen()
            mouse.wheel(vertical=-1)
            time.sleep(0.5)
            moved = api.machine.menu_screen()
            mouse.wheel(vertical=1)
            time.sleep(0.5)
            back = api.machine.menu_screen()
            # The right button is Back, which at the top of the browser
            # leaves the menu where it is.
            mouse.buttons("right")
            mouse.buttons()
            time.sleep(0.5)
            state = listener.state()
        finally:
            api.machine.close_menu_from_anywhere()
            wait_menu(api, False)
            if interface_type is not None:
                api.configs.set(UI_CATEGORY, "Interface Type", interface_type)
        detail(str(state))
        if moved == before:
            raise Failure("a wheel detent did not move the menu selection")
        if back != before:
            raise Failure("the opposite detent did not bring the selection back")
    with check("under the on-screen menu the C64 keeps running and gets no wheel pulse, button or key"):
        if state.frames - start.frames < MENU_RUNNING_FRAMES:
            check_skip(f"the menu froze the C64 ({state.frames - start.frames} frames); "
                       "it runs under the menu only with Interface Type Overlay on HDMI and a display attached")
        else:
            require((state.wheel_up, state.wheel_down) == (0, 0), "the C64 saw wheel pulses under the menu", state)
            require(state.presses["right"] == 0, "the C64 saw the right button under the menu", state)
            require_no_cursor_keys(state, "the mouse under the menu")

    with check("with Menu Mouse Navigation Disabled the wheel leaves the menu alone"):
        api.configs.set(UI_CATEGORY, "Interface Type", "Overlay on HDMI")
        configure(api, Menu_Mouse_Navigation="Disabled")
        try:
            api.machine.menu_button()
            wait_menu(api, True)
            time.sleep(0.5)
            before = api.machine.menu_screen()
            mouse.wheel(vertical=-1)
            time.sleep(0.5)
            after = api.machine.menu_screen()
        finally:
            api.machine.close_menu_from_anywhere()
            wait_menu(api, False)
            configure(api)
            if interface_type is not None:
                api.configs.set(UI_CATEGORY, "Interface Type", interface_type)
        if after != before:
            raise Failure("a wheel detent moved the menu selection with mouse navigation disabled")

    with check("once the menu has closed the mouse reaches the C64 again"), fresh(listener, mouse):
        press_each_button(listener, mouse)
        mouse.wheel(vertical=1)
        state = listener.quiet()
        detail(str(state))
        require(state.presses == dict.fromkeys(ALL_BUTTONS, 1), "expected one press of each button", state)
        require((state.wheel_up, state.wheel_down) == (1, 0), "expected 1 up pulse", state)


SCENARIOS = {
    "move": test_move, "motion-speed": test_motion_speed, "pacing": test_pacing,
    "precision": test_precision, "path": test_path, "flood": test_flood, "settings": test_settings,
    "buttons": test_buttons,
    "wheel-micromys": test_wheel_micromys, "wheel-count": test_wheel_count, "wheel-mouse": test_wheel_mouse,
    "wheel-cursor": test_wheel_cursor, "cursor-mode": test_cursor_mode, "concurrent": test_concurrent,
    "rest-joystick": test_rest_joystick, "menus": test_menus,
}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    cli.add_device_arguments(parser, password=None)
    parser.add_argument("--backend", choices=("rest", "pico"), default="rest",
                        help="where the mouse comes from (default: rest)")
    parser.add_argument("--pico-host", help="fixture IP address, for --backend pico, on networks that do "
                        "not forward broadcast between the wired test host and the Wi-Fi client")
    parser.add_argument("--test", action="append", choices=TESTS,
                        help="run one scenario; repeat for several (default: what --profile runs)")
    parser.add_argument("--profile", choices=profiles.ORDER, default=profiles.current(),
                        help="run the scenarios of this profile (default: the runner's, else quick)")
    args = parser.parse_args()
    if tuple(SCENARIOS) != TESTS:
        raise Failure("TESTS and SCENARIOS name different scenarios")
    # The scenarios that do more in a deeper profile read it back from here.
    os.environ[profiles.ENV] = args.profile
    api = UltimateApi(args.host, args.password, args.timeout)
    # Every scenario below reads and writes the mouse items of CATEGORY, which
    # only the U64-class firmware serves. A machine without that category has no
    # mouse to configure, so there is nothing here for it to fail.
    if CATEGORY not in api.configs.category_names():
        suite_skip(SUITE, f"this machine serves no {CATEGORY!r}, so it has none of "
                          "the mouse settings every scenario here drives")
        return 0
    pico = None
    if args.backend == "pico":
        fixture = args.pico_host or discover_pico(optional=True)
        if fixture is None:
            suite_skip(SUITE, "no Pico HID fixture answers on this network; name one "
                              "with --pico-host, or run --backend rest")
            return 0
        pico = Pico(fixture)
        with check("the Pico fixture offers a USB mouse"):
            pico.require_mouse()
        mouse = PicoMouse(pico)
    else:
        mouse = RestMouse(api)
    listener = MouseListener(api)
    saved = {item: api.configs.item(CATEGORY, item).get("current") for item in (*DEFAULTS, "System Mode")}
    detail("saved settings: " + ", ".join(f"{item}={value!r}" for item, value in saved.items()))
    try:
        configure(api)
        listener.start()
        # A first report gives the firmware a mouse position before any origin
        # is taken, so every check starts from a position the mouse itself set.
        mouse.move(1, 1)
        mouse.move(-1, -1)
        selected = tuple(args.test) if args.test else tests_for(args.profile)
        detail(f"{args.profile} profile: " + ", ".join(selected))
        for name in selected:
            SCENARIOS[name](api, listener, mouse)
    finally:
        for step in (mouse.release_all, (pico.release_all if pico else lambda: None), api.machine.release_all,
                     lambda: [api.configs.set(CATEGORY, item, value)
                              for item, value in saved.items() if value is not None],
                     lambda: api.machine.reset(force=True)):
            try:
                step()
            except Exception as exc:
                detail("cleanup failure: " + format_exception(exc))
        if pico:
            pico.close()
    suite_ok(SUITE)
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Failure as exc:
        suite_fail(SUITE, format_exception(exc))
        raise SystemExit(1)
