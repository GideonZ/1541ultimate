"""Waiting for the device to reach a state, and retrying what may not stick.

For polling the device: a screen to settle, a file to appear, a drive to
register a mount.

Two rules are built in rather than left to each caller:

- A wait states what it was waiting for. A timeout that says only "timed out"
  costs a rerun to find out what it was watching.
- A wait is bounded. There is no helper here that can block forever, because a
  suite that hangs takes the whole run's device with it.
"""

from __future__ import annotations

import time
from typing import Callable, Optional, Sequence, TypeVar

from report import Failure

T = TypeVar("T")

DEFAULT_INTERVAL_SECONDS = 0.25


def wait_until(predicate: Callable[[], bool], description: str,
               timeout: float, interval: float = DEFAULT_INTERVAL_SECONDS,
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
             timeout: float, interval: float = DEFAULT_INTERVAL_SECONDS) -> T:
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
