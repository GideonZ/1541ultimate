"""The band under the panes: what the run is doing, as it does it.

Two panes answer "what did the machine show". They cannot answer "what did the
harness send it, and what came back, and how long did that take", which is the
question a viewer of a stuck run actually has. The band is that answer, on
every frame, in the same file.

Seven rows, full width, under both panes and above the progress bar:

    1  activity   which suite and check, and what the run is doing right now
    2  header     the column names, fixed, so a line is readable without one
    3  ticker     the four most recent meaningful interactions, newest last
    4  ticker
    5  ticker
    6  ticker
    7  counters   how much of each kind the run has done, for the whole run

A line is stamped when its interaction is issued, not when it answers. A ticker
that only shows completions shows nothing for exactly as long as something is
stuck, which is when a viewer most needs it, so an interaction that is still in
flight is on the band with a `...` status and a duration that counts up on
every composed frame, and it finalises in place without moving when it answers.

Only state-changing and meaningful interactions reach the ticker. Polling and
health probes are the bulk of a run and would push everything else off the band
within a second; they are in the counters row, which is where a reader looks to
see that they happened at all. Consecutive identical interactions collapse to
one line naming the range they cover.

Colour is an accent and never a field. The left rule of a line and its type tag
carry it; everything else is white for what the line is and grey for what it
cost. A failure turns the tag red, never the line, because a red line reads as
a red band and the band is not a verdict.
"""

from __future__ import annotations

from dataclasses import dataclass
from collections.abc import Sequence

import glyphs

ROWS = 7
ACTIVITY_ROW = 0
HEADER_ROW = 1
TICKER_ROWS = 4
FIRST_TICKER_ROW = 2
COUNTER_ROW = 6
HEIGHT = ROWS * glyphs.GLYPH_HEIGHT

# The two pixel columns of colour at the left of a ticker line. Narrow on
# purpose: it is the only place a line carries colour of its own, and a wider
# one reads as a highlight over the whole row.
RULE_PIXELS = 2

# Where each field of a ticker line starts and how wide it is, in characters,
# derived from the band's own width rather than written down twice. Every field
# but the interaction is fixed, because a column that moves is a column a
# reader has to find again on every line.
TIME_WIDTH = 12
TYPE_WIDTH = 6
STATUS_WIDTH = 4
DURATION_WIDTH = 6
BYTES_WIDTH = 5
BODY_WIDTH = 6
REFERENCE_WIDTH = 10
GAP = 1


@dataclass
class Layout:
    """Where every field of a ticker line sits, for one band width."""

    columns: int
    time: tuple[int, int]
    type: tuple[int, int]
    interaction: tuple[int, int]
    status: tuple[int, int]
    duration: tuple[int, int]
    sent: tuple[int, int]
    received: tuple[int, int]
    body: tuple[int, int]
    reference: tuple[int, int]

    def fields(self) -> list[tuple[int, int]]:
        return [self.time, self.type, self.interaction, self.status,
                self.duration, self.sent, self.received, self.body,
                self.reference]


def layout_for(width: int) -> Layout:
    """The column layout for a band this many pixels wide.

    Everything except the interaction has a width the content fixes, so the
    interaction takes whatever is left. A narrow band therefore loses the
    subject of a line before it loses the numbers, and the reference, which is
    the way back to the record, is the last thing that can go.
    """
    columns = max(1, width // glyphs.GLYPH_WIDTH)
    fixed = (TIME_WIDTH + TYPE_WIDTH + STATUS_WIDTH + DURATION_WIDTH
             + BYTES_WIDTH * 2 + BODY_WIDTH + REFERENCE_WIDTH)
    # One column of rule, then a gap before and after every field.
    interaction = max(4, columns - 1 - fixed - GAP * 9)
    at = 1
    made = []
    for size in (TIME_WIDTH, TYPE_WIDTH, interaction, STATUS_WIDTH,
                 DURATION_WIDTH, BYTES_WIDTH, BYTES_WIDTH, BODY_WIDTH,
                 REFERENCE_WIDTH):
        at += GAP
        made.append((at, size))
        at += size
    return Layout(columns, *made)


def header(layout: Layout) -> str:
    """The fixed row of column names, padded to the band's own width."""
    names = ("time", "type", "interaction", "stat", "dur", "sent", "rcvd",
             "body", "ref")
    line = [" "] * layout.columns
    for (start, size), name in zip(layout.fields(), names):
        for offset, character in enumerate(name[:size]):
            if start + offset < layout.columns:
                line[start + offset] = character
    return "".join(line)


def count_of(value: object) -> int:
    """A byte count from a record field, and 0 from anything that is not one.

    The band is drawn from records the transports write while the run is
    happening, and it may not be able to stop the recording over the shape of
    one of them. A field that is not a number is not a byte count, so it counts
    as nothing and the recording carries on.
    """
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        return 0
    return int(value)


def size_of(count: int | None) -> str:
    """A byte count in binary units, three significant digits, five wide.

    Powers of 1024 with one letter, because the band is measuring what went
    over a link and a reader comparing two lines needs the same unit to mean
    the same thing on both.
    """
    if not count:
        return ""
    value = float(count)
    for suffix in ("B", "K", "M", "G"):
        if value < 1024 or suffix == "G":
            if suffix == "B":
                return f"{int(value)}{suffix}"
            if value >= 100:
                return f"{value:.0f}{suffix}"
            if value >= 10:
                return f"{value:.1f}{suffix}"
            return f"{value:.2f}{suffix}"
        value /= 1024
    return ""


def duration_of(seconds: float | None) -> str:
    """How long an interaction has taken, in the width the column allows."""
    if seconds is None:
        return ""
    if seconds < 1.0:
        return f"{seconds * 1000:.0f}ms"
    if seconds < 100:
        return f"{seconds:.1f}s"
    return f"{seconds:.0f}s"


def middle_truncate(text: str, width: int) -> str:
    """`text` cut to fit, keeping its head and its tail.

    The subject of an interaction is a path or a command, and both put what
    identifies them at the two ends: cutting only the tail loses the file name,
    and cutting only the head loses the verb.
    """
    if width <= 0:
        return ""
    if len(text) <= width:
        return text
    if width <= 3:
        return text[:width]
    keep = width - 1
    head = (keep + 1) // 2
    return text[:head] + ">" + text[len(text) - (keep - head):]


# What the type tag says for each transport, and which interactions never
# reach the ticker at all. Polling and health probes are the bulk of a run:
# `machine:menu_screen` alone is four hundred calls in a sweep, and a ticker
# carrying them shows nothing else. They are counted rather than shown.
TAGS = {"rest": "REST", "telnet": "TEL", "ftp": "FTP", "socket": "SOCK"}
KEY_TAG = "KEY"
WAIT_TAG = "WAIT"

# Paths whose whole purpose is to ask the device whether it is still there.
POLLING = ("machine:menu_screen", "/v1/info", "/v1/version", "machine:heap")


def is_polling(record: dict) -> bool:
    """Whether this interaction is the run asking rather than the run doing."""
    if record.get("transport") == "socket":
        return True
    operation = str(record.get("op") or "")
    return any(path in operation for path in POLLING)


def tag_of(record: dict) -> str:
    """The type tag of one interaction: what kind of thing it is."""
    operation = str(record.get("op") or "")
    if "machine:input" in operation:
        return KEY_TAG
    return TAGS.get(str(record.get("transport") or ""), "REST")


def subject_of(record: dict) -> str:
    """What the line is about, in the words the transport uses.

    A Telnet exchange says send and recv rather than an arrow, because the
    activity row already uses one arrow to separate the suite from the check
    and a second meaning for one character is a second thing to learn.
    """
    operation = str(record.get("op") or "")
    if "machine:input" in operation:
        keys = str(record.get("params") or "")
        return keys or operation
    if str(record.get("transport")) == "rest":
        operation = operation.replace("/v1/", "")
    parameters = str(record.get("params") or "")
    return f"{operation} {parameters}".strip()


@dataclass
class Line:
    """One ticker line, from issue to completion, in place."""

    time: str
    tag: str
    subject: str
    status: str
    seconds: float | None
    sent: int | None
    received: int | None
    body: str
    reference: str
    failed: bool = False
    running: bool = False


# How long an interaction may run before the band says it is stuck. The same
# threshold the interaction log uses to write a start record, so the line
# appears and turns colour together and there is one number behind both.
STALL_SECONDS = 1.0

# What the activity row says, derived from what is in flight rather than from
# anything the run announces.
RUNNING, WAITING, STALLED, PASSED, FAILED = (
    "RUNNING", "WAITING", "STALLED", "PASSED", "FAILED")


class Ticker:
    """The lines the band shows, and the counters under them.

    Lines are stamped when their interaction is issued and finalised in place
    when it answers, so a line never moves once a reader has found it. Only
    the last few are kept, because the band has four rows and a queue of
    interactions nobody will ever see is memory spent on nothing.
    """

    def __init__(self, rows: int = TICKER_ROWS) -> None:
        self.rows = rows
        self.lines: list[Line] = []
        self.counts: dict[str, int] = {}
        self.sent = 0
        self.received = 0
        self.stream = 0

    def apply(self, records: Sequence[dict], now: float) -> None:
        for record in records:
            self._count(record)
            if record.get("phase") == "start":
                self._add(record, running=True)
                continue
            if record.get("phase") == "end" and self._finish(record):
                continue
            if is_polling(record):
                # Counted above and shown nowhere: a ticker carrying the
                # polling carries nothing else.
                continue
            self._add(record, running=False)

    def _count(self, record: dict) -> None:
        if record.get("phase") == "start":
            # Counted when it finishes, so one interaction is one count.
            return
        tag = tag_of(record).lower()
        repeat = int(count_of(record.get("repeat")) or 1)
        self.counts[tag] = self.counts.get(tag, 0) + repeat
        self.sent += count_of(record.get("sent")) * repeat
        self.received += count_of(record.get("received")) * repeat

    def _line(self, record: dict, running: bool) -> Line:
        seconds = record.get("seconds")
        if seconds is None and record.get("ms") is not None:
            seconds = float(record["ms"]) / 1000.0
        status = "..." if running else str(record.get("status")
                                           or record.get("fault") or "ok")
        failed = bool(record.get("fault")) or (
            isinstance(record.get("status"), int)
            and int(record["status"]) >= 400)
        return Line(time=str(record.get("clock") or ""),
                    tag=tag_of(record), subject=subject_of(record),
                    status=status, seconds=seconds,
                    sent=count_of(record.get("sent")) or None,
                    received=count_of(record.get("received")) or None,
                    body=str(record.get("body_sha256") or ""),
                    reference=str(record.get("reference") or ""),
                    failed=failed, running=running)

    def _add(self, record: dict, running: bool) -> None:
        line = self._line(record, running)
        if self.lines and not running:
            last = self.lines[-1]
            if (not last.running and last.tag == line.tag
                    and last.subject == line.subject
                    and last.status == line.status):
                # Consecutive and identical: one line naming the range rather
                # than four copies pushing everything else off the band.
                last.reference = _extend(last.reference, line.reference)
                last.seconds = line.seconds
                return
        self.lines.append(line)
        del self.lines[:-self.rows]

    def _finish(self, record: dict) -> bool:
        line = self._line(record, running=False)
        for existing in reversed(self.lines):
            if existing.running and existing.tag == line.tag \
                    and existing.subject == line.subject:
                # Finalised where it already is, so a reader watching a stuck
                # line sees it answer rather than sees it move.
                existing.status = line.status
                existing.seconds = line.seconds
                existing.sent = line.sent
                existing.received = line.received
                existing.body = line.body
                existing.failed = line.failed
                existing.running = False
                return True
        return False

    def state(self, now: float, failing: bool, between: bool) -> str:
        """What the activity row says, from what is in flight and nothing else."""
        running = [line for line in self.lines if line.running]
        if any((line.seconds or 0.0) >= STALL_SECONDS for line in running):
            return STALLED
        if running:
            return RUNNING
        if failing:
            return FAILED
        return PASSED if between else WAITING

    def counters(self, layout: Layout) -> str:
        """The cumulative row, which is never reset and never a rate."""
        parts = [f"{name} {count}" for name, count
                 in sorted(self.counts.items())]
        parts += [f"tx {size_of(self.sent)}", f"rx {size_of(self.received)}",
                  f"av {size_of(self.stream)}"]
        return middle_truncate("  ".join(parts), layout.columns - 2)


def _extend(first: str, second: str) -> str:
    """One reference covering two consecutive interactions."""
    if not first:
        return second
    if not second:
        return first
    head = first.split("-", 1)[0]
    tail = second.split("-", 1)[-1]
    return head if head == tail else f"{head}-{tail.lstrip('#')}"


def draw(canvas, x: int, y: int, width: int, ticker: Ticker, layout: Layout,
         activity: str, state: str, colours: dict[str, int],
         now: float) -> None:
    """Draw the whole band at (x, y). Colour is an accent and never a field.

    `colours` names the palette the caller is using, so this holds no opinion
    about the frame it is drawn on: `background`, `primary`, `secondary`,
    `failure`, `warning` and `accent`.
    """
    background = colours["background"]
    canvas.fill(x, y, width, HEIGHT, background)

    # The activity row: which suite and check, and what the run is doing.
    right = f"{state:>{len(state)}}"
    line = middle_truncate(activity, layout.columns - len(right) - 3)
    canvas.draw_text(x + glyphs.GLYPH_WIDTH, y, line,
                     colours["primary"], background)
    state_colour = {FAILED: colours["failure"], STALLED: colours["warning"]}.get(
        state, colours["secondary"])
    canvas.draw_text(x + (layout.columns - len(state) - 1) * glyphs.GLYPH_WIDTH,
                     y, state, state_colour, background)

    # The header, which is fixed so a line below it needs no legend.
    canvas.draw_text(x, y + glyphs.GLYPH_HEIGHT, header(layout),
                     colours["secondary"], background)

    # The ticker, newest last, one row each and never moving once placed.
    for offset in range(ticker.rows):
        index = len(ticker.lines) - ticker.rows + offset
        row_y = y + (FIRST_TICKER_ROW + offset) * glyphs.GLYPH_HEIGHT
        if index < 0:
            continue
        _draw_line(canvas, x, row_y, ticker.lines[index], layout, colours,
                   background, now)

    canvas.draw_text(x + glyphs.GLYPH_WIDTH,
                     y + COUNTER_ROW * glyphs.GLYPH_HEIGHT,
                     ticker.counters(layout), colours["secondary"], background)


def _draw_line(canvas, x: int, y: int, line: Line, layout: Layout,
               colours: dict[str, int], background: int, now: float) -> None:
    # The rule: two pixels of colour, the only place a line carries any, and
    # never red for a failure because a red line reads as a red band.
    canvas.fill(x, y, RULE_PIXELS, glyphs.GLYPH_HEIGHT,
                colours["warning"] if line.running else colours["accent"])
    seconds = line.seconds
    duration = colours["secondary"]
    if line.running and (seconds or 0.0) >= STALL_SECONDS:
        duration = colours["warning"]
    if line.failed:
        duration = colours["failure"]
    fields = (
        (layout.time, line.time, colours["secondary"], False),
        (layout.type, line.tag,
         colours["failure"] if line.failed else colours["accent"], False),
        (layout.interaction, middle_truncate(line.subject,
                                             layout.interaction[1]),
         colours["primary"], False),
        (layout.status, line.status, colours["secondary"], True),
        (layout.duration, duration_of(seconds), duration, True),
        (layout.sent, size_of(line.sent), colours["secondary"], True),
        (layout.received, size_of(line.received), colours["secondary"], True),
        (layout.body, line.body[:layout.body[1]], colours["secondary"], False),
        (layout.reference, line.reference[-layout.reference[1]:],
         colours["secondary"], False),
    )
    for (start, size), text, colour, right in fields:
        if not text:
            continue
        text = text[:size]
        at = start + (size - len(text) if right else 0)
        canvas.draw_text(x + at * glyphs.GLYPH_WIDTH, y, text, colour,
                         background)
