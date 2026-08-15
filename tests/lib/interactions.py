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
import threading
import time
from typing import Dict, List, Optional

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

# How long an interaction may be in flight before it is written down as
# started rather than waited for. A completion-only log shows nothing for
# exactly as long as something is stuck, which is when a reader most needs it,
# and a harness that dies mid-call otherwise leaves no trace of the call at
# all. One second is above every ordinary device call on every target here and
# far below any timeout.
START_RECORD_SECONDS = 1.0

# How often the watcher looks for interactions that have passed that. Four
# times the threshold's resolution, which is enough for a reader watching a
# ticker and cheap enough to leave running in every suite process.
WATCH_INTERVAL_SECONDS = 0.25

# The digest is truncated because it is an index into one run's own directory
# rather than a security claim: 16 hex characters is 64 bits, and a run makes
# tens of thousands of interactions.
DIGEST_CHARS = 16

# The one-line-per-interaction form, beside the JSONL and sharing its sequence
# numbers. A reader who wants to see what happened reads this; a program that
# wants a field reads the JSONL line with the same `seq`. Neither is derived
# from the other after the fact: both are written from the same record, so they
# cannot disagree.
TRANSCRIPT_NAME = "transcript.txt"

# How wide one field may be on a transcript line. A line a person scans is one
# that fits a terminal; the JSONL record with the same `seq` is where the whole
# of a long field is, and a long one is content-addressed on top of that.
TRANSCRIPT_FIELD_CHARS = 96

# What a connection-level failure was, from the exception the transport raised.
# A key that never reached the device because the connection was refused and a
# key the device ignored are different findings, and `error` alone is a string
# that has to be parsed to tell them apart.
FAULTS = (
    (ConnectionRefusedError, "refused"),
    (ConnectionResetError, "reset"),
    (BrokenPipeError, "broken-pipe"),
    (TimeoutError, "timeout"),
)


# Digests this process has already written to the bodies directory, so one
# body costs one write however many interactions carry it.
_written_bodies: set = set()

# The sequence number the next record gets. One counter per destination, so a
# target's file numbers from one and a reader joining the transcript to the
# JSONL matches on (suite, attempt, seq).
_next_seq = [1]

# What the harness last saw of the device, carried onto every record so that a
# key injection says what was on screen when it was sent. Both are set by the
# transports rather than by a caller: `menu_open` from what
# `machine:menu_screen` answered, and `screen` from the screen spool.
_state = {"menu_open": None, "screen": ""}


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
    # A new destination is a new bodies directory and a new sequence, so what
    # this process has already written is nothing to do with the new one.
    _written_bodies.clear()
    _next_seq[0] = 1


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


def note_menu(open_now: Optional[bool]) -> None:
    """Say whether the device's overlay menu is open, as the harness last saw it.

    Written onto every record after it, because the answer to "was this key
    swallowed" is usually "the menu was open". A C64 Ultimate with its menu
    open accepts an injected key with HTTP 200 and does nothing with it, so an
    injection record that carries only its own status cannot tell that case
    from a key the machine ignored.

    `None` means nobody has looked, which is different from either answer.
    """
    _state["menu_open"] = open_now


def note_screen(identity: object) -> None:
    """Say what the harness is looking at, as an identity rather than a screen.

    A digest, not the text: the screen itself is in the spool, and repeating it
    on every interaction would be the same 2000 bytes over and over. Two
    consecutive records carrying different values is the observable effect of
    whatever happened between them, which for an injected key is the point.

    A record carries what the harness had last seen when it was written, so on
    the record of a screen read it is that read's own answer.
    """
    if identity is None:
        _state["screen"] = ""
        return
    if isinstance(identity, (bytes, bytearray)):
        raw = bytes(identity)
    else:
        raw = (identity if isinstance(identity, str)
               else repr(identity)).encode("utf-8", "replace")
    _state["screen"] = hashlib.sha256(raw).hexdigest()[:DIGEST_CHARS]


def fault_of(exc: Optional[BaseException]) -> str:
    """Which connection-level failure this is, as one word, or ""."""
    if exc is None:
        return ""
    reason = getattr(exc, "reason", None)
    for candidate in (exc, reason):
        if candidate is None:
            continue
        for kind, name in FAULTS:
            if isinstance(candidate, kind):
                return name
    if isinstance(exc, OSError) or isinstance(reason, OSError):
        return "unreachable"
    return "other"


class Call:
    """One interaction that has been issued and has not answered yet."""

    __slots__ = ("transport", "operation", "fields", "started", "announced")

    def __init__(self, transport: str, operation: str, fields: dict) -> None:
        self.transport = transport
        self.operation = operation
        self.fields = fields
        self.started = time.monotonic()
        self.announced = False


_in_flight: List[Call] = []
_in_flight_lock = threading.Lock()
_watcher: Optional[threading.Thread] = None


def begin(transport: str, operation: str, **fields) -> Optional[Call]:
    """Say that an interaction has been issued. Never raises.

    Returns a handle to pass to `finish`. A call that has not answered within
    `START_RECORD_SECONDS` is written down as started, so a hang and a harness
    that died mid-call both leave evidence, and a reader watching the run sees
    the line the moment it matters rather than when it is over.
    """
    if not LOG_PATH:
        return None
    try:
        call = Call(transport, operation, fields)
        with _in_flight_lock:
            _in_flight.append(call)
        _start_watcher()
        return call
    except Exception:  # noqa: BLE001 - a log may never end a run
        return None


def finish(call: Optional[Call], **fields) -> None:
    """Say that an issued interaction has answered. Never raises."""
    if call is None:
        return record_ended(None, **fields)
    try:
        with _in_flight_lock:
            if call in _in_flight:
                _in_flight.remove(call)
        merged = dict(call.fields)
        merged.update(fields)
        if call.announced:
            # Its start is already on record, so the completion says which
            # start it closes rather than reading as a second interaction.
            merged["phase"] = "end"
        record(call.transport, call.operation, **merged)
    except Exception:  # noqa: BLE001
        pass


def record_ended(_call, **fields) -> None:
    return None


def in_flight() -> List[dict]:
    """Every interaction issued and not yet answered, oldest first.

    For a caller showing what the run is doing now rather than what it has
    done: a ticker that only shows completions shows nothing for exactly as
    long as something is stuck.
    """
    now = time.monotonic()
    with _in_flight_lock:
        calls = list(_in_flight)
    return [{"transport": call.transport, "op": call.operation,
             "seconds": now - call.started, **call.fields} for call in calls]


def _start_watcher() -> None:
    global _watcher
    if _watcher is not None:
        return
    _watcher = threading.Thread(target=_watch, name="interactions",
                                daemon=True)
    _watcher.start()


def _watch() -> None:
    while True:
        time.sleep(WATCH_INTERVAL_SECONDS)
        try:
            now = time.monotonic()
            with _in_flight_lock:
                late = [call for call in _in_flight
                        if not call.announced
                        and now - call.started >= START_RECORD_SECONDS]
                for call in late:
                    call.announced = True
            for call in late:
                record(call.transport, call.operation, phase="start",
                       **call.fields)
        except Exception:  # noqa: BLE001 - a log may never end a run
            pass


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
            text = report.masked(text)
            if len(text) > TEXT_CHARS:
                # Kept whole rather than cut. A `machine:writemem` carries its
                # address and its bytes here, and a partial write is exactly
                # what a truncated record cannot show, so a long field is
                # content-addressed the way a body is.
                entry[name] = text[:TEXT_CHARS]
                entry.update({f"{name}_bytes": len(text)})
                stored = _describe_body(text)
                if "body_sha256" in stored:
                    entry[f"{name}_sha256"] = stored["body_sha256"]
            else:
                entry[name] = text
        else:
            entry[name] = value
    entry.update(_describe_body(body))
    if _state["menu_open"] is not None:
        entry["menu_open"] = bool(_state["menu_open"])
    if _state["screen"]:
        entry["screen"] = _state["screen"]

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
    entry["seq"] = _next_seq[0]
    _next_seq[0] += 1
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
    try:
        with open(transcript_path(), "a", encoding="utf-8") as handle:
            handle.write(transcript_line(entry) + "\n")
    except (OSError, TypeError, ValueError):
        pass


def transcript_path() -> str:
    return os.path.join(os.path.dirname(LOG_PATH) or ".", TRANSCRIPT_NAME)


def transcript_line(entry: Dict[str, object]) -> str:
    """One interaction as one line, for somebody reading rather than parsing.

    The same `seq` as the JSONL record it was written from, so a reader who
    finds a line here and wants every field of it looks that number up. Fields
    are cut to a width a person can scan, and the JSONL record beside it is
    where the whole of one is.
    """

    def short(value: object) -> str:
        text = str(value)
        return text if len(text) <= TRANSCRIPT_FIELD_CHARS else (
            text[:TRANSCRIPT_FIELD_CHARS] + ">")

    when = time.strftime("%H:%M:%S", time.localtime(
        float(entry.get("time") or 0.0)))
    parts = [f"{entry.get('seq', 0):>6}", when,
             f"{str(entry.get('transport', '')):<7}",
             str(entry.get("op", ""))]
    for name, prefix in (("status", "-> "), ("fault", "fault="),
                         ("params", "")):
        value = entry.get(name)
        if value not in (None, ""):
            parts.append(f"{prefix}{short(value)}")
    if entry.get("repeat"):
        parts.append(f"x{entry['repeat']}")
    if "ms" in entry:
        parts.append(f"{entry['ms']}ms")
    if "menu_open" in entry:
        parts.append("menu=open" if entry["menu_open"] else "menu=closed")
    if entry.get("screen"):
        parts.append(f"screen={entry['screen']}")
    for name in ("body", "body_hex", "body_sha256", "reply", "error"):
        if entry.get(name):
            parts.append(f"{name}={short(entry[name])}")
            break
    return " ".join(str(part) for part in parts)


atexit.register(flush)
