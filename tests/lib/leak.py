#!/usr/bin/env python3
# The steady-state heap slope, measured the one way the soak suites agree on.

"""One implementation of the leak measurement five soak suites describe.

Each of them said the same thing in its own module docstring: measure the slope
over repeated operations after a warm-up, never a single before/after. A first
run legitimately allocates one-time caches and lazy singletons that never come
back, and comparing one cold run against a warm one reads those as a leak.

The method is one; the numbers are not. Each suite keeps its own tolerance, its
own warm-up count and its own operation, because those come from what that
suite does and what was measured on the device. What is shared is the shape:
warm up, read the heap, repeat the operation a fixed number of times, let it
settle, read the heap again, and divide.

    measured = leak.slope(once=refresh_the_browser, heap=api.machine.heap_free,
                          warmup=1, iterations=20,
                          tolerance_bytes_per_op=250, unit="matrix run")

`slope` raises `Failure` above the tolerance and returns the measurement
otherwise; either way the caller has the numbers to report.
"""

from __future__ import annotations

import time
from collections.abc import Callable
from dataclasses import dataclass

from report import Failure, check, detail, section


@dataclass(frozen=True)
class Slope:
    """What one measurement saw."""

    before: int
    after: int
    iterations: int
    unit: str
    units: str

    @property
    def consumed(self) -> int:
        """Bytes the run did not give back. Negative means the heap grew."""
        return self.before - self.after

    @property
    def per_op(self) -> float:
        return self.consumed / self.iterations if self.iterations else 0.0

    def one_line(self, tolerance: float) -> str:
        return (f"consumed {self.consumed} bytes over {self.iterations} "
                f"{self.units} = {self.per_op:.0f} bytes each "
                f"(tolerance {tolerance:.0f})")


def slope(once: Callable[[], None], heap: Callable[[], int], *,
          warmup: int, iterations: int, tolerance_bytes_per_op: float,
          unit: str, units: str = "", settle_seconds: float = 0.0,
          title: str | None = None) -> Slope:
    """Measure the steady-state cost of `once`, and fail above the tolerance.

    `settle_seconds` is a pause before each heap reading, for a suite whose
    operation frees asynchronously: mount_cache_leak_test.py measured the
    device returning the mount cache a few seconds after the call answered, so
    sampling immediately read that as a leak.
    """
    units = units or unit + "s"
    if title:
        section(title)

    with check(f"warm up ({warmup} {units}, one-time costs land here)"):
        for _ in range(warmup):
            once()
        if settle_seconds:
            time.sleep(settle_seconds)

    measured: Slope | None = None
    try:
        with check(f"free heap is flat across {iterations} more {units}"):
            before = heap()
            for _ in range(iterations):
                once()
            if settle_seconds:
                time.sleep(settle_seconds)
            measured = Slope(before=before, after=heap(), iterations=iterations,
                             unit=unit, units=units)
            if measured.per_op > tolerance_bytes_per_op:
                raise Failure(
                    f"about {measured.per_op:.0f} bytes leak per {unit} "
                    f"({measured.consumed} bytes over {iterations})")
    finally:
        if measured is not None:
            detail(f"free before {measured.before}, after {measured.after}")
            detail(measured.one_line(tolerance_bytes_per_op))
    return measured
