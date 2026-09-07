#!/usr/bin/env python3
# The assertions the device-free self-tests share.

"""What a self-test says when a value is not the one it wanted.

Four modules had defined the same three-line `expect` (`navigation_test.py`,
`observability_test.py`, `runner_policy_test.py`, `telnet_drain_test.py`).
They are here so that a message reads the same wherever it comes from, and so
that a case moved between those files does not have to bring its asserter.

These are for the self-tests, which run on loopback and check the harness. A
suite driving a device raises `report.Failure` with a sentence about the
device instead; there is nothing to compare against a literal there.
"""

from __future__ import annotations

from report import Failure


def expect(label: str, actual: object, wanted: object) -> None:
    """Require `actual` to equal `wanted`, naming both when it does not."""
    if actual != wanted:
        raise Failure(f"{label}: got {actual!r}, expected {wanted!r}")


def expect_near(label: str, actual: float, wanted: float,
                tolerance: float) -> None:
    """Require `actual` to be within `tolerance` of `wanted`.

    For a measured duration, where equality is not a question that can be
    asked: the message gives both values and the tolerance, so a reader can
    tell a slow machine from a wrong answer.
    """
    if abs(actual - wanted) > tolerance:
        raise Failure(f"{label}: got {actual:.3f}, expected {wanted:.3f} "
                      f"give or take {tolerance:.3f}")
