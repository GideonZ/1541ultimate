#!/usr/bin/env python3
# Gate check: the runner's recovery decisions and exit statuses.

"""Verify what run-tests does when the device stops answering, without a device.

Two decisions in `run-tests` are easy to get wrong and expensive to find out
about later, because both only show themselves on a run that has already gone
badly:

- *when* the recovery command may run. It is the operator's command and it can
  reboot or reflash hardware, so it must fire only where the ordinary wait has
  already given up. A suite that merely fails must never trigger it.
- *what status the run exits with*. A programmatic caller reads `$?` and the
  `run` record in the JSONL, and both have to agree with what the console said.

Neither needs a device, so this runs at the start of the gate next to
`check_transport_usage.py` and costs nothing.
"""

import contextlib
import importlib.machinery
import importlib.util
import io
import argparse
import os
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import health  # noqa: E402
import targets  # noqa: E402
from report import Failure, check, detail, suite_fail, suite_ok  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
RUNNER_PATH = os.path.join(ROOT, "run-tests")

# The runner's own waits are minutes long by design. The decisions under test
# do not depend on their length, so they are shortened here rather than waited
# out; anything that did depend on them would be testing the clock instead.
FAST_POLL_SECONDS = 0.01
FAST_POST_RECOVERY_BUDGET_SECONDS = 0.05


def load_runner():
    """Import run-tests as a module. It has no .py suffix, so it needs a loader."""
    loader = importlib.machinery.SourceFileLoader("run_tests_harness", RUNNER_PATH)
    spec = importlib.util.spec_from_loader("run_tests_harness", loader)
    module = importlib.util.module_from_spec(spec)
    loader.exec_module(module)
    module.DEVICE_RECOVERY_POLL_SECONDS = FAST_POLL_SECONDS
    module.POST_RECOVERY_BUDGET_SECONDS = FAST_POST_RECOVERY_BUDGET_SECONDS
    return module


class ScriptedProbe:
    """A reachability probe that answers from a list, then False forever."""

    def __init__(self, answers):
        self.answers = list(answers)

    def reachable(self):
        return self.answers.pop(0) if self.answers else False


# Where a fixture's own records go. The runner code under test reports through
# the same module this suite reports through, so a fixture device that is
# declared unhealthy writes a `health` record and a fixture suite that fails
# writes a `suite` record. Under the gate this process's records go to the
# run's real per-suite file, so those synthetic records land in the run's
# JSONL and the report renders them as suite runs that never happened: a green
# run showed six `incomplete` rows in the verdict table and six entries under
# `## Failing checks`. The fix belongs here rather than in the report, which
# has no way to tell a fixture record from a real one and must not be taught
# fixture names.
_FIXTURE_RECORDS = None


def set_fixture_records(path):
    """Name the file a fixture's records are diverted to. Called once by main."""
    global _FIXTURE_RECORDS
    _FIXTURE_RECORDS = path


@contextlib.contextmanager
def isolated_records():
    """Run a block with everything it reports sent to the fixture's own file.

    Both ends are redirected: this process's own record destination, and the
    `E2E_JSONL` variable a child process reads at import, because `run_suite`
    starts real child processes.
    """
    report_module = sys.modules["report"]
    previous_path = report_module.JSONL_PATH
    previous_environment = os.environ.get("E2E_JSONL")
    report_module.set_jsonl_path(_FIXTURE_RECORDS or "")
    if _FIXTURE_RECORDS:
        os.environ["E2E_JSONL"] = _FIXTURE_RECORDS
    else:
        os.environ.pop("E2E_JSONL", None)
    try:
        yield
    finally:
        report_module.set_jsonl_path(previous_path)
        if previous_environment is None:
            os.environ.pop("E2E_JSONL", None)
        else:
            os.environ["E2E_JSONL"] = previous_environment


def isolating(function):
    """`function` with its records diverted, for a fixture's own device calls."""

    def call(*args, **kwargs):
        with isolated_records():
            return function(*args, **kwargs)

    return call


def device(runner, answers, **kwargs):
    # Health is reachability alone unless a check stubs the sweep: these
    # fixtures have no device, so a real sweep would fail every time and tell
    # us nothing about the decision under test.
    kwargs.setdefault("health_check", False)
    kwargs.setdefault("recover_max_per_suite", 1)
    kwargs.setdefault("recover_max_total", 1)
    made = runner.Device("device.invalid", "", 1.0, **kwargs)
    made.probe = ScriptedProbe(answers)
    # Every sweep this fixture device reports is a sweep of a device that does
    # not exist, so it is written to the fixture's file rather than the run's.
    made.ensure_healthy = isolating(made.ensure_healthy)
    made.start_suite()
    return made


def expect(label, actual, wanted):
    if actual != wanted:
        raise Failure(f"{label}: got {actual!r}, expected {wanted!r}")


def run_exit_status_checks(runner):
    passed = runner.Result("e2e", "overlay", "passed", runner.report.OK, 1.0)
    failed = runner.Result("e2e", "overlay", "failed", runner.report.FAIL, 1.0)
    unhealthy = runner.Result("e2e", "overlay", "unhealthy", runner.report.FAIL, 1.0,
                          device_unhealthy=True)

    with check("a clean run exits 0"):
        expect("clean", runner.exit_code_for([passed], 0), runner.EXIT_OK)

    with check("a failed suite exits 1"):
        expect("failure", runner.exit_code_for([passed, failed], 0),
               runner.EXIT_SUITE_FAILED)

    with check("a recovery with no failure exits 3"):
        expect("recovered", runner.exit_code_for([passed], 1), runner.EXIT_RECOVERED)

    with check("a failure outranks a recovery"):
        expect("failure and recovery", runner.exit_code_for([passed, failed], 1),
               runner.EXIT_SUITE_FAILED)

    with check("a device that cannot be made healthy exits 4, outranking a failure"):
        expect("unhealthy", runner.exit_code_for([failed, unhealthy], 1), runner.EXIT_DEVICE_UNHEALTHY)


def run_recovery_gating_checks(runner):
    """When the recovery command may run. Health is reachability here."""
    with check("a device that answers is never recovered"):
        made = device(runner, [True], recover_command="true")
        expect("healthy", made.ensure_healthy('fixture:', patient=False), True)
        expect("recoveries", made.recoveries, 0)

    with check("recovery runs only once the ordinary wait has given up"):
        # Three answers: the wait that gives up, the probe inside the recovery
        # command, and the re-check that decides whether it worked.
        made = device(runner, [False, True, True], recover_command="true")
        expect("healthy", made.ensure_healthy('fixture:', patient=False), True)
        expect("recoveries", made.recoveries, 1)

    with check("a recovery that does not bring the device back reports so"):
        made = device(runner, [], recover_command="true")
        expect("healthy", made.ensure_healthy('fixture:', patient=False), False)
        expect("recoveries", made.recoveries, 1)

    with check("a recovery command that exits non-zero is still followed by a probe"):
        # Some recovery tools report a failure the device has already come back
        # from, so the probe decides rather than the exit status.
        made = device(runner, [False, True, True], recover_command="false")
        expect("healthy", made.ensure_healthy('fixture:', patient=False), True)
        expect("recoveries", made.recoveries, 1)

    with check("a recovery command that hangs is bounded by --recover-timeout"):
        made = device(runner, [False, True], recover_command="sleep 30",
                      recover_timeout=0.2)
        expect("healthy", made.ensure_healthy('fixture:', patient=False), False)
        expect("recoveries", made.recoveries, 1)

    with check("a recovery command the shell cannot find is a failed recovery"):
        # The command is run through a shell, so an unrunnable one comes back
        # as exit 127 rather than as an exception. Its own stderr is silenced
        # here only to keep this check to one line; a real run wants to see it.
        made = device(runner, [],
                      recover_command="/definitely/not/a/command 2>/dev/null")
        expect("healthy", made.ensure_healthy('fixture:', patient=False), False)
        expect("recoveries", made.recoveries, 1)

    with check("no recovery command means no recovery"):
        made = device(runner, [])
        expect("healthy", made.ensure_healthy('fixture:', patient=False), False)
        expect("recoveries", made.recoveries, 0)


def run_recovery_limit_checks(runner):
    with check("a suite gives up after --recover-max-per-suite recoveries"):
        made = device(runner, [], recover_command="true",
                      recover_max_per_suite=2, recover_max_total=10)
        for _ in range(4):
            made.ensure_healthy('fixture:', patient=False)
        expect("recoveries", made.recoveries, 2)
        expect("blocked", made.may_recover()[0], False)

    with check("the per-suite budget starts again at the next suite"):
        made = device(runner, [], recover_command="true",
                      recover_max_per_suite=1, recover_max_total=10)
        made.ensure_healthy('fixture:', patient=False)
        expect("blocked within the suite", made.may_recover()[0], False)
        made.start_suite()
        expect("allowed in the next suite", made.may_recover()[0], True)

    with check("the run gives up after --recover-max-total recoveries"):
        made = device(runner, [], recover_command="true",
                      recover_max_per_suite=10, recover_max_total=2)
        for _ in range(4):
            made.ensure_healthy('fixture:', patient=False)
        expect("recoveries", made.recoveries, 2)
        made.start_suite()
        expect("a new suite does not reset the run budget",
               made.may_recover()[0], False)

    with check("the refusal says which ceiling was reached"):
        made = device(runner, [], recover_command="true",
                      recover_max_per_suite=1, recover_max_total=9)
        made.ensure_healthy('fixture:', patient=False)
        allowed, why = made.may_recover()
        expect("blocked", allowed, False)
        if "this suite" not in why:
            raise Failure(f"expected the per-suite ceiling to be named, got {why!r}")


def run_degraded_recovery_checks(runner):
    """The path that fires on a device which answers but is not healthy."""
    healthy = health.Health((health.Check("rest", health.OK, 9.0),))
    degraded = health.Health((health.Check("rest", health.OK, 9.0),
                              health.Check("ftp", health.FAIL, 2000.0, "refused")))

    def with_sweeps(sweeps, **kwargs):
        kwargs.setdefault("health_check", True)
        made = device(runner, [True] * 8, recover_command="true", **kwargs)
        remaining = list(sweeps)
        made.health_sweep = lambda: remaining.pop(0) if remaining else healthy
        return made

    with check("a healthy sweep after a failed suite recovers nothing"):
        made = with_sweeps([healthy])
        expect("healthy", made.ensure_healthy('fixture:', patient=False), True)
        expect("recoveries", made.recoveries, 0)

    with check("a degraded but reachable device is recovered"):
        # A reachability probe alone would call this device fine, which is the
        # gap this path exists to close.
        made = with_sweeps([degraded, healthy])
        expect("healthy afterwards", made.ensure_healthy('fixture:', patient=False), True)
        expect("recoveries", made.recoveries, 1)

    with check("a device still degraded after recovering is reported, not retried"):
        made = with_sweeps([degraded, degraded])
        expect("healthy afterwards", made.ensure_healthy('fixture:', patient=False), False)
        expect("recoveries", made.recoveries, 1)

    with check("a degraded device is not recovered past its ceiling"):
        made = with_sweeps([degraded, degraded, degraded], recover_max_per_suite=1)
        made.ensure_healthy('fixture:', patient=False)
        expect("blocked second time", made.ensure_healthy('fixture:', patient=False), False)
        expect("recoveries", made.recoveries, 1)

    with check("--no-health-check takes the sweep out of the decision"):
        # Without a sweep there is nothing to judge the device by, so it is
        # taken as healthy and recovery is left to the unreachable path. A
        # device with a listener deliberately off would otherwise be recovered
        # before every suite for the rest of the run.
        made = with_sweeps([degraded, degraded], health_check=False)
        expect("healthy", made.ensure_healthy('fixture:', patient=False), True)
        expect("recoveries", made.recoveries, 0)

    with check("--no-health-check still recovers a device that has gone"):
        made = device(runner, [], recover_command="true", health_check=False)
        expect("unhealthy", made.ensure_healthy('fixture:', patient=False), False)
        expect("recoveries", made.recoveries, 1)


def run_retry_checks(runner, tmpdir):
    """The loop that decides whether a failed suite is run again.

    Driven through run_suite with a real child process, because the rule is
    about what the loop does with a verdict rather than about any one call.
    """
    failing = os.path.join(tmpdir, "always_fails.py")
    with open(failing, "w", encoding="utf-8") as handle:
        handle.write("import sys\nsys.exit(1)\n")
    suite = runner.Suite("perf", "fixture-suite",
                         os.path.relpath(failing, runner.ROOT), "")

    def options(**kwargs):
        base = dict(host="device.invalid", password="", timeout="1.0",
                    soak_profile="stress", output_dir="", stop_on_fail=False,
                    health_check=False, retry=True, recover_command="true",
                    recover_max_per_suite=2, recover_max_total=10,
                    recover_timeout=5.0)
        base.update(kwargs)
        return runner.Options(**base)

    def quietly(action):
        """Run `action`, holding its output back unless it is needed.

        run_suite prints a banner and a suite line of its own, which would land
        in the middle of the check line reporting on it. The output is kept and
        replayed only when the check fails, where it is the diagnostic. That
        suite line is also a `suite` record, naming a suite that exists only
        here, so the records go to the fixture's file for the same reason the
        console output does not go to the terminal.
        """
        captured = io.StringIO()
        try:
            with contextlib.redirect_stdout(captured), isolated_records():
                return action()
        except BaseException:
            detail(captured.getvalue().rstrip())
            raise

    def device_that(fit_answers, **kwargs):
        made = device(runner, [True] * 20, recover_command="true", **kwargs)
        answers = list(fit_answers)
        made.attempts = 0

        def ensure_healthy(label, patient=True, extra=None):
            healthy, recovered = answers.pop(0) if answers else (True, False)
            if recovered:
                made.recoveries += 1
                made.suite_recoveries += 1
            return healthy
        made.ensure_healthy = ensure_healthy
        return made

    with check("a suite that fails on a healthy device is not run again"):
        made = device_that([(True, False)])
        result = quietly(lambda: runner.run_suite(suite, made, options(), "", "fixture"))
        expect("verdict", result.verdict, runner.report.FAIL)
        expect("recoveries", result.recoveries, 0)

    with check("a suite that fails on an unhealthy device runs again after recovery"):
        # Two recoveries are allowed, so it attempts, recovers, attempts,
        # recovers, attempts, and then the ceiling stops it.
        made = device_that([(True, True), (True, True), (True, False)])
        result = quietly(lambda: runner.run_suite(suite, made, options(), "", "fixture"))
        expect("verdict", result.verdict, runner.report.FAIL)
        expect("recoveries", result.recoveries, 2)

    with check("--no-retry keeps the recovery and drops the extra attempt"):
        made = device_that([(True, True)])
        result = quietly(lambda: runner.run_suite(suite, made, options(retry=False), "", "fixture"))
        expect("verdict", result.verdict, runner.report.FAIL)
        expect("recoveries", result.recoveries, 0)

    with check("a device that cannot be made healthy ends the run"):
        made = device_that([(False, True)])
        result = quietly(lambda: runner.run_suite(suite, made, options(), "", "fixture"))
        expect("verdict", result.verdict, runner.report.FAIL)
        expect("device unhealthy", result.device_unhealthy, True)

    with check("a suite killed by a signal says so instead of reading as a failure"):
        # A suite that is terminated rather than finished prints no verdict of
        # its own, because report.check prints one for every exception and a
        # signal raises none. The runner's FAIL line is then the only thing on
        # screen, and without the signal in it there is nothing to tell a
        # killed suite from one whose assertion failed. Seen live on
        # prg-context-menu: the log ended mid-check-line and the run reported
        # only "prg-context-menu: failed", while the suite passed 23 of 23
        # when it was run again on the same firmware.
        killed = os.path.join(tmpdir, "gets_killed.py")
        with open(killed, "w", encoding="utf-8") as handle:
            handle.write("import os, signal\nos.kill(os.getpid(), signal.SIGTERM)\n")
        signalled = runner.Suite("perf", "signalled-suite",
                                 os.path.relpath(killed, runner.ROOT), "")
        made = device_that([(True, False)], )
        captured = io.StringIO()
        with contextlib.redirect_stdout(captured), isolated_records():
            result = runner.run_suite(signalled, made, options(retry=False), "", "fixture")
        printed = captured.getvalue()
        expect("verdict", result.verdict, runner.report.FAIL)
        if "SIGTERM" not in printed:
            detail(printed.rstrip())
            raise Failure("the FAIL line does not name the signal that ended the suite")


def run_jsonl_contract_checks(runner, tmpdir):
    """What a programmatic caller reads has to carry what a person's log does.

    The console and the JSONL are two renderings of one run, and it is easy to
    add something to the first and forget the second: the health sweep was
    logged as a line for a while and was invisible to anything reading JSONL.
    """
    import json

    path = os.path.join(tmpdir, "records.jsonl")
    report_module = sys.modules["report"]
    previous = report_module.JSONL_PATH
    report_module.set_jsonl_path(path)
    try:
        report_module.health_result("suite:", False, [
            {"name": "rest", "state": "ok", "ms": 9.0, "detail": ""},
            {"name": "ftp", "state": "fail", "ms": 2000.0, "detail": "refused"},
        ])
        report_module.suite_ok("fixture", "", 1.5, mode="overlay", attempt=2,
                               recoveries=1)
        report_module.run_result(verdict="OK", suites=1, passed=1, failed=0,
                                 skipped=0, dirty=0, seconds=1.5, recoveries=1,
                                 exit_code=3)
        records = [json.loads(line) for line in open(path, encoding="utf-8")]
    finally:
        report_module.set_jsonl_path(previous)

    by_kind = {record["kind"]: record for record in records}

    with check("a health sweep reaches the JSONL with a latency per check"):
        sweep = by_kind.get("health")
        if sweep is None:
            raise Failure("no health record was written")
        expect("ok", sweep["ok"], False)
        expect("checks", [c["name"] for c in sweep["checks"]], ["rest", "ftp"])
        expect("latency", sweep["checks"][0]["ms"], 9.0)
        expect("why it failed", sweep["checks"][1]["detail"], "refused")

    with check("a suite record carries the profile, attempt and recoveries"):
        suite = by_kind.get("suite")
        if suite is None:
            raise Failure("no suite record was written")
        for field, wanted in (("mode", "overlay"), ("attempt", 2),
                              ("recoveries", 1), ("verdict", "OK")):
            expect(field, suite[field], wanted)

    with check("the run record agrees with the exit status"):
        run = by_kind.get("run")
        if run is None:
            raise Failure("no run record was written")
        expect("recoveries", run["recoveries"], 1)
        expect("exit_code", run["exit_code"], 3)
        # A caller that reads only the JSONL must reach the same verdict as one
        # that reads only $?, so the two are written from the same numbers.
        expect("exit code matches a recovered run", run["exit_code"],
               runner.EXIT_RECOVERED)


def run_output_dir_option_checks(runner):
    """One spelling for the run's output directory, and no other.

    The directory holds every artifact a run keeps: JSONL, each suite's
    console log, the screens the suites read, captured failure state, the
    device log and the recording. It was called -j/--jsonl-dir, which named
    one of those and implied the rest were something else, and there is no
    caller to keep compatible.
    """
    parser = runner.build_parser()

    with check("-o names the output directory"):
        expect("value", parser.parse_args(["-o", "runs", "u64"]).output_dir,
               "runs")

    with check("--output-dir names the same one"):
        expect("value",
               parser.parse_args(["--output-dir", "runs", "u64"]).output_dir,
               "runs")

    for spelling in ("-j", "--jsonl-dir"):
        with check(f"{spelling} is not accepted"):
            with contextlib.redirect_stderr(io.StringIO()):
                try:
                    parser.parse_args([spelling, "runs", "u64"])
                except SystemExit as exc:
                    expect("exit status", exc.code, 2)
                else:
                    raise Failure(f"{spelling} was accepted")

    text = parser.format_help()
    with check("the old spellings are gone from --help"):
        for gone in ("-j ", "--jsonl-dir"):
            if gone in text:
                raise Failure(f"--help still offers {gone.strip()}")

    with check("the examples use the option the parser accepts"):
        # The epilog is the first thing a reader copies, so an example naming
        # an option this parser would reject is worse than no example.
        for line in text.splitlines():
            stripped = line.strip()
            if not stripped.startswith("run-tests "):
                continue
            for word in stripped.split():
                if word.startswith("-") and word not in ("-", "--"):
                    if word.rstrip(",") not in {action_string
                                                for action in parser._actions
                                                for action_string in action.option_strings}:
                        raise Failure(f"the example {stripped!r} names {word}, "
                                      "which this parser does not accept")

    with check("a child is told where to write with the same option"):
        args = parser.parse_args(["-o", "runs", "u64", "u2@c64u"])
        command = runner.child_command(args, targets.parse("u64"), "runs/u64")
        if "--output-dir" not in command:
            raise Failure(f"the child was not given --output-dir: {command}")


def run_reset_guard_checks():
    """A reset that cannot change anything is not sent.

    wait=False throughout: these check whether the request goes out, not the
    READY poll that follows it, and the fake transport serves no memory.

    The device is untouched by these: they drive a RestClient whose transport
    is replaced, so the rules are checked without hardware.
    """
    import api as api_module

    class FakeRest:
        def __init__(self):
            self.mutations = 0
            self.sent = []

        def request(self, method, path, **kwargs):
            if method.upper() != "GET":
                self.mutations += 1
            self.sent.append((method, path))
            return 200, {}, b""

    def machine(just_reset=False):
        rest = FakeRest()
        previous = os.environ.get("U64_DEVICE_RESET")
        os.environ["U64_DEVICE_RESET"] = "1" if just_reset else "0"
        try:
            return rest, api_module.MachineApi(rest)
        finally:
            if previous is None:
                os.environ.pop("U64_DEVICE_RESET", None)
            else:
                os.environ["U64_DEVICE_RESET"] = previous

    with check("consecutive resets collapse into one"):
        rest, m = machine()
        m.reset(wait=False); m.reset(wait=False); m.reset(wait=False)
        expect("resets sent", sum(1 for _, p in rest.sent if p.endswith("reset")), 1)

    with check("a read between two resets does not warrant the second"):
        rest, m = machine()
        m.reset(wait=False)
        m._rest.request("GET", "/v1/machine:readmem")
        m.reset(wait=False)
        expect("resets sent", sum(1 for _, p in rest.sent if p.endswith("reset")), 1)

    with check("a mutating call between two resets warrants the second"):
        rest, m = machine()
        m.reset(wait=False)
        m._rest.request("POST", "/v1/machine:input")
        m.reset(wait=False)
        expect("resets sent", sum(1 for _, p in rest.sent if p.endswith("reset")), 2)

    with check("force resets even when nothing has changed"):
        rest, m = machine()
        m.reset(wait=False); m.reset(force=True, wait=False)
        expect("resets sent", sum(1 for _, p in rest.sent if p.endswith("reset")), 2)

    with check("a suite told the device was just reset does not reset again"):
        rest, m = machine(just_reset=True)
        m.reset(wait=False)
        expect("resets sent", sum(1 for _, p in rest.sent if p.endswith("reset")), 0)

    with check("a suite not told that still resets"):
        rest, m = machine(just_reset=False)
        m.reset(wait=False)
        expect("resets sent", sum(1 for _, p in rest.sent if p.endswith("reset")), 1)


def run_health_checks():
    ok = health.Check("rest", health.OK, 12.0)
    bad = health.Check("ftp", health.FAIL, 2000.0, "connection refused")
    skipped = health.Check("jiffy", health.SKIP, 0.0, "the menu is open")

    with check("a sweep with every check passing is healthy"):
        expect("ok", health.Health((ok,)).ok, True)
        expect("failed", health.Health((ok,)).failed, ())

    with check("a failed check makes the sweep degraded and names it"):
        sweep = health.Health((ok, bad))
        expect("ok", sweep.ok, False)
        expect("failed", sweep.failed, ("ftp",))
        expect("detail", sweep.detail_for("ftp"), "connection refused")

    with check("a skipped check never makes the sweep degraded"):
        # The jiffy and raster checks are skipped while the menu is open,
        # because under Freeze the menu has stopped the C64 on purpose.
        expect("ok", health.Health((ok, skipped)).ok, True)

    with check("the one-line summary carries every latency and the verdict"):
        line = health.Health((ok, bad, skipped)).one_line()
        for fragment in ("rest=12ms", "ftp=FAIL", "jiffy=skip", "DEGRADED (ftp)"):
            if fragment not in line:
                raise Failure(f"{fragment!r} missing from {line!r}")
        if "\n" in line:
            raise Failure("the health summary has to be one line")



def run_target_grammar_checks():
    """The target grammar, and what a target occupies while it runs."""
    with check("a bare target is its own C64-side computer"):
        target = targets.parse("u64")
        expect("device", target.device, "u64")
        expect("computer", target.computer, "u64")
        expect("split", target.split, False)
        expect("resources", target.resources, ("u64",))

    with check("a cartridge target names the computer it is plugged into"):
        target = targets.parse("u2@c64u")
        expect("device", target.device, "u2")
        expect("computer", target.computer, "c64u")
        expect("split", target.split, True)
        expect("resources", sorted(target.resources), ["c64u", "u2"])

    with check("keyboard input is the computer's, everything else the device's"):
        target = targets.parse("u2@c64u")
        expect("input", target.host_for("/v1/machine:input"), "c64u")
        expect("menu screen", target.host_for("/v1/machine:menu_screen"), "u2")
        expect("menu button", target.host_for("/v1/machine:menu_button"), "u2")
        expect("reset", target.host_for("/v1/machine:reset"), "u2")
        expect("memory", target.host_for("/v1/machine:readmem?address=0400"), "u2")
        # On a single-device target the same rule cannot route anything away.
        alone = targets.parse("u64")
        expect("input", alone.host_for("/v1/machine:input"), "u64")

    with check("a malformed target is rejected before anything starts"):
        for token in ("", "u2@", "@c64u", "u2@c64u@x", "u2@u2", "u2 c64u", "-u2"):
            try:
                targets.parse(token)
            except targets.TargetError:
                continue
            raise Failure(f"{token!r} was accepted")

    with check("a target's output path is safe to use as a file name"):
        expect("slug", targets.parse("u2@c64u").slug, "u2-at-c64u")
        expect("slug", targets.parse("u64").slug, "u64")


def run_resource_conflict_checks(runner):
    """Which targets may run at the same time, from the grammar alone."""
    cases = (
        ("u64", "c64u", False),
        ("u64", "u2@c64u", False),
        ("c64u", "u2@c64u", True),
        ("u64", "u2@u64", True),
        ("u2@u64", "u2@c64u", True),
        ("u2@c64u", "u2@c64u", True),
    )
    with check("two targets conflict exactly when they share a machine"):
        for first, second, conflict in cases:
            a, b = targets.parse(first), targets.parse(second)
            expect(f"{first} vs {second}", a.conflicts_with(b), conflict)
            expect(f"{second} vs {first}", b.conflicts_with(a), conflict)

    with check("a target does not start while a machine it needs is in use"):
        active = [targets.parse("u2@c64u")]
        expect("u64 may run", runner.schedulable(targets.parse("u64"), active), True)
        expect("c64u may not", runner.schedulable(targets.parse("c64u"), active), False)
        expect("nothing active", runner.schedulable(targets.parse("c64u"), []), True)

    with check("scheduling every target eventually runs each one exactly once"):
        # The loop the orchestrator runs, without the processes: start whatever
        # can start, then retire one, until nothing is left. A conflicting pair
        # must never be active together, and nothing may be left pending.
        wanted = [targets.parse(t) for t in ("c64u", "u2@c64u", "u64", "u2@u64")]
        pending, active, ran = list(wanted), [], []
        while pending or active:
            started = True
            while started:
                started = False
                for target in list(pending):
                    if runner.schedulable(target, active):
                        for other in active:
                            if target.conflicts_with(other):
                                raise Failure(f"{target} started beside {other}")
                        pending.remove(target)
                        active.append(target)
                        ran.append(target.token)
                        started = True
            if not active:
                raise Failure(f"nothing could start; {len(pending)} targets stuck")
            active.pop(0)
        expect("every target ran once", sorted(ran),
               sorted(t.token for t in wanted))


def run_multi_target_checks(runner):
    """What a child is asked to do, and what the parent makes of the answers."""
    parser = runner.build_parser()

    with check("a child repeats the parent's work against one target"):
        args = parser.parse_args(["--soak", "-m", "telnet,freeze", "-s", "input",
                                  "-s", "printer", "-x", "--manual",
                                  "--no-health-check", "-p", "secret",
                                  "u64", "u2@c64u"])
        command = runner.child_command(args, targets.parse("u2@c64u"), "runs/u2-at-c64u")
        expect("the target is the last word", command[-1], "u2@c64u")
        for expected in ("--soak", "--manual", "--stop-on-fail", "--no-health-check",
                         "--mode", "telnet,freeze", "--password", "secret",
                         "--output-dir", "runs/u2-at-c64u"):
            if expected not in command:
                raise Failure(f"{expected!r} missing from {command}")
        expect("both suites", [command[i + 1] for i, word in enumerate(command)
                               if word == "--suite"], ["input", "printer"])
        if "u64" in command:
            raise Failure(f"a child was given another target's name: {command}")

    with check("every runner option is forwarded to a child or excluded on purpose"):
        # An option added to the parser and forgotten here would make a
        # multi-target run quietly different from the single-target run it is
        # meant to repeat, which is exactly the failure this cannot detect
        # from its output.
        forwarded = set(runner.CHILD_FORWARDED_FLAGS)
        forwarded |= {name for name, _ in runner.CHILD_FORWARDED_VALUES}
        forwarded |= {name for name, _ in runner.CHILD_FORWARDED_NEGATIVE}
        forwarded |= set(runner.CHILD_EXCLUDED_OPTIONS)
        forwarded |= {"suite", "stop_on_fail"}
        known = {action.dest for action in parser._actions
                 if action.dest not in ("help",)}
        missing = sorted(known - forwarded)
        if missing:
            raise Failure("run-tests options neither forwarded to a child nor "
                          f"listed as excluded: {missing}")

    with check("children write into a directory of their own"):
        args = parser.parse_args(["-o", "runs", "u64", "u2@c64u"])
        first = runner.child_command(args, targets.parse("u64"), "runs/u64")
        second = runner.child_command(args, targets.parse("u2@c64u"), "runs/u2-at-c64u")
        expect("first", first[first.index("--output-dir") + 1], "runs/u64")
        expect("second", second[second.index("--output-dir") + 1], "runs/u2-at-c64u")

    with check("concurrent children never tear each other's lines"):
        # Two children writing at once, each line long enough that a pipe read
        # lands in the middle of one. Every line has to come out whole and
        # attributed: a run whose two devices' output is spliced together
        # cannot be read, whatever the exit status says.
        width = 200
        lines_each = 2000
        parent_args = parser.parse_args(["u64", "c64u"])
        printer = ("import sys\n"
                   "for i in range(%d):\n"
                   "    print('%%05d' %% i + 'x' * %d)\n" % (lines_each, width))
        original = runner.child_command
        runner.child_command = lambda args, target, output_dir: [
            sys.executable, "-c", printer]
        captured = io.StringIO()
        try:
            with contextlib.redirect_stdout(captured):
                runner.run_targets(parent_args, [targets.parse("u64"),
                                                 targets.parse("c64u")])
        finally:
            runner.child_command = original
        seen = {"u64": set(), "c64u": set()}
        for line in captured.getvalue().splitlines():
            if not line.startswith("["):
                continue          # the runner's own banner and summary
            token, _, text = line[1:].partition("] ")
            if token not in seen:
                continue
            if len(text) != width + 5 or text[5:] != "x" * width:
                raise Failure(f"torn line from {token}: {line!r}")
            seen[token].add(text[:5])
        for token, numbers in seen.items():
            expect(f"{token} lines, whole and unduplicated",
                   len(numbers), lines_each)

    with check("every line a child printed reaches the run's output"):
        # A child exits with its last lines still in the pipe, and those lines
        # are the summary and the per-suite verdicts. Retiring it on the exit
        # status alone drops exactly the part of the output that says what
        # happened, while the run still reports the right status.
        lines_per_child = 4000
        parent_args = parser.parse_args(["u64", "c64u"])
        printer = ("import sys\n"
                   "for i in range(%d):\n"
                   "    print('line %%d' %% i)\n" % lines_per_child)
        original = runner.child_command
        runner.child_command = lambda args, target, output_dir: [
            sys.executable, "-c", printer]
        captured = io.StringIO()
        try:
            with contextlib.redirect_stdout(captured):
                runner.run_targets(parent_args, [targets.parse("u64"),
                                                 targets.parse("c64u")])
        finally:
            runner.child_command = original
        for token in ("u64", "c64u"):
            printed = sum(1 for line in captured.getvalue().splitlines()
                          if line.startswith(f"[{token}] line "))
            expect(f"{token} printed every line", printed, lines_per_child)

    with check("the run's status is the worst of its children's"):
        expect("all clean", runner.combine_exit_codes([0, 0]), runner.EXIT_OK)
        expect("a failure", runner.combine_exit_codes([0, 1]), runner.EXIT_SUITE_FAILED)
        expect("a recovery", runner.combine_exit_codes([0, 3]), runner.EXIT_RECOVERED)
        expect("a failure outranks a recovery",
               runner.combine_exit_codes([3, 1]), runner.EXIT_SUITE_FAILED)
        expect("an unhealthy device outranks a failure",
               runner.combine_exit_codes([1, 4]), runner.EXIT_DEVICE_UNHEALTHY)
        expect("an unexpected status is a failure",
               runner.combine_exit_codes([0, 2]), runner.EXIT_SUITE_FAILED)
        expect("no children", runner.combine_exit_codes([]), runner.EXIT_OK)


def run_ui_state_routing_checks():
    """The UI-state gate has to drive both halves of a cartridge target.

    It reads the menu from the device under test and injects the keys that back
    out of a nested object into the C64-side computer. Sending both to one host
    is what reported "the root browser is not on top; a nested object still
    holds the UI" for every suite of a U2 run: the cartridge answers
    machine:input with HTTP 501, so nothing the gate pressed ever arrived.
    """
    import importlib.util

    spec = importlib.util.spec_from_file_location(
        "ui_state_under_test",
        os.path.join(ROOT, "tests", "e2e", "lib", "ui_state.py"))
    ui_state = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(ui_state)

    with check("the gate reads the menu from the device and types into the computer"):
        device = ui_state.Device("u2@c64u", None, 1.0)
        expect("device", device.host, "u2")
        expect("input host", device.input_host, "c64u")
        expect("menu screen", device.target.host_for("/v1/machine:menu_screen"), "u2")
        expect("menu button", device.target.host_for("/v1/machine:menu_button"), "u2")
        expect("unwind keys", device.target.host_for("/v1/machine:input"), "c64u")
        expect("reset", device.target.host_for("/v1/machine:reset"), "u2")

    with check("a single-device target still sends everything to one host"):
        device = ui_state.Device("u64", None, 1.0)
        for path in ("/v1/machine:menu_screen", "/v1/machine:input",
                     "/v1/machine:reset"):
            expect(path, device.target.host_for(path), "u64")

    with check("only a cartridge target has a computer menu to get out of the way"):
        # The computer's own menu takes the keyboard while it is open, so a
        # cartridge target has to clear it before any key can reach the
        # cartridge. A single-device target has no second menu, and asking
        # would be a request to the device under test for no reason.
        expect("cartridge", ui_state.Device("u2@c64u", None, 1.0).target.split, True)
        expect("single device",
               ui_state.Device("u64", None, 1.0).computer_menu_open(), False)


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0]
                                     if __doc__ else "")
    report_module = sys.modules["report"]
    report_module.add_colour_argument(parser)
    report_module.apply_colour(parser.parse_args().color)
    if not os.path.isfile(RUNNER_PATH):
        suite_fail("runner_policy_test", f"missing {RUNNER_PATH}")
        return 1
    try:
        runner = load_runner()
        # Under the repository root because run_suite names a suite's script by
        # its path relative to that root, and one directory for the whole suite
        # so that the fixtures have somewhere to write from the first check on.
        with tempfile.TemporaryDirectory(dir=os.path.dirname(RUNNER_PATH)) as tmpdir:
            set_fixture_records(os.path.join(tmpdir, "fixture-records.jsonl"))
            run_target_grammar_checks()
            run_resource_conflict_checks(runner)
            run_multi_target_checks(runner)
            run_ui_state_routing_checks()
            run_exit_status_checks(runner)
            run_recovery_gating_checks(runner)
            run_recovery_limit_checks(runner)
            run_degraded_recovery_checks(runner)
            run_output_dir_option_checks(runner)
            run_reset_guard_checks()
            run_retry_checks(runner, tmpdir)
            run_jsonl_contract_checks(runner, tmpdir)
            run_health_checks()
    except Failure as exc:
        suite_fail("runner_policy_test", str(exc))
        return 1

    detail("the recovery command runs on a degraded or unreachable device, "
           "never on a suite that merely failed")
    suite_ok("runner_policy_test")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
