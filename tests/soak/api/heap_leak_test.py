#!/usr/bin/env python3
"""Soak: assert REST operations return the heap they borrow.

Every dynamic allocation on this target reaches the FreeRTOS heap, so one
free-bytes figure accounts for all of it. Repeating an operation and watching
that figure is the only way to see a leak from outside the device -- nothing
else in the REST surface exposes it.

Uses GET /v1/machine:heap. Firmware predating that endpoint answers 404 and
every check here skips, so the suite is safe to run against any image.

Method: measure the steady-state slope, not a single before/after. The first
iteration of anything also pays one-time costs -- lazy singletons, caches,
first-touch filesystem structures -- that never come back and are not leaks.
Those land entirely in the warmup, so the slope over the measured iterations
is the per-operation leak alone.
"""

import argparse
import ftplib
import os
import sys
import urllib.parse
from typing import Optional

# tests/lib holds the reporting rules and the one shared REST client.
sys.path.insert(0, os.path.join(
    os.path.dirname(os.path.abspath(__file__)), "..", "..", "lib"))
import rest as rest_lib  # noqa: E402  (needs tests/lib on sys.path first)
import targets  # noqa: E402
from report import (  # noqa: E402
    Failure, check, check_ok, check_skip, check_start, detail, format_exception,
    section, suite_fail, suite_ok)

HEAP_PATH = "/v1/machine:heap"
# A disk image is the cheapest REST call that allocates a large buffer, which
# is what makes it a good canary: create_file_of_size -> write_zeros.
CREATE_PATH = "/v1/files/{dir}/{name}:create_d64"
# Enough iterations that a 16 KB-per-call leak is unmistakable, few enough that
# the suite stays inside a soak run's patience.
WARMUP = 3
MEASURED = 12
# Per-operation slope we are willing to call "no leak". The observed noise
# floor on an idle device is a few hundred bytes across a dozen operations;
# a real leak in this path is thousands of bytes per call.
TOLERANCE_BYTES_PER_OP = 512


class Device:
    """The device under measurement, over the shared REST client.

    Everything HTTP goes through tests/lib/rest.py so this suite inherits the
    one retry policy rather than growing another copy of it.
    """

    def __init__(self, host: str, password: Optional[str], timeout: float) -> None:
        self.host = host
        self.rest = rest_lib.RestClient(host, password, timeout)

    def heap_free(self) -> int:
        data = self.rest.json(HEAP_PATH)
        return int(data["free"])

    def heap_available(self) -> bool:
        return self.rest.status("GET", HEAP_PATH) == 200

    def create_d64(self, directory: str, name: str) -> None:
        path = CREATE_PATH.format(dir=urllib.parse.quote(directory),
                                  name=urllib.parse.quote(name))
        # Idempotent is false: this creates a file, and a blind retry after a
        # sent request would hit FA_CREATE_NEW and fail on the second attempt.
        self.rest.expect("PUT", path, params={"diskname": "LEAK"})


def cleanup(host: str, directory: str, names) -> int:
    """Remove what the measurement created. FTP, because the REST surface has
    no delete verb for files."""
    removed = 0
    try:
        ftp = ftplib.FTP()
        # The device's own FTP server, so a cartridge target means the
        # cartridge; see tests/lib/targets.py.
        ftp.connect(targets.device_of(host), 21, timeout=20)
        ftp.login()
        ftp.cwd(f"/{directory}")
        for n in names:
            try:
                ftp.delete(n)
                removed += 1
            except ftplib.all_errors:
                pass
        ftp.quit()
    except ftplib.all_errors:
        pass
    return removed


def run_create_d64_slope(dev: Device, directory: str) -> bool:
    """Repeat create_d64 and require the heap to come back each time."""
    section("create_d64 returns the heap it borrows")
    created = []
    try:
        with check(f"warm up ({WARMUP} images, one-time costs land here)"):
            for i in range(WARMUP):
                name = f"leakw{i}.d64"
                dev.create_d64(directory, name)
                created.append(name)

        measured = {}
        try:
            with check(f"free heap is flat across {MEASURED} more images"):
                before = dev.heap_free()
                for i in range(MEASURED):
                    name = f"leakm{i}.d64"
                    dev.create_d64(directory, name)
                    created.append(name)
                after = dev.heap_free()
                consumed = before - after
                measured.update(before=before, after=after, consumed=consumed,
                                per_op=consumed / MEASURED)
                if measured["per_op"] > TOLERANCE_BYTES_PER_OP:
                    raise Failure(
                        f"create_d64 leaks about {measured['per_op']:.0f} bytes per "
                        f"call ({consumed} bytes over {MEASURED} calls)")
            ok = True
        except Failure:
            ok = False
        if measured:
            detail(f"free before {measured['before']}, after {measured['after']}")
            detail(f"consumed {measured['consumed']} bytes over {MEASURED} images "
                   f"= {measured['per_op']:.0f} bytes/image "
                   f"(tolerance {TOLERANCE_BYTES_PER_OP})")
        return ok
    finally:
        removed = cleanup(dev.host, directory, created)
        detail(f"removed {removed} of {len(created)} images created by this suite")


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Assert that repeated REST operations do not consume heap. "
                    "Skips if the firmware has no machine:heap endpoint.")
    parser.add_argument("-H", "--host", default=os.environ.get("U64_HOST", "u64"))
    parser.add_argument("-p", "--password", default=os.environ.get("U64_PASS"))
    parser.add_argument("-t", "--timeout", type=float,
                        default=float(os.environ.get("U64_TIMEOUT", "30.0")))
    parser.add_argument("-d", "--directory", default="Temp",
                        help="Writable device directory to create images in.")
    args = parser.parse_args()

    dev = Device(args.host, args.password, args.timeout)

    check_start("device exposes GET /v1/machine:heap")
    if not dev.heap_available():
        check_skip("firmware predates GET /v1/machine:heap, nothing to measure")
        section("summary")
        detail("skipped: device firmware has no machine:heap endpoint")
        suite_ok("heap_leak_test")
        return 0
    check_ok()

    ok = run_create_d64_slope(dev, args.directory)

    section("summary")
    detail(f"create_d64 slope: {'OK' if ok else 'FAIL'}")
    if ok:
        suite_ok("heap_leak_test")
        return 0
    suite_fail("heap_leak_test", "see the summary above")
    return 1


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Failure as exc:
        suite_fail("heap_leak_test", format_exception(exc))
        raise SystemExit(1)
