#!/usr/bin/env python3
"""Build the profile-by-target coverage table from run-tests output directories.

One table per profile: a row per suite, a column per target, each cell the
number of checks that suite ran there and the seconds it took. It answers two
questions a profile ladder has to answer to be worth having, and answers them
from recorded runs rather than from intent: what does each profile cost on each
machine, and what does the extra time buy in checks.

Usage:

    tools/e2e/profile_matrix.py smoke=DIR quick=DIR standard=DIR > table.md

Each DIR is a `run-tests -o` output directory holding one subdirectory per
target. A directory may hold any subset of the targets; a target that did not
run that profile is left blank rather than counted as zero.
"""
from __future__ import annotations

import argparse
import json
import os
import sys
from typing import Dict, List, Optional, Tuple

# The column order, and the label each target is written under. Anything else
# found in a run directory is appended after these, so a new bench machine
# shows up without editing this.
TARGET_ORDER = ("c64u", "u64", "u2@c64u")
DIR_TO_TARGET = {"c64u": "c64u", "u64": "u64", "u2-at-c64u": "u2@c64u"}


class Suite:
    __slots__ = ("checks", "seconds", "verdict", "attempts")

    def __init__(self, checks: int, seconds: float, verdict: str, attempts: int):
        self.checks = checks
        self.seconds = seconds
        self.verdict = verdict
        self.attempts = attempts


def read_run(directory: str) -> Dict[str, Dict[str, Suite]]:
    """{target: {suite: Suite}} for one output directory."""
    out: Dict[str, Dict[str, Suite]] = {}
    for entry in sorted(os.listdir(directory)):
        path = os.path.join(directory, entry)
        if not os.path.isdir(path):
            continue
        target = DIR_TO_TARGET.get(entry, entry)
        # run.jsonl carries the runner's own view: wall seconds per suite run,
        # the verdict it recorded, and which attempt it was. The per-suite
        # files carry the check counts, which the runner cannot see because
        # each suite is its own process.
        runs: Dict[str, Tuple[float, str, int]] = {}
        run_path = os.path.join(path, "run.jsonl")
        if os.path.exists(run_path):
            for line in open(run_path, errors="replace"):
                try:
                    record = json.loads(line)
                except ValueError:
                    continue
                if record.get("kind") != "suite" or record.get("suite") != "run-tests":
                    continue
                name = record.get("name")
                attempt = int(record.get("attempt") or 1)
                seconds, _, previous = runs.get(name, (0.0, "", 0))
                # A retried suite is charged every attempt it took, because
                # that is what the run actually spent on it.
                runs[name] = (seconds + float(record.get("seconds") or 0.0),
                              str(record.get("verdict") or ""),
                              max(previous, attempt))
        checks: Dict[str, int] = {}
        for name in sorted(os.listdir(path)):
            if not name.endswith(".jsonl") or name in ("run.jsonl", "interactions.jsonl"):
                continue
            suite_name: Optional[str] = None
            count = 0
            for line in open(os.path.join(path, name), errors="replace"):
                try:
                    record = json.loads(line)
                except ValueError:
                    continue
                if record.get("kind") == "suite":
                    suite_name = record.get("suite") or suite_name
                    count = int(record.get("checks") or 0)
            if suite_name:
                # Last attempt wins: it is the one whose checks the run kept.
                checks[suite_name] = count
        out[target] = {
            name: Suite(checks.get(name, 0), seconds, verdict, attempt)
            for name, (seconds, verdict, attempt) in runs.items()
        }
    return out


def cell(suite: Optional[Suite]) -> str:
    if suite is None:
        return "-"
    mark = {"OK": "", "SKIP": " skip", "WARN": " warn"}.get(suite.verdict, " FAIL")
    retried = f" x{suite.attempts}" if suite.attempts > 1 else ""
    return f"{suite.checks} ({suite.seconds:.0f}s){mark}{retried}"


def table(profile: str, data: Dict[str, Dict[str, Suite]]) -> List[str]:
    targets = [t for t in TARGET_ORDER if t in data]
    targets += [t for t in sorted(data) if t not in targets]
    names: List[str] = []
    for target in targets:
        for name in data[target]:
            if name not in names:
                names.append(name)
    names.sort()

    lines = [f"### `--profile {profile}`", "",
             "| Suite | " + " | ".join(f"`{t}`" for t in targets) + " |",
             "| --- | " + " | ".join("---" for _ in targets) + " |"]
    for name in names:
        row = [cell(data[target].get(name)) for target in targets]
        lines.append(f"| `{name}` | " + " | ".join(row) + " |")

    totals, checks_total, suite_total = [], [], []
    for target in targets:
        suites = data[target].values()
        totals.append(f"**{sum(s.seconds for s in suites):.0f}s**")
        checks_total.append(f"**{sum(s.checks for s in suites)}**")
        suite_total.append(f"**{len(data[target])}**")
    lines.append("| **suites** | " + " | ".join(suite_total) + " |")
    lines.append("| **checks** | " + " | ".join(checks_total) + " |")
    lines.append("| **total** | " + " | ".join(totals) + " |")
    lines.append("")
    return lines


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("runs", nargs="+", metavar="PROFILE=DIR",
                        help="a profile name and the run-tests output "
                             "directory that profile produced")
    args = parser.parse_args()

    out: List[str] = []
    summary: List[Tuple[str, Dict[str, Dict[str, Suite]]]] = []
    for pair in args.runs:
        if "=" not in pair:
            parser.error(f"{pair!r} is not PROFILE=DIR")
        profile, directory = pair.split("=", 1)
        if not os.path.isdir(directory):
            parser.error(f"{directory!r} is not a directory")
        data = read_run(directory)
        summary.append((profile, data))
        out.extend(table(profile, data))

    targets: List[str] = []
    for _, data in summary:
        for target in TARGET_ORDER:
            if target in data and target not in targets:
                targets.append(target)
    out.append("### Totals")
    out.append("")
    out.append("| Profile | " + " | ".join(f"`{t}`" for t in targets)
               + " | Suites | Checks |")
    out.append("| --- | " + " | ".join("---" for _ in targets) + " | ---: | ---: |")
    for profile, data in summary:
        row = []
        for target in targets:
            if target not in data:
                row.append("-")
                continue
            row.append(f"{sum(s.seconds for s in data[target].values()):.0f}s")
        widest = max((len(v) for v in data.values()), default=0)
        checks = max((sum(s.checks for s in v.values()) for v in data.values()),
                     default=0)
        out.append(f"| `{profile}` | " + " | ".join(row)
                   + f" | {widest} | {checks} |")
    print("\n".join(out))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
