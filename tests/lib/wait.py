"""Waiting for the device to reach a state, and retrying what may not stick.

For polling the device: a screen to settle, a file to appear, a drive to
register a mount.

Two rules are built in rather than left to each caller:

- A wait states what it was waiting for. A timeout that says only "timed out"
  costs a rerun to find out what it was watching.
- A wait is bounded. There is no helper here that can block forever, because a
  suite that hangs takes the whole run's device with it.

Prefer these to `time.sleep`. A sleep is sized for the worst case and paid in
full every time, while a wait returns as soon as the thing it is waiting for has
happened: replacing the fixed pause after a machine reset with a poll for the
BASIC prompt took it from 1 to 3 seconds down to about 50ms. A fixed sleep is
right only where there is nothing observable to poll, and then it belongs in
tests/lib/pacing.py with the measurement that justifies it.
"""

from __future__ import annotations

import os
import time
from typing import Callable, Optional, Sequence, TypeVar

from report import Failure

T = TypeVar("T")


def _seconds(name: str, default: float) -> float:
    raw = os.environ.get(name)
    if raw is None:
        return default
    try:
        value = float(raw)
    except ValueError:
        raise SystemExit(f"{name} must be a number, got {raw!r}")
    if value < 0:
        raise SystemExit(f"{name} must not be negative, got {value}")
    return value


# How often a wait re-asks, and how long it waits by default. Both are
# overridable for one run without editing code, the same way tests/lib/pacing.py
# handles the UI timings:
#
#     U64_WAIT_INTERVAL=0.05 ./run-tests u64
#
# A caller whose subject is the timing passes its own numbers and says why.
DEFAULT_INTERVAL_SECONDS = _seconds("U64_WAIT_INTERVAL", 0.05)
DEFAULT_TIMEOUT_SECONDS = _seconds("U64_WAIT_TIMEOUT", 10.0)


def wait_until(predicate: Callable[[], bool], description: str,
               timeout: float = DEFAULT_TIMEOUT_SECONDS,
               interval: float = DEFAULT_INTERVAL_SECONDS,
               detail: Optional[Callable[[], str]] = None) -> float:
    """Poll until `predicate` holds, returning how long that took.

    `detail` is called only on failure, so a caller can attach an expensive
    snapshot (a screen capture, a directory listing) without paying for it on
    the normal path.
    """
    deadline = time.monotonic() + timeout
    started = time.monotonic()
    while True:
        if predicate():
            return time.monotonic() - started
        if time.monotonic() >= deadline:
            message = f"timed out after {timeout:g}s waiting for {description}"
            if detail is not None:
                message += f"; {detail()}"
            raise Failure(message)
        time.sleep(interval)


def wait_for(produce: Callable[[], Optional[T]], description: str,
             timeout: float = DEFAULT_TIMEOUT_SECONDS,
             interval: float = DEFAULT_INTERVAL_SECONDS) -> T:
    """Poll until `produce` returns something other than None, and return it."""
    deadline = time.monotonic() + timeout
    while True:
        value = produce()
        if value is not None:
            return value
        if time.monotonic() >= deadline:
            raise Failure(f"timed out after {timeout:g}s waiting for {description}")
        time.sleep(interval)


def retry(operation: Callable[[], T], description: str, attempts: int = 3,
          pause: float = 0.5,
          on: Sequence[type] = (Exception,)) -> T:
    """Call `operation` until it stops raising, re-raising the last failure.

    For an operation that can fail for a reason the next attempt will not see,
    such as a device that was briefly busy. It is not a way to make a real
    failure go away: `attempts` is small, and the original exception is what
    propagates once they are used up.
    """
    if attempts < 1:
        raise Failure(f"retry({description}) needs at least one attempt")
    last: Optional[BaseException] = None
    for attempt in range(attempts):
        try:
            return operation()
        except tuple(on) as exc:
            last = exc
            if attempt + 1 < attempts:
                time.sleep(pause)
    raise Failure(f"{description} failed after {attempts} attempts: {last}") from last
