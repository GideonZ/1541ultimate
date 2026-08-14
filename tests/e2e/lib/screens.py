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
from typing import List, Optional, Sequence

sys.path.insert(0, os.path.join(
    os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))),
    "lib"))

import report  # noqa: E402

MENU = "menu"
TELNET = "telnet"

# Where the spool goes, exported by the runner under -j the way E2E_JSONL is.
# Absent means no spool, which is every run without -j and every run given
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


def publish(kind: str, rows: Sequence[str], raw: Optional[bytes] = None,
            cols: int = 0) -> None:
    """Record one screen, when it differs from the last one of its kind.

    Never raises and never blocks: this runs inside a suite's own keystroke
    loop, and a spool that could fail a check would be changing how a suite
    reaches its verdict.
    """
    if not SPOOL_PATH:
        return
    try:
        lines = [str(row) for row in rows]
        if _last.get(kind) == lines:
            return
        _last[kind] = lines
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
    try:
        with open(TRANSCRIPT_PATH, "ab") as handle:
            handle.write(data)
    except OSError:
        pass


def read(path: str) -> List[dict]:
    """Every screen in a spool, for a reader that has one.

    A record written while a writer was mid-line is skipped, the same rule
    every other reader of these files follows.
    """
    found: List[dict] = []
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


def last_before(path: str, when: float, kind: str = "") -> Optional[dict]:
    """The most recent screen in `path` at or before `when`.

    What the harness was looking at when something happened, which is what a
    failure capture needs under a mode whose screen the device cannot be asked
    for.
    """
    best = None
    for record in read(path):
        if kind and record.get("kind") != kind:
            continue
        if float(record.get("time") or 0.0) <= when:
            best = record
    return best
