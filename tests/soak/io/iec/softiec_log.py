"""The device-log side of the Software IEC soak and stress suite.

The Software IEC drive writes one syslog line for a command that leaves an error, for an
open that fails and for the first failure of a channel, and, with the setting "Log Every
Operation" on, one more for every other command, open and data-channel close, with the
reply of a command that answers data and the host file an open reached. The format is
built by IecChannel::log_line in software/io/iec/iec_channel.cc and the text of each field
is rendered by softiec_log_text in software/io/iec/iec_log.h.

This module reproduces that rendering in Python, parses the lines back into fields, and
correlates a phase's lines with what the C64 lane (and the UCI target) actually did. It is
kept separate from the suite so it can be exercised on its own with synthetic lines, which
the suite's dry run does before any device is touched.

What is designed around, from the firmware:

  * A command is executed when the bus write to channel 15 ends. It always logs
    "command failed" when the drive failed (error code >= 20 and not 73, the check in
    IecChannel::drive_failed), and otherwise logs "command" only when logging is on. So the
    two-digit status the C64 reads next decides which line to expect and whether it appears
    with logging off.
  * An open on a data secondary logs "open" or "open failed" the same way, with chan the
    secondary address and txt the name bytes. A data-channel close logs "close" only with
    logging on. Reads and data writes log nothing. Closing channel 15 logs nothing.
  * "listing end" is logged when a listing is read to its end, and "channel fault" once
    after a channel fails. Both are treated as optional here, never as required.
  * The line's "-> status" is the error channel at the time of logging, which is what the
    C64 reads next for a command or an open, so the two-digit code is comparable. A command
    that answers data carries the first bytes of the reply behind a "reply" label.

Losses this correlation accounts for rather than hides:

  * printf is character by character and not atomic across tasks, so another task's message
    can split a "SoftIEC:" line; the part after the split arrives in a later datagram. A
    fragment that carries the prefix but does not parse is counted as a split, and a missing
    expected line is only a failure when the number missing exceeds the splits seen.
  * The syslog task throttles to about 200 lines a second and drops on a 16 KB overflow, and
    UDP can lose a datagram. These show as missing lines, bounded by the same rule.
"""

from __future__ import annotations

import os
import re
import socket
import threading
import time
from dataclasses import dataclass, field, replace

# The prefix every line carries (SOFTIEC_LOG_PREFIX in iec_log.h). Grep key in a device log.
LOG_PREFIX = "SoftIEC: "

# Room the firmware gives softiec_log_text for a rendered field: SOFTIEC_LOG_TEXT_SIZE,
# which is 4 * SOFTIEC_LOG_MAX_BYTES + 4. The renderer fills up to this many characters and
# then cuts with "..", so a long payload is cut by character count, not by byte count.
LOG_TEXT_SIZE = 4 * 64 + 4

# The line one external collector writes, one datagram per line: the source address, the
# time it arrived as HH:MM:SS, and the line text with its own spaces kept. The suite's
# --syslog-spool option reads exactly this, so the format is a documented constant the
# collector and the suite share.
SPOOL_FORMAT = "<source ip> <HH:MM:SS> <text>"

# The largest step between the sequence numbers of two lines in a row that still counts as
# lines lost between them; a longer step is a number made unreadable by another task's text.
MAX_SEQUENCE_STEP = 500

# The fewest characters another task's message puts into a field. The shortest seen is a
# socket number and address, over thirty; renderings of different bytes differ by far less.
MIN_INTERLEAVED = 8

# The sequence number a line ends with, also found at the end of the second half of a split line.
_SEQUENCE_TAIL = re.compile(r" #(\d+)$")

# How far from the diagonal the alignment of events and lines looks, on top of the difference
# in their counts. Wide enough for the lines a phase loses in a burst, bounded so the table
# stays linear in the length of the phase.
ALIGN_BAND = 400

_HEX = "0123456789ABCDEF"

# The head of a well-formed line, up to the first rendered field. "what" carries spaces
# ("command failed", "open failed"), so it is captured lazily up to " dev=".
_HEAD = re.compile(r"^(?P<what>.+?) dev=(?P<dev>-?\d+) chan=(?P<chan>-?\d+) "
                   r"part=(?P<part>-?\d+) dir=")


def render_text(data: bytes, out_size: int = LOG_TEXT_SIZE) -> str:
    """Render bytes the way softiec_log_text does: a printable ASCII byte stands for itself,
    a zero, line feed, carriage return, quote and backslash get short escapes, every other
    byte becomes \\xNN with uppercase hex, and a rendering that fills the buffer ends in
    "..". Reproduced character for character so a rendered field matches the device's."""
    out: list[str] = []
    written = 0
    if out_size < 1:
        return ""
    for b in data:
        if b == 0x00:
            esc = "\\0"
        elif b == 0x0A:
            esc = "\\n"
        elif b == 0x0D:
            esc = "\\r"
        elif b == 0x22:
            esc = "\\\""
        elif b == 0x5C:
            esc = "\\\\"
        elif 0x20 <= b < 0x7F:
            esc = chr(b)
        else:
            esc = "\\x" + _HEX[(b >> 4) & 15] + _HEX[b & 15]
        n = len(esc)
        if (written + n) >= (out_size - 2):
            if (written + 2) < out_size:
                out.append("..")
            break
        out.append(esc)
        written += n
    return "".join(out)


# ---------------------------------------------------------------------------
# Expected events, recorded by the suite as it drives the bus
# ---------------------------------------------------------------------------

# What the C64 lane did, in the order it did it. `status` is the two-digit code the C64 read
# from the error channel next, which is also the line's "-> status", and it decides the
# expected line: a failure (code >= 20 and not 73) always logs, anything else logs only with
# logging on. `reply` is the bytes a command that answers data returned, compared against the
# line's "reply" field when logging is on.
_FAILURE_MIN = 20
_NOT_FAILURE = 73  # ERR_DOS, the answer to UI and the resets, is not a failure


@dataclass
class Event:
    op: str                      # "command", "open", "close" or "marker"
    chan: int                    # the channel the line carries: 15 for a command, the
    #                              secondary address for an open or a close
    txt: bytes                   # the command or name bytes as the C64 sent them
    status: int | None = None    # the two-digit code the C64 read next, or None if unread
    reply: bytes | None = None   # a command's data reply, or None
    dev: int | None = 11         # the drive's bus id, or None to skip comparing it
    # When the bus operation started and finished, on the suite's own clock. Used only in
    # toggle mode, to decide which log setting was in force while it ran.
    t0: float | None = None
    t1: float | None = None
    # A line that may or may not appear: a recovery close of a channel that may not have been
    # open, or a UCI operation whose channel state is not tracked. Never required, but a line
    # that does appear is still verified.
    optional: bool = False

    @property
    def failure(self) -> bool:
        """Whether the drive failed, so the line is written whatever the log setting is."""
        return (self.status is not None) and (self.status >= _FAILURE_MIN) \
            and (self.status != _NOT_FAILURE)

    @property
    def what(self) -> str:
        """The `what` field this event should produce."""
        if self.op == "close":
            return "close"
        if self.op == "open":
            return "open failed" if self.failure else "open"
        return "command failed" if self.failure else "command"

    def always_logged(self) -> bool:
        """A failed command or open logs whatever the setting is; a marker is a failed
        command; a close and a successful command or open log only with logging on."""
        return self.op != "close" and self.failure

    @property
    def cls(self) -> str:
        return "close" if self.op == "close" else ("open" if self.op == "open" else "cmd")

    @property
    def key(self) -> tuple[str, int, str]:
        # The channel is part of the key: the closes that I and UJ log for channels 0 to 14
        # carry no text, and without it one lost close would pair every later one with its
        # neighbour's line.
        return (self.cls, self.chan, render_text(self.txt))


def across_resets(events: list[Event], resets: list[tuple[float, float]]) -> list[Event]:
    """The events, with each one a drive reset overlapped made optional and its status unknown.

    `resets` holds each reset as (request sent, answer received) on the same clock as the
    events. A reset replaces the error channel, so the status the C64 read after it is not
    the answer the drive logged, and a failure line may or may not have been written before
    it. The line that does appear is still verified against the event's text and channel."""
    out = []
    for ev in events:
        if (ev.t0 is not None) and (ev.t1 is not None) and \
                any((sent <= ev.t1) and (answered >= ev.t0) for sent, answered in resets):
            ev = replace(ev, status=None, optional=True)
        out.append(ev)
    return out


@dataclass
class Flip:
    """One change of the "Log Every Operation" setting made while the bus was busy: when it
    was requested, when a read-back confirmed the new value, and the value it set."""
    requested: float
    confirmed: float
    on: bool


def steady_state_at(flips: list[Flip], initial_on: bool, t0: float, t1: float) -> str:
    """Whether the log setting was steady, and to what, for the whole span [t0, t1].

    Returns "on" or "off" when the span lies inside one steady window, and "overlap" when a
    flip's request-to-confirm window touches it, so a line for it can honestly go either way.
    """
    on = initial_on
    for flip in flips:
        # The setting is unsettled from the request until the read-back confirmed it.
        if (t0 <= flip.confirmed) and (t1 >= flip.requested):
            return "overlap"
        if t0 >= flip.confirmed:
            on = flip.on
    return "on" if on else "off"


# ---------------------------------------------------------------------------
# Parsed lines
# ---------------------------------------------------------------------------


@dataclass
class LogLine:
    what: str
    dev: int
    chan: int
    part: int
    dir: str
    length: int
    txt: str
    label: str | None
    label_text: str | None
    status_code: int | None
    error: str
    raw: str
    # The sequence number the drive ends every line with (" #N"), or None for a firmware that
    # does not write one.
    seq: int | None = None

    @property
    def cls(self) -> str:
        if self.what.startswith("command"):
            return "cmd"
        if self.what.startswith("open"):
            return "open"
        if self.what == "close":
            return "close"
        if self.what == "listing end":
            return "listing"
        if self.what.startswith("channel fault"):
            return "fault"
        return "other"

    @property
    def optional(self) -> bool:
        # A marker line that pairs with no operation is one of the unrecorded outer markers.
        return (self.cls in ("listing", "fault", "other")) or self.txt.startswith(MARKER_PREFIX_TEXT)

    @property
    def success(self) -> bool:
        """A line whose `what` names an operation that succeeded, so it is not written with
        logging off; seeing one there is a defect in the setting, not a lost line."""
        return self.what in ("command", "open", "close")

    @property
    def key(self) -> tuple[str, int, str]:
        return (self.cls, self.chan, self.txt)


def _field(text: str, start: int) -> tuple[str | None, int]:
    """The rendered field that starts at the opening quote at `start`, and the index after
    its closing quote. A rendered field never carries an unescaped quote, so the first one
    not preceded by a backslash closes it. Returns (None, len) when it is not terminated,
    which marks the line as a split fragment."""
    if start >= len(text) or text[start] != '"':
        return None, start
    i = start + 1
    out: list[str] = []
    while i < len(text):
        c = text[i]
        if c == "\\" and (i + 1) < len(text):
            out.append(c)
            out.append(text[i + 1])
            i += 2
            continue
        if c == '"':
            return "".join(out), i + 1
        out.append(c)
        i += 1
    return None, i


def parse_line(text: str) -> LogLine | None:
    """One SoftIEC line to a LogLine, or None when the text carries the prefix but does not
    parse (a split fragment) or does not carry the prefix at all (a foreign line). A caller
    tells the two apart with `carries_prefix`."""
    at = text.find(LOG_PREFIX)
    if at < 0:
        return None
    body = text[at + len(LOG_PREFIX):]
    head = _HEAD.match(body)
    if not head:
        return None
    rest = body[head.end():]
    dir_text, i = _field(rest, 0)
    if dir_text is None:
        return None
    m = re.match(r" len=(\d+) txt=", rest[i:])
    if not m:
        return None
    i += m.end()
    txt, i = _field(rest, i)
    if txt is None:
        return None
    label = None
    label_text = None
    tail = rest[i:]
    lab = re.match(r" (\w+)=", tail)
    if lab:
        label = lab.group(1)
        label_text, j = _field(rest, i + lab.end())
        if label_text is None:
            return None
        tail = rest[j:]
    if not tail.startswith(" -> "):
        return None
    error = tail[4:]
    seq = None
    numbered = re.search(r" #(\d+)$", error)
    if numbered:
        seq = int(numbered.group(1))
        error = error[:numbered.start()]
    code = None
    if len(error) >= 2 and error[:2].isdigit():
        code = int(error[:2])
    return LogLine(what=head.group("what"), dev=int(head.group("dev")),
                   chan=int(head.group("chan")), part=int(head.group("part")),
                   dir=dir_text, length=int(m.group(1)), txt=txt, label=label,
                   label_text=label_text, status_code=code, error=error, raw=text, seq=seq)


def carries_prefix(text: str) -> bool:
    return LOG_PREFIX in text


# ---------------------------------------------------------------------------
# Correlation
# ---------------------------------------------------------------------------


@dataclass
class Correlation:
    label: str
    matched: int = 0
    optional: int = 0
    splits: int = 0
    missing: list[str] = field(default_factory=list)
    missing_failures: int = 0
    unexpected: list[str] = field(default_factory=list)
    # A well-formed non-optional line that matched no operation. A printf split leaves a
    # fragment that does not parse, so a line that parses and still matches nothing is a real
    # line the drive should not have written: a fault, not a lost line. Kept apart from the
    # split allowance, which forgives missing lines only.
    unexpected_bad: list[str] = field(default_factory=list)
    contradictions: list[str] = field(default_factory=list)
    logging_off_success: list[str] = field(default_factory=list)
    parsed: int = 0
    # From the sequence numbers: lines delivered twice (the same number and text, dropped
    # before aligning), and numbers missing between the first and the last line of the window,
    # each a line lost on the way or split so it did not parse.
    duplicates: int = 0
    gaps: int = 0
    unreadable_numbers: int = 0
    # Numbers seen only at the end of a split line: the line arrived but did not parse.
    split_numbers: int = 0
    # Lines another task's message was printed into, paired with their operation by kind,
    # channel and byte count.
    interleaved: int = 0
    numbered: bool = False
    # In toggle mode: how many events were checked strictly in each state, and how many fell
    # in a flip window and so had their success line treated as optional.
    strict_on: int = 0
    strict_off: int = 0
    overlap: int = 0

    @property
    def allowance(self) -> int:
        """How many expected lines may be missing: with sequence numbers, the numbers absent from
        the window and the numbers whose line arrived split; without them, the split fragments
        seen."""
        # A line whose number could not be read did arrive, and it spans one of the numbers
        # counted as missing, so each one lowers the allowance by one.
        return max(0, self.gaps + self.split_numbers - self.unreadable_numbers) \
            if self.numbered else self.splits

    @property
    def unexplained_missing(self) -> int:
        """Missing expected lines the lost or split lines cannot account for."""
        return max(0, len(self.missing) - self.allowance)

    def problems(self) -> list[str]:
        out: list[str] = []
        out += self.contradictions
        out += self.logging_off_success
        out += self.unexpected_bad
        if self.unexplained_missing > 0:
            out.append(f"{len(self.missing)} expected line(s) missing with only "
                       f"{self.allowance} lost or split line(s) to account for them; first: "
                       + "; ".join(self.missing[:5]))
        return out

    def ok(self) -> bool:
        return not self.problems()

    def summary(self) -> str:
        toggle = ""
        if self.strict_on or self.strict_off or self.overlap:
            toggle = (f", strict on {self.strict_on}, strict off {self.strict_off}, "
                      f"in a flip window {self.overlap}")
        return (f"{self.label}: {self.parsed} lines parsed, {self.matched} matched, "
                f"{len(self.missing)} missing ({self.missing_failures} of them failure "
                f"lines), {len(self.unexpected)} unexpected ({len(self.unexpected_bad)} "
                f"well-formed), {self.optional} optional, {self.splits} split, "
                f"{self.duplicates} delivered twice, {self.gaps} sequence gaps, "
                f"{self.unreadable_numbers} unreadable numbers, {self.split_numbers} numbers only in split lines, "
                f"{self.interleaved} interleaved{toggle}")


def _contradictions(ev: Event, ln: LogLine) -> list[str]:
    out = []
    if ev.chan != ln.chan:
        out.append(f"chan {ln.chan}, expected {ev.chan}")
    if (ev.dev is not None) and (ev.dev != ln.dev):
        out.append(f"dev {ln.dev}, expected {ev.dev}")
    # The line carries the real byte count in its len field even when the rendering is cut,
    # so two payloads with the same rendered prefix but a different length are told apart.
    if ln.length != len(ev.txt):
        out.append(f"len {ln.length}, expected {len(ev.txt)}")
    if ev.op == "close" and ln.txt != "":
        out.append(f"close carried txt {ln.txt!r}, expected none")
    if (ev.status is not None) and (ln.status_code is not None) and (ev.status != ln.status_code):
        out.append(f"status {ln.status_code}, expected {ev.status}")
    if (ev.reply is not None) and (ln.label == "reply"):
        want = render_text(ev.reply)
        if want != ln.label_text:
            out.append(f"reply {ln.label_text!r}, expected {want!r}")
    return out


def _quote(ev: Event | None = None, ln: LogLine | None = None) -> str:
    parts = []
    if ev is not None:
        parts.append(f"expected {ev.what} chan={ev.chan} txt={render_text(ev.txt)!r}"
                     + (f" status={ev.status}" if ev.status is not None else ""))
    if ln is not None:
        parts.append(f"line {ln.raw!r}")
    return " | ".join(parts)


def correlate(events: list[Event], texts: list[str], logging_on: bool,
              label: str | None = None) -> Correlation:
    """A phase whose log setting was steady the whole time. `state_of` says every event was
    in "on" or "off"; the unaligned-success-line rule applies, because with the setting off
    no success line should appear at all."""
    state = "on" if logging_on else "off"
    return _correlate(events, texts, lambda _ev: state,
                      label=label or f"logging {state}", steady_state=state)


def correlate_toggle(events: list[Event], texts: list[str], flips: list[Flip],
                     initial_on: bool, label: str | None = None) -> Correlation:
    """A phase whose log setting was flipped while the bus ran. Each event is checked against
    the state in force for the whole time it ran; an event that overlapped a flip window has
    its success line treated as optional, but a line that is present still has to match. So a
    flip that silently does not take effect shows up as a strict-state mismatch rather than
    being absorbed."""
    def state_of(ev: Event) -> str:
        if ev.t0 is None or ev.t1 is None:
            return "overlap"
        return steady_state_at(flips, initial_on, ev.t0, ev.t1)
    return _correlate(events, texts, state_of, label=label or "logging toggled")


def _deduplicate(lines: list[LogLine], result: Correlation,
                 arrivals: list[tuple[int, bool]]) -> list[LogLine]:
    """Drops a line delivered twice, counts the sequence numbers missing from the window and
    the numbers seen only at the end of a split line, and reports two different lines that
    carry the same number. Lines without a number (an older firmware) are passed through and
    the split count stays the allowance."""
    numbers = [ln.seq for ln in lines if ln.seq is not None]
    if not numbers:
        return lines
    result.numbered = True
    kept: list[LogLine] = []
    seen: dict[int, LogLine] = {}
    for ln in lines:
        if ln.seq is None:
            kept.append(ln)
            continue
        first = seen.get(ln.seq)
        if first is None:
            seen[ln.seq] = ln
            kept.append(ln)
        elif _body(first.raw) == _body(ln.raw):
            result.duplicates += 1
        else:
            result.contradictions.append(
                f"two different lines carry sequence number {ln.seq}: {first.raw!r} and {ln.raw!r}")
    # Gaps are counted between numbers that step forward in the order the lines arrived. A
    # number another task's message was printed into can be far off (#4415 read as #44152),
    # so a step backwards or of more than MAX_SEQUENCE_STEP is not a gap: that number is
    # counted as unreadable and skipped.
    parsed_numbers = set(seen)
    counted: set[int] = set()
    previous = None
    for seq, _parsed in arrivals:
        if seq in counted:
            continue  # the same number again: a line delivered twice
        if previous is None:
            previous = seq
        elif previous < seq <= previous + MAX_SEQUENCE_STEP:
            result.gaps += seq - previous - 1
            previous = seq
        else:
            result.unreadable_numbers += 1
            continue
        counted.add(seq)
        if seq not in parsed_numbers:
            result.split_numbers += 1
    return kept


def _align(events: list[Event], lines: list[LogLine], pairs) -> list[tuple[str, Event | None, LogLine | None]]:
    """The events and the lines in one order-keeping alignment with the most pairs, a pair
    with an operation that has to write its line counting twice one with an optional one, as a
    list of ("pair", event, line), ("event", event, None) and ("line", None, line).

    A longest common subsequence rather than a greedy walk: the same command, open or close
    recurs every few iterations, and with many lines lost a greedy walk can pair a line with a
    later copy of its operation and throw every pair after it out. The table is computed in a
    band around the diagonal, ALIGN_BAND cells to each side, which the losses of a phase stay
    well within, so its cost is linear in the length of the phase."""
    n, m = len(events), len(lines)
    if n == 0 or m == 0:
        return [("event", ev, None) for ev in events] + [("line", None, ln) for ln in lines]
    band = ALIGN_BAND + abs(n - m)
    width = m + 1
    # The exact pairing by key is checked first; the slower interleaving test runs only for a
    # line of the same kind and channel whose text is longer than the event's rendering.
    event_keys = [ev.key for ev in events]
    line_keys = [ln.key for ln in lines]
    score = [0] * ((n + 1) * width)
    move = bytearray((n + 1) * width)  # 1 pair, 2 skip an event, 3 skip a line
    for j in range(1, m + 1):
        move[j] = 3
    for i in range(1, n + 1):
        ev = events[i - 1]
        centre = (i * m) // n
        low = max(1, centre - band)
        high = min(m, centre + band)
        row = i * width
        above = row - width
        score[row + low - 1] = score[above + low - 1] if low > 1 else 0
        move[row] = 2
        if low > 1:
            move[row + low - 1] = 2
        # A line is worth more to an operation that has to write one than to an optional
        # one with the same text: the drive reset during an optional command's status read,
        # and the same command sent again, must not leave the second without its line.
        gain = 1 if ev.optional else 2
        for j in range(low, high + 1):
            best = score[above + j]
            step = 2
            if score[row + j - 1] > best:
                best = score[row + j - 1]
                step = 3
            if (score[above + j - 1] + gain > best) and (
                    event_keys[i - 1] == line_keys[j - 1]
                    or ((event_keys[i - 1][:2] == line_keys[j - 1][:2])
                        and (len(line_keys[j - 1][2]) >= len(event_keys[i - 1][2]) + MIN_INTERLEAVED)
                        and pairs(ev, lines[j - 1]))):
                best = score[above + j - 1] + gain
                step = 1
            score[row + j] = best
            move[row + j] = step
        for j in range(high + 1, m + 1):
            score[row + j] = score[row + j - 1]
            move[row + j] = 3
    out: list[tuple[str, Event | None, LogLine | None]] = []
    i, j = n, m
    while i > 0 or j > 0:
        step = move[i * width + j] if (i > 0 and j > 0) else (2 if i > 0 else 3)
        if step == 1:
            out.append(("pair", events[i - 1], lines[j - 1]))
            i -= 1
            j -= 1
        elif step == 2:
            out.append(("event", events[i - 1], None))
            i -= 1
        else:
            out.append(("line", None, lines[j - 1]))
            j -= 1
    out.reverse()
    return out


def _interleaved(want: str, got: str) -> bool:
    """Whether `got` is `want` with another task's text, at least MIN_INTERLEAVED characters,
    inserted at one place."""
    if len(got) < len(want) + MIN_INTERLEAVED:
        return False
    prefix = 0
    while (prefix < len(want)) and (want[prefix] == got[prefix]):
        prefix += 1
    suffix = 0
    while (suffix < len(want) - prefix) and (want[len(want) - 1 - suffix] == got[len(got) - 1 - suffix]):
        suffix += 1
    return prefix + suffix == len(want)


def _body(text: str) -> str:
    """A line from its prefix on, so the same line in two datagrams compares equal."""
    at = text.find(LOG_PREFIX)
    return text[at:] if at >= 0 else text


def _correlate(events, texts, state_of, label, steady_state=None):
    """The shared engine. `state_of(ev)` returns "on", "off" or "overlap" for one event, and
    decides whether its line is required, forbidden or optional. An aligned pair is verified
    field by field whatever the state, so a wrong chan, dev, status or reply always fails."""
    result = Correlation(label=label)
    lines: list[LogLine] = []
    arrivals: list[tuple[int, bool]] = []  # (sequence number, whether its line parsed)
    for text in texts:
        parsed = parse_line(text)
        if parsed is not None:
            lines.append(parsed)
            if parsed.seq is not None:
                arrivals.append((parsed.seq, True))
            continue
        if carries_prefix(text):
            result.splits += 1
        # The end of a split line arrives on its own and still ends in its sequence number.
        tail = _SEQUENCE_TAIL.search(text)
        if tail:
            arrivals.append((int(tail.group(1)), False))
    lines = _deduplicate(lines, result, arrivals)
    result.parsed = len(lines)

    def expected(ev: Event) -> bool:
        """Whether a line is required for this event: always for a failure, for every event
        in an on window, and never for an optional event or in an off or a flip window for a
        success."""
        if ev.optional:
            return False
        return ev.always_logged() or (state_of(ev) == "on")

    def count_state(ev: Event) -> None:
        state = state_of(ev)
        if state == "on":
            result.strict_on += 1
        elif state == "off":
            result.strict_off += 1
        else:
            result.overlap += 1

    def record_missing(ev: Event) -> None:
        if not expected(ev):
            return  # a silent success in an off or a flip window is correct, not missing
        result.missing.append(_quote(ev=ev))
        if ev.always_logged():
            result.missing_failures += 1

    def record_unexpected(ln: LogLine) -> None:
        result.unexpected.append(_quote(ln=ln))
        if (steady_state == "off") and ln.success:
            result.logging_off_success.append(
                f"a success line appeared with logging off: {ln.raw!r}")
        if not ln.optional:
            # A well-formed command, open or close line that matched no operation is a fault
            # whatever the log setting; a split fragment does not parse and never reaches here.
            result.unexpected_bad.append(
                f"a well-formed {ln.what} line matched no operation: {ln.raw!r}")

    # An event that cannot write a line in its state, a success while logging is strictly
    # off, is left out of the alignment: with logging off, hundreds of silent successes can
    # stand between two failure lines, more than the lookahead spans. A success line that does
    # appear with logging off then matches no event, which fails as an unexpected line.
    silent = [ev for ev in events
              if (not ev.optional) and (not ev.always_logged()) and (state_of(ev) == "off")]
    for ev in silent:
        count_state(ev)
    events = [ev for ev in events
              if ev.optional or ev.always_logged() or (state_of(ev) != "off")]

    def judge(ev: Event, ln: LogLine) -> None:
        count_state(ev)
        state = state_of(ev)
        if (not ev.optional) and (state == "off") and ln.success and not ev.always_logged():
            # A success line matched an event whose window was strictly off: the setting did
            # not take effect. In a flip window this would be allowed, which is why the
            # window classification is by the event's own run time, not the line's.
            result.logging_off_success.append(
                f"a success line appeared while logging was off: {ln.raw!r}")
            return
        problems = _contradictions(ev, ln)
        if problems:
            result.contradictions.append(_quote(ev=ev, ln=ln) + " -> " + "; ".join(problems))
        else:
            result.matched += 1

    def pairs(ev: Event, ln: LogLine) -> bool:
        """Whether a line is this event's line: the same kind, channel and text, or the same
        kind, channel and byte count with another task's message printed into the middle of
        the text (printf is not atomic, and a message without its newline in the right place
        leaves the line well-formed)."""
        if ev.key == ln.key:
            return True
        # Another task's message is a whole line of text, so a short difference is not one: the
        # renderings of two different binary parameters (\xA0 against \0) differ by as little
        # as two characters. The status has to agree as well.
        status_agrees = (ev.status is None) or (ln.status_code is None) or (ev.status == ln.status_code)
        return (ev.cls == ln.cls) and (ev.chan == ln.chan) and (ln.length == len(ev.txt)) \
            and status_agrees and _interleaved(render_text(ev.txt), ln.txt)

    for kind, ev, ln in _align(events, lines, pairs):
        if kind == "pair":
            if ev.key != ln.key:
                result.interleaved += 1
            judge(ev, ln)
        elif kind == "event":
            record_missing(ev)
        elif ln.optional:
            result.optional += 1
        else:
            record_unexpected(ln)
    return result


# ---------------------------------------------------------------------------
# The device log, read from a spool file or off the wire
# ---------------------------------------------------------------------------


def parse_spool_line(line: str) -> tuple[str, str] | None:
    """One SPOOL_FORMAT line to (source ip, text), or None when it is not that shape."""
    stripped = line.rstrip("\r\n")
    parts = stripped.split(" ", 2)
    if len(parts) < 3:
        return None
    ip, when, text = parts
    if not re.match(r"^\d{1,3}(\.\d{1,3}){3}$", ip):
        return None
    if not re.match(r"^\d\d:\d\d:\d\d$", when):
        return None
    return ip, text


def read_spool(path: str, offset: int = 0) -> list[tuple[str, str]]:
    """Every (source ip, text) the collector has written from byte `offset` on. A missing file
    is empty. A spool shared by long runs grows to tens of megabytes, so a phase reads only
    what was written since it started."""
    try:
        with open(path, "rb") as raw:
            raw.seek(offset)
            data = raw.read()
    except OSError:
        return []
    out: list[tuple[str, str]] = []
    for line in data.decode("utf-8", errors="replace").splitlines():
        entry = parse_spool_line(line)
        if entry is not None:
            out.append(entry)
    return out


MARKER_PREFIX = b"XSOAKLOG"
MARKER_PREFIX_TEXT = MARKER_PREFIX.decode("ascii")


def marker_text(nonce: str) -> bytes:
    """The bytes of a marker command. X is a command letter, so X followed by anything but
    PWD answers 30 and always logs a "command failed" line, whatever the log setting, which
    is what makes a marker visible with logging off. The nonce is printable, so it renders as
    itself in the log."""
    return MARKER_PREFIX + nonce.encode("ascii")


def select_window(entries: list[tuple[str, str]], start_nonce: str, end_nonce: str,
                  pre_nonce: str | None = None,
                  post_nonce: str | None = None) -> tuple[str | None, list[str]]:
    """The device address the markers came from, and its lines in the phase's window.

    The inner markers (start and end) are recorded operations; the outer ones (pre and post),
    when given, are not. The window runs from the pre marker, or from the start marker, to the
    post marker, or to the end marker, or to the last line seen. The outer markers stay in the
    window so that their sequence numbers bound it, and a split inner marker shows as a gap
    between two numbered lines; an unpaired marker line is optional (LogLine.optional). The
    address is read from the first marker found, so a spool holding several devices is
    filtered to this one. Returns (None, []) if no marker was found, which a caller reports
    rather than guessing."""
    wanted = {nonce: render_text(marker_text(nonce))
              for nonce in (pre_nonce, start_nonce, end_nonce, post_nonce) if nonce}

    def is_marker(text: str, nonce: str | None) -> bool:
        # Found by its text anywhere in a datagram, so a marker line another task's message
        # split still bounds the window.
        return bool(nonce) and (f'txt="{wanted[nonce]}"' in text or wanted[nonce] + '"' in text)

    device_ip = None
    for ip, text in entries:
        if any(is_marker(text, nonce) for nonce in wanted):
            device_ip = ip
            break
    if device_ip is None:
        return None, []
    device_texts = [text for ip, text in entries if ip == device_ip]

    def index_of(nonce: str | None, begin: int = 0) -> int | None:
        for i in range(begin, len(device_texts)):
            if is_marker(device_texts[i], nonce):
                return i
        return None

    pre = index_of(pre_nonce)
    start = index_of(start_nonce)
    if pre is not None:
        begin = pre
    elif start is not None:
        begin = start
    else:
        return None, []
    post = index_of(post_nonce, begin)
    end = index_of(end_nonce, begin)
    if post is not None:
        finish = post + 1
    elif end is not None:
        finish = end + 1
    else:
        finish = len(device_texts)
    return device_ip, device_texts[begin:finish]


# Where run-tests --syslog puts this target's collected log; see SYSLOG_FILE_ENV there.
COLLECTED_LOG_ENV = "E2E_SYSLOG_FILE"


class CollectedLogSource:
    """The device log that the runner's collector writes for this target, one
    "<receive time> <text>" line per log line (tests/lib/syslog_collector.py). The runner holds
    the syslog port for every target of a run, so a suite reads its target's file instead of
    binding the port. The file holds this device's lines only, so every entry carries `device`
    as its address. mark() remembers where the file ends, and entries() reads from there."""

    def __init__(self, path: str, device: str) -> None:
        self.path = path
        self.device = device
        self.offset = 0

    @classmethod
    def from_environment(cls, device: str) -> CollectedLogSource | None:
        path = os.environ.get(COLLECTED_LOG_ENV)
        return cls(path, device) if path else None

    def mark(self) -> None:
        try:
            self.offset = os.path.getsize(self.path)
        except OSError:
            self.offset = 0

    def entries(self) -> list[tuple[str, str]]:
        try:
            with open(self.path, "rb") as raw:
                raw.seek(self.offset)
                data = raw.read()
        except OSError:
            return []
        out = []
        # A last line without its newline is still being written, so it is left for next time.
        for line in data.decode("utf-8", errors="replace").split("\n")[:-1]:
            stamp, _, text = line.rstrip("\r").partition(" ")
            if re.fullmatch(r"\d+\.\d+", stamp):
                out.append((self.device, text))
        return out

    def stop(self) -> None:
        pass


class UdpLogSource:
    """A UDP sink bound to one address and port, for a run that owns the syslog port. It
    keeps (source ip, text) in arrival order, the same shape read_spool returns, so a phase
    correlates the same way whether the lines came from a file or from the wire.

    Only for a single-suite run: two suites on one host cannot both bind one port, which is
    why the spool file is the path a shared bench uses.
    """

    def __init__(self, bind_ip: str, port: int) -> None:
        self.bind_ip = bind_ip
        self.port = port
        self._entries: list[tuple[str, str]] = []
        self._lock = threading.Lock()
        self._sock: socket.socket | None = None
        self._running = False
        self._thread: threading.Thread | None = None

    def start(self) -> None:
        sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 1 << 20)
        sock.bind((self.bind_ip, self.port))
        sock.settimeout(0.25)
        self._sock = sock
        self._running = True
        self._thread = threading.Thread(target=self._receive, daemon=True)
        self._thread.start()

    def _receive(self) -> None:
        while self._running and self._sock is not None:
            try:
                data, (address, _port) = self._sock.recvfrom(4096)
            except (TimeoutError, OSError):
                continue
            when = time.strftime("%H:%M:%S")
            for text in data.decode("utf-8", "replace").rstrip("\r\n").split("\n"):
                with self._lock:
                    self._entries.append((address, f"{when} {text}"))

    def entries(self) -> list[tuple[str, str]]:
        # The stored text keeps the time in front, so it reads like a spool line without the
        # address, which select_window does not need past the address it already has.
        with self._lock:
            return [(ip, text.split(" ", 1)[-1]) for ip, text in list(self._entries)]

    def stop(self) -> None:
        self._running = False
        if self._thread is not None:
            self._thread.join(timeout=2)
        if self._sock is not None:
            self._sock.close()
            self._sock = None


def parse_syslog_server(value: str) -> tuple[str, int] | None:
    """The "Log to Syslog Server" setting as (ip, port), or None when it is empty or not an
    address. The firmware defaults the port to 514 (software/network/syslog.cc)."""
    value = (value or "").strip()
    if not value:
        return None
    ip, sep, port = value.partition(":")
    if not re.match(r"^\d{1,3}(\.\d{1,3}){3}$", ip):
        return None
    if sep and port.isdigit() and 0 < int(port) <= 0xFFFF:
        return ip, int(port)
    return ip, 514


def local_addresses(peer: str | None = None) -> set[str]:
    """This host's IPv4 addresses, so the suite can tell whether the device logs here.

    The host name resolves to 127.0.1.1 on a Debian host, so the address this host sends
    to `peer` from is asked of the routing table as well: connecting a UDP socket sends
    nothing and picks the interface a reply would come in on.
    """
    found = {"127.0.0.1", "0.0.0.0"}
    try:
        for info in socket.getaddrinfo(socket.gethostname(), None, socket.AF_INET):
            found.add(info[4][0])
    except OSError:
        pass
    if peer:
        try:
            with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as probe:
                probe.connect((peer, 9))
                found.add(probe.getsockname()[0])
        except OSError:
            pass
    return found
