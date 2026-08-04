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
import os
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import health  # noqa: E402
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


def device(runner, answers, **kwargs):
    # Health is reachability alone unless a check stubs the sweep: these
    # fixtures have no device, so a real sweep would fail every time and tell
    # us nothing about the decision under test.
    kwargs.setdefault("health_check", False)
    kwargs.setdefault("recover_max_per_suite", 1)
    kwargs.setdefault("recover_max_total", 1)
    made = runner.Device("device.invalid", "", 1.0, **kwargs)
    made.probe = ScriptedProbe(answers)
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
                    soak_profile="stress", jsonl_dir="", stop_on_fail=False,
                    health_check=False, retry=True, recover_command="true",
                    recover_max_per_suite=2, recover_max_total=10,
                    recover_timeout=5.0)
        base.update(kwargs)
        return runner.Options(**base)

    def quietly(action):
        """Run `action`, holding its output back unless it is needed.

        run_suite prints a banner and a suite line of its own, which would land
        in the middle of the check line reporting on it. The output is kept and
        replayed only when the check fails, where it is the diagnostic.
        """
        captured = io.StringIO()
        try:
            with contextlib.redirect_stdout(captured):
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


def main():
    if not os.path.isfile(RUNNER_PATH):
        suite_fail("runner_policy_test", f"missing {RUNNER_PATH}")
        return 1
    try:
        runner = load_runner()
        run_exit_status_checks(runner)
        run_recovery_gating_checks(runner)
        run_recovery_limit_checks(runner)
        run_degraded_recovery_checks(runner)
        with tempfile.TemporaryDirectory(dir=os.path.dirname(RUNNER_PATH)) as tmpdir:
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
