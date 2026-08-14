"""Console reporting shared by every test suite and by `run-tests`.

Used by `tests/e2e/`, `tests/perf/` and `tests/soak/` alike: nothing here is
specific to any one category. The rules it implements are written down in
`tests/lib/README.md`, and this is their only implementation, so a suite
reports by calling it rather than by formatting its own lines. The harness is
Python for the same reason: one implementation of the rules, not one per
language.

Three properties are built in rather than left to each caller:

- A check occupies exactly one line. A suite's own helpers are frequently
  written as checks and are also called from inside larger scenario checks.
  Without a guard the inner check prints its own numbered line in the middle of
  the outer one, which splits the outer line in two and numbers a step that is
  not a result of its own. Only the outermost check prints, and a `detail` call
  made inside a check body is held back until the verdict has been printed.
- Verdicts come from a fixed vocabulary and are coloured here. Suites that
  formatted their own verdicts produced `OK`, `PASS` and `[VERIFIED]` for the
  same idea, some coloured and some not.
- Everything reported here is also available as JSONL. Set `E2E_JSONL` to a path
  and every check, scenario and suite result is appended to it as one object per
  line, which is what makes a run consumable by something other than a reader.
  That variable and `E2E_SUITE` keep the names they were introduced with,
  because `run-tests` and the documented workflows already set them; both
  are read by every category, not only by the E2E suites.
"""

from __future__ import annotations

import json
import os
import sys
import time
from contextlib import contextmanager
from typing import Iterable, Iterator, List, Optional

# One colour set for the whole tree, so that a suite run on its own and the same
# suite run under a harness produce the same log, and harness lines and suite
# lines read as one convention rather than two.
BLUE = "\033[1;34m"
GREEN = "\033[1;32m"
RED = "\033[1;31m"
YELLOW = "\033[1;33m"
DIM = "\033[2m"
OFF = "\033[0m"

# Detail lines line up under the label, not under the "[NN] " index.
INDENT = " " * 5
RULE_WIDTH = 60

# The whole verdict vocabulary. Anything outside it is a suite inventing its own.
OK = "OK"
FAIL = "FAIL"
WARN = "WARN"
SKIP = "SKIP"

_VERDICT_COLOUR = {OK: GREEN, FAIL: RED, WARN: YELLOW, SKIP: YELLOW}

# A check that takes longer than this has its duration marked SLOW in yellow,
# so a slow one is visible while the run is happening rather than only in a
# later reading of the log. It is a prompt to look, not a failure.
#
# Ten seconds, not five, because five sat below what a whole class of checks
# can achieve rather than above it. Every context-menu action resets the C64
# and waits for its cold start, which is 2.4s of the machine's own time, and
# then loads and runs a program: measured at 3.1s to prepare, 1.5s to navigate,
# 0.6s to invoke and 1.8s for the program to report itself. None of those steps
# can be dropped, and removing the reset would let one action's running program
# corrupt the next. A threshold that every such check trips teaches people to
# ignore the mark, which costs more than it finds.
SLOW_CHECK_SECONDS = 10.0
# Worst first: a scenario reports the worst verdict any of its checks produced.
_SEVERITY = [FAIL, WARN, SKIP, OK]


def _colour_enabled() -> bool:
    """Colour on a terminal, plain text when redirected. The usual convention.

    Writing escapes into a redirected stream is what makes a saved log awkward
    to read afterwards: `less` sees the escape bytes and asks whether the file
    really is binary, and `grep` has to match around them.

    - NO_COLOR turns colour off even on a terminal. Any non-empty value counts,
      which is what no-color.org specifies.
    - FORCE_COLOR turns it on even when redirected, for a pager such as
      `less -R` or a CI viewer that renders escapes itself. FORCE_COLOR=0 means
      off, the way the npm ecosystem that introduced the variable reads it, so
      a value of "0" is not treated as "force it on".

    Decided once for the whole run rather than per stream: run-tests and the
    suites it starts share this stdout, so they agree with each other, and a
    captured log is either all plain or all coloured rather than a mixture.
    """
    if os.environ.get("NO_COLOR"):
        return False
    forced = os.environ.get("FORCE_COLOR")
    if forced:
        return forced != "0"
    try:
        return sys.stdout.isatty()
    except (AttributeError, ValueError):
        return False


_USE_COLOUR = _colour_enabled()


def colour_enabled() -> bool:
    """Whether this run is printing colour."""
    return _USE_COLOUR


def set_colour(enabled: bool) -> None:
    """Override the automatic choice, for a `--color` flag.

    A harness that takes the flag also exports NO_COLOR or FORCE_COLOR, so the
    suites it starts as child processes make the same choice it did.
    """
    global _USE_COLOUR
    _USE_COLOUR = enabled


# A harness sets E2E_SUITE so that every record a suite writes carries the name
# the harness knows it by; run-tests does this for each suite it starts. A
# suite started directly, which is how the perf and soak suites are normally
# run, falls back to its own script name.
SUITE_NAME = os.environ.get("E2E_SUITE") or os.path.splitext(
    os.path.basename(sys.argv[0] or "test"))[0]
JSONL_PATH = os.environ.get("E2E_JSONL") or ""

# Which device this process is testing, and which go at it this is. A harness
# exports both beside E2E_SUITE, so every record joins to a target and to an
# attempt without a correlation identifier of its own. A suite started by hand
# has neither, and records neither rather than a guessed value: a run's target
# is what the harness aimed it at, and a suite has no way to know.
#
# The attempt matters because a retried suite repeats its check indices in one
# file, which run_one_attempt truncates only on the first attempt. Two records
# carrying index 26 are told apart by this field and by nothing else.
TARGET_NAME = os.environ.get("E2E_TARGET") or ""
_raw_attempt = os.environ.get("E2E_ATTEMPT") or ""
ATTEMPT: Optional[int] = int(_raw_attempt) if _raw_attempt.isdigit() else None

_count = 0
_depth = 0
_last_label = ""
# Detail lines produced while a check line is still open.
_pending: List[str] = []
_check_started = 0.0
_suite_started = time.monotonic()
# The open scenario: its title, start time, check count and worst verdict.
_scenario: Optional[dict] = None


class Failure(RuntimeError):
    """A check did not hold. The message is what the run reports."""


# Strings that must not reach an artefact. The device password is the whole of
# it: build_command substitutes it into every suite's argument vector and
# RestClient carries it, so a recorded command line, a query or a response body
# can hold it, and the artefacts leave the machine that produced them. A CI
# artifact is downloadable by anyone who can see the build.
#
# Masking happens where records are written rather than at each call site,
# because every writer would otherwise need its own copy of the rule and the
# one that forgot would be the one that leaked.
SECRET_MASK = "***"
_secrets: List[str] = []


def mask_secret(value: str) -> None:
    """Never write `value` into a record. Empty values are ignored."""
    if value and value not in _secrets:
        _secrets.append(value)


def secrets() -> tuple:
    """Every string registered with mask_secret.

    For a component that has to mask something other than a record: the
    captured console log is the case, because a suite may print the password
    it was given and the runner is the process that saves what it printed.
    """
    return tuple(_secrets)


def masked(value):
    """`value` with every registered secret replaced, however deeply nested.

    For a component that writes something other than a record and has to
    honour the same rule: the screen spool is the case.
    """
    return _masked(value)


def _masked(value):
    """`value` with every registered secret replaced, however deeply nested."""
    if not _secrets:
        return value
    if isinstance(value, str):
        for secret in _secrets:
            value = value.replace(secret, SECRET_MASK)
        return value
    if isinstance(value, dict):
        return {key: _masked(item) for key, item in value.items()}
    if isinstance(value, (list, tuple)):
        return [_masked(item) for item in value]
    return value


def colour(text: str, code: str) -> str:
    return f"{code}{text}{OFF}" if _USE_COLOUR else text


def format_duration(seconds: float) -> str:
    """Wall time with fewer decimals as the number grows.

    `0.020s`, `1.002s`, `23.5s`, `264s`. At a second the milliseconds separate a
    round trip from a redraw; at a minute they are noise, and printing them only
    makes the column harder to scan.
    """
    if seconds < 10:
        return f"{seconds:.3f}s"
    if seconds < 100:
        return f"{seconds:.1f}s"
    return f"{seconds:.0f}s"


def _record(**fields) -> None:
    """Append one JSONL object, when a path was asked for.

    Opened per record and in append mode: under a harness the suites are
    separate processes writing one file, and a single short line under O_APPEND
    is not interleaved with another.
    """
    if not JSONL_PATH:
        return
    fields.setdefault("time", time.time())
    fields.setdefault("suite", SUITE_NAME)
    if TARGET_NAME:
        fields.setdefault("target", TARGET_NAME)
    if ATTEMPT is not None:
        fields.setdefault("attempt", ATTEMPT)
    try:
        line = json.dumps(_masked(fields), default=repr)
        with open(JSONL_PATH, "a", encoding="utf-8") as handle:
            handle.write(line + "\n")
    except (OSError, TypeError, ValueError):
        # Reporting must never be the reason a run fails, and a caller that
        # passed something unusual is not a reason to end one. `default=repr`
        # already carries an object json cannot encode; this catches whatever
        # it cannot.
        pass


def check_count() -> int:
    """How many checks have been reported, for a suite's closing line."""
    return _count


def current_check() -> Optional[int]:
    """The index of the check that is open, or None between two checks.

    For a record written by something other than the check itself, so that a
    device request made inside a check joins to it without an identifier of
    its own.

    None while nothing is open, and None in a process that reports only
    unnumbered steps, which is what `run-tests` does: `step_start` opens a line
    without numbering it, so a zero there would name a check that does not
    exist and several steps would share it.
    """
    return _count if _depth and _count else None


def last_label() -> str:
    """The label of the most recently started check, for crash reporting."""
    return _last_label


def check_start(label: str) -> None:
    """Open a check line as `[NN] label ... `, leaving the verdict for later."""
    global _count, _depth, _last_label, _check_started
    _depth += 1
    if _depth > 1:
        return
    _count += 1
    _last_label = label
    _check_started = time.monotonic()
    print(f"[{_count:02d}] {label} ... ", end="", flush=True)


def step_start(label: str) -> None:
    """Open an unnumbered line, for a step that is not a check of its own.

    A harness's precondition and teardown gates run around the suites rather
    than inside one, so numbering them would interleave two counters.
    """
    global _depth, _check_started, _last_label
    _depth += 1
    if _depth > 1:
        return
    _last_label = label
    _check_started = time.monotonic()
    print(f"{label} ... ", end="", flush=True)


def _close(verdict: str, extra: str = "") -> None:
    global _depth
    _depth = max(0, _depth - 1)
    if _depth:
        return
    elapsed = time.monotonic() - _check_started
    parts = [extra] if extra else []
    duration = format_duration(elapsed)
    if elapsed >= SLOW_CHECK_SECONDS:
        # The word as well as the colour: a captured log is read with less or
        # grep, where colour is either absent or the reason the file is taken
        # for a binary one, and the point of flagging a slow check is lost if
        # it only survives on a live terminal.
        duration = colour(duration + " SLOW", YELLOW)
    parts.append(duration)
    print(f"{colour(verdict, _VERDICT_COLOUR[verdict])} ({', '.join(parts)})", flush=True)
    if _pending:
        _emit_detail(_pending)
        _pending.clear()
    if _scenario is not None:
        _scenario["checks"] += 1
        if _SEVERITY.index(verdict) < _SEVERITY.index(_scenario["verdict"]):
            _scenario["verdict"] = verdict
    _record(kind="check", index=_count, label=_last_label, verdict=verdict,
            extra=extra, seconds=round(elapsed, 4),
            scenario=_scenario["title"] if _scenario else None)


def check_ok(extra: str = "") -> None:
    _close(OK, extra)


def check_fail(reason: str = "") -> None:
    _close(FAIL, reason)


def check_warn(reason: str = "") -> None:
    _close(WARN, reason)


def check_skip(reason: str = "") -> None:
    _close(SKIP, reason)


@contextmanager
def check(label: str) -> Iterator[None]:
    """Report one check, taking its verdict from whether the block raised.

    The verdict is printed even when the block raises, so the failing check is
    the last thing on screen before the traceback.
    """
    check_start(label)
    try:
        yield
    except BaseException:
        check_fail()
        raise
    check_ok()


def detail(text: str) -> None:
    """A continuation line under the check that produced it.

    For measurements and inventories that a one-line verdict cannot carry. Each
    line is indented, so it never reads as a check of its own.

    Called while a check line is still open, the line is held back until that
    check has printed its verdict. Printing it straight away would push the
    verdict onto the next line, which is the one-line rule broken from the other
    side: not a nested check, but a caller narrating mid-check.
    """
    if _depth:
        _pending.extend(str(text).splitlines() or [""])
        return
    _emit_detail(str(text).splitlines() or [""])


def _emit_detail(lines: Iterable[str]) -> None:
    for line in lines:
        print(f"{INDENT}{line}", flush=True)


def progress(text: str) -> None:
    """Overwrite a live progress line, or stay quiet when output is captured.

    Rewriting a line is a terminal affordance. In a captured log a carriage
    return lands in the middle of the file and the line before it is lost, so
    redirected output gets the summary that follows and nothing else.
    """
    if not sys.stdout.isatty():
        return
    sys.stdout.write(f"\r{INDENT}{text}")
    sys.stdout.flush()


def progress_done() -> None:
    """Finish a run of progress() so the next line starts on its own."""
    if sys.stdout.isatty():
        sys.stdout.write("\n")
        sys.stdout.flush()


def _close_scenario() -> None:
    """Print the open scenario's own verdict, so a group can be read at a glance."""
    global _scenario
    if _scenario is None:
        return
    scenario, _scenario = _scenario, None
    if not scenario["checks"]:
        # A heading that grouped no checks is a heading, not a scenario, and a
        # verdict on nothing is noise.
        return
    elapsed = time.monotonic() - scenario["started"]
    verdict = scenario["verdict"]
    summary = f"{scenario['checks']} checks, {format_duration(elapsed)}"
    print(f"{colour('--- ' + verdict, _VERDICT_COLOUR[verdict])} ({summary})", flush=True)
    _record(kind="scenario", title=scenario["title"], verdict=verdict,
            checks=scenario["checks"], seconds=round(elapsed, 4))


def section(title: str) -> None:
    """Start a scenario: a group of checks with a heading and its own verdict.

    The previous scenario is closed first, so its verdict and elapsed time land
    directly under its checks and a reader can see where one scenario ends and
    the next begins without counting lines.
    """
    _close_scenario()
    global _scenario
    print(f"\n{colour('--- ' + title, BLUE)}", flush=True)
    _scenario = {"title": title, "started": time.monotonic(), "checks": 0, "verdict": OK}


def banner(title: str) -> None:
    """A top-level heading: a rule above and below the title.

    For a harness's suite headings and its summary. Heavier than a scenario
    heading on purpose: this is where a reader looks to see a new suite start.
    """
    _close_scenario()
    rule = "=" * RULE_WIDTH
    print(f"\n{colour(rule, BLUE)}\n{colour(title, BLUE)}\n{colour(rule, BLUE)}", flush=True)


def warn(message: str) -> None:
    """A warning that belongs to no particular check."""
    print(f"{INDENT}{colour(WARN, YELLOW)} {message}", flush=True)
    _record(kind="warning", message=message)


def _suite_line(name: str, verdict: str, extra: str, seconds: Optional[float],
                fields: Optional[dict] = None) -> None:
    _close_scenario()
    # An explicit `seconds` means a harness is timing a suite it ran as a child
    # process, so this process's own check counter describes nothing and is left
    # out. A suite closing its own line passes no seconds and gets the count.
    own_closing_line = seconds is None
    elapsed = time.monotonic() - _suite_started if seconds is None else seconds
    parts = [part for part in
             (extra or (f"{_count} checks" if own_closing_line else ""),
              format_duration(elapsed)) if part]
    print(f"{name}: {colour(verdict, _VERDICT_COLOUR[verdict])} ({', '.join(parts)})",
          flush=True)
    _record(kind="suite", name=name, verdict=verdict, note=extra,
            checks=_count, seconds=round(elapsed, 4), **(fields or {}))


def suite_ok(name: str, extra: str = "", seconds: Optional[float] = None,
             **fields) -> None:
    """The closing line of a passing suite.

    `fields` are added to the JSONL record only, for a harness that knows
    things about the run the suite itself cannot: which UI profile it was, which
    attempt this was, how many times the device had to be recovered around it.
    They are deliberately not printed: the console line is one line.
    """
    _suite_line(name, OK, extra, seconds, fields)


def suite_fail(name: str, reason: str, seconds: Optional[float] = None,
               **fields) -> None:
    """The closing line of a failing suite."""
    _suite_line(name, FAIL, reason, seconds, fields)


def suite_skip(name: str, reason: str, seconds: Optional[float] = None,
               **fields) -> None:
    """The closing line of a suite that could not run."""
    _suite_line(name, SKIP, reason, seconds, fields)


def suite_warn(name: str, reason: str, seconds: Optional[float] = None,
               **fields) -> None:
    """A suite that passed but left something behind. Not a failure."""
    _suite_line(name, WARN, reason, seconds, fields)


def action(method: str, path: str, **fields) -> None:
    """One thing the harness did to the device, for the JSONL only.

    The other records say what the device showed and what the checks
    concluded. None of them says what the harness did to it, and without that
    a reader watching a screen go blank cannot tell a reset the run performed
    from a crash it observed.

    Written by the transport rather than by a caller, so every deliberate act
    reaches one timeline whichever call made it.
    """
    index = current_check()
    fields = dict(fields)
    fields.pop("kind", None)
    fields["method"] = method
    fields["path"] = path
    if index is not None:
        fields["check"] = index
    _record(kind="action", **fields)


def plan_result(suites: Iterable[dict], sequence: Iterable[dict],
                **fields) -> None:
    """What a run intends to do, recorded before it does any of it.

    A green run that quietly ran 17 of 25 registered suites reads exactly like
    a green run that ran all of them, and "are the tests working properly" is
    not answerable without the difference.
    """
    _record(kind="plan", suites=list(suites), sequence=list(sequence), **fields)


def log_result(target: str, path: str, started: float, port: int) -> None:
    """Where one device's own log is being collected, and from when.

    A reader who finds no log file needs to know whether the collector never
    started or the device never sent anything, and these two are different
    answers.
    """
    _record(kind="log", target=target, path=path, started=started, port=port)


def health_result(label: str, ok: bool, checks: Iterable[dict]) -> None:
    """The JSONL record for one device health sweep.

    The console gets the sweep as a single line; this is the same sweep in a
    shape something other than a reader can use, with a latency per check. A
    run consumed programmatically would otherwise have no way to see why a
    device was called unhealthy, or to watch a listener getting slower over a week
    of runs.
    """
    _record(kind="health", label=label, ok=ok, checks=list(checks))


def set_jsonl_path(path: str) -> None:
    """Send this process's records to `path`, for a harness taking a flag.

    The suites a harness starts are told through E2E_JSONL, which is read at
    import. A harness parses its own arguments after importing this module, so
    it needs a way to say the same thing afterwards.
    """
    global JSONL_PATH
    JSONL_PATH = path


def set_target(token: str) -> None:
    """Name the device this process's own records are about.

    A harness resolves its target after importing this module, which is read
    at import for the suites it starts, so it says the same thing about itself
    here. See TARGET_NAME.
    """
    global TARGET_NAME
    TARGET_NAME = token


def run_result(verdict: str, suites: Optional[int] = None,
               passed: Optional[int] = None, failed: Optional[int] = None,
               skipped: Optional[int] = None, dirty: Optional[int] = None,
               seconds: float = 0.0, recoveries: int = 0,
               exit_code: Optional[int] = None, **fields) -> None:
    """The JSONL record for a whole run, written by a harness rather than a suite.

    Record shapes belong to this module, so a harness reports its own result
    through here instead of formatting a JSON object of its own.

    `recoveries` is how many times the device had to be brought back during the
    run, and `exit_code` is the status the harness is about to exit with, so a
    caller reading only the JSONL sees the same verdict as one reading `$?`.

    A harness that ran no suites of its own passes no counts, and the record
    carries none: a multi-target run's parent has children that each counted
    their own, and a zero there would be summed as if it were a result.

    `fields` carries what the run is a run of - the commit, the branch, the
    host, the command line - so a downloaded artifact says what produced it
    without a second file.
    """
    counts = {"suites": suites, "passed": passed, "failed": failed,
              "skipped": skipped, "dirty": dirty}
    _record(kind="run", verdict=verdict,
            **{name: value for name, value in counts.items() if value is not None},
            seconds=round(seconds, 4), recoveries=recoveries,
            exit_code=exit_code, **fields)


def die(message: str) -> None:
    """Report a setup problem that stops the suite before any check runs."""
    print(f"{colour(FAIL, RED)} {message}", file=sys.stderr, flush=True)


def format_exception(exc: BaseException) -> str:
    """The message a failure is reported with.

    urllib raises errors whose `str` omits the reason, which is the only part
    that says whether the device refused the connection or never answered.
    """
    import urllib.error

    if isinstance(exc, urllib.error.URLError) and getattr(exc, "reason", None) is not None:
        return f"{exc} ({exc.reason})"
    return str(exc)


def reset(count_from: Optional[int] = None) -> None:
    """Start numbering again. Only for a harness that runs suites in-process."""
    global _count, _depth, _last_label, _scenario, _suite_started
    _count = 0 if count_from is None else count_from
    _depth = 0
    _last_label = ""
    _scenario = None
    _suite_started = time.monotonic()
    _pending.clear()
