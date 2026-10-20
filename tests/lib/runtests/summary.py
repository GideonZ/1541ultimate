import os
import sys
import time

import bootstrap
import machine as machine_lib
import report
from runtests.exits import EXIT_DEVICE_UNHEALTHY, exit_code_for
from runtests.identity import harness_hash
from runtests.model import Result

sys.path.insert(0, bootstrap.directory("..", "tools"))
import stale_gates  # noqa: E402  (needs tools/ on sys.path first)


def plural(count: int, noun: str) -> str:
    return f"{count} {noun}" if count == 1 else f"{count} {noun}s"


def report_stale_gates() -> None:
    """After a run under `--assume-fix`, say which entries it found stale.

    The tags ride on the per-suite JSONL files, not on this process's own
    `run.jsonl`, so the whole output directory is read. See tools/stale_gates.py.

    Read from the environment rather than from a flag, for the reason
    `identity.run_identity` already gives: a child run and a run started with
    the variable already exported both have assumptions and neither has the
    flag.
    """
    assumed = machine_lib.parse_assumptions(os.environ.get(machine_lib.ASSUME_ENV, ""))
    if not assumed:
        return
    path = report.JSONL_PATH
    if not path or not os.path.exists(path):
        report.detail("stale gates: not checked, no JSONL was recorded (-j)")
        return
    stale = stale_gates.find_stale(stale_gates.load_run_directory(os.path.dirname(path)))
    if not stale:
        report.detail("stale gates: none")
        return
    for line in stale_gates.render(stale):
        report.detail(line)


def summarise(results: list[Result], started: float, recoveries: int,
              identity: dict[str, object] | None = None) -> int:
    report.banner("Summary")

    groups: dict[str, list[Result]] = {}
    for result in results:
        key = f"{result.category} {result.mode}".strip() if result.category == "e2e" \
            else result.category
        groups.setdefault(key, []).append(result)

    for key, group in groups.items():
        counts = {verdict: sum(1 for r in group if r.verdict == verdict)
                  for verdict in (report.OK, report.WARN, report.FAIL, report.SKIP)}
        seconds = sum(r.seconds for r in group)
        worst = (report.FAIL if counts[report.FAIL] else
                 report.WARN if counts[report.WARN] else report.OK)
        parts = [f"{counts[report.OK]} ok"]
        for verdict, label in ((report.FAIL, "failed"), (report.WARN, "dirty"),
                               (report.SKIP, "skipped")):
            if counts[verdict]:
                parts.append(f"{counts[verdict]} {label}")
        report.detail("%s %-14s %s (%s)"
                      % (report.colour(worst.ljust(4), report._VERDICT_COLOUR[worst]),
                         key, report.format_duration(seconds), ", ".join(parts)))

    for result in results:
        if result.verdict in (report.FAIL, report.WARN, report.SKIP):
            note = {report.FAIL: "failed",
                    report.WARN: result.note
                    or "left the device UI outside the documented state",
                    report.SKIP: "skipped"}[result.verdict]
            if result.device_unhealthy:
                note = "failed; the device could not be made healthy"
            where = f"{result.category} {result.mode}".strip() \
                if result.category == "e2e" else result.category
            report.detail("%s %s %s: %s"
                          % (report.colour(result.verdict.ljust(4),
                                           report._VERDICT_COLOUR[result.verdict]),
                             where, result.name, note))

    for result in results:
        if result.recoveries:
            report.detail(f"{report.colour('RCVR', report.YELLOW)} "
                          f"{result.name}: the device was recovered "
                          f"{plural(result.recoveries, 'time')} around this suite")

    # Where the time went. A run is judged on wall clock as much as on its
    # verdict, and finding the expensive suites otherwise means scrolling a log
    # of several hundred lines. Three is enough to see the shape without the
    # summary turning into a second copy of the run.
    slowest = sorted(results, key=lambda r: r.seconds, reverse=True)[:3]
    if len(results) > 3 and slowest[0].seconds > 0:
        report.detail("slowest: " + ", ".join(
            f"{r.name} {report.format_duration(r.seconds)}" for r in slowest))

    total = time.monotonic() - started
    # The difference between the suites' own time and the run's is the health
    # sweeps, the UI-state gate and the teardown. Naming it stops it reading as
    # unexplained, and makes it obvious when a gate has started misbehaving.
    in_suites = sum(r.seconds for r in results)
    overhead = total - in_suites
    if overhead > 0:
        report.detail(f"{report.format_duration(in_suites)} in suites, "
                      f"{report.format_duration(overhead)} in health checks, "
                      "the UI-state gate and teardown")
    if recoveries:
        report.detail(f"the device was recovered {plural(recoveries, 'time')} "
                      "during the run")
    report.detail(f"{report.format_duration(total)} total")
    report_stale_gates()

    failed = [r for r in results if r.verdict == report.FAIL]
    # The harness this run finished with, against the one it started with. A
    # run whose files changed under it is two runs reported as one, and only
    # the run itself can notice.
    started_with = str((identity or {}).get("harness") or "")
    finished_with = harness_hash()
    if started_with and finished_with and started_with != finished_with:
        # "may not" rather than "did not". The hash covers every tracked file
        # under those roots, documentation included, so a change to it is
        # evidence that the tree moved and not proof that any suite behaved
        # differently. Narrowing the hash to the files a suite executes would
        # need a rule about which those are, and a rule that is wrong misses
        # the case the field exists for.
        report.warn(f"the harness changed during this run: it started on "
                    f"{started_with} and finished on {finished_with}, so the "
                    f"suites may not all have run the same code")
    retried = [r for r in results if r.attempts > 1]
    if retried:
        # In the summary as well as per suite, because a run that took 34
        # attempts to make 30 suites pass is a bench getting worse and no
        # single suite's line says so.
        report.detail(f"{plural(len(retried), 'suite run')} needed more than "
                      f"one attempt, "
                      f"{sum(r.attempts - 1 for r in retried)} extra in all")
    status = exit_code_for(results, recoveries)
    report.run_result(
        verdict=report.FAIL if failed else report.OK,
        suites=len(results),
        passed=sum(1 for r in results if r.verdict == report.OK),
        failed=len(failed),
        skipped=sum(1 for r in results if r.verdict == report.SKIP),
        dirty=sum(1 for r in results if r.verdict == report.WARN),
        seconds=total,
        recoveries=recoveries,
        # What retrying cost this run. Two numbers rather than one, because a
        # bench where one suite always needs three goes and a bench where six
        # suites each need two are different problems and both read as "some
        # retries happened" from either figure alone.
        retried=len(retried),
        extra_attempts=sum(r.attempts - 1 for r in retried),
        harness_changed=bool(started_with and finished_with
                             and started_with != finished_with),
        exit_code=status,
        **(identity or {}))

    if status == EXIT_DEVICE_UNHEALTHY:
        print(f"\n{report.colour('FAIL', report.RED)}  "
              f"the device could not be made healthy; "
              f"{len(failed)} of {len(results)} suite runs failed.")
    elif failed:
        print(f"\n{report.colour('FAIL', report.RED)}  "
              f"{len(failed)} of {len(results)} suite runs failed.")
    elif recoveries:
        print(f"\n{report.colour('WARN', report.YELLOW)}  "
              f"all {len(results)} suite runs passed, but the device had to be "
              f"recovered {plural(recoveries, 'time')}.")
    else:
        print(f"\n{report.colour('OK', report.GREEN)}  "
              f"all {len(results)} suite runs passed.")
    return status
