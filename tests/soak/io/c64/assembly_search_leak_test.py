#!/usr/bin/env python3
# Soak: asserts the Assembly 64 search returns the heap it borrows.

"""Repeat the search browser's two costly paths and require the free heap back.

Two independent leaks lived here, and neither is visible over REST: the query
path is only reachable by driving the menu.

  1. Opening the browser. TreeBrowser's constructor makes a plain
     TreeBrowserState, and AssemblySearch assigns over it with a form state of
     its own, orphaning 60 bytes per open. That bug is not specific to this
     browser -- ConfigBrowser and FormUI replace the same state the same way,
     so every config menu open paid it too. The search form is what this suite
     can drive in a tight loop, so it is what guards the fix.

  2. Running a query and stepping into a result. Each HTTP response is
     collected into a 16,392-byte t_BufferedBody, which the JSON tree copies
     out of and nobody then owned on the entries path; the results object and
     its entries had no owner either.

Method: measure the steady-state slope, never a single before/after. The first
iteration of anything pays one-time costs -- the preset list this browser
fetches once and caches for the life of the box, lazy singletons -- that never
come back and are not leaks, so those land entirely in the warmup. Each
measured block settles before its closing sample: an FTP or HTTP session
borrows several KB and returns it seconds later, and sampling immediately reads
that as a leak.

The open/close slope needs many iterations because what it guards is small: 60
bytes against a noise floor of a few dozen. The query slope needs few, because
a single leaked response body is 16 KB -- and each iteration is a request to a
third-party service, which is not something to make a habit of.

Uses GET /v1/machine:heap. Firmware predating that endpoint answers 404 and
every check here skips, so the suite is safe to run against any image. It also
talks to the live Assembly 64 service through the device: when that is
unreachable the form never opens, and the suite skips rather than reporting a
third-party outage as a firmware defect.
"""

import argparse
import os
import sys
import time
from pathlib import Path

# tests/lib holds the reporting rules and the shared REST client; tests/e2e
# holds the UI backend and this fixture's own e2e suite, which already knows
# how to open the form, fill a field and submit a query.
sys.path.insert(0, str(Path(__file__).resolve().parents[3] / "lib"))
sys.path.insert(0, str(Path(__file__).resolve().parents[3] / "e2e" / "lib"))
sys.path.insert(0, str(Path(__file__).resolve().parents[3] / "e2e" / "io" / "c64"))

import api as api_lib  # noqa: E402  (needs tests/lib on sys.path first)
import rest as rest_lib  # noqa: E402  (needs tests/lib on sys.path first)
from report import (  # noqa: E402
    Failure, best_effort, check, check_ok, check_skip, check_start, detail, format_exception,
    section, suite_fail, suite_ok, suite_skip)
from menu import wait_until  # noqa: E402
from ui_backend import add_mode_argument, make_backend  # noqa: E402
import assembly64_test as a64  # noqa: E402


# One-time costs land here. The very first open also fetches the preset list,
# which is cached for the life of the box and must not be counted.
OPEN_WARMUP = 3
# 40 opens put a 60-byte-per-open leak at 2,400 bytes, well clear of the few
# dozen bytes of allocator noise seen across a whole suite run.
OPEN_MEASURED = 40
OPEN_TOLERANCE_BYTES_PER_OP = 25

# One warmup is enough: nothing about a query is cached, and every iteration
# costs the Assembly 64 service two requests.
QUERY_WARMUP = 1
QUERY_MEASURED = 5
# A leaked response body alone is 16,392 bytes. This tolerance is loose on
# purpose: what it has to catch is thousands of bytes per query, and a result
# list whose size depends on what the service returns is not worth tightening
# against.
QUERY_TOLERANCE_BYTES_PER_OP = 2000

SETTLE_SECONDS = 6.0


def heap_free(rest: rest_lib.RestClient) -> int:
    return int(api_lib.MachineApi(rest).heap()["free"])


def open_and_leave(device) -> None:
    """One full open and close of the search browser."""
    a64.open_query_form(device)
    a64.leave_form(device)


def query_and_open_entry(device) -> bool:
    """One query, then step into the first result. True if an entry was opened.

    Stepping in is the part that calls request_entries(), so a run where the
    service returns nothing to step into still exercises the query itself but
    not the entries path. That is reported rather than failed: an empty corpus
    is the service's business, not the firmware's.
    """
    a64.open_query_form(device)
    a64.enter_field(device, a64.NAME_FIELD)
    device.type_text(a64.SEARCH_TERM)
    device.send_key("ENTER")
    a64.submit_query(device)

    opened = False
    if wait_until(lambda: device.menu_is_open() and not device.form_visible(),
                  a64.QUERY_TIMEOUT):
        rows = device.rows() or []
        if any(a64.SEARCH_TERM in text.lower() for text in rows):
            before = device.screen()
            device.send_key("ENTER")
            # The entry list is fetched over the network, so the press is given
            # time to produce it. Leaving before it lands would measure a fetch
            # still in flight.
            if before is not None:
                wait_until(lambda: device.screen_changed(before), a64.QUERY_TIMEOUT)
            opened = True

    a64.recover(device, "after a query")
    return opened


def measure(rest: rest_lib.RestClient, title: str, once, warmup: int,
            measured: int, tolerance: int, unit: str) -> bool:
    """Warm up, then require the free heap to be flat over `measured` runs."""
    section(title)

    with check(f"warm up ({warmup} {unit}, one-time costs land here)"):
        for _ in range(warmup):
            once()
        time.sleep(SETTLE_SECONDS)

    seen = {}
    try:
        with check(f"free heap is flat across {measured} more {unit}"):
            before = heap_free(rest)
            for _ in range(measured):
                once()
            time.sleep(SETTLE_SECONDS)
            after = heap_free(rest)
            consumed = before - after
            seen.update(before=before, after=after, consumed=consumed,
                        per_op=consumed / measured)
            if seen["per_op"] > tolerance:
                raise Failure(
                    f"{title} leaks about {seen['per_op']:.0f} bytes per "
                    f"iteration ({consumed} bytes over {measured})")
        ok = True
    except Failure:
        ok = False
    if seen:
        detail(f"free before {seen['before']}, after {seen['after']}")
        detail(f"consumed {seen['consumed']} bytes over {measured} {unit} "
               f"= {seen['per_op']:.0f} bytes each (tolerance {tolerance})")
    return ok


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Assert that repeated Assembly 64 searches do not consume "
                    "heap. Skips if the firmware has no machine:heap endpoint, "
                    "or if the Assembly 64 service is unreachable.")
    parser.add_argument("-H", "--host", default=os.environ.get("U64_HOST", "u64"))
    parser.add_argument("-p", "--password", default=os.environ.get("U64_PASS"))
    parser.add_argument("-t", "--timeout", type=float,
                        default=float(os.environ.get("U64_TIMEOUT", "30.0")))
    parser.add_argument("--telnet-port", type=int,
                        default=int(os.environ.get("U64_TELNET_PORT", "23")))
    add_mode_argument(parser, default=os.environ.get("U64_MODE", "overlay"))
    args = parser.parse_args()

    password = args.password or None
    rest = rest_lib.RestClient(args.host, password, args.timeout)

    check_start("device exposes GET /v1/machine:heap")
    if api_lib.MachineApi(rest).heap() is None:
        check_skip("firmware predates GET /v1/machine:heap, nothing to measure")
        section("summary")
        detail("skipped: device firmware has no machine:heap endpoint")
        suite_ok("assembly_search_leak_test")
        return 0
    check_ok()

    # The same wider Telnet session the e2e suite needs: at the standard width
    # the form box's own title renders truncated.
    backend = make_backend(args.mode, args.host, password, args.timeout,
                           telnet_port=args.telnet_port, telnet_width=60)
    device = a64.Device(backend, args.mode, args.host, password, args.timeout)

    entries_opened = 0

    def one_query() -> None:
        nonlocal entries_opened
        if query_and_open_entry(device):
            entries_opened += 1

    try:
        opens_ok = measure(rest, "opening and closing the search browser",
                           lambda: open_and_leave(device), OPEN_WARMUP,
                           OPEN_MEASURED, OPEN_TOLERANCE_BYTES_PER_OP, "opens")
        query_ok = measure(rest, "running a query and opening a result",
                           one_query, QUERY_WARMUP, QUERY_MEASURED,
                           QUERY_TOLERANCE_BYTES_PER_OP, "queries")

        section("summary")
        detail(f"open/close slope: {'OK' if opens_ok else 'FAIL'}")
        detail(f"query slope: {'OK' if query_ok else 'FAIL'}")
        detail(f"stepped into a result on {entries_opened} of "
               f"{QUERY_WARMUP + QUERY_MEASURED} queries")
        if entries_opened == 0:
            detail("the service returned no matches, so the entries path was "
                   "never exercised")
        if opens_ok and query_ok:
            suite_ok("assembly_search_leak_test")
            return 0
        suite_fail("assembly_search_leak_test", "see the summary above")
        return 1
    except a64.Skip as exc:
        suite_skip("assembly_search_leak_test", str(exc))
        return 0
    finally:
        best_effort("put the Assembly 64 UI back",
                    lambda: a64.recover(device, "tearing down"))
        backend.close()


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Failure as exc:
        suite_fail("assembly_search_leak_test", format_exception(exc))
        raise SystemExit(1)
