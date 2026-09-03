#!/usr/bin/env python3
# Soak: Asserts the PRG browser context-menu load actions return their heap.

"""Repeat a PRG context-menu action and require the free heap to come back.

Measuring over REST cannot see this one: launching a PRG through the API goes
straight to the C64 subsystem, while the browser's Run/Load/DMA entries go
through `FileTypePRG::execute_st` first. That function opened the file to
validate a P00 header and, on every path that then went on to load, never
closed it -- one 4,192-byte FatFs handle per action, reachable only by driving
the actual menu.

Method: measure the steady-state slope, never a single before/after. The first
iteration of anything also pays one-time costs
(lazy singletons, the browser's directory cache, first-touch filesystem
structures) that never come back and are not leaks, so those land entirely in
the warmup. Each iteration also has to settle: a DMA load borrows several KB
that it returns within a few seconds, and sampling before that reads as a leak.

Uses GET /v1/machine:heap. Firmware predating that endpoint answers 404 and
every check here skips, so the suite is safe to run against any image.
"""

import argparse
import os
import sys
import time
from pathlib import Path

# tests/lib holds the reporting rules and the shared REST client; tests/e2e
# holds the browser backend and this fixture's own menu-driving suite, which
# already knows how to seed a PRG and invoke a context-menu action on it.
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "lib"))
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "e2e" / "lib"))
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "e2e" / "api"))
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "e2e" / "filemanager"))

import api as api_lib  # noqa: E402  (needs tests/lib on sys.path first)
import rest as rest_lib  # noqa: E402  (needs tests/lib on sys.path first)
from report import (  # noqa: E402
    Failure, best_effort, check, check_ok, check_skip, check_start, detail, format_exception,
    section, suite_fail, suite_ok)
from ui_backend import add_mode_argument, make_browser  # noqa: E402
from menu_screen_test import RestSession  # noqa: E402
import prg_context_menu_test as ctx  # noqa: E402


# One-time costs land here.
WARMUP = 3
# Enough iterations that a 4 KB-per-action leak is unmistakable against the
# few-KB noise of a machine reset, few enough that the suite stays inside a
# soak run's patience: each iteration resets the C64 and waits for a program.
MEASURED = 10
# A DMA load borrows about 6 KB and returns all of it within five seconds.
SETTLE_SECONDS = 6.0
# The observed noise floor across a dozen menu actions is a few hundred bytes
# each; the leak this guards against is 4,192 bytes every single time.
TOLERANCE_BYTES_PER_OP = 1500


def heap_free(rest: rest_lib.RestClient) -> int:
    return int(api_lib.MachineApi(rest).heap()["free"])


def run_once(machine, fixtures) -> None:
    """One full browser Run: reset, descend to the PRG, pick Run, see it run."""
    ctx.run_action_run(machine, fixtures, ctx.open_plain_prg)
    machine.close_menu()


def measure_slope(rest: rest_lib.RestClient, machine, fixtures) -> bool:
    section("the browser's Run action returns the heap it borrows")

    with check(f"warm up ({WARMUP} runs, one-time costs land here)"):
        for _ in range(WARMUP):
            run_once(machine, fixtures)
        time.sleep(SETTLE_SECONDS)

    measured = {}
    try:
        with check(f"free heap is flat across {MEASURED} more Run actions"):
            before = heap_free(rest)
            for _ in range(MEASURED):
                run_once(machine, fixtures)
            time.sleep(SETTLE_SECONDS)
            after = heap_free(rest)
            consumed = before - after
            measured.update(before=before, after=after, consumed=consumed,
                            per_op=consumed / MEASURED)
            if measured["per_op"] > TOLERANCE_BYTES_PER_OP:
                raise Failure(
                    f"the Run context-menu action leaks about {measured['per_op']:.0f} "
                    f"bytes per invocation ({consumed} bytes over {MEASURED} runs)")
        ok = True
    except Failure:
        ok = False
    if measured:
        detail(f"free before {measured['before']}, after {measured['after']}")
        detail(f"consumed {measured['consumed']} bytes over {MEASURED} runs "
               f"= {measured['per_op']:.0f} bytes/run "
               f"(tolerance {TOLERANCE_BYTES_PER_OP})")
    return ok


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Assert that repeated PRG context-menu actions do not consume "
                    "heap. Skips if the firmware has no machine:heap endpoint.")
    parser.add_argument("-H", "--host", default=os.environ.get("U64_HOST", "u64"))
    parser.add_argument("-P", "--port", type=int, default=23,
                        help="Telnet port, for --mode telnet.")
    parser.add_argument("-p", "--password", default=os.environ.get("U64_PASS"))
    parser.add_argument("-t", "--timeout", type=float,
                        default=float(os.environ.get("U64_TIMEOUT", "30.0")))
    parser.add_argument("--rest-host", default=None,
                        help="Override the REST host when it differs from --host.")
    parser.add_argument("--fixture-token", default=ctx.default_fixture_token(),
                        help="Suffix that makes this run's /Temp fixture names unique.")
    parser.add_argument("--keep-fixtures", action="store_true")
    add_mode_argument(parser)
    args = parser.parse_args()

    rest_host = args.rest_host or args.host
    rest = rest_lib.RestClient(rest_host, args.password or None, args.timeout)

    check_start("device exposes GET /v1/machine:heap")
    if api_lib.MachineApi(rest).heap() is None:
        check_skip("firmware predates GET /v1/machine:heap, nothing to measure")
        section("summary")
        detail("skipped: device firmware has no machine:heap endpoint")
        suite_ok("prg_context_menu_leak_test")
        return 0
    check_ok()

    session = RestSession(rest_host, args.password or None, args.timeout)
    # The same wider Telnet session the e2e context-menu suite needs: at the
    # standard width the menu box's own labels render truncated and the
    # before/after overlay diff cannot read them back.
    browser = make_browser(
        args.mode, rest_host, args.password or None, args.timeout,
        entry_rows=ctx.ENTRY_ROWS, status_row=ctx.STATUS_ROW,
        telnet_host=args.host, telnet_port=args.port, telnet_width=60,
        telnet_entry_rows=ctx.TELNET_ENTRY_ROWS,
        telnet_status_row=ctx.TELNET_STATUS_ROW)
    machine = ctx.Machine(session, browser)
    fixtures = ctx.Fixtures(args.fixture_token)

    try:
        with check(f"seed /Temp with {fixtures.prg}"):
            fixtures.seed(rest_host, args.password)

        ok = measure_slope(rest, machine, fixtures)

        section("summary")
        detail(f"Run context-menu slope: {'OK' if ok else 'FAIL'}")
        if ok:
            suite_ok("prg_context_menu_leak_test")
            return 0
        suite_fail("prg_context_menu_leak_test", "see the summary above")
        return 1
    finally:
        best_effort("close the menu", machine.close_menu)
        best_effort("remove drive a", machine.remove_drive_a)
        if not args.keep_fixtures:
            fixtures.remove(rest_host, args.password)
        best_effort("close the session", machine.close)


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Failure as exc:
        suite_fail("prg_context_menu_leak_test", format_exception(exc))
        raise SystemExit(1)
