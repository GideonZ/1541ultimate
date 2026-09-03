#!/usr/bin/env python3
# Soak: asserts the browser gives back the heap it spends tracking file changes.

"""Repeat the filesystem-refresh matrix and require the free heap to come back.

Two leaks lived on this path, and neither is visible over REST -- both need a
browser open on a directory while its contents change underneath:

  1. A removed entry was unlinked from the browser's list and left. A
     BrowsableDirEntry owns a FileInfo, a FileType, its generated FAT name and a
     FileManager path reference, so every file that disappeared under an open
     browser stranded all of it, about 300 bytes and one path handle.

  2. FileManager::get_directory() fills the caller's list with FileInfo copies
     the caller owns. delete_recursive() and fcopy() freed the list and not its
     contents, so a recursively deleted directory stranded one FileInfo per
     entry.

Together they cost 16,656 bytes per run of the e2e suite this one drives.

The suite is run the way the runner runs it, as a subprocess, rather than by
importing it and re-driving its rows: it opens a Menu, a Telnet session and an
FTP connection on the same directory and its setup is what makes those three
agree. Reproducing that here would fork the part most likely to drift.

Method: measure the steady-state slope, never a single before/after. The first
iteration pays one-time costs -- lazy singletons, first-touch filesystem
structures, the browser's own caches -- that never come back and are not leaks,
so those land entirely in the warmup. Each measured block settles before its
closing sample, because an FTP session borrows several KB and returns it
seconds later.

Uses GET /v1/machine:heap. Firmware predating that endpoint answers 404 and
every check here skips, so the suite is safe to run against any image.
"""

import argparse
import subprocess
import sys
from pathlib import Path

# tests/lib holds the shared library; importing bootstrap adds tests/e2e/lib.
# The search walks up rather than counting directories, so this is the same in
# every entry point and a suite that moves needs no edit. See tests/lib/bootstrap.py.
sys.path.insert(0, str(next(p for p in Path(__file__).resolve().parents
                            if (p / "tests" / "lib").is_dir()) / "tests" / "lib"))
import bootstrap  # noqa: E402,F401
import leak  # noqa: E402
import cli  # noqa: E402

import api as api_lib  # noqa: E402  (needs tests/lib on sys.path first)
import rest as rest_lib  # noqa: E402  (needs tests/lib on sys.path first)
from report import (  # noqa: E402
    Failure, check_ok, check_skip, check_start, detail, format_exception,
    section, suite_fail, suite_ok)

SUITE = (Path(__file__).resolve().parents[2]
         / "e2e" / "filemanager" / "browser_filesystem_refresh_test.py")

# One-time costs land here.
WARMUP = 1
# Each iteration is a full matrix run of about two minutes, so three is the most
# a soak run should spend on this. It is enough: what this guards was 16.6 KB
# per run, and the fixed firmware sits within a few dozen bytes of flat.
MEASURED = 3
# Comfortably above the observed noise (28, -8, -24 bytes across three runs on
# the fixed firmware) and far below either leak. The smaller of the two was 696
# bytes per run on its own, so this still fails if only that one comes back.
TOLERANCE_BYTES_PER_OP = 250
SETTLE_SECONDS = 8.0


def run_suite(host: str, password: str, timeout: float) -> None:
    """One full matrix run. A failing run is not a leak verdict, so it stops us."""
    result = subprocess.run(
        [sys.executable, str(SUITE), "-H", host, "-p", password or "", "-t", str(timeout)],
        stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, check=False)
    if result.returncode != 0:
        tail = "\n".join(result.stdout.splitlines()[-15:])
        raise Failure(
            "the filesystem-refresh suite failed, so its heap figure means "
            f"nothing:\n{tail}")


def measure_slope(rest: rest_lib.RestClient, host: str, password: str,
                  timeout: float) -> bool:
    try:
        leak.slope(once=lambda: run_suite(host, password, timeout),
                   heap=api_lib.MachineApi(rest).heap_free,
                   warmup=WARMUP, iterations=MEASURED,
                   tolerance_bytes_per_op=TOLERANCE_BYTES_PER_OP,
                   unit="matrix run", settle_seconds=SETTLE_SECONDS,
                   title="the browser returns the heap it spends tracking "
                         "file changes")
        return True
    except Failure:
        return False


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Assert that repeated filesystem-refresh runs do not consume "
                    "heap. Skips if the firmware has no machine:heap endpoint.")
    cli.add_device_arguments(parser, timeout=30.0, colour=False)
    args = parser.parse_args()

    rest = rest_lib.RestClient(args.host, args.password or None, args.timeout)

    check_start("device exposes GET /v1/machine:heap")
    if api_lib.MachineApi(rest).heap() is None:
        check_skip("firmware predates GET /v1/machine:heap, nothing to measure")
        section("summary")
        detail("skipped: device firmware has no machine:heap endpoint")
        suite_ok("browser_refresh_leak_test")
        return 0
    check_ok()

    ok = measure_slope(rest, args.host, args.password, args.timeout)

    section("summary")
    detail(f"filesystem-refresh slope: {'OK' if ok else 'FAIL'}")
    if ok:
        suite_ok("browser_refresh_leak_test")
        return 0
    suite_fail("browser_refresh_leak_test", "see the summary above")
    return 1


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Failure as exc:
        suite_fail("browser_refresh_leak_test", format_exception(exc))
        raise SystemExit(1)
