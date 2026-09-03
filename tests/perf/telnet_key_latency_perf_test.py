#!/usr/bin/env python3
"""Measure how long a Telnet keystroke takes to show up on the device's screen.

One figure per key kind: a printable character arrives on the byte that carries
it, an arrow key on the third of three, and a lone ESC only after the gap that
tells it from a sequence. The clock runs from the socket write to the screen.

    python3 tests/perf/telnet_key_latency_perf_test.py -H u2@c64u
    python3 tests/perf/telnet_key_latency_perf_test.py -H c64u --samples 30
"""
from __future__ import annotations

import argparse
import os
import statistics
import sys
import time
from collections.abc import Callable

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "e2e", "lib"))
sys.path.insert(0, os.path.join(HERE, "..", "lib"))
sys.path.insert(0, os.path.join(HERE, "..", "e2e", "monitor"))

import targets  # noqa: E402
from report import (Failure, check, check_ok, detail,  # noqa: E402
                    suite_ok)
from ui_backend import make_backend  # noqa: E402
import monitor_test as mt  # noqa: E402

# What a key is allowed to cost, socket write to screen. Deliberately generous:
# the point is to catch a regression that makes the monitor feel slow, not to
# pin the current figure.
BUDGET_PRINTABLE_MS = 700.0
BUDGET_CURSOR_MS = 700.0
# ESC pays VT100_ESCAPE_ALONE_MS plus one Telnet receive timeout on top of the
# redraw the others pay, so its budget is larger. A regression that made every
# key wait for that gap fails the two budgets above.
BUDGET_ESC_MS = 1400.0


SUITE = "telnet_key_latency_perf_test"


def _percentile(values: list[float], fraction: float) -> float:
    ordered = sorted(values)
    index = min(len(ordered) - 1, round(fraction * (len(ordered) - 1)))
    return ordered[index]


def measure(session, backend, send: Callable[[], None],
            settled: Callable[[], bool], samples: int,
            settle_first: Callable[[], None] | None = None) -> list[float]:
    """Time `send` until `settled` reports the screen caught up, `samples` times."""
    timings: list[float] = []
    for _ in range(samples):
        if settle_first:
            settle_first()
        deadline = time.monotonic() + 5.0
        while not settled() and time.monotonic() < deadline:
            time.sleep(0.02)
        started = time.monotonic()
        send()
        while time.monotonic() - started < 5.0:
            if settled():
                timings.append((time.monotonic() - started) * 1000.0)
                break
            time.sleep(0.01)
        else:
            raise Failure("the screen never showed the keystroke within 5s")
    return timings


def summarise(name: str, timings: list[float], budget_ms: float) -> bool:
    median = statistics.median(timings)
    worst = max(timings)
    p90 = _percentile(timings, 0.90)
    detail(f"{name}: median {median:.0f}ms, p90 {p90:.0f}ms, worst {worst:.0f}ms, "
           f"over {len(timings)} samples (budget {budget_ms:.0f}ms on the median)")
    return median <= budget_ms


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("-H", "--host", default=os.environ.get("U64_HOST", "u64"))
    parser.add_argument("-p", "--password", default=os.environ.get("U64_PASS", ""))
    parser.add_argument("-P", "--telnet-port", type=int, default=23)
    parser.add_argument("-t", "--timeout", type=float, default=30.0)
    parser.add_argument("--samples", type=int, default=15)
    args = parser.parse_args()

    target = targets.parse(args.host)
    device_host = target.device
    backend = make_backend("telnet", target.token, args.password, args.timeout,
                           telnet_host=device_host, telnet_port=args.telnet_port)
    session = mt.MonitorSession(backend)
    session.enter_monitor()
    mt.ensure_view(session, "HEX ")

    def header_addr() -> str | None:
        try:
            return mt.monitor_header_address(session.capture())
        except Failure:
            return None

    with check("a printable key reaches the screen inside its budget"):
        # The Jump prompt echoes each character, so one keystroke is one
        # visible change with no memory access behind it.
        session.send_char("J")
        mt.wait_for_prompt(session, "Jump AAAA")
        state = {"want": ""}

        def send_one() -> None:
            state["want"] += "C"
            backend._send(b"C", "latency C")  # noqa: SLF001

        def shown() -> bool:
            try:
                return mt.prompt_field(session.capture(), "Jump AAAA").endswith(state["want"][-1:])
            except Failure:
                return False

        timings = []
        for _ in range(args.samples):
            state["want"] = ""
            session.send_key("BACKSPACE")
            time.sleep(0.05)
            started = time.monotonic()
            send_one()
            deadline = time.monotonic() + 5.0
            while time.monotonic() < deadline:
                if shown():
                    timings.append((time.monotonic() - started) * 1000.0)
                    break
                time.sleep(0.01)
            else:
                raise Failure("a printable key never reached the screen within 5s")
        session.send_key("ARROW_LEFT")
        mt.wait_for_monitor(session, "leaving the Jump prompt")
        if summarise("printable", timings, BUDGET_PRINTABLE_MS):
            check_ok(f"median {statistics.median(timings):.0f}ms")
        else:
            raise Failure(f"a printable key took a median "
                          f"{statistics.median(timings):.0f}ms, over the "
                          f"{BUDGET_PRINTABLE_MS:.0f}ms budget")

    with check("a cursor key reaches the screen inside its budget"):
        # Three bytes, delivered on the third, and it moves the view so the
        # change is visible in the header address.
        timings = []
        for _ in range(args.samples):
            before = header_addr()
            started = time.monotonic()
            backend._send(b"\x1b[B", "latency DOWN")  # noqa: SLF001
            deadline = time.monotonic() + 5.0
            while time.monotonic() < deadline:
                if header_addr() not in (None, before):
                    timings.append((time.monotonic() - started) * 1000.0)
                    break
                time.sleep(0.01)
            else:
                raise Failure("a cursor key never moved the view within 5s")
        if summarise("cursor", timings, BUDGET_CURSOR_MS):
            check_ok(f"median {statistics.median(timings):.0f}ms")
        else:
            raise Failure(f"a cursor key took a median "
                          f"{statistics.median(timings):.0f}ms, over the "
                          f"{BUDGET_CURSOR_MS:.0f}ms budget")

    with check("a lone ESC still arrives, and inside its own budget"):
        # ESC waits out the gap that tells it apart from a sequence, so it is
        # slower by design; what matters is that it arrives at all.
        timings = []
        for _ in range(max(4, args.samples // 3)):
            session.send_char("J")
            mt.wait_for_prompt(session, "Jump AAAA")
            started = time.monotonic()
            backend._send(b"\x1b", "latency ESC")  # noqa: SLF001
            deadline = time.monotonic() + 5.0
            while time.monotonic() < deadline:
                try:
                    if "Jump AAAA" not in session.capture().text():
                        timings.append((time.monotonic() - started) * 1000.0)
                        break
                except Failure:
                    pass
                time.sleep(0.01)
            else:
                raise Failure("a lone ESC never closed the prompt within 5s, so "
                              "it was not delivered as a key at all")
            mt.wait_for_monitor(session, "after ESC closed the prompt")
        if summarise("lone ESC", timings, BUDGET_ESC_MS):
            check_ok(f"median {statistics.median(timings):.0f}ms")
        else:
            raise Failure(f"a lone ESC took a median "
                          f"{statistics.median(timings):.0f}ms, over the "
                          f"{BUDGET_ESC_MS:.0f}ms budget")

    suite_ok(SUITE)
    return 0


if __name__ == "__main__":
    sys.exit(main())
