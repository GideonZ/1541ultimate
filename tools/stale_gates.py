#!/usr/bin/env python3
"""Which `machine.FIXES` entries a run under `--assume-fix` found stale.

    python3 tools/stale_gates.py RUN.jsonl

A gate tagged with `machine.FIXES` skips rather than fails, and that is also
how a check for a since-fixed firmware gap stays disabled: nothing says so
when the gap closes, and a stale gate reads exactly like a gap that is still
there. `--assume-fix` makes `Machine.skip_without_fix` run the check instead
of skipping it, and `report.note_assumed_fix` tags that check's own JSONL
record with the entry and the machine it stood in for (see
`tests/lib/machine.py`'s module docstring).

This reads those tags back. A tagged check is evidence either way: passing
every time it ran says the machine now has the fix, so the entry naming it is
stale; failing even once says the gap is still real. A tagged check that
never ran (skipped for an unrelated reason, or the process died first) is
neither and is left out, because absence is not evidence.

Standard library only, so a CI step or an agent can run this over a
downloaded `run.jsonl` with nothing else installed.
"""

from __future__ import annotations

import argparse
import json
import sys
from collections import defaultdict
from dataclasses import dataclass


@dataclass(frozen=True)
class Stale:
    """One `machine.FIXES` entry a run found no longer needed.

    `checks` is every distinct check label that ran under the assumption and
    passed every time, so the reader can see what was actually exercised
    rather than take the verdict on trust.
    """

    fix: str
    machine: str
    checks: tuple[str, ...]


def find_stale(records) -> list[Stale]:
    """The FIXES entries every one of whose tagged checks passed.

    `records` is whatever a run's JSONL decodes to: one dict per line, in any
    order, mixing check records with everything else a run writes. Only
    records with `kind == "check"` and a `fix` field carry the tag; the rest
    are ignored rather than required to be absent, so this can be handed a
    whole run's records instead of a pre-filtered list.
    """
    by_entry: dict[tuple[str, str], list[dict]] = defaultdict(list)
    for record in records:
        if record.get("kind") != "check" or "fix" not in record:
            continue
        by_entry[(record["fix"], record["machine"])].append(record)

    stale = []
    for (fix, machine), checks in sorted(by_entry.items()):
        if all(c.get("verdict") == "OK" for c in checks):
            labels = tuple(sorted({c.get("label", "") for c in checks}))
            stale.append(Stale(fix=fix, machine=machine, checks=labels))
    return stale


def render(stale: list[Stale]) -> list[str]:
    """One line per entry, in the shape a console summary or a log wants."""
    return [
        f"STALE {entry.fix} on {entry.machine}: "
        f"{', '.join(entry.checks)} now passes assumed present"
        for entry in stale
    ]


def load_records(path: str):
    """Every line of a JSONL file that parses, in order.

    A malformed line costs nothing here: this reads a file a run is still
    writing as often as a finished one, and a torn last line must not hide
    every entry before it.
    """
    records = []
    with open(path, encoding="utf-8") as handle:
        for line in handle:
            line = line.strip()
            if not line:
                continue
            try:
                records.append(json.loads(line))
            except json.JSONDecodeError:
                continue
    return records


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("jsonl", help="path to a run's JSONL file")
    args = parser.parse_args()

    stale = find_stale(load_records(args.jsonl))
    if not stale:
        print("no stale gates")
        return 0
    for line in render(stale):
        print(line)
    return 0


if __name__ == "__main__":
    sys.exit(main())
