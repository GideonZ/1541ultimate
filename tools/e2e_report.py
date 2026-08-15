#!/usr/bin/env python3
"""Turn an E2E run's `-j` directory into one Markdown report.

    python3 tools/e2e_report.py DIR

Reads the JSONL, the captured console logs and the captures a run left in DIR,
and writes DIR/index.md covering the whole run, every target included. It takes
no device connection and runs after a run has finished.

Three readers, all three first class, and the document is shaped by the third:

    a person   opens a build page, then downloads the artifact
    a program  greps it, or parses the JSONL it names
    an agent   is handed the whole thing and asked why a run failed

The agent is the one that changes the design. It has no device, no session and
no way to ask a follow-up question, so whatever the run captured is the entire
evidence base and whatever this document does not say has to be reachable from
a file it names. That is why there is one entry point rather than a set of
links, why the status line is machine-readable, why a screen is a fenced text
block rather than an image, and why the same identity key names a check in
every artefact.

Two rules govern every line of it. The document is deterministic, so two runs
over one tree produce identical bytes and last night's report diffs against
tonight's. And it states facts the run recorded and never a diagnosis: a wrong
guess printed in a fixed format is read as a finding and costs more than an
absent one.

It renders whatever tree it is given and never fails on missing or malformed
input. A run killed mid-suite is exactly when the evidence matters most, so
refusing the tree would remove the report at the worst moment.

Standard library only, plus tests/lib/report.py for the duration format and the
slow-check threshold, so a duration here and a duration on the console are the
same string.
"""

from __future__ import annotations

import argparse
import json
import os
import re
import sys
import time
import urllib.parse
from dataclasses import dataclass, field
from typing import Dict, Iterable, List, Optional, Sequence, Tuple

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
# The one duration format and the one slow-check threshold, so this document
# and the console agree rather than drifting into two conventions.
sys.path.insert(0, os.path.join(ROOT, "tests", "lib"))
import report as report_lib  # noqa: E402

DETAIL_MARKER = "<!-- detail -->"
INDEX_NAME = "index.md"
# One file per target, appended to by every suite. Neither is a suite's own
# records, so neither is read as a suite run: a file named for a suite is how
# this generator finds one, and a shared file named that way would render as a
# suite that never ran.
SPOOL_NAME = "screens.jsonl"
INTERACTIONS_NAME = "interactions.jsonl"
SHARED_FILES = ("run.jsonl", SPOOL_NAME, INTERACTIONS_NAME)

# How much of a failing suite's console log is inlined. Enough for a traceback
# and the checks around it, short enough that ten failures do not turn the
# summary part of the document into the whole log.
LOG_TAIL_LINES = 40

# A run of harness actions between two other events collapses to one line on
# the timeline. Actions are by far the most numerous entry, and a reader
# following what happened wants the shape rather than every request.
TIMELINE_ACTION_RUN = 3
# A request the harness repeats all run long is shown once and counted after
# that. The menu-open probe in every health sweep is the case: without this the
# timeline is that one line between every pair of events.
TIMELINE_REPEAT_LIMIT = 2

# The slowest few of each, for "where the time went". Enough to see the shape
# without the section becoming a second copy of the run.
SLOWEST_ROWS = 10

# The password is taken out of a record when the record is written, by
# report.mask_secret, which is the writer that knows what the password is.
# These patterns are a second net over text this document copies out of a
# console log, which nothing masked on the way in because a suite may print
# anything. They match where a password is conventionally written and cannot
# match where it is not, so they are a guard rather than the guarantee.
PASSWORD_MASK = "***"
PASSWORD_PATTERNS = (
    re.compile(r"(--password[=\s]+)(\S+)"),
    re.compile(r"(-p\s+)(\S+)"),
    re.compile(r"([Xx]-[Pp]assword:\s*)(\S+)"),
    re.compile(r"(['\"]?password['\"]?\s*[:=]\s*['\"]?)([^\s'\",}]+)"),
)

# GitHub displays at most 1 MiB per step summary and a step summary cannot be
# modified by a later step, so the whole thing is written once and this is the
# ceiling it is written under. A green run's summary part is a few kilobytes
# and a run with ten failures is around fifty, so truncating is a guard rather
# than the normal case; it is a guard a genuinely bad run can reach.
SUMMARY_LIMIT_BYTES = 1024 * 1024
# Room for the two lines OBS-4.1 permits the summary step to append.
SUMMARY_APPENDED_MARGIN = 512

VERDICT_ORDER = ("FAIL", "WARN", "SKIP", "OK")
# A word this document prints for a suite whose closing record never arrived.
# Not a verdict: the vocabulary in tests/lib/report.py is closed at OK, FAIL,
# WARN and SKIP, and this names the absence of a record rather than a result.
# Lower case where every real verdict is upper case, so the two stay apart on
# the page as well as in the code.
INCOMPLETE = "incomplete"

# Exit statuses run-tests documents, so the header can say what one meant
# without the reader looking them up.
EXIT_MEANING = {
    0: "every suite passed and the device never needed recovering",
    1: "at least one suite failed",
    2: "the command line was wrong",
    3: "every suite passed, but the device had to be recovered",
    4: "the device could not be made healthy, and the run was abandoned",
}

# The four suites that validate the harness itself. When one of these has
# already failed, every later failure that depends on it is suspect, and
# saying so is the single most useful line this document prints for a reader
# deciding whether the firmware is at fault. The list and the reasoning are in
# the registry comments in run-tests.
FOUNDATION_SUITES = ("transport-usage", "runner-policy", "observability",
                     "telnet-drain", "ui-backend-smoke")


# ---------------------------------------------------------------------------
# Reading a run
# ---------------------------------------------------------------------------
#
# Every value read out of a record goes through one of the four conversions
# below. A tree is written by a run that may have been killed mid-write, and by
# a version of the harness that may not be this one, so a field of the wrong
# type has to cost that field rather than the whole document. Refusing the tree
# would remove the report exactly when the evidence matters most.


def as_int(value, fallback: int = 0) -> int:
    try:
        return int(value)
    except (TypeError, ValueError):
        return fallback


def as_float(value, fallback: float = 0.0) -> float:
    try:
        return float(value)
    except (TypeError, ValueError):
        return fallback


def as_dict(value) -> dict:
    return value if isinstance(value, dict) else {}


def as_list(value) -> list:
    return [item for item in value if isinstance(item, dict)] \
        if isinstance(value, list) else []


@dataclass
class Check:
    """One check a suite reported."""

    target: str
    label: str
    suite: str
    attempt: int
    index: int
    check_label: str
    verdict: str
    extra: str
    seconds: float
    time: float
    scenario: str

    @property
    def key(self) -> str:
        return f"{self.target}/{self.label}/{self.suite}/{self.attempt}/{self.index}"


    @property
    def started(self) -> float:
        """When the check opened. See the interval rule in the module docstring."""
        return self.time - self.seconds

    @property
    def slow(self) -> bool:
        return self.seconds >= report_lib.SLOW_CHECK_SECONDS


@dataclass
class SuiteRun:
    """One go at one suite, as the runner recorded it."""

    target: str
    label: str
    suite: str
    attempt: int
    verdict: str
    seconds: Optional[float]
    note: str
    mode: str
    recoveries: int
    time: float
    checks: List[Check] = field(default_factory=list)
    actions: List[dict] = field(default_factory=list)
    log_name: str = ""
    # Whether a failure capture exists for this suite run, so the timeline can
    # say the run stopped to look at the device.
    captured: bool = False

    @property
    def key(self) -> str:
        return f"{self.target}/{self.label}/{self.suite}/{self.attempt}"

    @property
    def stem(self) -> str:
        """The file-name form of this run's key, per the one substitution."""
        return f"{self.label}-{self.suite}"

    @property
    def started(self) -> float:
        return self.time - (self.seconds or 0.0)


@dataclass
class TargetRun:
    """Everything one target's directory holds."""

    token: str
    slug: str
    suites: List[SuiteRun] = field(default_factory=list)
    health: List[dict] = field(default_factory=list)
    warnings: List[dict] = field(default_factory=list)
    # Every interval an observability component could not observe anything:
    # a device that stopped answering, a stream that went quiet, a log that
    # stopped arriving. A gap still open when the run ended carries no `ended`.
    gaps: List[dict] = field(default_factory=list)
    # What the runner itself did to the device, outside any suite: the health
    # sweeps, the UI-state gate and the teardown. A suite's own actions sit on
    # the suite run that made them.
    actions: List[dict] = field(default_factory=list)
    plan: Optional[dict] = None
    run: Optional[dict] = None
    # Where the collector put this device's own log, from the `log` record it
    # wrote when it started, and empty when no collector ran.
    log: Optional[dict] = None
    # The recorder's own record: where the files are, when the capture started
    # and how the recording went. Absent from a run with no recorder.
    capture: Optional[dict] = None
    skipped_lines: int = 0

    @property
    def product(self) -> str:
        """What the health sweep said this device is, or an honest absence.

        The `ident` check already carries the product and the firmware version,
        so this needs no device call and no record of its own.
        """
        for detail in self._ident_details():
            return detail
        return "firmware unknown"

    def firmware_changed(self) -> Tuple[str, str]:
        """The first and last device identity, when they differ.

        A change means the recovery command reflashed the device mid-run, so
        every result before that point was produced by different firmware from
        every result after. No reader can reconstruct that and few would think
        to look for it.
        """
        seen = list(self._ident_details())
        if len(seen) >= 2 and seen[0] != seen[-1]:
            return seen[0], seen[-1]
        return "", ""

    def _ident_details(self) -> Iterable[str]:
        for sweep in self.health:
            for check in as_list(sweep.get("checks")):
                if check.get("name") == "ident" and check.get("detail"):
                    yield str(check["detail"])


@dataclass
class Run:
    """A whole `-j` tree."""

    directory: str
    targets: List[TargetRun] = field(default_factory=list)
    parent: Optional[dict] = None
    skipped_lines: int = 0

    @property
    def run_records(self) -> List[dict]:
        return [t.run for t in self.targets if t.run]

    def counts(self) -> Dict[str, int]:
        """The run's counts, from the `run` records and never from a recount.

        Summing the per-target records is not a recount: the alternative is
        re-deriving what `summarise` and `combine_exit_codes` already decided,
        in a second implementation that can drift from them.
        """
        totals = {"targets": len(self.targets), "suites": 0, "ok": 0, "fail": 0,
                  "warn": 0, "skip": 0, "recoveries": 0}
        for record in self.run_records:
            totals["suites"] += as_int(record.get("suites"))
            totals["ok"] += as_int(record.get("passed"))
            totals["fail"] += as_int(record.get("failed"))
            totals["warn"] += as_int(record.get("dirty"))
            totals["skip"] += as_int(record.get("skipped"))
            totals["recoveries"] += as_int(record.get("recoveries"))
        return totals

    @property
    def exit_code(self) -> Optional[int]:
        """The status the run exited with, from whoever combined it."""
        if self.parent and self.parent.get("exit_code") is not None:
            return as_int(self.parent["exit_code"], -1)
        for record in self.run_records:
            if record.get("exit_code") is not None:
                return as_int(record["exit_code"], -1)
        return None

    @property
    def identity(self) -> dict:
        """The run's own identity, from the parent record or from a target's."""
        if self.parent:
            return self.parent
        return self.run_records[0] if self.run_records else {}

    @property
    def started(self) -> float:
        for record in [self.identity] + self.run_records:
            if record.get("started"):
                return as_float(record["started"])
        earliest = [s.started for t in self.targets for s in t.suites]
        return min(earliest) if earliest else 0.0

    def all_suites(self) -> List[SuiteRun]:
        return [s for t in self.targets for s in t.suites]

    def all_checks(self) -> List[Check]:
        return [c for s in self.all_suites() for c in s.checks]


def read_records(path: str) -> Tuple[List[dict], int]:
    """Every record in one JSONL file, and how many lines could not be read.

    A truncated final line means the writer was killed mid-write, which is the
    run whose records most need reading, so it is skipped and counted rather
    than treated as an error.
    """
    records: List[dict] = []
    skipped = 0
    try:
        with open(path, encoding="utf-8", errors="replace") as handle:
            for line in handle:
                if not line.strip():
                    continue
                try:
                    decoded = json.loads(line)
                except ValueError:
                    skipped += 1
                    continue
                if isinstance(decoded, dict):
                    records.append(decoded)
                else:
                    skipped += 1
    except OSError:
        return [], 0
    return records, skipped


def split_stem(stem: str, suite: str) -> Tuple[str, str]:
    """A per-suite file's stem as (label, suite).

    Both halves can contain a hyphen, so the suite name comes from the records
    the file holds and the label is what is left. A file with no usable record
    falls back to the first hyphen, which is right for every label the runner
    produces: the three UI modes and the three category names.
    """
    if suite and stem == suite:
        return "", suite
    if suite and stem.endswith("-" + suite):
        return stem[:-(len(suite) + 1)], suite
    label, _, rest = stem.partition("-")
    return label, rest or stem


def suite_categories(records: Sequence[dict]) -> Dict[str, str]:
    """Which category each registered suite belongs to, from the plan record."""
    found: Dict[str, str] = {}
    for record in records:
        if record.get("kind") != "plan":
            continue
        for entry in as_list(record.get("suites")):
            if entry.get("name"):
                found[str(entry["name"])] = str(entry.get("category") or "")
    return found


def label_for(suite: str, mode: str, categories: Dict[str, str]) -> str:
    """What names this suite run everywhere: its UI mode, or its category.

    A `suite` record carries the mode the runner passed, which for a perf or
    soak suite is the default mode rather than anything that suite has. The
    plan says which category each suite is in, and the category is the label
    for everything outside e2e, which is also how the per-suite file is named.
    """
    category = categories.get(suite, "e2e")
    return mode if category in ("", "e2e") else category


def load_target(directory: str, slug: str) -> TargetRun:
    """One target's directory: its runner records and every suite run in it."""
    records, skipped = read_records(os.path.join(directory, "run.jsonl"))
    token = slug
    for record in records:
        if record.get("target"):
            token = str(record["target"])
            break
    target = TargetRun(token=token, slug=slug, skipped_lines=skipped)
    categories = suite_categories(records)

    by_key: Dict[Tuple[str, str, int], SuiteRun] = {}
    for record in records:
        kind = record.get("kind")
        if kind == "health":
            target.health.append(record)
        elif kind == "warning":
            target.warnings.append(record)
        elif kind == "gap":
            target.gaps.append(record)
        elif kind == "action":
            target.actions.append(record)
        elif kind == "capture":
            target.capture = record
        elif kind == "log":
            # One record when collection starts and one when it ends, merged
            # into the one view the report needs. The first says where the log
            # is going, which a killed run still leaves behind; the second
            # adds which addresses the lines actually came from, which is only
            # known at the end.
            target.log = {**(target.log or {}), **record}
        elif kind == "plan":
            target.plan = record
        elif kind == "run":
            target.run = record
        elif kind == "suite":
            name = str(record.get("name", ""))
            attempt = as_int(record.get("attempt"), 1)
            mode = str(record.get("mode") or "")
            label = label_for(name, mode, categories)
            made = SuiteRun(
                target=token, label=label, suite=name, attempt=attempt,
                verdict=str(record.get("verdict", "")),
                seconds=as_float(record["seconds"])
                if record.get("seconds") is not None else None,
                note=str(record.get("note") or ""),
                mode=mode,
                recoveries=as_int(record.get("recoveries")),
                time=as_float(record.get("time")))
            by_key[(label, name, attempt)] = made
            target.suites.append(made)

    # The label on a `suite` record is the mode, which is what an e2e suite is
    # named by. A perf or soak suite is named by its category, and only the
    # per-suite file's name carries that, so the files decide the label and the
    # records fill it in.
    for name in sorted(os.listdir(directory) if os.path.isdir(directory) else []):
        # The screen spool shares the suffix and is not a per-suite record
        # file: every suite appends to the one file, so reading a suite name
        # out of it invents a suite run named after whichever suite wrote
        # last, with no closing record and therefore no verdict.
        if not name.endswith(".jsonl") or name in SHARED_FILES:
            continue
        path = os.path.join(directory, name)
        file_records, file_skipped = read_records(path)
        target.skipped_lines += file_skipped
        suite_name = ""
        for record in file_records:
            if record.get("suite"):
                suite_name = str(record["suite"])
                break
        label, suite_name = split_stem(name[:-len(".jsonl")], suite_name)
        attach_suite_records(target, by_key, label, suite_name, name,
                             file_records)

    captures = os.path.join(directory, "capture")
    for made in target.suites:
        made.captured = os.path.exists(
            os.path.join(captures, f"{made.stem}-{made.attempt}-screen.txt"))

    target.suites.sort(key=lambda s: (s.time, s.suite, s.attempt))
    return target


def attach_suite_records(target: TargetRun,
                         by_key: Dict[Tuple[str, str, int], SuiteRun],
                         label: str, suite: str, file_name: str,
                         records: Sequence[dict]) -> None:
    """Put one per-suite file's records on the suite runs they belong to.

    A retried suite writes into one file, so the attempt on each record is
    what tells its checks apart. A file whose runner record never arrived gets
    a suite run of its own, marked incomplete, rather than being dropped: that
    is the run the evidence matters most for.
    """
    stem = file_name[:-len(".jsonl")]
    seen_attempts = {as_int(record.get("attempt"), 1) for record in records}
    # Every attempt this file holds records for, and every attempt the runner
    # recorded. A suite that was killed before it wrote anything has the
    # second and not the first, and its console log is the whole of what it
    # left behind.
    known = {attempt for (one_label, one_suite, attempt) in by_key
             if one_label == label and one_suite == suite}
    for attempt in sorted(seen_attempts | known):
        key = (label, suite, attempt)
        made = by_key.get(key)
        if made is None:
            # No closing record from the runner. The suite ran, and either the
            # runner or the suite was killed before it said how it went.
            made = SuiteRun(target=target.token, label=label, suite=suite,
                            attempt=attempt, verdict=INCOMPLETE, seconds=None,
                            note="", mode="", recoveries=0,
                            time=max((as_float(r.get("time"))
                                      for r in records), default=0.0))
            by_key[key] = made
            target.suites.append(made)
        made.log_name = stem + ".log"
        for record in records:
            if as_int(record.get("attempt"), 1) != attempt:
                continue
            if record.get("kind") == "check":
                made.checks.append(Check(
                    target=target.token, label=made.label, suite=suite,
                    attempt=attempt, index=as_int(record.get("index")),
                    check_label=str(record.get("label") or ""),
                    verdict=str(record.get("verdict") or ""),
                    extra=str(record.get("extra") or ""),
                    seconds=as_float(record.get("seconds")),
                    time=as_float(record.get("time")),
                    scenario=str(record.get("scenario") or "")))
            elif record.get("kind") == "action":
                made.actions.append(record)


def load_tree(directory: str) -> Run:
    """Read a whole `-j` directory. Never raises on what it finds there."""
    run = Run(directory=directory)
    parent_records, run.skipped_lines = read_records(
        os.path.join(directory, "run.jsonl"))
    logs = {}
    gaps: Dict[str, List[dict]] = {}
    for record in parent_records:
        if record.get("kind") == "run":
            run.parent = record
        elif record.get("kind") == "log" and record.get("target"):
            logs[str(record["target"])] = record
        elif record.get("kind") == "gap" and record.get("target"):
            gaps.setdefault(str(record["target"]), []).append(record)

    for name in sorted(os.listdir(directory) if os.path.isdir(directory) else []):
        path = os.path.join(directory, name)
        if not os.path.isdir(path):
            continue
        if not os.path.exists(os.path.join(path, "run.jsonl")):
            continue
        run.targets.append(load_target(path, name))

    # The collector runs in the process that owns the whole run, so on a
    # multi-target run its records are the parent's and name the target each
    # log belongs to.
    for target in run.targets:
        if target.log is None and target.token in logs:
            target.log = logs[target.token]
        target.gaps.extend(gaps.get(target.token, ()))
    return run


# ---------------------------------------------------------------------------
# Rendering helpers
# ---------------------------------------------------------------------------


def redact(text: str) -> str:
    """`text` with anything that looks like a password masked.

    The run masks its own command line before recording it. This catches a
    password that reached a captured console log through a suite's own
    arguments, which the run had no chance to mask.
    """
    for pattern in PASSWORD_PATTERNS:
        text = pattern.sub(lambda m: m.group(1) + PASSWORD_MASK, text)
    return text


def table(headers: Sequence[str], rows: Sequence[Sequence[str]]) -> List[str]:
    """One GFM pipe table, every column padded to a common width.

    The padding is what makes the raw file aligned in a terminal, which is
    half the reason this document is Markdown rather than HTML.
    """
    if not rows:
        return []
    columns = [list(map(str, headers))] + [list(map(str, row)) for row in rows]
    widths = [max(len(row[i]) for row in columns) for i in range(len(headers))]
    lines = ["| " + " | ".join(cell.ljust(widths[i])
                               for i, cell in enumerate(columns[0])) + " |"]
    lines.append("| " + " | ".join("-" * widths[i]
                                   for i in range(len(headers))) + " |")
    for row in columns[1:]:
        lines.append("| " + " | ".join(cell.ljust(widths[i])
                                       for i, cell in enumerate(row)) + " |")
    return lines


def fenced(lines: Sequence[str], language: str = "") -> List[str]:
    """One fenced block, with any fence inside it defused."""
    body = [line.replace("```", "'''") for line in lines]
    return ["```" + language] + body + ["```"]


def position_in(target: TargetRun, when: float) -> str:
    """Where a moment is in this target's recording, as mm:ss.

    Every mm:ss anywhere in this document is a position in the file rather
    than an elapsed time in the run: the JSONL is where wall-clock time lives,
    the recording is where file positions live, and `started` and `lead_in`
    on the capture record are the two numbers that convert between them.
    """
    if not target.capture or not when:
        return ""
    started = as_float(target.capture.get("started"))
    if not started:
        return ""
    seconds = max(0.0, as_float(target.capture.get("lead_in")) + (when - started))
    return f"{int(seconds) // 60:02d}:{int(seconds) % 60:02d}"


def duration(seconds: Optional[float]) -> str:
    """One duration, in the format the console uses. See OBS-3.21."""
    if seconds is None:
        return "-"
    return report_lib.format_duration(seconds)


def clock(when: float) -> str:
    """A wall-clock time of day, in UTC, so two runs read the same anywhere."""
    if not when:
        return "-"
    return time.strftime("%Y-%m-%d %H:%M:%S", time.gmtime(when))


def offset(when: float, start: float) -> str:
    """How far into the run something happened, as +MM:SS."""
    if not when or not start:
        return "-"
    seconds = max(0, int(when - start))
    return f"+{seconds // 60:02d}:{seconds % 60:02d}"


def tail(path: str, lines: int) -> List[str]:
    """The last `lines` lines of a file, or an empty list when there is none."""
    try:
        with open(path, encoding="utf-8", errors="replace") as handle:
            found = handle.read().splitlines()
    except OSError:
        return []
    return found[-lines:]


def read_text(path: str) -> List[str]:
    try:
        with open(path, encoding="utf-8", errors="replace") as handle:
            return handle.read().splitlines()
    except OSError:
        return []




# ---------------------------------------------------------------------------
# The sections, in the order OBS-3.14 fixes
# ---------------------------------------------------------------------------


PREAMBLE = """\
## How to read this

- The line under the title is the whole run, greppable, counted by the runner.
- A suite run is `target/label/suite/attempt` and a check is that plus its
  index. The label is the UI mode for an E2E suite and the category for a perf
  or soak one. A suite run's files are that key with the target dropped and
  `/` written `-`: `overlay-prg-context-menu.log` for its console,
  `-1-screen.txt` under `capture/` for the screen its first attempt left.
- Which section answers what, and what to open when it does not:
  `Verdict` what happened, then `<slug>/<label>-<suite>.jsonl`;
  `Coverage` what did not run or was skipped;
  `Failing checks` one entry each, then that suite's `.log`;
  `Device health` every sweep, then `<slug>/run.jsonl`;
  `Files in this run` everything else this run wrote.
- Below the detail marker: the whole timeline, every check including the
  passing ones and their measurements, and where the time went.
- Nothing here diagnoses anything. Every line is a fact the run recorded."""


def status_line(run: Run) -> str:
    """The whole run in one machine-readable line."""
    counts = run.counts()
    exit_code = run.exit_code
    return ("RESULT: {verdict}  targets={targets}  suites={suites}  ok={ok}  "
            "fail={fail}  warn={warn}  skip={skip}  recoveries={recoveries}  "
            "exit={exit}").format(
        verdict=overall_verdict(run), exit="-" if exit_code is None else exit_code,
        **counts)


def overall_verdict(run: Run) -> str:
    """The run's verdict, on the same rule the runner's own summary uses."""
    counts = run.counts()
    exit_code = run.exit_code
    if counts["fail"] or (exit_code not in (None, 0, 3)):
        return "FAIL"
    if counts["recoveries"] or counts["warn"]:
        return "WARN"
    return "OK"


def header(run: Run) -> List[str]:
    identity = run.identity
    lines = [f"# E2E gate run: {overall_verdict(run)}", "", status_line(run), ""]

    rows = []
    for name, value in (("commit", identity.get("commit")),
                        ("branch", identity.get("branch")),
                        ("worktree", "dirty" if identity.get("worktree_dirty")
                         else ("clean" if "worktree_dirty" in identity else None)),
                        ("CI run", ci_run(identity)),
                        ("host", identity.get("host")),
                        ("python", identity.get("python")),
                        ("started", clock(run.started)),
                        ("duration", duration(identity.get("seconds")))):
        if value:
            rows.append([name, str(value)])
    for target in run.targets:
        rows.append([f"device {target.token}", target.product])
        first, last = target.firmware_changed()
        if first:
            rows.append([f"device {target.token} changed",
                         f"{first} at the start, {last} at the end"])
    exit_code = run.exit_code
    if exit_code is not None:
        rows.append(["exit status",
                     f"{exit_code}: {EXIT_MEANING.get(exit_code, 'unknown')}"])
    if identity.get("argv"):
        rows.append(["command",
                     "`" + redact(" ".join(str(a) for a in identity["argv"])) + "`"])
    lines += table(["Field", "Value"], rows)

    lines += [""] + completeness(run)
    return lines


def ci_run(identity: dict) -> str:
    run_id = identity.get("ci_run_id")
    if not run_id:
        return ""
    attempt = identity.get("ci_run_attempt")
    return f"{run_id} attempt {attempt}" if attempt else str(run_id)


def completeness(run: Run) -> List[str]:
    """Whether this tree is the whole run, stated rather than left to be noticed."""
    lines = []
    missing_run = [t.token for t in run.targets if not t.run]
    if missing_run:
        lines.append("This run wrote no closing record for "
                     + ", ".join(missing_run)
                     + ", so it did not finish or was killed, and the counts "
                       "on the status line above cover the "
                     + str(len(run.run_records))
                     + " of " + str(len(run.targets))
                     + " target(s) that did record one.")
    open_suites = [s.key for s in run.all_suites() if s.verdict == INCOMPLETE]
    if open_suites:
        lines.append("No closing record for " + ", ".join(sorted(open_suites))
                     + ", so `" + INCOMPLETE + "` in the table below means the "
                     "record is absent rather than the suite having a verdict.")
    skipped = run.skipped_lines + sum(t.skipped_lines for t in run.targets)
    if skipped:
        lines.append(f"{skipped} JSONL line(s) could not be read and were "
                     "skipped, which is what a writer killed mid-line leaves.")
    if not lines:
        return []
    return ["**Completeness.** " + " ".join(lines), ""]


def verdict_section(run: Run) -> List[str]:
    rows = []
    for made in sorted(run.all_suites(), key=lambda s: (s.target, s.time, s.suite)):
        rows.append([made.target, made.label or "-", made.suite, str(made.attempt),
                     made.verdict, duration(made.seconds), str(made.recoveries),
                     redact(made.note) or "-"])
    if not rows:
        return []
    return (["## Verdict", ""]
            + table(["Target", "Label", "Suite", "Attempt", "Verdict",
                     "Duration", "Recoveries", "Note"], rows) + [""])


def coverage_section(run: Run) -> List[str]:
    """What this run did not do. The section a green run most needs."""
    lines: List[str] = []
    planned = 0
    absent: List[List[str]] = []
    for target in run.targets:
        if not target.plan:
            continue
        planned += len(as_list(target.plan.get("sequence")))
        for entry in as_list(target.plan.get("suites")):
            if not entry.get("run"):
                absent.append([target.token, str(entry.get("name")),
                               str(entry.get("category")),
                               str(entry.get("reason") or "-")])
    # One planned suite run is one entry in the sequence, and a retry is not an
    # entry: the plan says what the run set out to do, and running a suite
    # twice is the runner deciding the first attempt proved nothing.
    completed = len({(s.target, s.label, s.suite) for s in run.all_suites()
                     if s.verdict != INCOMPLETE})
    if planned:
        lines.append(f"- {completed} of {planned} planned suite runs completed.")

    assumptions = sorted({str(name) for record in run.run_records
                          for name in (record.get("assumptions") or [])
                          if isinstance(name, str)})
    if assumptions:
        lines.append("- Assumed present rather than proved: "
                     + ", ".join(f"`{name}`" for name in assumptions)
                     + ". Checks tagged with these ran that would otherwise "
                       "have reported SKIP.")
    elif run.run_records:
        lines.append("- No firmware fixes were assumed; every check tagged "
                     "with a missing fix reported SKIP.")

    skipped_rows = []
    grouped: Dict[Tuple[str, str, str], int] = {}
    for check in run.all_checks():
        if check.verdict == "SKIP":
            key = (check.target, check.suite, check.extra)
            grouped[key] = grouped.get(key, 0) + 1
    for (target, suite, reason), count in sorted(grouped.items()):
        skipped_rows.append([target, suite, str(count), redact(reason) or "-"])

    if not lines and not absent and not skipped_rows:
        return []
    out = ["## Coverage", ""] + lines
    if absent:
        out += ["", "Registered suites this run did not run:", ""]
        out += table(["Target", "Suite", "Category", "Reason"],
                     sorted(absent))
    if skipped_rows:
        out += ["", "Checks that reported SKIP:", ""]
        out += table(["Target", "Suite", "Checks", "Reason"], skipped_rows)
    return out + [""]


# ---------------------------------------------------------------------------
# What the run changed, and what it left behind
# ---------------------------------------------------------------------------

# A mutation and the request that puts it back. Everything a suite does to the
# device comes in pairs, and what is left over at the end is what the run
# changed and did not change back.
INVERSE_ACTIONS = {
    "mount": ("remove", "unlink"),
    "start": ("stop",),
    "on": ("off",),
}
# A configuration item and a file have no inverse verb: a setting is restored
# by writing it again, and a file the run created is removed over FTP, which
# this log does not see. Both are reported for what they are.
CONFIG_PREFIX = "/v1/configs/"
FILE_ACTION = "/v1/files:"


def split_action(path: str) -> Tuple[str, str]:
    """A device action path as (resource, verb)."""
    resource, _, verb = path.partition(":")
    return resource, verb


def unmatched_changes(run: Run) -> List[List[str]]:
    """Every mutation with no matching restore, with the suite that made it.

    Not a verdict, and deliberately so: a suite may leave something behind on
    purpose, and a run cannot always tell. It is a list for a reader to judge.
    """
    open_by_resource: Dict[Tuple[str, str], List[Tuple[str, str]]] = {}
    config_writes: Dict[Tuple[str, str], List[str]] = {}
    created: List[List[str]] = []

    everything = [(made.target, made.key, action)
                  for made in sorted(run.all_suites(),
                                     key=lambda s: (s.target, s.time))
                  for action in made.actions]
    everything += [(target.token, f"{target.token} (the runner itself)", action)
                   for target in run.targets for action in target.actions]
    everything.sort(key=lambda item: as_float(item[2].get("time")))

    for target_token, made_key, action in everything:
        method = str(action.get("method", ""))
        path = str(action.get("path", ""))
        status = as_int(action.get("status"), 0)
        if method == "GET" or not 200 <= status < 300:
            continue
        if path.startswith(CONFIG_PREFIX):
            config_writes.setdefault((target_token, path), []).append(made_key)
            continue
        if path.startswith(FILE_ACTION):
            created.append([target_token, made_key, "created",
                            str(as_dict(action.get("params")).get("path")
                                or action.get("params") or path)])
            continue
        resource, verb = split_action(path)
        if verb in INVERSE_ACTIONS:
            opened = open_by_resource.setdefault((target_token, resource), [])
            if any(entry[0] == verb for entry in opened):
                # The resource is already in that state. A stream started
                # twice is one stream, and one stop puts it back, so a second
                # start is not a second thing left behind. Files are not here:
                # each one created is its own resource, above.
                continue
            opened.append((verb, made_key))
        else:
            # Only the verb that undoes this one closes it: a stopped stream
            # does not put back a mounted image.
            opened = open_by_resource.get((target_token, resource)) or []
            for index in range(len(opened) - 1, -1, -1):
                if verb in INVERSE_ACTIONS.get(opened[index][0], ()):
                    opened.pop(index)
                    break

    rows: List[List[str]] = []
    for (target, resource), opened in sorted(open_by_resource.items()):
        for verb, suite_key in opened:
            rows.append([target, suite_key, f"{verb} not undone", resource])
    for (target, path), writers in sorted(config_writes.items()):
        # Which value a setting had before the run is not in any record, so
        # this states what the run wrote rather than guessing at whether it put
        # it back. A suite that sets a value and sets it back writes twice.
        rows.append([target, writers[-1], f"written {len(writers)} time(s)",
                     urllib.parse.unquote(path)])
    rows += created
    return rows


def changes_section(run: Run) -> List[str]:
    rows = unmatched_changes(run)
    if not rows:
        return []
    return (["## What this run changed", "",
             "What the action log says the run did to the device and, where "
             "a mutation has an undoing request, whether that request was "
             "made. A suite may leave something behind on purpose, so this is "
             "a list for a reader to judge rather than a verdict.", ""]
            + table(["Target", "Suite run", "What", "Where"], rows) + [""])


# ---------------------------------------------------------------------------
# Failing checks: the facts the run already knows about each one
# ---------------------------------------------------------------------------


def what_the_run_knows(run: Run, check: Check,
                       made: SuiteRun) -> List[str]:
    """Facts the run recorded about one failure. Never a diagnosis.

    Each line is present only when its condition holds and each names the
    record it came from. Nothing here guesses at a cause, ranks likelihood or
    suggests a fix: those are the reader's job, and a wrong guess printed in a
    fixed format is worse than no guess, because it is read as a finding.
    """
    lines: List[str] = []
    if made.note:
        lines.append(f"- Killed: the runner recorded `{redact(made.note)}`, so "
                     "the suite was signalled rather than failing by itself.")

    before = sweep_before(run, made)
    if before and not before.get("ok"):
        failed = ", ".join(str(c.get("name") or "") for c in
                           as_list(before.get("checks"))
                           if c.get("state") == "fail")
        lines.append("- Unhealthy before: the health sweep "
                     f"`{redact(str(before.get('label')))}` reported {failed} "
                     "failing.")
    if made.recoveries:
        lines.append(f"- Recovered: the device was recovered {made.recoveries} "
                     "time(s) around this suite.")

    same = [c for c in run.all_checks()
            if c.target == check.target and c.suite == check.suite
            and c.label == check.label and c.index == check.index]
    repeated = [c for c in same if c.attempt != check.attempt]
    if any(c.verdict == "FAIL" for c in repeated):
        lines.append("- Repeated: this check failed on more than one attempt.")
    if any(c.verdict == "OK" for c in repeated):
        lines.append("- Passed on retry: this check passed on another attempt.")

    elsewhere = [c for c in run.all_checks()
                 if c.target != check.target and c.suite == check.suite
                 and c.index == check.index]
    for verdict, wording in (("FAIL", "Failed elsewhere"),
                             ("OK", "Passed elsewhere"),
                             ("SKIP", "Skipped elsewhere")):
        named = sorted({c.target for c in elsewhere if c.verdict == verdict})
        if named:
            reason = ""
            if verdict == "SKIP":
                reasons = sorted({c.extra for c in elsewhere
                                  if c.verdict == verdict and c.extra})
                reason = (": " + "; ".join(redact(r) for r in reasons)) if reasons else ""
            lines.append(f"- {wording}: the same check {verdict} on "
                         + ", ".join(named) + reason + ".")

    foundation = [s for s in run.all_suites()
                  if s.suite in FOUNDATION_SUITES and s.verdict == "FAIL"
                  and s.time <= made.time]
    if foundation:
        lines.append("- Foundation failed: "
                     + ", ".join(sorted({s.suite for s in foundation}))
                     + " failed earlier in this run, and it validates the "
                       "harness every later UI failure depends on.")

    earlier = [c for c in made.checks
               if c.index < check.index and c.verdict == "FAIL"]
    if not earlier and len(made.checks) > 1:
        lines.append("- First failure: no other check in this suite run failed "
                     "before it.")
    return lines


def sweep_before(run: Run, made: SuiteRun) -> Optional[dict]:
    """The health sweep this suite ran on, or None when there was none."""
    target = next((t for t in run.targets if t.token == made.target), None)
    if target is None:
        return None
    earlier = [s for s in target.health if as_float(s.get("time")) <= made.started]
    return earlier[-1] if earlier else None


def reproduce_command(made: SuiteRun) -> str:
    """The command that runs this suite again on this target.

    The password is not in it; the reader supplies their own.
    """
    command = f"./run-tests -H {made.target} -s {made.suite}"
    if made.mode in ("overlay", "freeze", "telnet"):
        command += f" --mode {made.mode}"
    return command


def suite_path(run: Run, made: SuiteRun) -> str:
    """Where the code that produced this failure lives, from the plan record."""
    for target in run.targets:
        for entry in as_list(as_dict(target.plan).get("suites")):
            if entry.get("name") == made.suite and entry.get("path"):
                return str(entry["path"])
    return ""


def capture_block(run: Run, made: SuiteRun) -> List[str]:
    """The screen the failing suite left, when a capture exists for it."""
    target = next((t for t in run.targets if t.token == made.target), None)
    if target is None:
        return []
    stem = f"{made.stem}-{made.attempt}"
    directory = os.path.join(run.directory, target.slug, "capture")
    screen = read_text(os.path.join(directory, stem + "-screen.txt"))
    state = read_state(os.path.join(directory, stem + "-state.json"))
    lines: List[str] = []
    if screen:
        source = str(state.get("source"))
        # "when this suite ended" is not true of the earlier screen, which is
        # the whole reason that source has a name of its own.
        ended = "" if source == "telnet-spool-earlier" else " when this suite ended"
        lines += ["", f"{SCREEN_SOURCE.get(source, 'The screen')}"
                      f"{ended} "
                      f"(`{target.slug}/capture/{stem}-screen.txt`):", ""]
        lines += fenced([redact(row) for row in screen])
    if str(state.get("source")) == "readmem":
        # The menu was already closed when the capture ran, so the C64's own
        # screen is what the device could still be asked for. For a suite that
        # was driving the menu that is not what it was looking at, and the
        # screen it was looking at is in the spool the run already wrote. No
        # device read: the same argument OBS-5.9 makes for the Telnet capture.
        spooled = last_menu_screen(run, target, made)
        if spooled:
            lines += ["", "The last menu screen this suite read, from "
                          f"`{target.slug}/{SPOOL_NAME}`, which is what it was "
                          "driving before the menu closed:", ""]
            lines += fenced([redact(row) for row in spooled])
    summary = describe_state(state)
    if summary:
        lines += ["", summary + f" Everything the capture read is in "
                                f"`{target.slug}/capture/{stem}-state.json`."]
    return lines


def last_menu_screen(run: Run, target: TargetRun,
                     made: SuiteRun) -> List[str]:
    """The final menu screen this suite run spooled, or nothing.

    Keyed on the suite and the attempt, so a suite that read no menu screen at
    all is shown none rather than the previous suite's presented as its own.
    """
    path = os.path.join(run.directory, target.slug, SPOOL_NAME)
    best: List[str] = []
    best_at = -1.0
    for record in read_records(path)[0]:
        if record.get("kind") != "menu":
            continue
        if record.get("suite") != made.suite:
            continue
        if as_int(record.get("attempt"), 1) != made.attempt:
            continue
        when = as_float(record.get("time"))
        text = record.get("text")
        # The rows themselves, not `as_list`, which keeps only the dicts of a
        # record list and would drop every line of a screen.
        rows = [str(row) for row in text] if isinstance(text, list) else []
        if when >= best_at and any(row.strip() for row in rows):
            best, best_at = rows, when
    return best


# What the capture read, said in the report rather than left for the reader to
# work out. The two screens are different encodings of different things, and
# presenting one as the other is the mistake this naming exists to prevent.
SCREEN_SOURCE = {
    "menu_screen": "The menu screen",
    "readmem": "The C64's own screen, read from $0400 and decoded as screen "
               "codes, which is best effort because the matrix moves with the "
               "VIC bank and with $D018,",
    "telnet-spool": "The Telnet screen the suite was driving, from the spool,",
    # A session that dropped mid-suite publishes an empty screen last, so the
    # capture takes the one before it. Named apart, because a reader has to
    # know this is not the screen the suite ended on.
    "telnet-spool-earlier": "The last Telnet screen with anything on it "
                            "before the session dropped, from the spool,",
    "unavailable": "No screen could be read,",
}


def read_state(path: str) -> dict:
    try:
        with open(path, encoding="utf-8") as handle:
            decoded = json.load(handle)
    except (OSError, ValueError):
        return {}
    return decoded if isinstance(decoded, dict) else {}


def describe_state(state: dict) -> str:
    """The heap and the drives in one line, rather than as a JSON dump.

    A build page carries the summary part of this document, and a run with ten
    failures would otherwise put ten copies of a forty-line object on it.
    """
    if not state:
        return ""
    parts = []
    heap = state.get("heap")
    if isinstance(heap, dict):
        parts.append(f"free heap {int(heap.get('free', 0))} B, low-water "
                     f"{int(heap.get('min_ever_free', 0))} B of "
                     f"{int(heap.get('total', 0))} B")
    elif "heap" in state:
        parts.append("no machine:heap on this firmware")
    drives = state.get("drives")
    if isinstance(drives, dict):
        mounted = sorted(f"{slot}: {info.get('image_file')}"
                         for slot, info in drives.items()
                         if isinstance(info, dict) and info.get("image_file"))
        parts.append("drives " + (", ".join(mounted) if mounted
                                  else "with nothing mounted"))
    for problem in state.get("errors") or []:
        parts.append(f"the capture could not read {redact(str(problem))}")
    if not parts:
        return ""
    return "Device state: " + "; ".join(parts) + "."


def failing_section(run: Run) -> List[str]:
    failures = [(s, c) for s in run.all_suites() for c in s.checks
                if c.verdict == "FAIL"]
    open_suites = [s for s in run.all_suites()
                   if s.verdict in ("FAIL", INCOMPLETE)
                   and not any(c.verdict == "FAIL" for c in s.checks)]
    if not failures and not open_suites:
        return []

    lines = ["## Failing checks", ""]
    for made, check in sorted(failures, key=lambda pair: (pair[1].time,)):
        target = next((t for t in run.targets if t.token == check.target), None)
        at = position_in(target, check.started) if target else ""
        lines += [f"### {check.key} - {check.check_label}", "",
                  f"`FAIL` after {duration(check.seconds)}, at {clock(check.time)}"
                  + (f", {at} into the recording" if at else "")
                  + (f", scenario `{check.scenario}`" if check.scenario else "")
                  + (f", reported `{redact(check.extra)}`" if check.extra else "")
                  + "."]
        known = what_the_run_knows(run, check, made)
        if known:
            lines += [""] + known
        lines += capture_block(run, made)
        lines += failing_stills(run, made)
        lines += ["", "Reproduce: `" + reproduce_command(made) + "`"]
        path = suite_path(run, made)
        if path:
            lines += ["", f"Source: `{path}`, which carries the check's label "
                          "as a literal string."]
        lines += log_tail_block(run, made)
        lines.append("")

    for made in sorted(open_suites, key=lambda s: s.time):
        lines += [f"### {made.key} - the suite itself", "",
                  f"`{made.verdict}`"
                  + (f": {redact(made.note)}" if made.note else
                     ", with no failing check of its own.")]
        known = what_the_run_knows(
            run, Check(made.target, made.label, made.suite, made.attempt, 0, "",
                       "FAIL", "", 0.0, made.time, ""), made)
        if known:
            lines += [""] + known
        lines += capture_block(run, made)
        lines += ["", "Reproduce: `" + reproduce_command(made) + "`"]
        lines += log_tail_block(run, made)
        lines.append("")
    return lines


def log_tail_block(run: Run, made: SuiteRun) -> List[str]:
    """The last lines of that suite run's console log, when one was captured."""
    if not made.log_name:
        return []
    target = next((t for t in run.targets if t.token == made.target), None)
    if target is None:
        return []
    relative = f"{target.slug}/{made.log_name}"
    found = tail(os.path.join(run.directory, target.slug, made.log_name),
                 LOG_TAIL_LINES)
    if not found:
        return []
    attempts = len({s.attempt for s in run.all_suites()
                    if s.target == made.target and s.label == made.label
                    and s.suite == made.suite})
    covering = ("" if attempts < 2 else
                f", which holds all {attempts} attempts appended in order")
    return (["", f"Last {len(found)} line(s) of `{relative}`{covering}:", ""]
            + fenced([redact(line) for line in found]))


# ---------------------------------------------------------------------------
# Device health, and the file index
# ---------------------------------------------------------------------------


def health_section(run: Run) -> List[str]:
    """Every sweep the run took, one table per target, in wall-clock order.

    A table of 30 to 50 rows is readable, greppable and free, and it answers
    "was this device already degraded" for every suite in the run.
    """
    lines: List[str] = []
    for target in run.targets:
        if not target.health:
            continue
        names: List[str] = []
        for sweep in target.health:
            for check in as_list(sweep.get("checks")):
                name = str(check.get("name") or "")
                if name and name not in names:
                    names.append(name)
        rows = []
        for sweep in target.health:
            by_name = {str(c.get("name") or ""): c
                       for c in as_list(sweep.get("checks"))}
            row = [redact(str(sweep.get("label") or "-")),
                   "OK" if sweep.get("ok") else "DEGRADED"]
            for name in names:
                row.append(render_health_check(by_name.get(name)))
            rows.append(row)
        lines += [f"### {target.token}", ""]
        lines += table(["Sweep", "Verdict"] + names, rows) + [""]
    if not lines:
        return []
    return ["## Device health", ""] + lines


def render_health_check(check: Optional[dict]) -> str:
    """One cell, in the words health.Check.render uses on the console."""
    if not check:
        return "-"
    state = check.get("state")
    if state == "skip":
        return "skip"
    if state == "fail":
        return "FAIL"
    heap = as_dict(check.get("heap"))
    if heap:
        return f"{as_int(heap.get('free'))}B"
    return f"{as_float(check.get('ms')):.0f}ms"


# What each file in the tree is, keyed by how its name is built. The index is
# how a reader who has downloaded the artifact finds the JSONL, the captures
# and the recording without listing the directory and guessing.
def describe_file(relative: str) -> str:
    name = os.path.basename(relative)
    if relative == INDEX_NAME:
        return "this report, written by tools/e2e_report.py"
    if name == "run.jsonl":
        return ("the run's own records: the plan, the health sweeps, the suite "
                "verdicts and the run result, written by run-tests")
    if name == "run.log":
        return "run-tests' own console output"
    if name == "screens.jsonl":
        return "every distinct screen the harness read, as text and as raw bytes"
    if name == "interactions.jsonl":
        return ("every interaction the harness had with this device: each REST "
                "request and its answer, each Telnet exchange, each FTP "
                "command and reply")
    if name == "transcript.txt":
        return ("the same interactions as one line each, sharing their "
                "sequence numbers with `interactions.jsonl`")
    if name == "screen-text.jsonl":
        return ("the C64's own screen as text, decoded from the recorded video "
                "frames against the character ROM")
    if relative.replace(os.sep, "/").split("/")[-2:-1] == ["bodies"]:
        return "one response body, kept once and referred to by its digest"
    if name.endswith(".telnet.log"):
        return "the raw Telnet session stream, unparsed"
    if name.endswith(".jsonl"):
        return "one suite run's checks, scenarios and device actions"
    if name.endswith(".log"):
        return "that suite run's console output, stderr merged in, ANSI stripped"
    if name == "syslog-unknown-sender.txt":
        return ("log lines from an address no target in this run claimed, "
                "kept with the address that sent them")
    if name.startswith("syslog"):
        return ("the device's own log, as the collector received it, best "
                "effort and incomplete by construction")
    if name.endswith("-screen.txt"):
        return "the screen a failing suite left, as text"
    if name.endswith("-screen.bin"):
        return "the same screen, as the device's own bytes"
    if name.endswith("-state.json"):
        return "the drive state and free heap when a suite failed"
    if name.endswith(".png"):
        return "a still from the recording"
    if name.endswith(".srt"):
        return "subtitles naming the suite and check at each moment"
    if name.endswith(".mp4"):
        return "the recording: the harness pane, the device's video and its audio"
    if name == "audio.m4a":
        # The finishing pass muxes this into every video file and removes it,
        # so a tree that still has one is a tree whose run did not finish.
        return ("the run's audio track, left behind by a recording that was "
                "not finished")
    return "part of this run"


def files_section(run: Run) -> List[str]:
    # This document lists itself first, always, and with no size. It is
    # written after the walk, so whether the walk finds it depends on whether
    # the generator has run before, and its size would change with its own
    # content. Either would break the rule that two runs over one tree produce
    # identical bytes.
    rows = [[f"`{INDEX_NAME}`", "-", describe_file(INDEX_NAME)]]
    for base, directories, names in os.walk(run.directory):
        directories.sort()
        for name in sorted(names):
            path = os.path.join(base, name)
            relative = os.path.relpath(path, run.directory)
            if relative == INDEX_NAME:
                continue
            try:
                size = os.path.getsize(path)
            except OSError:
                continue
            rows.append([f"`{relative}`", f"{size}", describe_file(relative)])
    if not rows:
        return []
    return (["## Files in this run", "",
             "A capture file's name is its suite run's key with `/` written `-` "
             "and the target dropped, because the file already sits under that "
             "target's directory.", "",
             "Reaching one of these from a build page is a download and an "
             "unzip: GitHub serves no URL for a single file inside a zipped "
             "artifact, so the artifact link on the build page is the last "
             "click there is.", ""]
            + table(["Path", "Bytes", "What it is"], rows) + [""])


# ---------------------------------------------------------------------------
# The detail part
# ---------------------------------------------------------------------------


# The first distinctive boot marker that reaches the device log, and the only
# signal there is that a device restarted: the firmware has no uptime counter
# and nothing on the REST surface answers the question.
BOOT_MARKER = "All linked modules have been initialized and are now running."

# How many device log lines are inlined around one failure. The whole log is a
# sibling file, and this is the slice a reader would otherwise go looking for.
LOG_SLICE_LINES = 30

LOG_CAVEAT = (
    "The device log is best-effort and incomplete by construction. It is UDP "
    "with no retransmission, the firmware's 16 KB forwarding buffer discards "
    "itself whole on overflow, output is throttled to about 200 lines a "
    "second, and an assertion failure arrives only from firmware that flushes "
    "it from the failing task. A line's time is when this host "
    "received it, which lags when the firmware printed it by an unbounded "
    "amount, so these are lines received during a check and not lines the "
    "device produced during it.")


def device_log(run: Run, target: TargetRun) -> List[Tuple[float, str]]:
    """One target's collected log, with the time each line was received."""
    if not target.log or not target.log.get("path"):
        return []
    path = os.path.join(run.directory, str(target.log["path"]))
    found: List[Tuple[float, str]] = []
    for line in read_text(path):
        stamp, _, text = line.partition(" ")
        try:
            found.append((float(stamp), text))
        except ValueError:
            continue
    return found


UNKNOWN_SENDER_NAME = "syslog-unknown-sender.txt"


def unmapped_senders(run: Run) -> Optional[dict]:
    """Who sent the lines no target claimed, and how many each sent.

    Read from the file rather than from the records, so a tree renders the
    same whether or not the run that wrote it finished. None when the file is
    absent or empty, which is the ordinary case and says every line was
    attributed.
    """
    path = os.path.join(run.directory, UNKNOWN_SENDER_NAME)
    if not os.path.exists(path) or not os.path.getsize(path):
        return None
    senders: Dict[str, int] = {}
    count = 0
    try:
        with open(path, encoding="utf-8", errors="replace") as handle:
            for line in handle:
                parts = line.split(" ", 2)
                if len(parts) >= 2:
                    senders[parts[1]] = senders.get(parts[1], 0) + 1
                count += 1
    except OSError:
        return None
    return {"addresses": sorted(senders), "senders": senders, "lines": count}


def expected_senders(run: Run) -> List[List[str]]:
    """Each target, the addresses its log was expected from, and which sent."""
    rows = []
    for target in run.targets:
        if not target.log:
            continue
        expected = [str(a) for a in as_list(target.log.get("addresses"))]
        observed = as_dict(target.log.get("senders"))
        rows.append([target.token,
                     ", ".join(f"`{a}`" for a in expected) or "-",
                     ", ".join(f"`{a}` ({count})" for a, count
                               in sorted(observed.items())) or "none"])
    return rows


def log_section(run: Run) -> List[str]:
    """The device log around each failure, and nowhere else.

    Inlining a slice for every check would multiply the document by the check
    count to answer a question only ever asked about failures.
    """
    lines: List[str] = []
    senders = expected_senders(run)
    if senders:
        lines += ["Each target's log was expected from the addresses its "
                  "machines resolve to, and arrived from these:", ""]
        lines += table(["Target", "Expected from", "Arrived from"], senders)
        lines += [""]
    unmapped = unmapped_senders(run)
    if unmapped:
        # The file exists to make the omission visible, and a reader who has to
        # list the directory to find it is not being told. The source address
        # is the only identification a datagram carries, so an address that is
        # no target's is reported as exactly that and nothing is guessed.
        lines += [f"{unmapped['lines']} line(s) arrived from a sender no "
                  f"target in this run is known by, and are kept in "
                  f"`{UNKNOWN_SENDER_NAME}` with the address that sent them. "
                  f"No target is guessed for them: a datagram carries no "
                  f"identification but its source address.", ""]
        lines += table(["Sender", "Lines", "Why it could not be attributed"],
                       [[f"`{address}`", str(count),
                         "no machine of any target in this run resolves to it"]
                        for address, count in sorted(
                            unmapped["senders"].items())])
        lines += ["",
                  "Another device on the same network logging to this "
                  "collector lands here, which is not a problem with this run. "
                  "A device of this run's whose log arrives from an address "
                  "its name does not resolve to is: "
                  "`U64_LOG_ADDRESSES=\"<machine>=<address>\"` attaches that "
                  "address to that machine.", ""]
    for target in run.targets:
        collected = device_log(run, target)
        if not collected:
            continue
        lines += [f"### {target.token}", "",
                  f"{len(collected)} line(s) received, from "
                  f"`{target.log['path']}`.", ""]
        for made in sorted(target_suites(run, target), key=lambda s: s.time):
            for check in made.checks:
                if check.verdict != "FAIL":
                    continue
                # The gap before a check belongs to the suite rather than to a
                # check: nothing is recorded when a check starts, so that gap
                # is setup, teardown, the health sweep and the UI-state gate.
                previous = [c.time for c in made.checks if c.time <= check.started]
                start = max(previous) if previous else made.started
                slice_lines = [text for when, text in collected
                               if start <= when <= check.time]
                if not slice_lines:
                    continue
                lines += [f"**{check.key}**, from the end of the check before "
                          "it:", ""]
                lines += fenced([redact(one) for one in
                                 slice_lines[-LOG_SLICE_LINES:]]) + [""]
    if not lines:
        return []
    return ["## Device log", "", LOG_CAVEAT, ""] + lines


def target_suites(run: Run, target: TargetRun) -> List[SuiteRun]:
    return [s for s in run.all_suites() if s.target == target.token]


def timeline_section(run: Run) -> List[str]:
    """The whole run as one narrative, in wall-clock order.

    The actions are what make it a narrative rather than a list of outcomes: a
    timeline that reads "check 26 failed" says less than one that reads "check
    26 pressed RETURN, the machine was reset, the device did not answer for
    four seconds, check 26 failed".
    """
    # Rank orders events that share a wall-clock time, which a zero-duration
    # record makes common: a suite starts before it acts and closes after it.
    START, DURING, CLOSING = 0, 1, 2
    # (time, rank, whether it is a device request, text). The flag is a field
    # rather than something read back out of the text, because the collapsing
    # below has to be able to tell them apart without parsing what it wrote.
    events: List[Tuple[float, int, bool, str]] = []
    for target in run.targets:
        for sweep in target.health:
            state = "OK" if sweep.get("ok") else "DEGRADED"
            events.append((as_float(sweep.get("time")), DURING, False,
                           f"{target.token} sweep {sweep.get('label')}: {state}"))
        for warning in target.warnings:
            events.append((as_float(warning.get("time")), DURING, False,
                           f"{target.token} warning: "
                           + redact(str(warning.get("message") or ""))))
        for gap in target.gaps:
            # Both ends, as two events, so a reader sees what was running when
            # it opened and what was running when it closed rather than one
            # line naming two times. A gap with no end is still open: the run
            # finished without the resource coming back, and saying "to the end
            # of the run" would be inventing the end this record does not have.
            component = str(gap.get("component") or "a component")
            reason = redact(str(gap.get("reason") or "unavailable"))
            named = f"{target.token} {component}"
            events.append((as_float(gap.get("started")), DURING, False,
                           f"{named} gap opened: {reason}"))
            if gap.get("ended") is None:
                events.append((as_float(gap.get("started")), DURING, False,
                               f"{named} gap still open when the run ended"))
            else:
                events.append((as_float(gap.get("ended")), DURING, False,
                               f"{named} gap closed after "
                               f"{as_float(gap['ended']) - as_float(gap['started']):.1f}s"))
        for action in target.actions:
            events.append((as_float(action.get("time")), DURING, True,
                           f"{target.token} " + describe_action(action)))
        for when, text in device_log(run, target):
            if BOOT_MARKER in text:
                events.append((when, DURING, False,
                               f"{target.token} restarted, seen in its own log"))
    for made in run.all_suites():
        events.append((made.started, START, False, f"{made.key} started"))
        if made.recoveries:
            events.append((made.time, DURING, False,
                           f"{made.target} was recovered {made.recoveries} "
                           f"time(s) around {made.suite}"))
        if made.captured:
            events.append((made.time, CLOSING, False,
                           f"{made.key} device state captured"))
        events.append((made.time, CLOSING, False,
                       f"{made.key} {made.verdict}"
                       + (f": {redact(made.note)}" if made.note else "")))
        for check in made.checks:
            if check.verdict in ("FAIL", "WARN"):
                events.append((check.time, DURING, False,
                               f"{check.key} {check.verdict} "
                               f"{check.check_label}"))
        for action in made.actions:
            events.append((as_float(action.get("time")), DURING, True,
                           f"{made.key} " + describe_action(action)))

    events.sort(key=lambda item: (item[0], item[1], item[3]))
    if not events:
        return []

    start = run.started
    lines = ["## Timeline", ""]
    pending: List[Tuple[float, int, bool, str]] = []
    # A request the harness makes once per sweep would otherwise sit between
    # every pair of events for the length of the run.
    repeats: Dict[str, int] = {}
    for _when, _rank, is_action, text in events:
        if is_action:
            repeats[text] = repeats.get(text, 0) + 1
    shown: Dict[str, int] = {}

    def flush() -> None:
        if not pending:
            return
        if len(pending) <= TIMELINE_ACTION_RUN:
            for when, _rank, _is_action, text in pending:
                seen = shown.get(text, 0)
                shown[text] = seen + 1
                if seen >= TIMELINE_REPEAT_LIMIT:
                    continue
                total = repeats.get(text, 1)
                suffix = ("" if total <= TIMELINE_REPEAT_LIMIT
                          else f"  (this request is made {total} times in this "
                               "run and is shown twice)")
                lines.append(f"{offset(when, start)}  {text}{suffix}")
        else:
            methods = sorted({text.split()[1] for _w, _r, _a, text in pending})
            lines.append(f"{offset(pending[0][0], start)}  {len(pending)} device "
                         f"requests ({', '.join(methods)})")
        pending.clear()

    for when, rank, is_action, text in events:
        if is_action:
            pending.append((when, rank, is_action, text))
            continue
        flush()
        lines.append(f"{offset(when, start)}  {text}")
    flush()
    return lines + [""]


def describe_action(action: dict) -> str:
    """One harness action, in the words the timeline reads best in."""
    text = f"{action.get('method')} {action.get('path')}"
    params = action.get("params")
    if isinstance(params, dict):
        # A query may reach a record as an object or as one string, and both
        # read the same here.
        params = " ".join(f"{k}={v}" for k, v in sorted(params.items()))
    if params:
        text += " " + str(params)
    if action.get("status") not in (None, 200):
        text += f" -> {action['status']}"
    if action.get("retries"):
        text += f" after {action['retries']} attempts"
    if action.get("error"):
        text += f": {redact(str(action['error']))}"
    return redact(text)


def checks_section(run: Run) -> List[str]:
    """Every check, passing ones included.

    Q1 in the specification's Purpose is about checks that passed, so a passing
    check has to be in the document. It is here rather than in the summary part
    because a build page does not need 1300 rows to say a run was green, and
    the `extra` string is the only per-check evidence in the JSONL that a check
    measured anything: `OK` and `OK (0 rows)` are the same verdict and
    different results.
    """
    lines: List[str] = []
    for made in sorted(run.all_suites(), key=lambda s: (s.target, s.time)):
        if not made.checks:
            continue
        lines += [f"### {made.key}", ""]
        # Grouped by scenario, which is how the suite grouped them and how its
        # console output reads, with everything a suite reported outside one
        # under a heading of its own.
        scenarios: List[str] = []
        for check in made.checks:
            if check.scenario not in scenarios:
                scenarios.append(check.scenario)
        for scenario in scenarios:
            rows = [[str(c.index), c.check_label,
                     c.verdict + (" SLOW" if c.slow else ""),
                     duration(c.seconds), clock(c.started), clock(c.time),
                     redact(c.extra) or "-"]
                    for c in made.checks if c.scenario == scenario]
            if len(scenarios) > 1 or scenario:
                lines += [f"**{scenario or 'outside any scenario'}**", ""]
            lines += table(["#", "Check", "Verdict", "Duration", "Opened at",
                            "Closed at", "Reported"], rows) + [""]
    if not lines:
        return []
    return ["## Checks", ""] + lines


def time_section(run: Run) -> List[str]:
    """Where the time went. A gate is judged on wall clock as much as verdict."""
    suites = sorted((s for s in run.all_suites() if s.seconds),
                    key=lambda s: s.seconds or 0.0, reverse=True)[:SLOWEST_ROWS]
    checks = sorted(run.all_checks(), key=lambda c: c.seconds,
                    reverse=True)[:SLOWEST_ROWS]
    slow = [c for c in run.all_checks() if c.slow]
    if not suites and not checks:
        return []
    lines = ["## Where the time went", ""]
    if suites:
        lines += ["Slowest suite runs:", ""]
        lines += table(["Suite run", "Duration"],
                       [[s.key, duration(s.seconds)] for s in suites]) + [""]
    if checks:
        lines += ["Slowest checks:", ""]
        lines += table(["Check", "Label", "Duration"],
                       [[c.key, c.check_label, duration(c.seconds)]
                        for c in checks]) + [""]
    if slow:
        lines += [f"Checks over {report_lib.SLOW_CHECK_SECONDS:g}s, which the "
                  "console marks SLOW. A prompt to look, not a verdict.", ""]
        lines += table(["Check", "Label", "Duration"],
                       [[c.key, c.check_label, duration(c.seconds)]
                        for c in sorted(slow, key=lambda c: c.seconds,
                                        reverse=True)]) + [""]
    return lines


# ---------------------------------------------------------------------------
# Assembling the document
# ---------------------------------------------------------------------------


def render(run: Run, previous: Optional[Run] = None) -> str:
    """The whole document. Deterministic: two runs over one tree agree.

    A section whose source does not exist in this run is omitted rather than
    rendered empty, and the file index is what tells a reader that an artefact
    was not produced rather than lost.
    """
    lines: List[str] = []
    lines += header(run)
    lines += ["", PREAMBLE, ""]
    lines += verdict_section(run)
    lines += coverage_section(run)
    lines += changes_section(run)
    if previous is not None:
        lines += compare_section(run, previous)
    lines += failing_section(run)
    lines += health_section(run)
    lines += files_section(run)
    lines += [DETAIL_MARKER, ""]
    lines += timeline_section(run)
    lines += checks_section(run)
    lines += time_section(run)
    lines += screens_section(run)
    lines += log_section(run)

    text = "\n".join(line.rstrip() for line in lines)
    while "\n\n\n" in text:
        text = text.replace("\n\n\n", "\n\n")
    return text.rstrip("\n") + "\n"


def write_report(directory: str, compare: str = "") -> str:
    """Render `directory` and write its index.md. Returns the path written."""
    run = load_tree(directory)
    previous = load_tree(compare) if compare else None
    path = os.path.join(directory, INDEX_NAME)
    with open(path, "w", encoding="utf-8") as handle:
        handle.write(render(run, previous))
    return path


# ---------------------------------------------------------------------------
# The GitHub job summary: a copy of part of the report, never a second render
# ---------------------------------------------------------------------------


def summary_part(document: str) -> str:
    """Everything above the detail marker, or the whole document without one."""
    marker = document.find(DETAIL_MARKER)
    if marker < 0:
        return document
    return document[:marker]


def job_summary(directory: str) -> int:
    """Append the report's summary part to the CI job summary.

    A copy, not a render. There is one authored report and every other format
    is derived from it by a program, because two renderers of one fact drift
    and the drift is invisible until somebody compares them.

    Exactly two lines may follow the copy, and nothing else ever: the artifact
    link, and a note when the copy was truncated. The link cannot be in the
    report, because the report is generated before the artifact exists; putting
    a placeholder there for the job to substitute would make the report a
    template and the summary a renderer, which is what having one authored
    report exists to prevent.

    Writes nothing and exits zero when there is no job summary to write to,
    which is every run outside CI.
    """
    path = os.environ.get("GITHUB_STEP_SUMMARY")
    if not path:
        return 0
    try:
        with open(os.path.join(directory, INDEX_NAME), encoding="utf-8") as handle:
            document = handle.read()
    except OSError as exc:
        print(f"e2e_report: no report to summarise: {exc}", file=sys.stderr)
        return 0

    text = summary_part(document)
    appended = []
    limit = SUMMARY_LIMIT_BYTES - SUMMARY_APPENDED_MARGIN
    if len(text.encode("utf-8")) > limit:
        kept = []
        used = 0
        for line in text.splitlines(keepends=True):
            size = len(line.encode("utf-8"))
            if used + size > limit:
                break
            kept.append(line)
            used += size
        text = "".join(kept)
        appended.append(f"\n_Truncated at a line boundary to stay under "
                        f"{SUMMARY_LIMIT_BYTES // 1024} KiB. The whole report is "
                        f"in the artifact._\n")

    url = os.environ.get("E2E_ARTIFACT_URL")
    if url:
        appended.append(f"\n[Download the full report and the run's JSONL]({url}) "
                        "(you must be logged in to GitHub).\n")

    try:
        with open(path, "a", encoding="utf-8") as handle:
            handle.write(text + "".join(appended))
    except OSError as exc:
        print(f"e2e_report: could not write the job summary: {exc}",
              file=sys.stderr)
    return 0


# ---------------------------------------------------------------------------
# Comparing two runs
# ---------------------------------------------------------------------------


def comparison_key(check: Check) -> str:
    """A check's identity across two runs.

    The attempt is dropped. It is what makes a key unique inside one run, where
    a retried suite repeats its check indices, and it is a property of that
    run's flakiness rather than of the check, so keeping it would report every
    retried check as newly run.
    """
    return f"{check.target}/{check.label}/{check.suite}/{check.index}"


def final_verdicts(run: Run) -> Dict[str, Tuple[str, str]]:
    """Each check's last verdict in this run, and its label."""
    found: Dict[str, Tuple[str, str]] = {}
    for check in sorted(run.all_checks(), key=lambda c: (c.attempt, c.time)):
        found[comparison_key(check)] = (check.verdict, check.check_label)
    return found


def compare_section(run: Run, previous: Run) -> List[str]:
    """What changed since another run of the same gate.

    A reader looking at a red build asks "is this new" before anything else,
    and a reader looking at a green one asks "did we lose coverage". Both are a
    comparison of two trees, and the identity key is what joins them. No
    service, no retention and no accumulation: two directories on disk, which
    is what a CI job already has when it downloads the previous run's artifact.
    """
    now = final_verdicts(run)
    before = final_verdicts(previous)
    groups = (
        ("Newly failing", [k for k, v in now.items()
                           if v[0] == "FAIL" and before.get(k, ("",))[0] != "FAIL"
                           and k in before]),
        ("Newly passing", [k for k, v in now.items()
                           if v[0] == "OK" and k in before
                           and before[k][0] != "OK"]),
        ("Newly skipped", [k for k, v in now.items()
                           if v[0] == "SKIP" and k in before
                           and before[k][0] != "SKIP"]),
        ("No longer run", [k for k in before if k not in now]),
        ("New in this run", [k for k in now if k not in before]),
    )
    if not any(members for _title, members in groups):
        return []

    identity = previous.identity
    named = identity.get("commit") or clock(previous.started)
    lines = [f"## Changes since {named}", "",
             "Joined on each check's identity key without its attempt, so a "
             "retried check compares as one.", ""]
    for title, members in groups:
        if not members:
            continue
        rows = []
        for key in sorted(members):
            was = before.get(key, ("-", ""))[0]
            is_now = now.get(key, ("-", ""))[0]
            label = (now.get(key) or before.get(key))[1]
            rows.append([key, label, was, is_now])
        lines += [f"### {title}", ""]
        lines += table(["Check", "Label", "Was", "Now"], rows) + [""]
    return lines


def main(argv: Sequence[str]) -> int:
    parser = argparse.ArgumentParser(
        prog="e2e_report",
        description=__doc__.splitlines()[0],
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="The exit status is zero whenever a document was written.")
    parser.add_argument("directory", metavar="DIR",
                        help="A run's output directory. index.md is written into it.")
    parser.add_argument("--compare", default="", metavar="DIR",
                        help="Another run's output directory. Adds a section naming "
                             "every check whose verdict differs between the two.")
    parser.add_argument("--job-summary", action="store_true",
                        help="Append the report's summary part to the file named "
                             "by GITHUB_STEP_SUMMARY, and do nothing without it. "
                             "A copy of part of index.md, never a second render.")
    args = parser.parse_args(list(argv))
    if not os.path.isdir(args.directory):
        print(f"e2e_report: {args.directory} is not a directory", file=sys.stderr)
        return 2
    if args.job_summary:
        return job_summary(args.directory)
    print(write_report(args.directory, args.compare))
    return 0


# ---------------------------------------------------------------------------
# The stills, which are the artefact most readers will actually look at
# ---------------------------------------------------------------------------


@dataclass
class StillEntry:
    """One still as the report shows it: what it is, where it is, what it said."""

    kind: str
    relative: str
    text: List[str]
    # Where in the recording this frame is, as the recorder wrote it down.
    # Empty when the tree predates the recorder writing it, and empty is then
    # what the report says: a position recomputed from the suite's timing was
    # wrong by up to 4.7 seconds, and a wrong position is worse than none.
    position: str = ""
    # How a reader gets from a picture back to the raw record: the interaction
    # the run had last recorded when this frame was composed. A still carries
    # no chrome, because extracting the video at its frame has to reproduce it
    # pixel for pixel, so the way back is a reference rather than pixels.
    interaction: str = ""


def recorded_stills(target: TargetRun) -> Dict[str, dict]:
    """Every still the capture record describes, keyed by its text file name.

    The recorder writes one entry per still with the frame it was taken at,
    which is the only place that number exists: a suite record says when a
    suite ran, not which of its frames was kept.
    """
    found: Dict[str, dict] = {}
    if not target.capture:
        return found
    for entry in as_list(target.capture.get("stills")):
        if isinstance(entry, dict) and entry.get("text"):
            found[str(entry["text"])] = entry
    return found


def mmss(seconds: float) -> str:
    """A position in a recording, as the report writes every one of them."""
    whole = max(0, int(seconds))
    return f"{whole // 60:02d}:{whole % 60:02d}"


def stills_for(run: Run, target: TargetRun,
               made: SuiteRun) -> List[StillEntry]:
    """One suite run's stills, in capture order.

    Ordered by the frame the recorder took each one at when it recorded them,
    and by file name otherwise, so a tree written by an older recorder still
    renders in a fixed order.
    """
    directory = os.path.join(run.directory, target.slug, "capture")
    described = recorded_stills(target)
    found: List[Tuple[Tuple[int, str], StillEntry]] = []
    try:
        names = sorted(os.listdir(directory))
    except OSError:
        return []
    prefix = f"{made.stem}-{made.attempt}-"
    for name in names:
        if not name.startswith(prefix) or not name.endswith(".txt"):
            continue
        kind = name[len(prefix):-len(".txt")].split("-")[-1]
        if kind not in ("first", "change", "last"):
            continue
        entry = described.get(name, {})
        position = (mmss(as_float(entry.get("position")))
                    if "position" in entry else "")
        found.append(((as_int(entry.get("frame")), name),
                      StillEntry(kind, f"{target.slug}/capture/{name}",
                                 read_text(os.path.join(directory, name)),
                                 position, str(entry.get("interaction") or ""))))
    found.sort(key=lambda item: item[0])
    return [entry for _order, entry in found]


# What a recorder counts, and which of the two questions each figure answers.
# The first four are the network: packets and frames that were sent and did
# not arrive, or arrived in the wrong order, or arrived incomplete. The last
# two are this host: frames the recorder gave up composing, and frames it
# padded to a changed geometry.
TRANSPORT_COUNTS = (("packets_dropped", "packets dropped"),
                    ("frames_lost", "frames lost"),
                    ("frames_incomplete", "frames incomplete"),
                    ("frames_reordered", "frames reordered"),
                    ("audio_packets_lost", "audio packets lost"))
RECORDER_COUNTS = (("frames_shed", "frames shed"),
                   ("frames_padded", "frames padded"),
                   ("packets_malformed", "packets malformed"),
                   ("foreign_senders", "packets from another sender"))


def recording_block(run: Run) -> List[str]:
    """What each recording is, and what it lost, kept apart from what it did not.

    A recording competes with the suites for the device's streams, and the
    suites win. The interval a suite holds a stream for is not a lossy link
    and the two must not be one number: the figures below are the network's,
    and the re-anchors under them are the run's own doing.
    """
    rows = []
    reasons: List[str] = []
    for target in run.targets:
        capture = target.capture
        if not capture:
            continue
        frames = as_int(capture.get("frames"))
        fps = as_int(capture.get("fps")) or 1
        transport = [f"{as_int(capture.get(name))} {label}"
                     for name, label in TRANSPORT_COUNTS
                     if as_int(capture.get(name))]
        recorder = [f"{as_int(capture.get(name))} {label}"
                    for name, label in RECORDER_COUNTS
                    if as_int(capture.get(name))]
        rows.append([target.token,
                     ", ".join(f"`{name}`" for name in as_list(
                         capture.get("files"))) or "-",
                     f"{frames}",
                     f"{frames // fps // 60:02d}:{frames // fps % 60:02d}",
                     ", ".join(transport) or "nothing lost",
                     ", ".join(recorder) or "nothing"])
        lifecycle = as_dict(capture.get("stream_lifecycle"))
        for stream in sorted(lifecycle):
            named = ", ".join(f"{reason} {count}" for reason, count
                              in sorted(as_dict(lifecycle[stream]).items()))
            if named:
                reasons.append(f"- {target.token} {stream}: {named}")
    if not rows:
        return []
    lines = ["### Recordings", "",
             "The `lost` column is the network's: packets and frames the "
             "device sent that did not arrive, arrived incomplete, or arrived "
             "out of order. It does not include anything missing across a "
             "re-anchor.", ""]
    lines += table(["Target", "Files", "Frames", "Length", "Lost", "Recorder"],
                   rows)
    if reasons:
        lines += ["",
                  "A re-anchor is an interval across which the device's own "
                  "counters cannot be compared with each other, so nothing "
                  "missing across it is counted as loss. A suite stopping a "
                  "stream, the recorder asking for one again and a device "
                  "restarting all produce one:", ""]
        lines += reasons
    return lines + [""]


def screens_section(run: Run) -> List[str]:
    """Each suite run's stills, as text, with the image beside them.

    This is what makes a recording useful to a reader who never opens it: a
    handful of stills answers "what did this suite see" at a glance, with no
    download and no player. The text is what the report inlines and what a
    program or an agent can match on; the image is what a person opens.

    The recording's own accounting opens the section, because a reader has to
    know whether the recording is complete before drawing conclusions from
    what it shows.
    """
    lines: List[str] = list(recording_block(run))
    for target in run.targets:
        shown = set()
        groups = []
        for made in sorted(target_suites(run, target), key=lambda s: s.time):
            chosen = stills_for(run, target, made)
            if chosen:
                shown.update(entry.relative for entry in chosen)
                groups.append((made.key, chosen))
        # Anything in the capture directory that no suite run claimed. The
        # recorder writes a still under the identity it had when it took it,
        # and a run that ended between a retry and its records leaves one
        # behind; showing it under its own name beats not showing it.
        for stem, chosen in orphan_stills(run, target, shown):
            groups.append((f"{target.token}/{stem} (no suite record)", chosen))
        for heading, chosen in groups:
            lines += [f"### {heading}", ""]
            for entry in chosen:
                image = entry.relative[:-len(".txt")] + ".png"
                exists = os.path.exists(os.path.join(run.directory, image))
                # The kind, then where in the recording it is. A tree whose
                # recorder did not write the frame down gets the kind alone.
                where = f" at {entry.position}" if entry.position else ""
                reference = (f", interaction `{entry.interaction}`"
                             if entry.interaction else "")
                lines.append(f"**{entry.kind}**{where} (`{entry.relative}`"
                             + (f", image `{image}`" if exists else "")
                             + reference + "):")
                lines += [""] + fenced([redact(row) for row in entry.text]) + [""]
    if not lines:
        return []
    return ["## Screens", ""] + lines


def orphan_stills(run: Run, target: TargetRun,
                  shown: "set") -> List[Tuple[str, List[StillEntry]]]:
    """Stills in the capture directory that no suite run in the report claimed."""
    directory = os.path.join(run.directory, target.slug, "capture")
    described = recorded_stills(target)
    try:
        names = sorted(os.listdir(directory))
    except OSError:
        return []
    found: Dict[str, List[StillEntry]] = {}
    for name in names:
        if not name.endswith(".txt"):
            continue
        parts = name[:-len(".txt")].rsplit("-", 2)
        if len(parts) != 3 or parts[2] not in ("first", "change", "last"):
            continue
        relative = f"{target.slug}/capture/{name}"
        if relative in shown:
            continue
        entry = described.get(name, {})
        position = (mmss(as_float(entry.get("position")))
                    if "position" in entry else "")
        found.setdefault(parts[0], []).append(
            StillEntry(parts[2], relative,
                       read_text(os.path.join(directory, name)), position))
    return sorted(found.items())


def failing_stills(run: Run, made: SuiteRun) -> List[str]:
    """A failing suite's first and last still, where a reader is already looking.

    The transition stills stay in the detail part: they are the most numerous
    and the least likely to be the one a reader needs, and the summary part
    has to budget for what it carries per failure.
    """
    target = next((t for t in run.targets if t.token == made.target), None)
    if target is None:
        return []
    chosen = [entry for entry in stills_for(run, target, made)
              if entry.kind in ("first", "last")]
    lines: List[str] = []
    for entry in chosen:
        where = f" at {entry.position}" if entry.position else ""
        lines += ["", f"The {entry.kind} frame of this suite run{where} "
                      f"(`{entry.relative}`):", ""]
        lines += fenced([redact(row) for row in entry.text])
    return lines


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
