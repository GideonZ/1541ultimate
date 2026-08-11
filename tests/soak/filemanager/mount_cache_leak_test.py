#!/usr/bin/env python3
# Soak: Asserts that browsing many disk images does not consume heap.

"""Enter one disk image after another and require the heap to come back.

Looking inside a disk image mounts it, and the mount holds the image file
open. The mount is a cache -- entering the same image twice costs nothing --
but until it was bounded, nothing released one until the media went away, so
browsing a collection of images consumed about 5 KB per distinct image and
never gave any of it back. On an 8 MB heap a few hundred images is real money.

The trigger is deliberately not the browser: listing a directory inside an
image over FTP goes through the same FileManager path (vfs_opendir ->
get_directory -> find_pathentry -> find_mount_point), so this measures the
mount cache without depending on screen scraping or menu navigation.

Two things are asserted, because a fix that simply stopped caching would pass
the first one and make the firmware slower:

  1. entering many distinct images has a flat heap slope
  2. re-entering an image that is still mounted costs nothing

Uses GET /v1/machine:heap. Firmware predating that endpoint answers 404 and
every check here skips, so the suite is safe to run against any image.
"""

import argparse
import os
import sys
import time
from pathlib import Path

# tests/lib holds the reporting rules and the shared REST client; the D64
# builder lives with the suite that already needed one.
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "lib"))
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "e2e" / "filemanager"))

import ftp as ftp_lib  # noqa: E402
import rest as rest_lib  # noqa: E402
from report import (  # noqa: E402
    Failure, check, check_ok, check_skip, check_start, detail, format_exception,
    section, suite_fail, suite_ok)
from prg_context_menu_test import build_d64, default_fixture_token, PRG_BYTES  # noqa: E402

HEAP_PATH = "/v1/machine:heap"
TEMP = "/Temp/"
# Every run needs image names it has never used before. A mount is keyed by
# path, so recreating a file the previous run deleted would be matched against
# that run's stale mount and allocate nothing -- the suite would pass on
# firmware that leaks, having measured a cache hit.
PREFIX = "mountleak"

# The cache holds eight mounts. Entering appreciably more than that is what
# separates "bounded" from "grows for ever": the first eight are the working
# set and are expected to cost their ~5 KB, everything after them must be free.
WARMUP = 10
MEASURED = 20
# A mount is about 5 KB. Anything approaching that per image means the cache is
# still growing; a few hundred bytes is the ordinary noise of a REST round trip.
TOLERANCE_BYTES_PER_IMAGE = 512
# An FTP session borrows several KB and returns it shortly after it closes, so
# every heap figure here is taken after a settle. Sampling without one reads
# that borrowing as a leak: measured at 7568 bytes still outstanding
# immediately after twenty listings, and zero a few seconds later.
SETTLE_SECONDS = 6.0
# Re-entering images that are already mounted must not mount them again. The
# bar is deliberately well under what one fresh mount costs, so remounting even
# a single image fails this, while ordinary session noise does not.
TOLERANCE_REENTRY_BYTES = 2048


def heap_free(rest) -> int:
    """Free heap, after letting transient allocations come back."""
    time.sleep(SETTLE_SECONDS)
    return int(rest.json(HEAP_PATH)["free"])


TOKEN = default_fixture_token()


def image_name(index: int) -> str:
    return f"{PREFIX}{TOKEN}_{index}.d64"


def seed_and_enter(host: str, password: str, indices) -> None:
    """Create each image and look inside it, which is what mounts it."""
    with ftp_lib.session(host, password, timeout=60) as ftp:
        for i in indices:
            name = image_name(i)
            ftp_lib.store(ftp, TEMP + name, build_d64(f"DISK{i:03d}", "HELLO", PRG_BYTES))
            ftp_lib.names(ftp, TEMP + name + "/")


def re_enter(host: str, password: str, indices, times: int) -> None:
    with ftp_lib.session(host, password, timeout=60) as ftp:
        for _ in range(times):
            for i in indices:
                ftp_lib.names(ftp, TEMP + image_name(i) + "/")


def cleanup(host: str, password: str, indices) -> int:
    removed = 0
    try:
        with ftp_lib.session(host, password, timeout=60) as ftp:
            for i in indices:
                try:
                    ftp_lib.delete_quietly(ftp, TEMP + image_name(i))
                    removed += 1
                except Exception:
                    pass
    except Exception:
        pass
    return removed


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Assert that browsing many disk images does not consume heap. "
                    "Skips if the firmware has no machine:heap endpoint.")
    parser.add_argument("-H", "--host", default=os.environ.get("U64_HOST", "u64"))
    parser.add_argument("-p", "--password", default=os.environ.get("U64_PASS"))
    parser.add_argument("-t", "--timeout", type=float,
                        default=float(os.environ.get("U64_TIMEOUT", "30.0")))
    args = parser.parse_args()

    rest = rest_lib.RestClient(args.host, args.password or None, args.timeout)
    password = args.password or ""

    check_start("device exposes GET /v1/machine:heap")
    if rest.status("GET", HEAP_PATH) != 200:
        check_skip("firmware predates GET /v1/machine:heap, nothing to measure")
        section("summary")
        detail("skipped: device firmware has no machine:heap endpoint")
        suite_ok("mount_cache_leak_test")
        return 0
    check_ok()

    warm = range(WARMUP)
    measured = range(WARMUP, WARMUP + MEASURED)
    everything = range(WARMUP + MEASURED)
    ok = True
    stats = {}

    try:
        section("entering one image after another")
        with check(f"fill the mount cache ({WARMUP} images, the working set is paid for here)"):
            seed_and_enter(args.host, password, warm)

        try:
            with check(f"free heap is flat across {MEASURED} further images"):
                before = heap_free(rest)
                seed_and_enter(args.host, password, measured)
                after = heap_free(rest)
                consumed = before - after
                stats.update(before=before, after=after, consumed=consumed,
                             per_image=consumed / MEASURED)
                if stats["per_image"] > TOLERANCE_BYTES_PER_IMAGE:
                    raise Failure(
                        f"browsing images leaks about {stats['per_image']:.0f} bytes "
                        f"each ({consumed} bytes over {MEASURED} images)")
        except Failure:
            ok = False
        if stats:
            detail(f"free before {stats['before']}, after {stats['after']}")
            detail(f"consumed {stats['consumed']} bytes over {MEASURED} images "
                   f"= {stats['per_image']:.0f} bytes/image "
                   f"(tolerance {TOLERANCE_BYTES_PER_IMAGE})")

        section("the cache still does its job")
        with check("re-entering images that are still mounted costs nothing"):
            recent = list(measured)[-5:]
            before = heap_free(rest)
            re_enter(args.host, password, recent, 4)
            after = heap_free(rest)
            if before - after > TOLERANCE_REENTRY_BYTES:
                raise Failure(
                    f"re-entering {len(recent)} mounted images consumed "
                    f"{before - after} bytes; they should already be mounted")
            detail(f"{len(recent)} images re-entered 4 times: {before - after} bytes")

        section("summary")
        detail(f"mount cache slope: {'OK' if ok else 'FAIL'}")
        if ok:
            suite_ok("mount_cache_leak_test")
            return 0
        suite_fail("mount_cache_leak_test", "see the summary above")
        return 1
    finally:
        removed = cleanup(args.host, password, everything)
        detail(f"removed {removed} of {len(list(everything))} images created by this suite")


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Failure as exc:
        suite_fail("mount_cache_leak_test", format_exception(exc))
        raise SystemExit(1)
