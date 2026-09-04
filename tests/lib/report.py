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
import threading
from contextlib import contextmanager
from collections.abc import Callable, Iterable, Iterator
from dataclasses import dataclass, field

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
    - GITHUB_ACTIONS is a redirected stream that does render escapes, so the
      verdicts are coloured there rather than left plain. Same rule as
      tools/app_space.py, which reads the same three variables.

    Decided once for the whole run rather than per stream: run-tests and the
    suites it starts share this stdout, so they agree with each other, and a
    captured log is either all plain or all coloured rather than a mixture.
    """
    if os.environ.get("NO_COLOR"):
        return False
    forced = os.environ.get("FORCE_COLOR")
    if forced:
        return forced != "0"
    if os.environ.get("GITHUB_ACTIONS") == "true":
        return True
    try:
        return sys.stdout.isatty()
    except (AttributeError, ValueError):
        return False


_USE_COLOUR = _colour_enabled()


def colour_enabled() -> bool:
    """Whether this run is printing colour."""
    return _USE_COLOUR


COLOUR_CHOICES = ("auto", "always", "never")


def add_colour_argument(parser) -> None:
    """Give a suite the one --color flag every harness here takes.

    One spelling and one set of values wherever a verdict is printed, so a
    caller does not have to remember which program takes which.
    """
    parser.add_argument("--color", choices=COLOUR_CHOICES, default="auto",
                        help="Colour the verdicts. auto means a terminal, or "
                             "GitHub Actions, which renders the escapes "
                             "itself; NO_COLOR and FORCE_COLOR are honoured "
                             "(default: auto).")


def apply_colour(choice: str) -> None:
    """Act on what --color was given, and tell any child process the same.

    The environment is set as well as the module state, because a suite that
    starts another program has no other way to pass the decision on.
    """
    if choice == "always":
        os.environ.pop("NO_COLOR", None)
        os.environ["FORCE_COLOR"] = "1"
        set_colour(True)
    elif choice == "never":
        os.environ.pop("FORCE_COLOR", None)
        os.environ["NO_COLOR"] = "1"
        set_colour(False)
    else:
        set_colour(_colour_enabled())


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
_SUITE_NAME = os.environ.get("E2E_SUITE") or os.path.splitext(
    os.path.basename(sys.argv[0] or "test"))[0]

# Which device this process is testing, and which go at it this is. A harness
# exports both beside E2E_SUITE, so every record joins to a target and to an
# attempt without a correlation identifier of its own. A suite started by hand
# has neither, and records neither rather than a guessed value: a run's target
# is what the harness aimed it at, and a suite has no way to know.
#
# The attempt matters because a retried suite repeats its check indices in one
# file, which run_one_attempt truncates only on the first attempt. Two records
# carrying index 26 are told apart by this field and by nothing else.
_raw_attempt = os.environ.get("E2E_ATTEMPT") or ""


@dataclass
class Reporter:
    """Everything one reporting process is part-way through saying.

    This was nine `global` statements over a dozen module variables: the check
    count, the nesting depth, the open line, the held-back detail lines, the
    open scenario, the JSONL path and the target name. The convention that kept
    it working was that only the main thread reports, written in a docstring
    and enforced by nothing, so a case calling `detail()` from a worker printed
    under whichever check happened to be open.

    The state is here instead, the free functions below delegate to `_default`,
    and `reset()` replaces that instance rather than reassigning a dozen names.
    `lock` guards the console-line state, so two threads cannot interleave a
    check line and its verdict.
    """

    # Which suite is reporting, which device it is aimed at, and which go this
    # is. A harness exports all three beside E2E_SUITE, so every record joins
    # to a target and an attempt without a correlation identifier of its own. A
    # suite started by hand has neither target nor attempt, and records neither
    # rather than a guessed value: a run's target is what the harness aimed it
    # at, and a suite has no way to know.
    #
    # The attempt matters because a retried suite repeats its check indices in
    # one file, which run_one_attempt truncates only on the first attempt. Two
    # records carrying index 26 are told apart by this field and nothing else.
    suite_name: str = _SUITE_NAME
    jsonl_path: str = os.environ.get("E2E_JSONL") or ""
    target_name: str = os.environ.get("E2E_TARGET") or ""
    attempt: int | None = int(_raw_attempt) if _raw_attempt.isdigit() else None

    count: int = 0
    depth: int = 0
    last_label: str = ""
    # Detail lines produced while a check line is still open.
    pending: list[str] = field(default_factory=list)
    # Whether those lines are being printed as they come rather than held back.
    # Set when a heading is printed under a check line that never got its
    # verdict; see _release_details.
    details_live: bool = False
    check_started: float = 0.0
    # Whether a check or step line is open and still owes a verdict. A body
    # that reports its own verdict, `check_skip` inside a `with check(...)`
    # being the common one, closes the line itself, and the block's own closing
    # call must then do nothing rather than print a second line and write a
    # second record.
    line_open: bool = False
    suite_started: float = field(default_factory=time.monotonic)
    # The open scenario: its title, start time, check count and worst verdict.
    scenario: dict | None = None
    # Guards every field above that the console line is made of: count, depth,
    # last_label, pending, details_live, check_started, line_open and scenario.
    # Held only across the few statements that open, extend or close a line,
    # never across a caller's own work.
    lock: threading.RLock = field(default_factory=threading.RLock)
    # Which thread opened the line that is currently open. One line is one
    # check, and `depth` counts nesting rather than concurrency, so two threads
    # reporting at once do not produce two lines: they produce one line with
    # the other thread's verdict on it. The convention has always been that
    # only the thread that opened a line writes to it; this is what says so.
    owner: int | None = None


_default = Reporter()


# The names other modules read straight off this one - interactions.py,
# screens.py, rest.py and the self-tests all do - now answer from the live
# Reporter. PEP 562: this runs only for a name the module does not define, so
# nothing else changes.
_FORWARDED = {"SUITE_NAME": "suite_name", "JSONL_PATH": "jsonl_path",
              "TARGET_NAME": "target_name", "ATTEMPT": "attempt"}


def __getattr__(name: str):
    field_name = _FORWARDED.get(name)
    if field_name is None:
        raise AttributeError(f"module {__name__!r} has no attribute {name!r}")
    return getattr(_default, field_name)


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
_secrets: list[str] = []


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
    if not _default.jsonl_path:
        return
    fields.setdefault("time", time.time())
    fields.setdefault("suite", _default.suite_name)
    if _default.target_name:
        fields.setdefault("target", _default.target_name)
    if _default.attempt is not None:
        fields.setdefault("attempt", _default.attempt)
    try:
        line = json.dumps(_masked(fields), default=repr)
        with open(_default.jsonl_path, "a", encoding="utf-8") as handle:
            handle.write(line + "\n")
    except (OSError, TypeError, ValueError):
        # Reporting must never be the reason a run fails, and a caller that
        # passed something unusual is not a reason to end one. `default=repr`
        # already carries an object json cannot encode; this catches whatever
        # it cannot.
        pass


def check_count() -> int:
    """How many checks have been reported, for a suite's closing line."""
    return _default.count


def current_check() -> int | None:
    """The index of the check that is open, or None between two checks.

    For a record written by something other than the check itself, so that a
    device request made inside a check joins to it without an identifier of
    its own.

    None while nothing is open, and None in a process that reports only
    unnumbered steps, which is what `run-tests` does: `step_start` opens a line
    without numbering it, so a zero there would name a check that does not
    exist and several steps would share it.
    """
    return _default.count if _default.depth and _default.count else None


def current_scenario() -> str:
    """The title of the open scenario, or "" when none is.

    For a record written by something other than the scenario itself, so that
    a device interaction joins to the group of checks it happened inside.
    """
    return str(_default.scenario["title"]) if _default.scenario else ""


def last_label() -> str:
    """The label of the most recently started check, for crash reporting."""
    return _default.last_label


def _require_owner(what: str) -> None:
    """Refuse a write to a line another thread opened.

    A pool that reports from its workers used to interleave silently: the
    second thread's `check_start` saw depth 1 and returned without printing,
    and its `check_ok` closed the first thread's line. The rule is that a
    check is opened, detailed and closed on one thread, and a harness running
    cases concurrently reports them from the main thread once each future
    resolves. See tests/lib/observability_test.py's `run_cases`.
    """
    owner = _default.owner
    if owner is not None and owner != threading.get_ident():
        raise Failure(
            f"{what} from thread {threading.get_ident()} while thread {owner} "
            f"has the line for {_default.last_label!r} open. A check is "
            "opened, detailed and closed on one thread; report a case's "
            "result from the thread that collects it.")


def check_start(label: str) -> None:
    """Open a check line as `[NN] label ... `, leaving the verdict for later."""
    with _default.lock:
        _require_owner(f"check_start({label!r})")
        if _default.depth == 0:
            _default.owner = threading.get_ident()
        _default.depth += 1
        if _default.depth > 1:
            return
        _default.count += 1
        _default.last_label = label
        _default.check_started = time.monotonic()
        _default.line_open = True
        _default.details_live = False
        print(f"[{_default.count:02d}] {label} ... ", end="", flush=True)


def step_start(label: str) -> None:
    """Open an unnumbered line, for a step that is not a check of its own.

    A harness's precondition and teardown gates run around the suites rather
    than inside one, so numbering them would interleave two counters.
    """
    with _default.lock:
        _require_owner(f"step_start({label!r})")
        if _default.depth == 0:
            _default.owner = threading.get_ident()
        _default.depth += 1
        if _default.depth > 1:
            return
        _default.last_label = label
        _default.check_started = time.monotonic()
        _default.line_open = True
        print(f"{label} ... ", end="", flush=True)


def _close(verdict: str, extra: str = "", *, elapsed: float | None = None) -> None:
    with _default.lock:
        _require_owner(f"{verdict} for {_default.last_label!r}")
        _default.depth = max(0, _default.depth - 1)
        if _default.depth:
            return
        _default.owner = None
        _default.details_live = False
        if not _default.line_open:
            # Already answered by the block itself. Closing again would print a
            # second verdict for one check and record a second, contradictory one:
            # a skipped check was written as SKIP and then as OK.
            return
        _default.line_open = False
        # A caller that measured its own check's duration (a case run on a worker
        # thread, reported afterwards on the main thread once the future resolves)
        # passes it explicitly, because time.monotonic() - _default.check_started would
        # otherwise measure from this call's check_start rather than from when the
        # work actually started.
        if elapsed is None:
            elapsed = time.monotonic() - _default.check_started
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
        if _default.pending:
            _emit_detail(_default.pending)
            _default.pending.clear()
        if _default.scenario is not None:
            _default.scenario["checks"] += 1
            if _SEVERITY.index(verdict) < _SEVERITY.index(_default.scenario["verdict"]):
                _default.scenario["verdict"] = verdict
        _record(kind="check", index=_default.count, label=_default.last_label, verdict=verdict,
                extra=extra, seconds=round(elapsed, 4),
                scenario=_default.scenario["title"] if _default.scenario else None)


def check_ok(extra: str = "", *, elapsed: float | None = None) -> None:
    _close(OK, extra, elapsed=elapsed)


def check_fail(reason: str = "", *, elapsed: float | None = None) -> None:
    _close(FAIL, reason, elapsed=elapsed)


def check_warn(reason: str = "", *, elapsed: float | None = None) -> None:
    _close(WARN, reason, elapsed=elapsed)


def check_skip(reason: str = "", *, elapsed: float | None = None) -> None:
    _close(SKIP, reason, elapsed=elapsed)


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
    with _default.lock:
        _require_owner("detail()")
        if _default.depth and not _default.details_live:
            _default.pending.extend(str(text).splitlines() or [""])
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


def _release_details() -> None:
    """Stop holding detail lines back once a check line has been left behind.

    `detail` holds its lines until the open check has printed its verdict, so
    that a caller narrating mid-check cannot push the verdict onto the next
    line. That is right while the check is still running, and wrong once it is
    not: an exception raised between `check_start` and the verdict call unwinds
    past the verdict, the line stays open for the rest of the process, and
    every later `detail` is buffered and never printed.

    That is where a suite prints its failure diagnostics. Measured on
    ftp_client_test against a C64 Ultimate: the check that raised printed no
    verdict, and the whole of `print_failure_diagnostics` -- the exception, the
    last REST input, the last FTP command, the decoded menu screen, the server
    log tail -- went into the buffer and was never seen, leaving "FAIL
    (failed)" as the only evidence the run kept.

    A heading is the signal that the check is behind us, so the pending lines
    are emitted here and later ones go straight out. No verdict is invented for
    the check: a heading printed while a check is open is also what an
    in-process suite driving the runner produces, and that check does go on to
    report its own verdict. See runner_policy_test, which runs `run_targets`
    inside a `check` block.
    """
    with _default.lock:
        if not _default.line_open:
            return
        _default.details_live = True
        print(flush=True)          # end the check line the verdict never closed
        if _default.pending:
            _emit_detail(_default.pending)
            _default.pending.clear()


def _close_scenario() -> None:
    """Print the open scenario's own verdict, so a group can be read at a glance."""
    with _default.lock:
        _release_details()
        if _default.scenario is None:
            return
        scenario, _default.scenario = _default.scenario, None
        if not scenario["checks"]:
            # A heading that grouped no checks is a heading, not a scenario, and
            # a verdict on nothing is noise.
            return
        elapsed = time.monotonic() - scenario["started"]
        verdict = scenario["verdict"]
        summary = f"{scenario['checks']} checks, {format_duration(elapsed)}"
        print(f"{colour('--- ' + verdict, _VERDICT_COLOUR[verdict])} ({summary})",
              flush=True)
        _record(kind="scenario", title=scenario["title"], verdict=verdict,
                checks=scenario["checks"], seconds=round(elapsed, 4))


def section(title: str) -> None:
    """Start a scenario: a group of checks with a heading and its own verdict.

    The previous scenario is closed first, so its verdict and elapsed time land
    directly under its checks and a reader can see where one scenario ends and
    the next begins without counting lines.
    """
    with _default.lock:
        _close_scenario()
        print(f"\n{colour('--- ' + title, BLUE)}", flush=True)
        _default.scenario = {"title": title, "started": time.monotonic(),
                             "checks": 0, "verdict": OK}


def banner(title: str) -> None:
    """A top-level heading: a rule above and below the title.

    For a harness's suite headings and its summary. Heavier than a scenario
    heading on purpose: this is where a reader looks to see a new suite start.
    """
    with _default.lock:
        _close_scenario()
        rule = "=" * RULE_WIDTH
        print(f"\n{colour(rule, BLUE)}\n{colour(title, BLUE)}\n{colour(rule, BLUE)}",
              flush=True)


def warn(message: str) -> None:
    """A warning that belongs to no particular check."""
    print(f"{INDENT}{colour(WARN, YELLOW)} {message}", flush=True)
    _record(kind="warning", message=message)


def _suite_line(name: str, verdict: str, extra: str, seconds: float | None,
                fields: dict | None = None) -> None:
    _close_scenario()
    # An explicit `seconds` means a harness is timing a suite it ran as a child
    # process, so this process's own check counter describes nothing and is left
    # out. A suite closing its own line passes no seconds and gets the count.
    own_closing_line = seconds is None
    elapsed = time.monotonic() - _default.suite_started if seconds is None else seconds
    parts = [part for part in
             (extra or (f"{_default.count} checks" if own_closing_line else ""),
              format_duration(elapsed)) if part]
    print(f"{name}: {colour(verdict, _VERDICT_COLOUR[verdict])} ({', '.join(parts)})",
          flush=True)
    _record(kind="suite", name=name, verdict=verdict, note=extra,
            checks=_default.count, seconds=round(elapsed, 4), **(fields or {}))


def suite_ok(name: str, extra: str = "", seconds: float | None = None,
             **fields) -> None:
    """The closing line of a passing suite.

    `fields` are added to the JSONL record only, for a harness that knows
    things about the run the suite itself cannot: which UI mode it was, which
    attempt this was, how many times the device had to be recovered around it.
    They are deliberately not printed: the console line is one line.
    """
    _suite_line(name, OK, extra, seconds, fields)


def suite_fail(name: str, reason: str, seconds: float | None = None,
               **fields) -> None:
    """The closing line of a failing suite."""
    _suite_line(name, FAIL, reason, seconds, fields)


def suite_skip(name: str, reason: str, seconds: float | None = None,
               **fields) -> None:
    """The closing line of a suite that could not run."""
    _suite_line(name, SKIP, reason, seconds, fields)


def suite_warn(name: str, reason: str, seconds: float | None = None,
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


def log_result(target: str, path: str, started: float, port: int,
               **fields) -> None:
    """Where one device's own log is being collected, and from when.

    A reader who finds no log file needs to know whether the collector never
    started or the device never sent anything, and these two are different
    answers.

    `fields` carries what only the collector knows: the addresses the run
    expects this target's lines from, the ports it collects them on, and, in
    the record written when the run ends, the addresses they actually arrived
    from and whether the port or the address is what attributed them. A device
    that logs from a second interface is the difference between the expected
    addresses and the observed ones, and without both a reader cannot see it
    at all; `ports` and `attributed` are what say whether the run was still
    able to file its lines correctly.
    """
    _record(kind="log", target=target, path=path, started=started, port=port,
            **fields)


def gap_result(component: str, started: float, ended: float | None = None,
               **fields) -> None:
    """One interval an observability component could not observe anything.

    A device that stopped answering, a stream that went quiet and a log that
    stopped arriving are one shape of event: a resource was unavailable from
    time A to time B. Recorded that way the timeline puts it beside the suite
    that was running, which is almost always the explanation. Recorded as a
    line per failed attempt it would be noise, and recorded not at all a reader
    could not tell an empty file from a quiet device.

    A gap still open when the run ends carries no `ended`, and the report says
    so rather than inventing one.
    """
    if ended is not None:
        fields["ended"] = ended
    _record(kind="gap", component=component, started=started, **fields)


def capture_result(**fields) -> None:
    """The recording's own health, for a reader of the file it produced.

    The counts are what makes a recording readable as evidence: a file with
    thousands of padded frames or hundreds of re-arms is telling a reader that
    the run fought the recorder for the stream, which is worth knowing before
    drawing conclusions from what it shows.
    """
    _record(kind="capture", **fields)


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
    _default.jsonl_path = path


def set_target(token: str) -> None:
    """Name the device this process's own records are about.

    A harness resolves its target after importing this module, which is read
    at import for the suites it starts, so it says the same thing about itself
    here. See _default.target_name.
    """
    _default.target_name = token


def run_result(verdict: str, suites: int | None = None,
               passed: int | None = None, failed: int | None = None,
               skipped: int | None = None, dirty: int | None = None,
               seconds: float = 0.0, recoveries: int = 0,
               exit_code: int | None = None, **fields) -> None:
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


def assert_or_warn(assertions_enabled: bool, condition: object,
                   message: str) -> bool:
    """Require `condition`, or only warn about it, and say which happened.

    A suite run with assertions off is measuring rather than judging, and a
    device that behaves differently is then a note beside the numbers. Three
    suites had this, and the copies disagreed on what they returned: one gave
    a bool, two gave None, so a caller ported between them lost its result and
    read every check as passing. It returns the bool.
    """
    if condition:
        return True
    if assertions_enabled:
        raise Failure(message)
    warn(message)
    return False


def teardown_step(label: str, action: Callable[[], object]) -> bool:
    """Run a teardown step, report what it could not do, and carry on.

    Teardown must not mask a verdict: a settings restore that fails after a
    check has already failed should not replace the real reason with its own.
    Every suite therefore ended with blocks of this shape:

        try:
            api.configs.set(store, item, original)
        except Exception:
            pass

    which is right about the verdict and wrong about the evidence. The device
    is left changed, nothing on the console or in the JSONL says so, and the
    next suite fails for a reason its report cannot connect to the cause. That
    is the mechanism behind the "unrelated failure after X" entries this tree's
    commit history keeps chasing.

    So: the same swallow, with the exception written down. Returns True when
    the action succeeded.

    What counts as the device rather than the caller is the set `run-tests`
    already calls DEVICE_ERRORS, less `TypeError`. A device being taken down
    mid-teardown does not only refuse connections: it answers with a truncated
    or mismatched body, which reaches a decoder as a `ValueError`
    (`json.JSONDecodeError` is one) or as an `http.client.HTTPException` that
    is not an `OSError`. This firmware's httpd has been seen handing back one
    request's body under another request's status while several REST workers
    are active, so those are device faults here too. A `TypeError` stays out,
    because a teardown that calls something wrongly is a bug to see.

        teardown_step(f"restore {item}", lambda: api.configs.set(store, item, was))
    """
    import ftplib
    import http.client

    try:
        action()
    except (Failure, OSError, TimeoutError, ValueError,
            http.client.HTTPException, *ftplib.all_errors) as exc:
        message = format_exception(exc) or type(exc).__name__
        detail(f"teardown: {label}: {message}")
        # `message` is what the report renders; `label` and `error` are what a
        # reader of the JSONL wants apart. See tools/e2e_report.py.
        _record(kind="teardown", label=label, ok=False,
                message=f"teardown: {label}: {message}",
                error=f"{type(exc).__name__}: {message}")
        return False
    return True


def format_exception(exc: BaseException) -> str:
    """The message a failure is reported with.

    urllib raises errors whose `str` omits the reason, which is the only part
    that says whether the device refused the connection or never answered.
    """
    import urllib.error

    if isinstance(exc, urllib.error.URLError) and getattr(exc, "reason", None) is not None:
        return f"{exc} ({exc.reason})"
    return str(exc)


def reset(count_from: int | None = None) -> None:
    """Start numbering again. Only for a harness that runs suites in-process."""
    with _default.lock:
        _default.count = 0 if count_from is None else count_from
        _default.depth = 0
        _default.owner = None
        _default.details_live = False
        _default.last_label = ""
        # Cleared with the owner, not left behind: an open line whose owner has
        # been dropped would let a bare check_ok() past _require_owner and print
        # a verdict for a check that was never started.
        _default.line_open = False
        _default.check_started = 0.0
        _default.scenario = None
        _default.suite_started = time.monotonic()
        _default.pending.clear()
