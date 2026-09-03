"""What each profile covers: statically from the registry, measured from runs.

Two views, deliberately separate, because they can answer different questions
and only one of them can be trusted about time.

*Static* is what the registry knows before anything runs: which suites a
profile selects, how many transports it sweeps, and whether it includes the
suites an ordinary run leaves out. That is knowable exactly, and it is what
`--list-profiles` prints.

*Measured* is what recorded runs say: how many scenarios and checks a suite
actually ran on a given machine, and how long it took. That is the only honest
source for either number. A suite's checks are `check_start` calls made while
it runs, and how many of them happen depends on the machine: a declared
firmware gap turns a scenario into one skip, a listing's length decides how
many rows a matrix walks. Nothing here estimates a duration or a check count
from the registry, because the registry does not know them and a plausible
wrong number is worse than a blank.

The vocabulary is report.py's, used consistently: a **suite** is one registered
executable, a **scenario** is a named group inside it (`report.section`), and a
**check** is one reported outcome (`report.check_start` and its verdict).
"""

from __future__ import annotations

import json
import os
from collections.abc import Iterable, Sequence

import profiles

# Which target a run directory's subdirectory name refers to. run-tests writes
# one subdirectory per target, with "@" spelled "-at-" so it is a legal path.
DIR_TO_TARGET = {"u2-at-c64u": "u2@c64u"}
# The order targets are written in, when a run holds more than one.
TARGET_ORDER = ("c64u", "u64", "u2@c64u")


def target_of(directory: str) -> str:
    return DIR_TO_TARGET.get(directory, directory)


# ----------------------------------------------------------------- static --

class Coverage:
    """Which suites each profile selects, from the registry alone."""

    def __init__(self, suites: Sequence[object], names: Sequence[str],
                 categories: Sequence[str]) -> None:
        self.names = list(names)
        self.categories = list(categories)
        self.suites = list(suites)
        # {profile: {suite name}}, by the same rule the runner selects with:
        # a suite runs from its own profile up, and the manual ones only where
        # the profile includes them.
        self.included: dict[str, list[str]] = {}
        for profile in self.names:
            chosen = []
            for suite in self.suites:
                if suite.manual and not profiles.includes_manual(profile):
                    continue
                if not profiles.includes(suite.profile, profile):
                    continue
                chosen.append(suite.name)
            self.included[profile] = chosen

    def transports(self, profile: str) -> Sequence[str]:
        return profiles.modes_for(profile)

    def in_category(self, profile: str, category: str) -> list[str]:
        by_name = {s.name: s for s in self.suites}
        return [n for n in self.included[profile]
                if by_name[n].category == category]

    def passes(self, profile: str) -> int:
        """Suite runs a profile performs: one per suite per transport swept.

        The transports multiply everything that drives the menu, which is what
        makes the deeper profiles expensive, so this is the number that
        explains the ladder rather than the suite count.
        """
        return len(self.included[profile]) * len(self.transports(profile))


def build(suites: Sequence[object], categories: Sequence[str],
          names: Sequence[str] | None = None) -> Coverage:
    return Coverage(suites, names or profiles.ORDER, categories)


def render_static(cover: Coverage, width: int = 30) -> str:
    """The inclusion matrix: a row per suite, a column per profile."""
    columns = cover.names
    head = " " * width + "".join(f"{name[:6]:>7}" for name in columns)
    lines = ["Profiles, shallowest first. A suite runs from its own profile up.",
             "'x' selected, '.' not. A profile sweeping two transports runs "
             "every suite twice.", "", head,
             " " * width + "".join(f"{'-' * 6:>7}" for _ in columns)]

    def row(label: str, cells: Iterable[str]) -> str:
        return f"  {label:<{width - 2}}" + "".join(f"{c:>7}" for c in cells)

    lines.append(row("transports", (str(len(cover.transports(p))) for p in columns)))
    lines.append(row("manual suites",
                     ("yes" if profiles.includes_manual(p) else "-" for p in columns)))
    lines.append(row("default", ("yes" if p == profiles.DEFAULT else "-"
                                 for p in columns)))
    for category in cover.categories:
        members = sorted({n for p in columns for n in cover.in_category(p, category)})
        if not members:
            continue
        lines.append("")
        lines.append(f"{category}:")
        for name in members:
            lines.append(row(name, ("x" if name in cover.included[p] else "."
                                    for p in columns)))
    lines.append("")
    lines.append(" " * width + "".join(f"{'-' * 6:>7}" for _ in columns))
    lines.append(row("suites", (str(len(cover.included[p])) for p in columns)))
    lines.append(row("suite runs", (str(cover.passes(p)) for p in columns)))
    lines.append("")
    lines.append("Scenario and check counts, and durations, are not shown here:")
    lines.append("the registry does not know them. They depend on the machine "
                 "and are")
    lines.append("only knowable by running, so they come from --measured.")
    return "\n".join(lines)


def static_payload(cover: Coverage) -> dict:
    {s.name: s for s in cover.suites}
    return {
        "default_profile": profiles.DEFAULT,
        "order": list(cover.names),
        "profiles": {
            profile: {
                "transports": list(cover.transports(profile)),
                "includes_manual": profiles.includes_manual(profile),
                "suites": cover.included[profile],
                "suite_count": len(cover.included[profile]),
                "suite_runs": cover.passes(profile),
            }
            for profile in cover.names
        },
        "suites": {
            suite.name: {
                "category": suite.category,
                "profile": suite.profile,
                "manual": suite.manual,
                "path": suite.path,
            }
            for suite in cover.suites
        },
    }


# --------------------------------------------------------------- measured --

class Measured:
    """What one suite did on one machine, from a recorded run."""

    __slots__ = ("attempts", "checks", "scenarios", "seconds", "verdict")

    def __init__(self, scenarios: int, checks: int, seconds: float,
                 verdict: str, attempts: int) -> None:
        self.scenarios = scenarios
        self.checks = checks
        self.seconds = seconds
        self.verdict = verdict
        self.attempts = attempts


def read_run(directory: str) -> dict[str, dict[str, Measured]]:
    """{target: {suite: Measured}} for one `run-tests -o` directory."""
    out: dict[str, dict[str, Measured]] = {}
    for entry in sorted(os.listdir(directory)):
        path = os.path.join(directory, entry)
        if not os.path.isdir(path):
            continue
        out[target_of(entry)] = _read_target(path)
    return out


def _read_target(path: str) -> dict[str, Measured]:
    # run.jsonl is the runner's own view: wall seconds per suite run, the
    # verdict, and which attempt. The per-suite files carry the scenario and
    # check counts, which the runner cannot see because each suite is its own
    # process.
    runs: dict[str, tuple[float, str, int]] = {}
    run_path = os.path.join(path, "run.jsonl")
    if os.path.exists(run_path):
        for record in _records(run_path):
            if record.get("kind") != "suite" or record.get("suite") != "run-tests":
                continue
            name = record.get("name")
            seconds, _, previous = runs.get(name, (0.0, "", 0))
            runs[name] = (
                # Charged every attempt it took, because that is what the run
                # actually spent on it.
                seconds + float(record.get("seconds") or 0.0),
                str(record.get("verdict") or ""),
                max(previous, int(record.get("attempt") or 1)),
            )

    counts: dict[str, tuple[int, int]] = {}
    for name in sorted(os.listdir(path)):
        if not name.endswith(".jsonl") or name in ("run.jsonl", "interactions.jsonl"):
            continue
        suite_name: str | None = None
        running = 0
        final: tuple[int, int] = (0, 0)
        for record in _records(os.path.join(path, name)):
            kind = record.get("kind")
            if kind == "scenario":
                running += 1
            elif kind == "suite":
                # A suite record ends an attempt. A retried suite writes every
                # attempt to the same file, and the last one is the result the
                # run kept, so each attempt replaces the one before it and the
                # scenario counter restarts.
                suite_name = record.get("suite") or suite_name
                final = (running, int(record.get("checks") or 0))
                running = 0
        if suite_name:
            counts[suite_name] = final

    return {
        name: Measured(counts.get(name, (0, 0))[0], counts.get(name, (0, 0))[1],
                       seconds, verdict, attempt)
        for name, (seconds, verdict, attempt) in runs.items()
    }


def _records(path: str) -> Iterable[dict]:
    for line in open(path, errors="replace"):
        try:
            yield json.loads(line)
        except ValueError:
            continue


def _targets_of(data: dict[str, dict[str, Measured]]) -> list[str]:
    ordered = [t for t in TARGET_ORDER if t in data]
    return ordered + [t for t in sorted(data) if t not in ordered]


def _cell(entry: Measured | None) -> str:
    if entry is None:
        return "-"
    mark = {"OK": "", "SKIP": " skip", "WARN": " warn"}.get(entry.verdict, " FAIL")
    retried = f" x{entry.attempts}" if entry.attempts > 1 else ""
    return f"{entry.checks} ({entry.seconds:.0f}s){mark}{retried}"


def render_measured(runs: Sequence[tuple[str, dict[str, dict[str, Measured]]]],
                    markdown: bool = True) -> str:
    """One table per profile: a row per suite, a column per target."""
    out: list[str] = []
    for profile, data in runs:
        targets = _targets_of(data)
        names = sorted({n for target in targets for n in data[target]})
        if markdown:
            out.append(f"#### `--profile {profile}`")
            out.append("")
            out.append("| Suite | " + " | ".join(f"`{t}`" for t in targets) + " |")
            out.append("| --- | " + " | ".join("---" for _ in targets) + " |")
            for name in names:
                out.append(f"| `{name}` | "
                           + " | ".join(_cell(data[t].get(name)) for t in targets)
                           + " |")
            for label, value in (
                ("suites", lambda t: str(len(data[t]))),
                ("scenarios", lambda t: str(sum(m.scenarios for m in data[t].values()))),
                ("checks", lambda t: str(sum(m.checks for m in data[t].values()))),
                ("total", lambda t: f"{sum(m.seconds for m in data[t].values()):.0f}s"),
            ):
                out.append(f"| **{label}** | "
                           + " | ".join(f"**{value(t)}**" for t in targets) + " |")
            out.append("")
        else:
            width = max([len(n) for n in names] + [12]) + 2
            out.append(f"--profile {profile}")
            out.append(" " * width + "".join(f"{t:>18}" for t in targets))
            for name in names:
                out.append(f"  {name:<{width - 2}}"
                           + "".join(f"{_cell(data[t].get(name)):>18}"
                                     for t in targets))
            out.append(f"  {'total':<{width - 2}}"
                       + "".join(f"{sum(m.seconds for m in data[t].values()):>17.0f}s"
                                 for t in targets))
            out.append("")
    return "\n".join(out)


def measured_payload(
        runs: Sequence[tuple[str, dict[str, dict[str, Measured]]]]) -> dict:
    return {
        profile: {
            target: {
                name: {
                    "scenarios": entry.scenarios,
                    "checks": entry.checks,
                    "seconds": round(entry.seconds, 3),
                    "verdict": entry.verdict,
                    "attempts": entry.attempts,
                }
                for name, entry in suites.items()
            }
            for target, suites in data.items()
        }
        for profile, data in runs
    }
