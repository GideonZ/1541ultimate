import signal
import sys
from collections.abc import Sequence

import report
from runtests.model import Result


# Exit statuses. Documented in --help and in tests/README.md, because a
# programmatic caller reads these rather than the console output.
# The exit status is a severity scale, and it is ordered, so `[ $? -le 2 ]`
# means "I will tolerate a retry and a recovery" and reads as it says. A
# caller comparing the status with an ordering operator is doing the right
# thing, which is why the values are in this order rather than in the order
# they were thought of.
EXIT_OK = 0
EXIT_RETRIED = 1
EXIT_RECOVERED = 2
EXIT_SUITE_FAILED = 3
EXIT_DEVICE_UNHEALTHY = 4
# A usage error is not an outcome of a run, so it is not on the scale. 64 is
# `EX_USAGE` from sysexits.h, which is the established value for exactly this
# and is far enough above the scale that no threshold reaches it.
EXIT_USAGE = 64


def die(message: str) -> None:
    print(report.colour(f"ERROR: {message}", report.RED), file=sys.stderr, flush=True)
    raise SystemExit(EXIT_USAGE)


def describe_exit(returncode: int) -> str:
    """Why a suite's exit status is not zero, or "" when it merely failed.

    A suite that ended by itself has already said what went wrong: every check
    prints its own verdict, and report.check prints one for any exception it
    catches. A suite that was terminated printed nothing, because a signal
    raises nothing to catch, so the runner's own line is the only record of it
    and has to carry the reason. subprocess reports this as a negative status
    (Popen.returncode: "a negative value -N indicates the child was terminated
    by signal N"), which is the only place the distinction survives.

    Seen live: a stray `pkill -f prg_context_menu_test` aimed at another
    device's run killed this one's copy of the same suite. The log ended in the
    middle of a check line and the run said only "prg-context-menu: failed",
    and the suite passed on the next run against the same firmware.
    """
    if returncode >= 0:
        return ""
    try:
        name = signal.Signals(-returncode).name
    except ValueError:
        name = f"signal {-returncode}"
    return f"killed by {name}, so it reported nothing of its own"


def exit_code_for(results: list[Result], recoveries: int) -> int:
    """The run's status, worst thing first.

    A device that could not be made healthy outranks a failed suite: it says
    nothing could run rather than something did not hold. A recovery outranks
    a retry, because a device that had to be brought back is a stronger signal
    than a suite that needed a second go, and a retry outranks a clean run
    because a caller polling only `$?` would otherwise never learn that four
    suites needed three attempts each.
    """
    if any(r.device_unhealthy for r in results):
        return EXIT_DEVICE_UNHEALTHY
    if any(r.verdict == report.FAIL for r in results):
        return EXIT_SUITE_FAILED
    if recoveries:
        return EXIT_RECOVERED
    if any(r.attempts > 1 for r in results):
        return EXIT_RETRIED
    return EXIT_OK


def combine_exit_codes(codes: Sequence[int]) -> int:
    """One status for the whole run, worst thing first.

    The same order a single run uses, for the same reason: a device that could
    not be made healthy says nothing ran, which outranks a suite that failed,
    which outranks a run that needed the device brought back.
    """
    if not codes:
        return EXIT_OK
    known = (EXIT_OK, EXIT_RETRIED, EXIT_RECOVERED, EXIT_SUITE_FAILED,
             EXIT_DEVICE_UNHEALTHY)
    if any(code not in known for code in codes):
        # A child that exited with something this runner does not produce
        # crashed or was killed, and a run whose child did that has not
        # passed. Reported as a failed suite rather than as a new status,
        # because that is what it is from the run's point of view.
        return EXIT_SUITE_FAILED
    # The statuses are a severity scale, so the worst of them is the largest.
    return max(codes)
