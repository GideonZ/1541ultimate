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
    At 50Hz (PAL) and 60Hz (NTSC) frames: slow, medium, fast and
    faster-than-the-clamp movement, in all four diagonal directions and with
    reversals, and motion and wheel in one report moving 126 counts. No step
    may go against the movement or exceed 63 counts, which a 1351 driver would
    read as movement the other way. The total must be exact wherever the POT
    lines can carry the speed: always at 60Hz, and at 50Hz up to 40 counts per
    report; beyond that the firmware drops what it cannot show within a
    bounded backlog (`MousePotPacer`). Then, at 60Hz, sensitivities 1 and 16
    and adaptive acceleration, all exact.
buttons
    All eight button states and all 64 transitions between them: what is held
    and how many presses were counted, without moving the position.
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
    Mouse + Cursor mode: both wheels type cursor keys, at wheel sensitivities 1
    and 2, and the pointer stays where it is.
cursor-mode
    Cursor mode: both wheels and the motion type cursor keys. Motion far
    faster than keys can be typed stops typing within 1.5 seconds of the mouse
    and leaves no key stuck, and turning the other way drops the keys still
    waiting.
concurrent
    Buttons held while moving, wheel detents and buttons inside motion
    reports, a USB key held while moving and while the wheel pulses, and REST
    joystick and keyboard input on both ports while the USB mouse moves.
rest-joystick
    REST joystick input on port 1 leaves the mouse position alone, and a REST
    fire2 press holds both POT lines against mouse reports until it is
    released (issue #909).
menus
    With a Telnet menu session open, the C64 still gets every button and wheel
    pulse and no cursor key: that menu has its own keyboard. With the menu open
    on the machine's own screen, the wheel moves its selection and the C64 gets
    neither buttons, pulses nor keys; that part is skipped when the menu
    freezes the C64 (Interface Type Freeze, or no HDMI display). Once the menu
    closes, the mouse reaches the C64 again.

With --backend pico the suite needs the fixture on a USB port of the machine.
"""

from __future__ import annotations

import argparse
import socket
import sys
import threading
import time
from collections.abc import Callable, Iterator
from contextlib import contextmanager
from pathlib import Path

# The one stanza that puts the shared library on sys.path; see tests/lib/bootstrap.py.
sys.path.insert(0, str(next(p for p in Path(__file__).resolve().parents
                            if (p / "tests" / "lib").is_dir()) / "tests" / "lib"))
import bootstrap  # noqa: E402,F401
import cli  # noqa: E402
from api import UltimateApi  # noqa: E402
from mouse import MouseListener, MouseState, PicoMouse, RestMouse  # noqa: E402
from pico_hid import Pico, discover_pico  # noqa: E402
from report import Failure, check, check_skip, detail, format_exception, suite_fail, suite_ok  # noqa: E402

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

TESTS = ("move", "motion-speed", "buttons", "wheel-micromys", "wheel-count", "wheel-mouse",
         "wheel-cursor", "cursor-mode", "concurrent", "rest-joystick", "menus")

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


def configure(api: UltimateApi, **settings: object) -> None:
    """Apply DEFAULTS, then `settings` given with underscores for spaces."""
    wanted = dict(DEFAULTS)
    for name, value in settings.items():
        wanted[name.replace("_", " ")] = value
    for item, value in wanted.items():
        api.configs.set(CATEGORY, item, value)


@contextmanager
def fresh(listener: MouseListener, mouse: PicoMouse | RestMouse) -> Iterator[None]:
    """Start a check from a quiet listener with its counters at zero.

    The origin is always a position the mouse itself set, which can never be
    $80: a settings change or a machine reset can leave the POT lines at the
    released joystick value, and a check that started there could not see the
    firmware put them back to it.
    """
    mouse.move(1, 1)
    mouse.move(-1, -1)
    state = listener.quiet()
    if (state.potx, state.poty) == (0x80, 0x80):
        raise Failure(f"the POT lines do not follow the mouse: {state}")
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

# System Mode and the frame rate its C64 runs at.
FRAME_RATES = {"PAL": 50, "NTSC": 60}
# How long the firmware takes to show a full MousePotPacer backlog (252 counts,
# 63 per 22ms window released on 5ms timer runs), with a margin.
PACER_SETTLE_SECONDS = 0.15


def stream_case(listener, mouse, label: str, dx: int, dy: int, count: int, interval_ms: int,
                expected: tuple[int, int] | None,
                bounds: tuple[tuple[int, int], tuple[int, int]] | None = None, wheel: int = 0,
                pan: int = 0, frame_rate: int | None = None) -> None:
    """`count` identical reports; `dx` and `dy` give the direction for the step check."""
    with check(label), fresh(listener, mouse):
        started = time.monotonic()
        timing = mouse.stream(dx=dx, dy=dy, count=count, interval_ms=interval_ms, wheel=wheel, pan=pan)
        # No read until the POT lines have caught up: a read stops the 6510, and
        # a frame the listener missed would add two frames of movement into one
        # step, which could look like a wrap the firmware never made.
        time.sleep(PACER_SETTLE_SECONDS)
        first_read = time.monotonic()
        after = listener.state()
        state = listener.quiet()
        rate = count * 1000.0 / max(1, timing.get("elapsed_ms", 1))
        detail(f"{count} reports in {timing.get('elapsed_ms')}ms ({rate:.0f}/s): {state}")
        if frame_rate is not None:
            frames = (first_read - started) * frame_rate
            require(after.frames >= 0.9 * frames, f"the listener sampled fewer than 90% of {frames:.0f} frames",
                    after)
        if expected is not None:
            require((state.x, state.y) == expected, f"expected position {expected}", state)
        if bounds is not None:
            (x_low, x_high), (y_low, y_high) = bounds
            require(x_low <= state.x <= x_high and y_low <= state.y <= y_high,
                    f"expected x in {bounds[0]} and y in {bounds[1]}", state)
        require_monotonic(state, dx, dy)


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


def set_system_mode(api, listener: MouseListener, mouse: PicoMouse, mode: str) -> None:
    """Switch the video timing and start the listener again on the new frames."""
    api.configs.set(CATEGORY, "System Mode", mode)
    listener.start()
    mouse.move(1, 1)
    mouse.move(-1, -1)


def motion_at_frame_rate(api, listener, mouse, mode: str) -> None:
    rate = FRAME_RATES[mode]
    set_system_mode(api, listener, mouse, mode)
    configure(api, Mouse_Mode="Mouse")
    at = f"{rate}Hz: "
    stream_case(listener, mouse, at + "slow: 1 count every 30ms", 1, 0, 100, 30, (100, 0), frame_rate=rate)
    stream_case(listener, mouse, at + "slow diagonal down-left", -1, 1, 100, 30, (-100, 100), frame_rate=rate)
    stream_case(listener, mouse, at + "medium: 20 counts as fast as the host polls", 20, -20, 100, 0,
                (2000, -2000), frame_rate=rate)
    # Two reports handled inside one frame add up to 62 counts, still a step
    # the driver reads the right way.
    stream_case(listener, mouse, at + "31 counts as fast as the host polls", -31, 31, 100, 0,
                (-3100, 3100), frame_rate=rate)
    # 80 counts can land inside one frame; the firmware carries what is over
    # 63 to the next, and 2000 counts per second is within what 50Hz frames
    # carry.
    stream_case(listener, mouse, at + "40 counts as fast as the host polls are carried and exact",
                40, 40, 60, 0, (2400, 2400), frame_rate=rate)
    for dx, dy in ((63, 63), (-63, 63), (63, -63), (-63, -63)):
        exact = (dx * 60, dy * 60)
        if rate == 60:
            stream_case(listener, mouse, at + f"fast: {dx},{dy} as fast as the host polls is exact",
                        dx, dy, 60, 0, exact, frame_rate=rate)
        else:
            # 3150 counts per second is more than 63 counts per 19.95ms frame,
            # with the margin the pacing keeps, can show. Well over two thirds
            # arrives, and the backlog caps how much can still arrive late.
            low = [int(value * 0.7) for value in exact]
            stream_case(listener, mouse, at + f"fast: {dx},{dy} as fast as the host polls never wraps",
                        dx, dy, 60, 0, None,
                        ((min(low[0], exact[0]), max(low[0], exact[0])),
                         (min(low[1], exact[1]), max(low[1], exact[1]))), frame_rate=rate)
    for dx in (127, -127):
        if rate == 60:
            stream_case(listener, mouse, at + f"faster than the clamp: {dx} per report lands as {clamp(dx)}",
                        dx, 0, 60, 0, (clamp(dx) * 60, 0), frame_rate=rate)
    with check(at + "a reversal at 40 counts comes back to where it started"), fresh(listener, mouse):
        mouse.stream(dx=40, count=50)
        mouse.stream(dx=-40, count=50)
        state = listener.quiet()
        detail(str(state))
        require((state.x, state.y) == (0, 0), "the reversal did not come back", state)
        require(state.step_x[0] >= -REPORT_CLAMP and state.step_x[1] <= REPORT_CLAMP,
                "a step exceeded what a report may move", state)
    if rate == 60:
        with check(at + "a fast reversal comes back to where it started"), fresh(listener, mouse):
            mouse.stream(dx=63, count=50)
            mouse.stream(dx=-63, count=50)
            state = listener.quiet()
            detail(str(state))
            require((state.x, state.y) == (0, 0), "the reversal did not come back", state)

    # A report whose motion and wheel both move the pointer the same way moves
    # it by more than 63 counts; on the POT lines that has to become two
    # frames. At wheel sensitivity 16, scaleVerticalWheelAxisDelta turns a
    # vertical value of 1 into 40 counts (normalizeVerticalWheel makes it 8,
    # then 8 * 16 * 10/32), and scaleHorizontalWheelAxisDelta turns a
    # horizontal value of 2 into 64 counts (2 * 16 * 2), clamped to 63. REST
    # cannot put motion and wheel into one report: it sends the move, then one
    # report of value 1 per detent, 32 counts each horizontally.
    configure(api, Mouse_Mode="Mouse", Mouse_Wheel_Sensitivity=16)
    one_report = isinstance(mouse, PicoMouse)
    how = "in one report" if one_report else "one report after the other"
    vertical = 40
    horizontal = 63 if one_report else 2 * 32
    stream_case(listener, mouse, at + f"motion and vertical wheel {how}: {63 + vertical} counts up, no wrap",
                0, -63, 10, 150, (0, -10 * (63 + vertical)), wheel=1, frame_rate=rate)
    stream_case(listener, mouse, at + f"motion and horizontal wheel {how}: {63 + horizontal} counts right, no wrap",
                63, 0, 10, 150, (10 * (63 + horizontal), 0), pan=2 * PAN_POSITIVE_SIGN, frame_rate=rate)
    stream_case(listener, mouse, at + "motion and both wheels as fast as the host polls never wrap",
                63, -63, 30, 0, None, ((1, 30 * (63 + horizontal)), (-30 * (63 + vertical), -1)), wheel=1,
                pan=2 * PAN_POSITIVE_SIGN, frame_rate=rate)


def test_motion_speed(api, listener, mouse) -> None:
    original = api.configs.item(CATEGORY, "System Mode").get("current")
    try:
        for mode in FRAME_RATES:
            motion_at_frame_rate(api, listener, mouse, mode)
        # The clamp checks below move 63 counts every 20ms, which only 60Hz
        # frames show in full; at 50Hz the firmware drops part of it.
        set_system_mode(api, listener, mouse, "NTSC")
        # scaleFixedWithRemainder: sensitivity 1 is 32/256 of a count per count,
        # with the remainder carried, so 160 single counts are exactly 20.
        configure(api, Mouse_Mode="Mouse", Mouse_Sensitivity=1)
        stream_case(listener, mouse, "sensitivity 1 carries fractions: 160 x 1 count is 20",
                    1, 0, 160, 0, (20, 0))
        stream_case(listener, mouse, "sensitivity 1 carries fractions the other way", 0, -1, 160, 0, (0, -20))
        # Sensitivity 16 doubles, and the clamp still applies after it.
        configure(api, Mouse_Mode="Mouse", Mouse_Sensitivity=16)
        stream_case(listener, mouse, "sensitivity 16 doubles: 100 x 10 counts is 2000", 10, 0, 100, 0, (2000, 0))
        stream_case(listener, mouse, "sensitivity 16 clamps: 50 x 40 counts is 50 x 63", 0, 40, 50, 0, (0, 50 * 63))
        # Adaptive acceleration scales by 384/256 to 576/256 depending on recent
        # speed, before the clamp; adaptive_acceleration_total follows it.
        for dx, dy, count in ((10, 10, 100), (30, 30, 60)):
            configure(api, Mouse_Mode="Mouse")
            # A report with acceleration off clears the speed history.
            mouse.move(1, 1)
            configure(api, Mouse_Mode="Mouse", Mouse_Acceleration="Adaptive")
            expected = adaptive_acceleration_total(dx, dy, count)
            stream_case(listener, mouse, f"adaptive acceleration: {count} x {dx},{dy} counts is {expected}",
                        dx, dy, count, 0, expected)
        configure(api, Mouse_Mode="Mouse")
    finally:
        if original is not None:
            set_system_mode(api, listener, mouse, original)


# ------------------------------------------------------------------ buttons --

def test_buttons(api, listener, mouse) -> None:
    configure(api)
    masks = [tuple(name for bit, name in enumerate(ALL_BUTTONS) if code & (1 << bit)) for code in range(8)]
    with check("all 8 button states and all 64 transitions between them"), fresh(listener, mouse):
        expected = dict.fromkeys(ALL_BUTTONS, 0)
        held: tuple[str, ...] = ()
        for first in masks:
            for second in masks:
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
        for combination in (("left",), ("right",), ("middle",), ALL_BUTTONS):
            with check(f"a REST tap of {'+'.join(combination)} is one press each and ends released"), \
                    fresh(listener, mouse):
                mouse.tap(*combination)
                state = listener.quiet()
                detail(str(state))
                expected = {name: int(name in combination) for name in ALL_BUTTONS}
                require(state.presses == expected, f"expected presses {expected}", state)
                require(not state.held, "a button is still held", state)


# ----------------------------------------------------------- wheel-micromys --

def test_wheel_micromys(api, listener, mouse) -> None:
    for sensitivity, up, down in ((1, 5, 4), (3, 2, 1), (8, 1, 1)):
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

    with check("a reversal inside a fast burst pulses the new direction exactly"), fresh(listener, mouse):
        mouse.wheel(vertical=8, gap_ms=0)
        mouse.wheel(vertical=-3, gap_ms=0)
        state = listener.quiet()
        detail(str(state))
        require(state.wheel_down == 3, "expected exactly 3 down pulses after the reversal", state)
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
    with check("120 slow detents in alternating directions are counted exactly"), fresh(listener, mouse):
        for _ in range(30):
            mouse.wheel(vertical=2)
            mouse.wheel(vertical=-2)
        state = listener.quiet()
        detail(str(state))
        require((state.wheel_up, state.wheel_down) == (60, 60), "expected 60 up and 60 down pulses", state)
        require_still(state, "the wheel")


# -------------------------------------------------------------- wheel-mouse --

def test_wheel_mouse(api, listener, mouse) -> None:
    # scaleVerticalWheelAxisDelta: a detent is 8 units, times the wheel
    # sensitivity, times 10/32; at 4 that is 10 counts, up for a detent away
    # from the user. scaleHorizontalWheelAxisDelta: one unit times sensitivity
    # times 2, so 8 counts, in the direction PAN_POSITIVE_SIGN gives.
    for direction, sign in (("Normal", 1), ("Reversed", -1)):
        configure(api, Mouse_Mode="Mouse", Mouse_Wheel_Sensitivity=4, Mouse_Wheel_Direction=direction)
        for label, vertical, horizontal, gap_ms, expected in (
                ("slow vertical", 2, 0, 150, (0, -20 * sign)),
                ("slow vertical, other way", -3, 0, 150, (0, 30 * sign)),
                ("slow horizontal", 0, 2, 150, (16 * PAN_POSITIVE_SIGN * sign, 0)),
                ("slow horizontal, other way", 0, -3, 150, (-24 * PAN_POSITIVE_SIGN * sign, 0)),
                ("fast vertical", 12, 0, 0, (0, -120 * sign)),
                ("fast horizontal", 0, -12, 0, (-96 * PAN_POSITIVE_SIGN * sign, 0))):
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


def wheel_cursor_cases(api, listener, mouse, mode: str) -> None:
    for sensitivity in (1, 2):
        configure(api, Mouse_Mode=mode, Mouse_Wheel_Sensitivity=sensitivity)
        for vertical, horizontal in ((1, 0), (-2, 0), (0, 1), (0, -2)):
            expected = cursor_expectation(vertical, horizontal, sensitivity)
            with check(f"{mode}, sensitivity {sensitivity}: wheel {vertical}/{horizontal} "
                       f"types cursor keys {expected}"), fresh(listener, mouse):
                mouse.wheel(vertical=vertical, horizontal=horizontal, gap_ms=300)
                state = listener.quiet()
                detail(str(state))
                require(state.cursor == expected, f"expected cursor keys {expected}", state)
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
    # scaleCursorMotionKeys: a report types divideRounded(|motion|, 4) keys,
    # at least one; the vertical motion is negated first, so down is down.
    for dx, dy, expected in ((8, 0, {"right": 2}), (-12, 0, {"left": 3}), (0, 8, {"down": 2}),
                             (0, -2, {"up": 1}), (1, 0, {"right": 1})):
        keys = dict.fromkeys(("up", "down", "left", "right"), 0)
        keys.update(expected)
        with check(f"Cursor: motion {dx},{dy} types {expected} and leaves the pointer"), fresh(listener, mouse):
            mouse.move(dx, dy)
            state = listener.quiet()
            detail(str(state))
            require(state.cursor == keys, f"expected cursor keys {keys}", state)
            require_still(state, "motion in Cursor mode")
    with check("Cursor: motion far faster than the keys can be typed stops soon after the mouse"), \
            fresh(listener, mouse):
        mouse.stream(dx=63, count=30)
        stopped = time.monotonic()
        hold = 0.3
        state = listener.quiet(timeout=30, hold=hold)
        run_on = time.monotonic() - stopped - hold
        detail(f"keys kept coming for {run_on:.2f}s after the mouse stopped: {state}")
        require(16 <= state.cursor["right"] <= 30 * 16, "expected a bounded number of right keys", state)
        require(run_on < CURSOR_RUN_ON_SECONDS, f"keys kept coming for more than {CURSOR_RUN_ON_SECONDS}s", state)
        require(state.keys_held == 0, "a key is still down", state)
        require_still(state, "motion in Cursor mode")
    with check("Cursor: turning the other way drops the keys still waiting"), fresh(listener, mouse):
        mouse.stream(dx=63, count=10)
        mouse.move(-4, 0)
        state = listener.quiet(timeout=30)
        detail(str(state))
        require(state.cursor["left"] == 1, "expected one left key after the turn", state)
        # Ten reports take about 200ms, enough for a handful of keys; the rest
        # of the 16 that were waiting must not be typed.
        require(state.cursor["right"] <= 10, "right keys still waiting were typed after the turn", state)
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
            api.machine.send_input([{"kind": "joystick", "port": 2, "inputs": ["up"], "transition": "tap"}])
            time.sleep(0.3)
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

    with check("a REST fire2 press holds the POT lines against the mouse and gives them back"):
        # Start from a position the mouse set, which can never be $80.
        mouse.move(1, 1)
        mouse.move(-1, -1)
        listener.quiet()
        before = listener.reset()
        api.machine.send_input([{"kind": "joystick", "port": 1, "inputs": ["fire2"], "transition": "press"}])
        listener.wait_until(lambda s: (s.potx, s.poty) == (0x00, 0x80))
        # Mouse reports while fire2 is held must not take the lines back.
        mouse.move(12, 7)
        held = listener.quiet()
        detail(f"held, after moving the mouse: {held}")
        require((held.potx, held.poty) == (0x00, 0x80),
                "a mouse report replaced the held fire2 POT values", held)
        api.machine.send_input([{"kind": "joystick", "port": 1, "inputs": ["fire2"], "transition": "release"}])
        state = listener.quiet()
        detail(f"released: {state}")
        # Compared as POT values: the position totals add two jumps across
        # $00/$80, which a 7-bit change can read the wrong way round.
        expected = ((before.potx + 12) & 0x7F, (before.poty - 7) & 0x7F)
        require((state.potx, state.poty) == expected,
                f"after release POT is not ${expected[0]:02X}/${expected[1]:02X}, where the mouse moved it",
                state)


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

    with check("once the menu has closed the mouse reaches the C64 again"), fresh(listener, mouse):
        press_each_button(listener, mouse)
        mouse.wheel(vertical=1)
        state = listener.quiet()
        detail(str(state))
        require(state.presses == dict.fromkeys(ALL_BUTTONS, 1), "expected one press of each button", state)
        require((state.wheel_up, state.wheel_down) == (1, 0), "expected 1 up pulse", state)


SCENARIOS = {
    "move": test_move, "motion-speed": test_motion_speed, "buttons": test_buttons,
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
                        help="run one scenario; repeat for several (default: all)")
    args = parser.parse_args()
    if tuple(SCENARIOS) != TESTS:
        raise Failure("TESTS and SCENARIOS name different scenarios")
    api = UltimateApi(args.host, args.password, args.timeout)
    pico = None
    if args.backend == "pico":
        pico = Pico(args.pico_host or discover_pico())
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
        for name in args.test or TESTS:
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
