#!/usr/bin/env python3
# Gate check: the harness that watches an E2E gate run.

"""Verify the observability harness, without a device.

Everything the observability design adds is code the gate's own verdicts
cannot exercise: a component there may not change a suite verdict, a health
verdict or the process exit status, so nothing notices when one of them
breaks. A defect here is not found by a failing gate. It is found weeks later
by somebody who opened an artifact to diagnose a firmware defect and found an
empty report, a black recording or a log with half its lines, by which time
the run is gone.

Four tiers, and every piece of logic belongs to exactly one:

    1 pure       functions over bytes and records: rendering, interval and
                 timecode arithmetic, ANSI stripping, datagram attribution
    2 component  one component against the device double over real sockets
    3 pipeline   a whole scripted run with no real device, producing an output tree
                 with the current runner, then the report generated from it
    4 golden     the report generated from a fixture built for the run,
                 compared with the checked-in expected document

Tier 3 is the one the others cannot replace: the report generator can be
perfect against a fixture the runner no longer writes. Tier 4 is the one that
makes a rendering change visible in review, because a diff of the expected
document is exactly the diff a reader of a real report would see.

Runs three ways, one implementation: `make observability_test`, the
`observability` entry in the `SUITES` registry in `run-tests`, and a step in
.github/workflows/build.yml. It reports through tests/lib/report.py like every
other registered suite rather than through unittest, because a registered
suite that printed unittest output would break the one rule
tests/lib/README.md says is not negotiable.
"""

import sys
from pathlib import Path

# The one stanza that puts the shared library on sys.path; see tests/lib/bootstrap.py.
sys.path.insert(0, str(next(p for p in Path(__file__).resolve().parents
                            if (p / "tests" / "lib").is_dir()) / "tests" / "lib"))
import bootstrap  # noqa: E402,F401

# The tiers live in tests/lib/observability, each written against support.py.
sys.path.insert(0, bootstrap.directory("lib", "observability"))

from report import Failure
import argparse
import concurrent.futures
import report

# Importing a tier is what registers its cases, and in tier order so CASES
# is in the order run_cases takes them. All four, so this file runs what it
# always ran: `make observability_test`, the Check Observability Harness step
# in .github/workflows/build.yml, and the `observability` suite in run-tests
# all invoke exactly this.
import pure_test  # noqa: E402,F401  (tier 1)
import component_test  # noqa: E402,F401  (tier 2)
import pipeline_test  # noqa: E402,F401  (tier 3)
import golden_test  # noqa: E402,F401  (tier 4)
from support import (  # noqa: E402
    TIERS, Case, Skipped, _execute_case, _report_outcome,
    prewarm_fixture, record_fixture, selected)


def run_case(entry: Case) -> str:
    """One case, reported as one check. Never lets an exception end the suite."""
    report.check_start(entry.label)
    try:
        extra = entry.run() or ""
    except Skipped as exc:
        report.check_skip(str(exc))
        return report.SKIP
    except Failure as exc:
        report.check_fail(str(exc))
        return report.FAIL
    except Exception as exc:  # noqa: BLE001 - a case must not end the suite
        report.check_fail(f"{type(exc).__name__}: {report.format_exception(exc)}")
        return report.FAIL
    report.check_ok(str(extra))
    return report.OK


def run_cases(cases: list[Case]) -> list[str]:
    """Run a tier's cases, its non-exclusive ones concurrently, in registration order.

    A non-exclusive case runs on a worker thread from a pool; its result is
    reported here on the main thread once its future resolves. An exclusive
    case runs the plain sequential way instead, through `run_case`, right here
    in its normal place in the order: check_start before its body runs and its
    verdict after, exactly as when every case ran one at a time.

    The cases are therefore submitted in runs rather than all at once. Every
    worker started before an exclusive case has resolved before that case
    begins, and no worker is submitted until it has finished. Submitting the
    whole tier up front instead left workers running alongside the exclusive
    case, which is what "exclusive" is meant to rule out: a case that mutates
    os.environ, swaps report._default or redirects stdout does all three
    process-wide, so a worker overlapping it reads the substitute state.

    That is what a case mutating shared state needs to never overlap another
    case touching the same state, and what the one case that calls
    report.detail() itself needs for that call to queue under its own check
    rather than print out of place - report.detail() only holds a line back
    for the check it belongs to if that check's line is already open.
    """
    verdicts: list[str] = []
    with concurrent.futures.ThreadPoolExecutor() as pool:
        pending: list[tuple[Case, concurrent.futures.Future]] = []

        def drain() -> None:
            for entry, future in pending:
                verdicts.append(_report_outcome(entry, future.result()))
            pending.clear()

        for entry in cases:
            if entry.exclusive:
                drain()
                verdicts.append(run_case(entry))
            else:
                pending.append((entry, pool.submit(_execute_case, entry)))
        drain()
    return verdicts


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--tier", action="append", type=int, default=[],
                        help="Run only this tier. Repeatable.")
    parser.add_argument("--record-fixture", action="store_true",
                        help="Re-record tests/lib/fixtures/ from a scripted run "
                             "and regenerate the expected document.")
    parser.add_argument("-k", "--only", action="append", default=[],
                        help="Run only cases whose label or requirement matches. "
                             "Repeatable.")
    report.add_colour_argument(parser)
    args = parser.parse_args(argv)
    report.apply_colour(args.color)
    if args.record_fixture:
        return record_fixture()

    verdicts: list[str] = []
    for tier, title in TIERS:
        cases = [c for c in selected(args.tier, args.only) if c.tier == tier]
        if not cases:
            continue
        report.section(f"tier {tier}: {title}")
        if tier == 4:
            prewarm_fixture()
        verdicts += run_cases(cases)

    failed = verdicts.count(report.FAIL)
    skipped = verdicts.count(report.SKIP)
    note = f"{len(verdicts)} cases" + (f", {skipped} skipped" if skipped else "")
    if failed:
        report.suite_fail("observability", f"{failed} of {note} failed")
        return 1
    report.suite_ok("observability", note)
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
