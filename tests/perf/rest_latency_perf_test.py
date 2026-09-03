#!/usr/bin/env python3
# PERF: what a single REST call costs, per route and per network path.

"""Time the REST calls the runner makes between suites, one route at a time.

The runner spends real time before each suite on the health sweep and the
UI-state gate, and both are made of REST calls. Whether that time is the
network, the firmware, or a wait the harness chose is not visible from a run
log, so this measures each route on its own.

Two things it separates that a run cannot:

- *Route cost.* A read of `/v1/info` and a read of `/v1/machine:menu_screen`
  are both "one REST call" and do not cost the same, because one answers from
  memory and the other copies the whole character matrix with interrupts off.
- *Network path.* An Ultimate 64 answers on both its wired and its wireless
  address, and the Ultimate II+L has only wireless, so a figure measured on one
  path says nothing about the other until both are measured.

Give it an address rather than a name, so which interface is being measured is
explicit:

    tests/perf/rest_latency_perf_test.py -H 192.168.1.15    # u64, wired
    tests/perf/rest_latency_perf_test.py -H 192.168.1.71    # u64, wireless
"""
import argparse
import os
import statistics
import sys
import time
from collections.abc import Callable
from pathlib import Path

# tests/lib holds the shared library; importing bootstrap adds tests/e2e/lib.
# The search walks up rather than counting directories, so this is the same in
# every entry point and a suite that moves needs no edit. See tests/lib/bootstrap.py.
sys.path.insert(0, str(next(p for p in Path(__file__).resolve().parents
                            if (p / "tests" / "lib").is_dir()) / "tests" / "lib"))
import bootstrap  # noqa: E402,F401

import health as health_lib  # noqa: E402
from api import UltimateApi  # noqa: E402
from report import (  # noqa: E402
    Failure, add_colour_argument, apply_colour, check_ok, check_start, detail,
    section, suite_fail, suite_ok)

# Enough samples that the median is not one lucky packet, few enough that the
# whole sweep stays under a minute on a wireless path.
SAMPLES = 15


def timed(call: Callable[[], object], samples: int) -> tuple[list[float], object]:
    """Milliseconds per call, and whatever the last call returned."""
    timings: list[float] = []
    answer: object = None
    for _ in range(samples):
        started = time.perf_counter()
        answer = call()
        timings.append((time.perf_counter() - started) * 1000.0)
    return timings, answer


def report_route(label: str, timings: list[float], extra: str = "") -> None:
    detail(f"{label:<34} median {statistics.median(timings):6.1f}ms  "
           f"min {min(timings):6.1f}  max {max(timings):6.1f}"
           + (f"  {extra}" if extra else ""))


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("-H", "--host", default=os.environ.get("U64_HOST", "u64"),
                        help="address or name of the interface to measure")
    parser.add_argument("-p", "--password", default=os.environ.get("U64_PASS", ""))
    parser.add_argument("-t", "--timeout", type=float, default=15.0)
    parser.add_argument("-n", "--samples", type=int, default=SAMPLES)
    add_colour_argument(parser)
    args = parser.parse_args()
    apply_colour(args.color)

    api = UltimateApi(args.host, args.password or None, args.timeout)
    try:
        section(f"1. One REST call, by route ({args.host})")
        with_menu_closed = [
            ("GET /v1/info", api.info),
            ("GET /v1/version", lambda: api.rest.request("GET", "/v1/version")),
            ("GET /v1/configs", api.configs.category_names),
            ("GET machine:readmem 1 byte",
             lambda: api.machine.readmem(0x00A2, 1)),
            ("GET machine:readmem 256 bytes",
             lambda: api.machine.readmem(0x0000, 256)),
        ]
        for label, call in with_menu_closed:
            check_start(label)
            timings, _ = timed(call, args.samples)
            check_ok(f"median {statistics.median(timings):.1f}ms")
            report_route(label, timings)

        section("2. The routes the UI-state gate uses")
        # menu_screen copies the whole character matrix with interrupts
        # disabled (UserInterface::copy_active_screen_matrix), so it is not
        # the same cost as a read that answers from memory.
        check_start("PUT machine:menu_button, open")
        timings, _ = timed(
            lambda: api.rest.request("PUT", "/v1/machine:menu_button"), 1)
        check_ok(f"{timings[0]:.1f}ms")
        time.sleep(0.5)
        check_start("GET machine:menu_screen, menu open")
        screen, _ = timed(
            lambda: api.rest.request("GET", "/v1/machine:menu_screen"),
            args.samples)
        check_ok(f"median {statistics.median(screen):.1f}ms")
        report_route("GET machine:menu_screen", screen)
        check_start("PUT machine:menu_button, close")
        closing, _ = timed(
            lambda: api.rest.request("PUT", "/v1/machine:menu_button"), 1)
        check_ok(f"{closing[0]:.1f}ms")
        time.sleep(0.5)

        section("3. How soon a menu toggle is visible")
        # The gate sleeps a fixed settle after every toggle. What that has to
        # cover is the time between the request returning and the screen route
        # answering differently, which is what this measures.
        delays: list[float] = []
        for _ in range(5):
            for wanted_open in (True, False):
                api.rest.request("PUT", "/v1/machine:menu_button")
                started = time.perf_counter()
                deadline = started + 5.0
                while time.perf_counter() < deadline:
                    status, _, _ = api.rest.request(
                        "GET", "/v1/machine:menu_screen")
                    if (status == 200) == wanted_open:
                        delays.append((time.perf_counter() - started) * 1000.0)
                        break
                else:
                    raise Failure("the menu never reached the state the toggle "
                                  "asked for within 5s")
        check_start("a toggle becomes visible")
        check_ok(f"median {statistics.median(delays):.0f}ms")
        report_route("toggle visible after", delays)

        section("4. One health sweep")
        check_start("health.probe")
        timings, sweep = timed(
            lambda: health_lib.probe(args.host, args.password), 3)
        check_ok(f"median {statistics.median(timings):.0f}ms")
        report_route("health.probe", timings)
        for entry in sweep.checks:
            detail(f"  {entry.name:<10} {entry.state:<5} {entry.ms:7.1f}ms")
    except Failure as exc:
        suite_fail("rest_latency_perf_test", str(exc))
        return 1

    suite_ok("rest_latency_perf_test")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
