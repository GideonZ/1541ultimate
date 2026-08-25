#!/usr/bin/env python3
# E2E: Calls every REST endpoint the firmware registers and checks the device
# survives each one.
#
# Why this exists. A lockup in files:create_d64 shipped because no suite ever
# called that route: the API surface was covered by whichever endpoints the
# feature suites happened to need. So this suite reads the route table out of
# software/api/*.cc and requires every route to be classified, either with a
# probe here or with a written reason for not calling it. A new endpoint fails
# this suite until somebody decides which it is.
#
# What a probe is. Most probes deliberately drive the argument or error path
# rather than the successful one: a nonexistent file, an unknown drive, a
# missing required argument. That keeps the suite fast and side-effect free,
# and it is not a weaker check than a happy-path call would be. The bug that
# prompted this suite was in ArgsURI's destructor, which runs on every request
# whatever the outcome, so an error-path call catches it exactly as well.
#
# After every probe the device is read back. That read is what turns "the route
# answered" into "the route answered and the firmware is still running".
#
# Repetition. The lockup this suite exists for did not fire on a cold device on
# the first call, so every probe runs --repeat times. By default the repetitions
# are spread across worker threads so that different endpoints are in flight at
# once, which is the harder case: it overlaps request teardown on one connection
# with argument parsing on another. --order sequential runs each endpoint's
# repetitions back to back on one thread instead, which is what you want when a
# single endpoint is suspect and you need the calls attributable.

import argparse
import ftplib
import os
import posixpath
import re
import sys
import threading
import urllib.error
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
from typing import Dict, List, Optional, Sequence, Set, Tuple

# tests/lib holds the reporting rules and the one shared REST client.
sys.path.insert(0, os.path.join(
    os.path.dirname(os.path.abspath(__file__)), "..", "..", "lib"))
import ftp as ftp_lib  # noqa: E402  (needs tests/lib on sys.path first)
import rest as rest_lib  # noqa: E402  (needs tests/lib on sys.path first)
from api import UltimateApi  # noqa: E402  (needs tests/lib on sys.path first)
from report import (  # noqa: E402  (needs tests/lib on sys.path first)
    Failure, check_count, check_fail, check_ok, check_skip, check_start, detail,
    format_exception, section, suite_fail, suite_ok)

SUITE = "rest_api_coverage_test"
API_SOURCE_DIR = Path(__file__).resolve().parents[3] / "software" / "api"
API_CALL_RE = re.compile(
    r"^API_CALL\(\s*(\w+)\s*,\s*(\w+)\s*,\s*(\w+)\s*,", re.MULTILINE)

# A scratch directory that exists on every product, cleared on reboot.
SCRATCH = "/Temp"
# A path that is guaranteed not to exist, for probes that must not create
# anything. The directory component does not exist either, so a route that
# creates a file on the way to failing still creates nothing.
MISSING = "/Temp/rest_api_coverage_no_such_dir/no_such_file"

LIVENESS_TIMEOUT_SECONDS = 10.0

# The firmware serves MAX_HTTP_CLIENT = 4 connections (httpd/c-version/lib/
# server.h). Each worker holds one for the call and one for the liveness read
# that follows it, but never both at once, so the ceiling is one per worker.
# Three leaves a slot free: saturating the last one turns a slow answer into a
# refused connection, which would read as a device failure rather than as the
# queueing it is.
DEFAULT_WORKERS = 3
DEFAULT_REPEAT = 3

# Endpoints that are deliberately not called, and why. Anything listed here is
# still required to exist: if one is removed from the firmware this suite fails,
# so the list cannot rot into a set of names nobody recognises.
EXCLUDED: Dict[Tuple[str, str, str], str] = {
    ("PUT", "machine", "poweroff"):
        "turns the device off; no test can turn it back on",
    ("PUT", "machine", "reboot"):
        "restarts the machine mid-run; exercised by readmem-writemem",
    ("PUT", "machine", "reset"):
        "resets the C64 under whatever else is running; exercised by "
        "readmem-writemem",
    ("PUT", "configs", "save_to_flash"):
        "writes the settings store; a run that started in safe mode would "
        "overwrite the real values with defaults",
    ("PUT", "configs", "load_from_flash"):
        "discards the settings the run is using, including the ones a later "
        "suite set up",
    ("PUT", "configs", "reset_to_default"):
        "destroys the operator's settings",
    ("PUT", "machine", "menu_button"):
        "leaves the on-screen menu open or closed for every later suite; "
        "exercised by menu-screen",
}


class Probe:
    """One endpoint, one call, and what counts as an answer.

    `accept` is a set of status codes rather than one code because several
    routes are compiled per product: machine:input is behind #if U64 and
    answers 501 on a cartridge, and menu_screen answers 404 when no menu is
    open. A probe that accepted only one of those would fail on half the
    hardware for a reason that is not a defect.
    """

    def __init__(self, method: str, group: str, command: str,
                 path: str, accept: Sequence[int], *,
                 params: Optional[Dict[str, object]] = None,
                 body: Optional[bytes] = None,
                 note: str = "",
                 exclusive: bool = False) -> None:
        self.method = method
        self.group = group
        self.command = command
        self.path = path
        self.accept = set(accept)
        self.params = params
        self.body = body
        self.note = note
        # An exclusive probe changes machine state that the next probe would
        # see, so it never runs concurrently with anything, including itself.
        self.exclusive = exclusive

    @property
    def key(self) -> Tuple[str, str, str]:
        return (self.method, self.group, self.command)

    @property
    def label(self) -> str:
        name = self.group if self.command == "none" else f"{self.group}:{self.command}"
        return f"{self.method} {name}"


def _route_path(group: str, command: str) -> str:
    return f"/v1/{group}" if command == "none" else f"/v1/{group}:{command}"


OK = (200,)
# A route that refuses the probe's arguments. Which of these a route picks is
# its own business; the point of the probe is that it answers and stays up.
REFUSED = (400, 403, 404, 412, 500, 501)


def build_probes() -> List[Probe]:
    p: List[Probe] = []

    # ---- read-only ------------------------------------------------------
    p.append(Probe("GET", "version", "none", "/v1/version", OK))
    p.append(Probe("GET", "info", "none", "/v1/info", OK))
    p.append(Probe("GET", "help", "none", "/v1/help", OK,
                   params={"command": "version"}))
    p.append(Probe("GET", "drives", "none", "/v1/drives", OK))
    p.append(Probe("GET", "configs", "none", "/v1/configs", OK))
    p.append(Probe("GET", "files", "info", f"/v1/files{SCRATCH}:info", OK))
    p.append(Probe("GET", "machine", "readmem", "/v1/machine:readmem", OK,
                   params={"address": 1024, "length": 16}))
    p.append(Probe("GET", "machine", "heap", "/v1/machine:heap", OK))
    # Both debugreg routes are inside #if U64 in route_machine.cc, so on a
    # product without that register they are not in the route table at all and
    # the dispatcher answers 404. That is different from machine:input, which
    # is always registered and answers 501.
    p.append(Probe("GET", "machine", "debugreg", "/v1/machine:debugreg",
                   OK + (404,), note="404 where the route is not compiled in"))
    p.append(Probe("GET", "machine", "menu_screen", "/v1/machine:menu_screen",
                   OK + (404,), note="404 when no menu is open"))
    p.append(Probe("GET", "machine", "measure", "/v1/machine:measure",
                   OK + (501,), note="501 when the FPGA has no bus measurement"))
    p.append(Probe("GET", "machine", "input", "/v1/machine:input",
                   OK + (501,), note="501 on products without key injection"))

    # ---- argument and error paths, no state change ----------------------
    for kind, extra in (("d64", {"tracks": 35}), ("d71", {}),
                        ("d81", {}), ("dnp", {"tracks": 1})):
        p.append(Probe("PUT", "files", f"create_{kind}",
                       f"/v1/files{MISSING}.{kind}:create_{kind}", REFUSED,
                       params=extra,
                       note="unwritable path; the happy path is create-disk-image"))
    p.append(Probe("PUT", "machine", "writemem", "/v1/machine:writemem", REFUSED,
                   params={"address": 1024},
                   note="no data argument; real writes are readmem-writemem"))
    # debugreg takes any value and answers 200, so there is no argument that
    # refuses. run() replaces this with the value the register already holds,
    # which makes the call a no-op, and cleanup() puts it back either way.
    p.append(Probe("PUT", "machine", "debugreg", "/v1/machine:debugreg",
                   OK + (404,), params={"value": "0"},
                   note="writes back the value already in the register, "
                        "404 where the route is not compiled in"))
    p.append(Probe("PUT", "drives", "mount", "/v1/drives:mount", REFUSED,
                   params={"drive": "a", "image": MISSING + ".d64"}))
    p.append(Probe("PUT", "drives", "load_rom", "/v1/drives:load_rom", REFUSED,
                   params={"drive": "a", "file": MISSING + ".rom"}))
    p.append(Probe("PUT", "drives", "set_mode", "/v1/drives:set_mode", REFUSED,
                   params={"drive": "a", "mode": "no-such-mode"}))
    p.append(Probe("PUT", "drives", "reset", "/v1/drives:reset", REFUSED,
                   params={"drive": "z"}, note="unknown drive letter"))
    p.append(Probe("PUT", "drives", "remove", "/v1/drives:remove", REFUSED,
                   params={"drive": "z"}))
    p.append(Probe("PUT", "drives", "unlink", "/v1/drives:unlink", REFUSED,
                   params={"drive": "z"}))
    p.append(Probe("PUT", "drives", "on", "/v1/drives:on", REFUSED,
                   params={"drive": "z"}))
    p.append(Probe("PUT", "drives", "off", "/v1/drives:off", REFUSED,
                   params={"drive": "z"}))
    p.append(Probe("PUT", "configs", "none", "/v1/configs/No%20Such%20Category",
                   REFUSED, params={"value": "1"},
                   note="unknown category; real writes are the cfg-* suites"))
    for command in ("sidplay", "modplay", "load_prg", "run_prg", "run_crt"):
        p.append(Probe("PUT", "runners", command, f"/v1/runners:{command}", REFUSED,
                       params={"file": MISSING + ".prg"},
                       note="nonexistent file, so nothing is launched"))
    p.append(Probe("PUT", "streams", "start", "/v1/streams:start", REFUSED,
                   params={"stream": "no-such-stream"}))
    p.append(Probe("PUT", "streams", "stop", "/v1/streams:stop", REFUSED,
                   params={"stream": "no-such-stream"}))

    # ---- round trip, state restored immediately -------------------------
    # These two are the only pair that has to run for real: there is no
    # argument that makes pause fail without also skipping the code that
    # freezes the machine. They are adjacent and in this order, and cleanup()
    # sends a resume as well, so an abort between them cannot leave the C64
    # paused for the suites that follow.
    p.append(Probe("PUT", "machine", "pause", "/v1/machine:pause", OK,
                   exclusive=True))
    p.append(Probe("PUT", "machine", "resume", "/v1/machine:resume", OK,
                   exclusive=True))

    # ---- POST routes ----------------------------------------------------
    # Every POST route takes its payload as an attachment. Sending none is
    # refused before the handler runs, which reaches the same request teardown
    # without uploading anything or starting anything.
    for group, command in (("configs", "none"), ("drives", "mount"),
                           ("drives", "load_rom"), ("machine", "writemem"),
                           ("machine", "input"), ("runners", "sidplay"),
                           ("runners", "modplay"), ("runners", "load_prg"),
                           ("runners", "run_prg"), ("runners", "run_crt")):
        p.append(Probe("POST", group, command, _route_path(group, command), REFUSED,
                       note="no body, so the handler never runs"))

    return p


class SuiteRunner:
    def __init__(self, args) -> None:
        self.args = args
        self.api = UltimateApi(args.host, args.password, args.timeout)
        # The probes assert on status codes, so they go through the transport
        # rather than the typed API: rest.py returns the status instead of
        # raising on it.
        self.rest = self.api.rest
        self.probes = build_probes()
        self.debugreg_before: Optional[str] = None
        self.local = threading.local()
        self.local.api = self.api
        # Set by the first worker that finds the device gone, so the others
        # stop rather than each spending the liveness budget discovering it.
        self.dead = threading.Event()

    # -- helpers ------------------------------------------------------------
    def alive(self) -> Optional[str]:
        return self.api.unreachable_reason(LIVENESS_TIMEOUT_SECONDS)

    def source_routes(self) -> Set[Tuple[str, str, str]]:
        found: Set[Tuple[str, str, str]] = set()
        for source in sorted(API_SOURCE_DIR.glob("*.cc")):
            for method, group, command in API_CALL_RE.findall(
                    source.read_text(encoding="utf-8", errors="replace")):
                found.add((method.upper(), group, command))
        return found

    def read_debugreg(self) -> Optional[str]:
        try:
            code, _, body = self.rest.request("GET", "/v1/machine:debugreg",
                                              idempotent=True)
        except (Failure, OSError, TimeoutError, urllib.error.URLError):
            return None
        if code != 200:
            return None
        match = re.search(rb'"value"\s*:\s*"([^"]*)"', body)
        return match.group(1).decode("ascii", "replace") if match else None

    # -- checks -------------------------------------------------------------
    def check_coverage(self) -> bool:
        """Every registered route is probed here or excluded with a reason."""
        section("coverage")
        check_start(f"read the route table from {API_SOURCE_DIR.name}/*.cc")
        try:
            registered = self.source_routes()
        except OSError as exc:
            check_fail(f"could not read the API sources: {format_exception(exc)}")
            return False
        if not registered:
            check_fail(f"no API_CALL found under {API_SOURCE_DIR}")
            return False
        check_ok(f"{len(registered)} endpoints")

        probed = {probe.key for probe in self.probes}
        classified = probed | set(EXCLUDED)

        check_start("every registered endpoint is probed or excluded")
        unclassified = sorted(registered - classified)
        if unclassified:
            for method, group, command in unclassified:
                detail(f"unclassified: {method} {_route_path(group, command)}")
            check_fail(
                f"{len(unclassified)} endpoint(s) have no probe and no written "
                "reason to skip. Add a probe, or add one to EXCLUDED saying why "
                "it must not be called.")
            return False
        check_ok(f"{len(probed)} probed, {len(EXCLUDED)} excluded")

        check_start("nothing is classified that the firmware no longer has")
        stale = sorted(classified - registered)
        if stale:
            for method, group, command in stale:
                detail(f"stale: {method} {_route_path(group, command)}")
            check_fail(f"{len(stale)} classified endpoint(s) are not registered "
                       "any more; remove them from this suite")
            return False
        check_ok()

        for (method, group, command), reason in sorted(EXCLUDED.items()):
            detail(f"not called: {method} {_route_path(group, command)} - {reason}")
        return True

    def call_once(self, api, probe: Probe) -> Optional[str]:
        """Run one probe once on `api`; None when it passed, else the reason.

        Each worker owns its client, so nothing here touches shared state and
        the transport's counters stay meaningful per thread.
        """
        try:
            code, _, body = api.rest.request(
                probe.method, probe.path, params=probe.params, body=probe.body)
        except (Failure, OSError, TimeoutError, urllib.error.URLError) as exc:
            return f"did not answer: {format_exception(exc)}"
        if code not in probe.accept:
            return (f"answered HTTP {code}, expected one of "
                    f"{sorted(probe.accept)}: {body[:160]!r}")
        reason = api.unreachable_reason(LIVENESS_TIMEOUT_SECONDS)
        if reason:
            self.dead.set()
            return (f"the device stopped answering after this call: {reason}. "
                    "It needs a power cycle.")
        return None

    def client(self):
        """One UltimateApi per thread.

        Keyed by thread rather than by task, because the pool decides which
        thread runs which task: two tasks that happened to pick the same client
        would share its transport counters across threads.
        """
        api = getattr(self.local, "api", None)
        if api is None:
            api = UltimateApi(self.args.host, self.args.password, self.args.timeout)
            self.local.api = api
        return api

    def run_probes(self) -> bool:
        """Run every probe `repeat` times and report one check per endpoint."""
        shared = [p for p in self.probes if not p.exclusive]
        exclusive = [p for p in self.probes if p.exclusive]
        results: Dict[Tuple[str, str, str], Optional[str]] = {}
        lock = threading.Lock()

        def record(probe: Probe, reason: Optional[str]) -> None:
            if reason is None:
                return
            with lock:
                results.setdefault(probe.key, reason)

        def task(probe: Probe) -> None:
            if self.dead.is_set():
                return
            with lock:
                if probe.key in results:
                    return          # already failed; do not pile on
            record(probe, self.call_once(self.client(), probe))

        if self.args.order == "sequential":
            # a,a,a,b,b,b: one endpoint at a time, on one connection.
            for probe in shared:
                for _ in range(self.args.repeat):
                    task(probe)
        else:
            # a,b,c,...,a,b,c,...: one pass of every endpoint per round, so the
            # calls in flight together are always different endpoints.
            with ThreadPoolExecutor(max_workers=self.args.workers) as pool:
                for _ in range(self.args.repeat):
                    if self.dead.is_set():
                        break
                    list(pool.map(task, shared))

        # The exclusive ones are always sequential and always in list order, so
        # pause is followed by resume however the rest of the suite ran.
        for _ in range(self.args.repeat):
            if self.dead.is_set():
                break
            for probe in exclusive:
                task(probe)

        ok = True
        for probe in self.probes:
            label = probe.label
            if probe.note:
                label = f"{label} ({probe.note})"
            check_start(f"{label} x{self.args.repeat}")
            reason = results.get(probe.key)
            if reason is None:
                if self.dead.is_set() and probe.key not in results:
                    check_skip("device was already down")
                    ok = False
                    continue
                check_ok()
                continue
            check_fail(f"{probe.label} {reason}")
            ok = False
            if self.dead.is_set():
                detail("device is down; the remaining endpoints were not called")
                break
        return ok

    def run(self) -> bool:
        if not self.check_coverage():
            return False

        section("preconditions")
        check_start("device reachable")
        reason = self.alive()
        if reason:
            check_fail(f"device did not answer before the suite started: {reason}")
            return False
        check_ok()

        # Make the debugreg write a no-op by giving it back what it holds.
        self.debugreg_before = self.read_debugreg()
        if self.debugreg_before is not None:
            for probe in self.probes:
                if probe.key == ("PUT", "machine", "debugreg"):
                    probe.params = {"value": f"0x{self.debugreg_before}"}

        section("endpoints")
        detail(f"{len(self.probes)} endpoints x{self.args.repeat}, "
               f"{self.args.order}"
               + (f", {self.args.workers} workers" if self.args.order == "concurrent"
                  else ""))
        return self.run_probes()

    def cleanup(self) -> None:
        """The probes are chosen not to leave anything behind. What is left is
        the pause/resume pair, which must not end paused however the run ended,
        and the scratch directory in the unlikely case a route created it on its
        way to failing."""
        try:
            self.rest.request("PUT", "/v1/machine:resume")
        except (Failure, OSError, TimeoutError, urllib.error.URLError):
            pass
        if self.debugreg_before is not None:
            try:
                self.rest.request("PUT", "/v1/machine:debugreg",
                                  params={"value": f"0x{self.debugreg_before}"})
            except (Failure, OSError, TimeoutError, urllib.error.URLError):
                pass
        try:
            with ftp_lib.session(self.args.host, self.args.password) as client:
                ftp_lib.quietly(lambda: client.rmd(posixpath.dirname(MISSING)))
        except ftplib.all_errors + (OSError, Failure):
            pass


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("-H", "--host", default=os.environ.get("U64_HOST", "u64"))
    parser.add_argument("-p", "--password", default=os.environ.get("U64_PASS"))
    parser.add_argument("-t", "--timeout", type=float,
                        default=float(os.environ.get("U64_TIMEOUT", "10.0")))
    parser.add_argument("-r", "--repeat", type=int, default=DEFAULT_REPEAT,
                        help="how many times each endpoint is called "
                             "(default: %(default)s)")
    parser.add_argument("--order", choices=("concurrent", "sequential"),
                        default="concurrent",
                        help="concurrent spreads the repetitions across workers "
                             "so different endpoints overlap; sequential calls "
                             "each endpoint's repetitions back to back on one "
                             "thread (default: %(default)s)")
    parser.add_argument("-w", "--workers", type=int, default=DEFAULT_WORKERS,
                        help="concurrent workers, at most one HTTP connection "
                             "each (default: %(default)s)")
    args = parser.parse_args()
    if args.repeat < 1:
        parser.error("--repeat must be at least 1")
    if args.workers < 1:
        parser.error("--workers must be at least 1")

    runner = SuiteRunner(args)
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
        try:
            runner.cleanup()
        except Exception:
            pass

    if passed:
        suite_ok(SUITE, f"{check_count()} checks")
        return 0
    suite_fail(SUITE, "see the failed check above")
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
