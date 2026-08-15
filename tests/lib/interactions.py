"""Every interaction the harness had with a device, written as it happens.

The `action` records of `report.py` are a curated subset: a GET that answered
200 first time is dropped, because the timeline in the report is a narrative
and a run's reads are its bulk. That is the right rule for a reader and the
wrong one for a program. An investigation asks questions the run did not
anticipate, and the commonest of them is "what exactly was sent, and what came
back, in the seconds before this went wrong". A curated log cannot answer it,
and a suite that catches a failure and carries on destroys the only copy of
what the device said.

So this is the exhaustive one. Every REST request and its response, every
Telnet exchange, every FTP command and reply, from inside the transports, so a
suite gains the coverage without a line of its own and cannot opt out of it.

Three properties make an exhaustive log affordable:

- **Consecutive identical interactions collapse.** A settle loop reads the same
  screen until it stops changing, which is the same request with the same
  answer thirty times. That is one record with a `repeat` count and an `until`
  time, not thirty records. The collapse is only ever of *consecutive*
  interactions, so nothing is reordered and nothing is merged across a gap.
- **A response body is written once.** Bodies repeat far more than requests do:
  a 2000-byte menu screen read four hundred times is four hundred copies of
  eight bytes of difference. A short answer is in the record, as text when it
  is text and as hex when it is not, because a one-byte read of memory is the
  byte and that is the thing an investigation reads. Anything larger goes to
  `bodies/<digest>.bin` under the target's directory and the record carries the
  digest, so the second and every later occurrence costs 71 bytes.
- **Nothing here can end a run.** Every entry point swallows what it hits. An
  observability component that fails a run it was watching is worse than one
  that is missing.

The file is one per target, appended to by the runner and by every suite that
drives that target, the same shape as `screens.jsonl`. Records carry the suite,
the attempt, the scenario and the check that were open, so they join to the
rest of the run with no correlation identifier of their own, and the report
turns their wall-clock time into a position in the recording the same way it
does for every other record.
"""

from __future__ import annotations

import atexit
import hashlib
import json
import os
import time
from typing import Dict, Optional

import report

# Where the log goes, exported by the runner under -o the way E2E_SCREENS is.
# Absent means no log, which is every run without -o.
LOG_ENV = "E2E_INTERACTIONS"
LOG_PATH = os.environ.get(LOG_ENV) or ""

# Where a body too large or too binary to inline is kept, beside the log.
BODY_DIRECTORY = "bodies"

# A body at or under this, and printable, is written into the record. Above it
# the record carries a digest and the bytes go to a file of their own. 200
# characters is a REST error message or a short JSON answer, which is what a
# reader wants in front of them; a menu screen is 2000 bytes and is not.
INLINE_BODY_CHARS = 200

# A body that is not text but is no larger than this goes into the record as
# hex. A `machine:readmem` of one byte answers with that byte, which is the
# value the caller asked for and the value an investigation reads, and a file
# of its own per byte would cost more than it stores.
INLINE_BODY_BYTES = 64

# How much of a request's parameters or of an error a record keeps. Long enough
# for a memory address and a block of hex, short enough that one pathological
# call cannot dominate the file.
TEXT_CHARS = 400

# The digest is truncated because it is an index into one run's own directory
# rather than a security claim: 16 hex characters is 64 bits, and a run makes
# tens of thousands of interactions.
DIGEST_CHARS = 16


# Digests this process has already written to the bodies directory, so one
# body costs one write however many interactions carry it.
_written_bodies: set = set()


def enabled() -> bool:
    """Whether this run asked for an interaction log."""
    return bool(LOG_PATH)


def set_path(path: str) -> None:
    """Send this process's interactions to `path`.

    For a harness that parses its arguments after importing this module, which
    is read at import for the suites it starts. See `report.set_jsonl_path`.
    """
    global LOG_PATH
    flush()
    LOG_PATH = path
    # A new destination is a new bodies directory, so what this process has
    # already written there is nothing to do with the new one.
    _written_bodies.clear()


class _Pending:
    """The record held back so an identical one after it can collapse into it."""

    def __init__(self) -> None:
        self.record: Optional[dict] = None
        self.identity: tuple = ()
        self.repeat = 0


_pending = _Pending()


def _body_directory() -> str:
    return os.path.join(os.path.dirname(LOG_PATH) or ".", BODY_DIRECTORY)


def _describe_body(body) -> Dict[str, object]:
    """A response body as either its text or a digest and a file beside the log.

    Returns the fields that go on the record. A body that could not be stored
    is described as one that could not be stored, because a record saying
    nothing about a body and a record saying there was none are different
    answers.
    """
    if body is None:
        return {}
    if isinstance(body, str):
        raw = body.encode("utf-8", "replace")
    elif isinstance(body, (bytes, bytearray)):
        raw = bytes(body)
    else:
        raw = str(body).encode("utf-8", "replace")
    found: Dict[str, object] = {"body_bytes": len(raw)}
    if not raw:
        return found
    text = raw.decode("utf-8", "replace")
    if len(text) <= INLINE_BODY_CHARS and text.isprintable():
        found["body"] = report.masked(text)
        return found
    if len(raw) <= INLINE_BODY_BYTES:
        # A short answer that is not text: a `machine:readmem` of one byte is
        # the byte, which is the answer the caller asked for and the thing an
        # investigation reads. A file per byte would cost more than it stores.
        found["body_hex"] = raw.hex()
        return found
    digest = hashlib.sha256(raw).hexdigest()[:DIGEST_CHARS]
    found["body_sha256"] = digest
    if digest in _written_bodies:
        return found
    path = os.path.join(_body_directory(), digest + ".bin")
    try:
        os.makedirs(_body_directory(), exist_ok=True)
        if not os.path.exists(path):
            # Written under a temporary name and moved, so a reader that finds
            # the file finds all of it: several suites drive one target at
            # once and can be writing the same digest at the same moment.
            temporary = f"{path}.{os.getpid()}"
            with open(temporary, "wb") as handle:
                handle.write(raw)
            os.replace(temporary, path)
        _written_bodies.add(digest)
    except OSError as exc:
        found["body_error"] = str(exc)[:TEXT_CHARS]
    return found


def record(transport: str, operation: str, **fields) -> None:
    """One interaction with a device. Never raises.

    `transport` is `rest`, `telnet` or `ftp`, and `operation` is what was done
    on it: a method and a path, a key, a command. Everything else is whatever
    that transport knows, and `body` is the device's answer, which this stores
    rather than the caller.
    """
    if not LOG_PATH:
        return
    try:
        _record(transport, operation, fields)
    except Exception:  # noqa: BLE001 - a log may never end a run
        pass


def _record(transport: str, operation: str, fields: Dict[str, object]) -> None:
    body = fields.pop("body", None)
    entry: Dict[str, object] = {"kind": "interaction", "transport": transport,
                                "op": operation}
    for name, value in fields.items():
        if value is None or value == "":
            continue
        if isinstance(value, (str, bytes, bytearray)):
            text = (value if isinstance(value, str)
                    else bytes(value).decode("utf-8", "replace"))
            entry[name] = report.masked(text[:TEXT_CHARS])
        else:
            entry[name] = value
    entry.update(_describe_body(body))

    now = time.time()
    check = report.current_check()
    scenario = report.current_scenario()
    # What makes two interactions the same one happening twice. The duration
    # is deliberately not in it: the same request answering in 12ms and then
    # in 14ms is the same interaction, and splitting on that would defeat the
    # collapse entirely.
    identity = (transport, operation, check, scenario,
                tuple(sorted((name, str(value)) for name, value in entry.items()
                             if name != "ms")))
    if _pending.record is not None and identity == _pending.identity:
        _pending.repeat += 1
        _pending.record["repeat"] = _pending.repeat
        _pending.record["until"] = now
        if "ms" in entry:
            _pending.record["ms_last"] = entry["ms"]
        return

    flush()
    entry["time"] = now
    entry["suite"] = report.SUITE_NAME
    if report.TARGET_NAME:
        entry["target"] = report.TARGET_NAME
    if report.ATTEMPT is not None:
        entry["attempt"] = report.ATTEMPT
    if check is not None:
        entry["check"] = check
    if scenario:
        entry["scenario"] = scenario
    _pending.record = entry
    _pending.identity = identity
    _pending.repeat = 1


def flush() -> None:
    """Write the held record, if there is one. Never raises.

    Called before a different interaction is held, and at process exit, so the
    last interaction of a suite reaches the file even when the suite ends
    immediately after it.
    """
    entry, _pending.record = _pending.record, None
    _pending.identity = ()
    _pending.repeat = 0
    if entry is None or not LOG_PATH:
        return
    try:
        line = json.dumps(entry, default=repr)
        with open(LOG_PATH, "a", encoding="utf-8") as handle:
            handle.write(line + "\n")
    except (OSError, TypeError, ValueError):
        # A log that cannot be written is not a reason to end a run, and the
        # report says the file is short rather than that the run went quiet.
        pass


atexit.register(flush)
