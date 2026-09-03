#!/usr/bin/env python3
# E2E: Verifies the files:create_* disk-image routes and that the device stays
# alive afterwards.
#
# The liveness check after every create is the point of this suite. A create
# call that answers HTTP 200 but leaves the device unable to serve the next
# request is the failure this suite exists to catch: ArgsURI::ClearAll() used to
# release a strdup()'d string with delete, which reaches heap_4 with an address
# that is not a block start and stops the whole device inside configASSERT.

import argparse
import ftplib
import sys
import urllib.error
from collections.abc import Iterable
from pathlib import Path

# tests/lib holds the shared library; importing bootstrap adds tests/e2e/lib.
# The search walks up rather than counting directories, so this is the same in
# every entry point and a suite that moves needs no edit. See tests/lib/bootstrap.py.
sys.path.insert(0, str(next(p for p in Path(__file__).resolve().parents
                            if (p / "tests" / "lib").is_dir()) / "tests" / "lib"))
import bootstrap  # noqa: E402,F401
import cli  # noqa: E402
import ftp as ftp_lib  # noqa: E402  (needs tests/lib on sys.path first)
import machine as machine_lib  # noqa: E402  (needs tests/lib first)
import rest as rest_lib  # noqa: E402  (needs tests/lib on sys.path first)
import targets  # noqa: E402  (needs tests/lib on sys.path first)
from api import UltimateApi  # noqa: E402  (needs tests/lib on sys.path first)
from report import (  # noqa: E402  (needs tests/lib on sys.path first)
    Failure, best_effort, check_count, check_fail, check_ok, check_start, detail,
    format_exception, section, suite_fail, suite_ok)

SUITE = "create_disk_image_test"
TEST_DIR = "/Temp"

# (label, kind, keyword arguments, expected size in bytes)
# Sizes are the ones software/api/route_files.cc computes.
CASES: list[tuple[str, str, dict, int]] = [
    ("d64-35", "d64", {"tracks": 35}, 683 * 256),
    ("d64-40", "d64", {"tracks": 40}, (17 * 5 + 683) * 256),
    ("d71", "d71", {}, 683 * 2 * 256),
    ("d81", "d81", {}, 3200 * 256),
    ("dnp-1", "dnp", {"tracks": 1}, 65536),
    ("dnp-16", "dnp", {"tracks": 16}, 16 * 65536),
]

# How long to wait for the device to answer the liveness read after a create.
# The failure mode this guards against is permanent, so a short budget is
# enough and keeps a broken build from stalling the run.
LIVENESS_TIMEOUT_SECONDS = 10.0


class SuiteRunner:
    def __init__(self, args) -> None:
        self.args = args
        self.api = UltimateApi(args.host, args.password, args.timeout)

    # -- helpers ------------------------------------------------------------
    def remote_path(self, label: str, kind: str) -> str:
        return f"{TEST_DIR}/cdi_{label}.{kind}"

    def ftp_delete(self, paths: Iterable[str]) -> None:
        """Remove several paths over one FTP session.

        One session per file costs a control and a data connection each time,
        which is most of this suite's wall clock for work that is not under
        test.
        """
        paths = list(paths)
        if not paths:
            return
        try:
            with ftp_lib.session(self.args.host, self.args.password) as client:
                for path in paths:
                    ftp_lib.delete_quietly(client, path)
        except (*ftplib.all_errors, OSError, Failure):
            pass

    def alive(self) -> str | None:
        return self.api.unreachable_reason(LIVENESS_TIMEOUT_SECONDS)

    def all_paths(self) -> list[str]:
        paths = [self.remote_path(label, kind) for label, kind, _p, _e in CASES]
        label, kind, _p, _e = CASES[0]
        paths.append(self.remote_path(f"{label}-repeat", kind))
        return paths

    # -- checks -------------------------------------------------------------
    def create_case(self, label: str, kind: str, params: dict, expected: int) -> bool:
        path = self.remote_path(label, kind)

        check_start(f"create {kind} at {path}")
        try:
            getattr(self.api.files, f"create_{kind}")(path, **params)
        except (Failure, OSError, TimeoutError, urllib.error.URLError) as exc:
            check_fail(f"create_{kind} did not complete: {format_exception(exc)}")
            return False
        check_ok()

        # Run this before the size check: when the create takes the device down,
        # files:info cannot answer either, and the liveness wording says what
        # actually happened.
        check_start(f"device still answers after create {kind}")
        reason = self.alive()
        if reason:
            check_fail(
                f"device stopped answering after create_{kind}: {reason}. "
                "It needs a power cycle.")
            return False
        check_ok()

        check_start(f"{path} is {expected} bytes")
        try:
            info = self.api.files.info(path)
        except (Failure, OSError, TimeoutError, urllib.error.URLError) as exc:
            check_fail(f"files:info failed: {format_exception(exc)}")
            return False
        if info is None:
            check_fail(f"{path} does not exist after a successful create")
            return False
        if info.size != expected:
            check_fail(f"{path} is {info.size} bytes, expected {expected}")
            return False
        check_ok()
        return True

    def repeat_case(self) -> bool:
        """A second create in a fresh request proves the first one left the
        device in a state that can still serve requests, not merely alive."""
        label, kind, params, expected = CASES[0]
        section("repeat")
        return self.create_case(f"{label}-repeat", kind, params, expected)

    def run(self) -> bool:
        section("preconditions")
        check_start("device reachable")
        reason = self.alive()
        if reason:
            check_fail(f"device did not answer before the suite started: {reason}")
            return False
        check_ok()

        # create_* refuses to overwrite (FA_CREATE_NEW), so a leftover from an
        # aborted run would fail every case. One session clears them all.
        check_start("clear leftovers from an earlier run")
        self.ftp_delete(self.all_paths())
        check_ok()

        all_ok = True
        for label, kind, params, expected in CASES:
            section(label)
            if not self.create_case(label, kind, params, expected):
                all_ok = False
                # Once the device is down every later case reports the same
                # thing, so stop and say so once.
                if self.alive():
                    detail("device is down; skipping the remaining cases")
                    return False
        if all_ok:
            all_ok = self.repeat_case()
        return all_ok

    def cleanup(self) -> None:
        self.ftp_delete(self.all_paths())


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    cli.add_device_arguments(parser, password=None, colour=False)
    args = parser.parse_args()

    runner = SuiteRunner(args)
    device = UltimateApi(args.host, args.password, args.timeout)
    info = device.info()
    machine = machine_lib.identify(
        targets.device_of(args.host),
        lambda: (info.product, info.firmware_version))
    if machine.skip_without_fix(machine_lib.FILES_CREATE_IMAGE_SURVIVES,
                                "the device survives creating a disk image"):
        suite_ok(SUITE)
        return 0

    try:
        passed = runner.run()
    except Failure as exc:
        suite_fail(SUITE, format_exception(exc))
        return 1
    except (OSError, TimeoutError, urllib.error.URLError) as exc:
        if rest_lib.looks_unreachable(exc):
            suite_fail(SUITE, f"connection failure: {format_exception(exc)}")
        else:
            suite_fail(SUITE, f"REST failure: {format_exception(exc)}")
        return 1
    finally:
        best_effort("remove the images this run created", runner.cleanup)

    if passed:
        suite_ok(SUITE, f"{check_count()} checks")
        return 0
    suite_fail(SUITE, "see the failed check above")
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
