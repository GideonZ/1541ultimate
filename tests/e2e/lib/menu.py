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
import time
from typing import Callable, List, Optional, Sequence

# software/api/route_input.cc rejects a larger batch with HTTP 400.
INPUT_MAX_EVENTS = 60

# Only used where nothing observable can be polled.
KEY_SETTLE_SECONDS = 0.05
POLL_INTERVAL_SECONDS = 0.05
MENU_TOGGLE_TIMEOUT_SECONDS = 6.0


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
               interval: float = POLL_INTERVAL_SECONDS) -> bool:
    """Poll until predicate holds. Returns False on timeout."""
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
                        stable_samples: int = 2) -> bool:
    """Wait until the menu screen stops changing.

    A batch is accepted by REST immediately but drains through the C64 matrix
    over time, so the caller must not act until it has been consumed. Watching
    the screen go quiet measures that directly.
    """
    last = screen()
    stable = 0
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        time.sleep(POLL_INTERVAL_SECONDS)
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
                        timeout: float) -> bool:
    """Wait for the menu screen to differ from 'before'.

    Drawing takes longer than a key tap, and acting before it finishes loses the
    next press. This waits for the event itself rather than guessing at it.
    """
    return wait_until(lambda: screen() != before, timeout)
