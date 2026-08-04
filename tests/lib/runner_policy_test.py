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

import importlib.machinery
import importlib.util
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
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
    made = runner.Device("device.invalid", "", 1.0, **kwargs)
    made.probe = ScriptedProbe(answers)
    return made


def expect(label, actual, wanted):
    if actual != wanted:
        raise Failure(f"{label}: got {actual!r}, expected {wanted!r}")


def run_exit_status_checks(runner):
    passed = runner.Result("e2e", "overlay", "passed", runner.report.OK, 1.0)
    failed = runner.Result("e2e", "overlay", "failed", runner.report.FAIL, 1.0)
    lost = runner.Result("e2e", "overlay", "lost", runner.report.FAIL, 1.0,
                         device_lost=True)

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

    with check("a lost device exits 4, outranking a failure"):
        expect("lost", runner.exit_code_for([failed, lost], 1), runner.EXIT_DEVICE_LOST)


def run_recovery_gating_checks(runner):
    with check("a device that answers is never recovered"):
        made = device(runner, [True], recover_command="true", recover_attempts=1)
        expect("reachable", made.ensure_reachable(0.0), True)
        expect("recoveries", made.recoveries, 0)

    with check("recovery runs only once the ordinary wait has given up"):
        made = device(runner, [False, True], recover_command="true", recover_attempts=1)
        expect("reachable", made.ensure_reachable(0.0), True)
        expect("recoveries", made.recoveries, 1)

    with check("a recovery that does not bring the device back reports so"):
        made = device(runner, [], recover_command="true", recover_attempts=1)
        expect("reachable", made.ensure_reachable(0.0), False)
        expect("recoveries", made.recoveries, 1)
        expect("attempts left", made.recover_attempts_left, 0)

    with check("attempts are spent rather than retried forever"):
        made = device(runner, [], recover_command="true", recover_attempts=1)
        made.ensure_reachable(0.0)
        expect("second call recovers again", made.ensure_reachable(0.0), False)
        expect("recoveries", made.recoveries, 1)

    with check("a recovery command that exits non-zero is still followed by a probe"):
        # Some recovery tools report a failure the device has already come back
        # from, so the probe decides rather than the exit status.
        made = device(runner, [False, True], recover_command="false", recover_attempts=1)
        expect("reachable", made.ensure_reachable(0.0), True)
        expect("recoveries", made.recoveries, 1)

    with check("a recovery command that hangs is bounded by --recover-timeout"):
        made = device(runner, [False, True], recover_command="sleep 30",
                      recover_attempts=1, recover_timeout=0.2)
        expect("reachable", made.ensure_reachable(0.0), False)
        expect("recoveries", made.recoveries, 1)

    with check("a recovery command the shell cannot find is a failed recovery"):
        # The command is run through a shell, so an unrunnable one comes back
        # as exit 127 rather than as an exception. Its own stderr is silenced
        # here only to keep this check to one line; a real run wants to see it.
        made = device(runner, [], recover_attempts=1,
                      recover_command="/definitely/not/a/command 2>/dev/null")
        expect("reachable", made.ensure_reachable(0.0), False)
        expect("recoveries", made.recoveries, 1)

    with check("no recovery command means no recovery"):
        made = device(runner, [])
        expect("reachable", made.ensure_reachable(0.0), False)
        expect("recoveries", made.recoveries, 0)


def main():
    if not os.path.isfile(RUNNER_PATH):
        suite_fail("runner_policy_test", f"missing {RUNNER_PATH}")
        return 1
    try:
        runner = load_runner()
        run_exit_status_checks(runner)
        run_recovery_gating_checks(runner)
    except Failure as exc:
        suite_fail("runner_policy_test", str(exc))
        return 1

    detail("the recovery command runs only on an unreachable device, never on a "
           "failed suite")
    suite_ok("runner_policy_test")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
