#!/usr/bin/env python3
# E2E helper: fast, shared primitives for driving the menu over REST.

"""Key presses, menu state and the menu button, in one place.

Each suite had grown its own copy of these with its own settle constants, which
is why the same idea appeared at 0.10, 0.12, 0.15 and 0.35 seconds. Two things
made runs slow:

  - one HTTP round-trip per keystroke, at ~20ms each, so a 64-tap field clear
    cost over a second in transport alone before any settle;
  - fixed sleeps after every action, sized for the worst case, where the device
    usually finishes far sooner.

So: batch taps into as few requests as the firmware allows, and wait on observed
state instead of a fixed delay. Callers pass their own transport, so a suite can
adopt this without restructuring its session class.
"""
import os
import sys
import time
from typing import Callable, List, Optional, Sequence

# tests/lib holds the pacing every suite shares; see tests/lib/pacing.py for
# why these are not constants of this module any more.
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                "..", "..", "lib"))
import pacing  # noqa: E402  (needs tests/lib on sys.path first)

# software/api/route_input.cc rejects a larger batch with HTTP 400.
INPUT_MAX_EVENTS = 60

# Kept as names so callers read the same way; the values live in tests/lib/pacing.py.
KEY_SETTLE_SECONDS = pacing.KEY_SETTLE_SECONDS
MENU_TOGGLE_TIMEOUT_SECONDS = pacing.MENU_TOGGLE_TIMEOUT_SECONDS


def tap_event(keys: Sequence[str]) -> dict:
    return {"kind": "keyboard", "inputs": list(keys), "transition": "tap"}


def tap_batches(keys_list: Sequence[Sequence[str]]) -> List[List[dict]]:
    """Split a run of taps into batches the firmware will accept."""
    events = [tap_event(k) for k in keys_list]
    return [events[i:i + INPUT_MAX_EVENTS] for i in range(0, len(events), INPUT_MAX_EVENTS)]


def send_taps(post_events: Callable[[List[dict]], None], keys_list: Sequence[Sequence[str]]) -> None:
    """Send a run of taps in as few requests as possible.

    The firmware drains the batch through the same matrix path as separate
    requests, so the keys the machine sees are unchanged.
    """
    for batch in tap_batches(keys_list):
        post_events(batch)


def repeat_key(post_events: Callable[[List[dict]], None], keys: Sequence[str], count: int) -> None:
    send_taps(post_events, [keys] * count)


def wait_until(predicate: Callable[[], bool], timeout: float,
               interval: Optional[float] = None) -> bool:
    """Poll until predicate holds. Returns False on timeout.

    `interval` defaults to the shared pacing value at call time, not at import
    time: a default argument would bind whatever pacing held when this module
    was first imported, so an override would silently not reach this function.
    """
    if interval is None:
        interval = pacing.POLL_INTERVAL_SECONDS
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if predicate():
            return True
        time.sleep(interval)
    return predicate()


def wait_menu_state(menu_is_open: Callable[[], bool], want_open: bool,
                    timeout: float = MENU_TOGGLE_TIMEOUT_SECONDS) -> bool:
    return wait_until(lambda: menu_is_open() == want_open, timeout)


def toggle_menu(press_button: Callable[[], None], menu_is_open: Callable[[], bool],
                want_open: bool, timeout: float = MENU_TOGGLE_TIMEOUT_SECONDS) -> bool:
    """Press the menu button and wait for the state to change.

    The press is a toggle, so a transport failure is not retried: the request may
    already have been applied. The state is polled instead, which answers it
    either way.
    """
    if menu_is_open() == want_open:
        return True
    try:
        press_button()
    except Exception:
        pass
    return wait_menu_state(menu_is_open, want_open, timeout)


def wait_screen_settled(screen: Callable[[], Optional[bytes]], timeout: float,
                        stable_samples: Optional[int] = None) -> bool:
    """Wait until the menu screen stops changing.

    A batch is accepted by REST immediately but drains through the C64 matrix
    over time, so the caller must not act until it has been consumed. Watching
    the screen go quiet measures that directly.
    """
    if stable_samples is None:
        stable_samples = pacing.SETTLE_STABLE_SAMPLES
    last = screen()
    stable = 0
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        time.sleep(pacing.POLL_INTERVAL_SECONDS)
        current = screen()
        if current == last:
            stable += 1
            if stable >= stable_samples:
                return True
        else:
            stable = 0
            last = current
    return False


def wait_screen_changes(screen: Callable[[], Optional[bytes]], before: Optional[bytes],
                        timeout: float, min_samples: int = 1,
                        hard_timeout: Optional[float] = None) -> bool:
    """Wait for the menu screen to differ from 'before'.

    Drawing takes longer than a key tap, and acting before it finishes loses the
    next press. This waits for the event itself rather than guessing at it.

    Giving up is bounded by two things at once, and it needs both. A wall-clock
    budget alone is wrong when the device is busy: it serves few concurrent HTTP
    connections, so one read of the screen can take seconds, and a short budget
    then expires having looked only once. That is how a change that really
    happened gets reported as "nothing happened", which is the failure this
    guards against, rather than a slow redraw: a redraw's first visible change
    was measured at 83ms worst across every mode and every heavy operation.
    So `min_samples` reads must also have been taken, which stretches the wait
    in proportion to how slow the transport actually is. `hard_timeout` caps the
    result either way.

    Reads are spaced by the shared poll interval rather than taken back to
    back. Reading the screen is not free to the device: the firmware copies the
    whole matrix with interrupts disabled
    (UserInterface::copy_active_screen_matrix), so polling it as fast as the
    transport allows holds interrupts off for a large share of the time. With
    `min_samples` doing the work of surviving a slow transport, there is
    nothing to be gained from that, and it costs about 0.1s per keypress that
    changes nothing.
    """
    if hard_timeout is None:
        hard_timeout = max(timeout, MENU_TOGGLE_TIMEOUT_SECONDS)
    started = time.monotonic()
    samples = 0
    while True:
        if screen() != before:
            return True
        samples += 1
        elapsed = time.monotonic() - started
        if elapsed >= hard_timeout:
            return False
        if samples >= min_samples and elapsed >= timeout:
            return False
        time.sleep(pacing.POLL_INTERVAL_SECONDS)
