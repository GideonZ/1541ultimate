"""Every distinct screen the harness read, spooled as it reads it.

The suites read the device's menu screen constantly, and they read it at
exactly the moments that matter: before a key is sent, while the redraw starts,
and again until it stops. Publishing what they already fetched costs the device
nothing and produces the richest textual record a run has, so this is written
whenever `-j` is, like the console capture and the action log, rather than
being part of anything optional.

What it answers, for a run nobody recorded video of: what was on screen when
check 26 failed. The failure capture depends on it as well, because under
`--mode telnet` the harness is looking at a Telnet session and
`machine:menu_screen` answers with the overlay's screen or with 404, so a
capture taken from the device would be showing a screen nobody was driving.

    time      when the screen was read, on the host's clock
    suite     which suite read it, from E2E_SUITE
    attempt   which go at that suite, from E2E_ATTEMPT
    check     the index of the check that was open, absent between checks
    kind      `menu` for a machine:menu_screen payload, `telnet` for a session
    cols/rows the geometry, declared rather than inferred from a length
    text      the screen as a list of strings, one per row
    raw       the device's own bytes, hex encoded, for `menu` only

Both `text` and `raw`, because they answer different questions. The text is
what a person greps, what a program matches on and what an agent can read with
no tooling at all. The raw bytes carry the colour plane and the reverse-video
bit that mark the selected row, and neither survives into the text. One writer
writes both from one payload, so there is nothing to drift.

Written only on a change. The settle loops read the same screen many times per
keystroke, and writing on a change collapses that to one record per redraw,
which is both the volume control and exactly the one-per-navigation-step the
recorder's harness pane wants. The raw Telnet transcript is not deduplicated:
it is a stream, and a gap in it would be a lie about what arrived.
"""

from __future__ import annotations

import json
import os
import sys
import time
from collections.abc import Sequence
from pathlib import Path

# tests/lib holds the shared library; importing bootstrap adds tests/e2e/lib.
# The search walks up rather than counting directories, so this is the same in
# every entry point and a suite that moves needs no edit. See tests/lib/bootstrap.py.
sys.path.insert(0, str(next(p for p in Path(__file__).resolve().parents
                            if (p / "tests" / "lib").is_dir()) / "tests" / "lib"))
import bootstrap  # noqa: E402,F401

import report  # noqa: E402

MENU = "menu"
TELNET = "telnet"

# Where the spool goes, exported by the runner under -o the way E2E_JSONL is.
# Absent means no spool, which is every run without -o and every run given
# --no-screens.
SPOOL_ENV = "E2E_SCREENS"
SPOOL_PATH = os.environ.get(SPOOL_ENV) or ""

# The raw Telnet transcript shares a stem with this suite run's other files, so
# a reader who has one has the others by changing the suffix. It is derived
# from the JSONL path rather than exported separately, because that path is
# already the one thing carrying this suite run's label.
_jsonl = os.environ.get("E2E_JSONL") or ""
TRANSCRIPT_PATH = (_jsonl[:-len(".jsonl")] + ".telnet.log"
                   if SPOOL_PATH and _jsonl.endswith(".jsonl") else "")

# The last payload published, per kind, so only a change is written.
_last: dict = {}


def enabled() -> bool:
    """Whether this run asked for a spool."""
    return bool(SPOOL_PATH)


def publish(kind: str, rows: Sequence[str], raw: bytes | None = None,
            cols: int = 0, key: object = None) -> None:
    """Record one screen, when it differs from the last one of its kind.

    `key` is what "differs" means, and the rendered text is not it. The
    selected row is marked by bit 7 of a character byte on a REST screen and
    by colour on a Telnet one, and neither survives into the text, so a cursor
    moving one row would otherwise be a screen this never recorded, which is
    exactly the navigation step a reader is looking for.

    Never raises and never blocks: this runs inside a suite's own keystroke
    loop, and a spool that could fail a check would be changing how a suite
    reaches its verdict.
    """
    if not SPOOL_PATH:
        return
    try:
        lines = [str(row) for row in rows]
        distinct = lines if key is None else key
        if _last.get(kind) == distinct:
            return
        _last[kind] = distinct
        record = {
            "time": time.time(),
            "suite": report.SUITE_NAME,
            "kind": kind,
            "cols": cols or (len(lines[0]) if lines else 0),
            "rows": len(lines),
            "text": lines,
        }
        if report.TARGET_NAME:
            record["target"] = report.TARGET_NAME
        if report.ATTEMPT is not None:
            record["attempt"] = report.ATTEMPT
        index = report.current_check()
        if index is not None:
            record["check"] = index
        if raw is not None:
            record["raw"] = raw.hex()
        line = json.dumps(report.masked(record))
        with open(SPOOL_PATH, "a", encoding="utf-8") as handle:
            handle.write(line + "\n")
    except (OSError, TypeError, ValueError):
        # Observing a run may not be the reason one fails.
        pass


def publish_event(kind: str, **fields) -> None:
    """Record something the harness did to a device stream.

    The same spool the screens go into, because a reader following what the
    harness was looking at also needs to know when a suite took a stream away:
    that is the difference between "the av suite stopped this stream here" and
    an unexplained gap in a recording.
    """
    if not SPOOL_PATH:
        return
    try:
        record = {"time": time.time(), "suite": report.SUITE_NAME, "kind": kind}
        if report.TARGET_NAME:
            record["target"] = report.TARGET_NAME
        if report.ATTEMPT is not None:
            record["attempt"] = report.ATTEMPT
        index = report.current_check()
        if index is not None:
            record["check"] = index
        record.update(fields)
        line = json.dumps(report.masked(record))
        with open(SPOOL_PATH, "a", encoding="utf-8") as handle:
            handle.write(line + "\n")
    except (OSError, TypeError, ValueError):
        pass


def publish_stream(data: bytes) -> None:
    """Append what a Telnet session sent, unparsed.

    `VT100Screen` is a parser, and a parser is a lossy view of its input: a
    defect in what the device sent, or in how the parser read it, is invisible
    in the parsed screen and obvious in the stream. This is the device's own
    text output for the one mode where the screen is not a device payload at
    all.
    """
    if not TRANSCRIPT_PATH or not data:
        return
    # A Telnet session carries the password to the device, and whether the
    # device echoes it back is the device's decision, not this one's. The
    # artefacts leave the machine that produced them.
    for secret in report.secrets():
        data = data.replace(secret.encode("utf-8", "replace"),
                            report.SECRET_MASK.encode())
    try:
        with open(TRANSCRIPT_PATH, "ab") as handle:
            handle.write(data)
    except OSError:
        pass


def read(path: str) -> list[dict]:
    """Every screen in a spool, for a reader that has one.

    A record written while a writer was mid-line is skipped, the same rule
    every other reader of these files follows.
    """
    found: list[dict] = []
    try:
        with open(path, encoding="utf-8", errors="replace") as handle:
            for line in handle:
                if not line.strip():
                    continue
                try:
                    decoded = json.loads(line)
                except ValueError:
                    continue
                if isinstance(decoded, dict):
                    found.append(decoded)
    except OSError:
        return []
    return found


def last_before(path: str, when: float, kind: str = "", suite: str = "",
                attempt: int | None = None,
                non_blank: bool = False) -> dict | None:
    """The most recent screen in `path` at or before `when`.

    What the harness was looking at when something happened, which is what a
    failure capture needs under a mode whose screen the device cannot be asked
    for.

    One spool holds every suite's screens, so `suite` and `attempt` are what
    keep a suite that read no screen at all from being shown the previous
    suite's, presented as its own.

    `non_blank` skips screens with nothing on them. A Telnet session that
    dropped mid-suite publishes an empty screen last, and an empty block in a
    report answers nothing; the screen before it is what the suite was working
    on. A caller that asks for this has to say in its own record that it did.
    """
    best = None
    for record in read(path):
        if kind and record.get("kind") != kind:
            continue
        if suite and record.get("suite") != suite:
            continue
        if attempt is not None and record.get("attempt") != attempt:
            continue
        if float(record.get("time") or 0.0) > when:
            continue
        if non_blank and not any(str(row).strip()
                                 for row in record.get("text") or []):
            continue
        best = record
    return best
