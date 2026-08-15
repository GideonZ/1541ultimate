"""Record what a gate run looked like: both screens, the audio, one file.

The device's VIC output is not the whole picture, and on two of the three UI
modes it is not the interesting half. Under `freeze` the menu is drawn into the
C64's own screen, so the VIC stream carries it. Under `overlay` the menu is a
separate hardware layer composited after the VIC, so the VIC stream carries
what the C64 is showing and not the menu the harness is driving. Under `telnet`
the menu is drawn into a Telnet session and reaches no video path at all. A
recording of the VIC stream alone therefore cannot answer "what was the harness
looking at when it decided this check failed" for two thirds of the gate.

So there are two panes: the harness's screen on the left, the device's video on
the right, the device's audio over both. The harness pane is on the left
because it is the pane that changes, because left is cause and right is effect
to a reader of a left-to-right script, and because the stamp names the test in
the same corner.

The pipeline is fixed and every boundary in it has one format:

    1 receive   UDP datagrams, filtered by source, and the screens the suites
                already read, into complete frames and an audio timeline
    2 hold      the newest of each: one VIC frame, one harness screen, an
                audio write cursor
    3 slot      one tick per output frame, the unit of alignment for
                everything downstream
    4 compose   the held sources plus the JSONL tail, into one RGB canvas
    5 encode    canvases on one pipe, PCM on another, one ffmpeg per file
    6 finish    chapters copied in, the subtitles written

The slot loop is the spine. Every enabled output receives exactly one frame per
tick, a pane with nothing new repeats its last frame, and a pane with no source
emits its placeholder card. Alignment between panes, between files, and between
the burned-in stamp and the player's clock all follow from that and from
nothing else.

The loop never blocks on the encoder. A write to a full pipe would stall it,
and a stalled loop stops draining the sockets, which loses packets from both
streams at once and corrupts frames rather than merely thinning them. It sheds
composed frames instead, and audio is never shed because its packets are the
timeline.

Nothing here may change what a suite does. A suite may stop a stream this is
recording, take the device off the network or reboot it; every one of those is
a suite doing its job. This yields, records that it yielded, and resumes.
"""

from __future__ import annotations

import json
import os
import select
import shutil
import subprocess
import sys
import threading
import time
from dataclasses import dataclass, field
from typing import Callable, Dict, List, Optional, Sequence, Tuple

sys.path.insert(0, os.path.join(
    os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))),
    "lib"))

import targets as targets_lib  # noqa: E402

import glyphs  # noqa: E402
import screens as screen_spool  # noqa: E402
import streams  # noqa: E402

# The two panes and the gutter between them. Both dimensions are even, which
# every encoder wants, and neither pane is ever resampled: both are pixel grids
# whose whole value is being exact.
SCREEN_PANE_WIDTH = 384
PANE_HEIGHT = 272
# Sized for the widest and tallest screen either transport produces: the REST
# menu is 40x25 and a Telnet session is 60x24, so 60 columns by 25 rows at 8x8
# glyphs. One geometry for the whole file, because a run can pass through all
# three modes and because two runs of one suite are only comparable frame to
# frame if they are the same shape.
HARNESS_PANE_WIDTH = 480
HARNESS_TEXT_HEIGHT = 200
# Eight pixels because everything here is on the 8-pixel character grid, and it
# is there so the two read as two pictures rather than as one wide one.
GUTTER_WIDTH = 8

# The visual system is the C64's own: the sixteen VIC colours, the character
# ROM, the 8-pixel grid, and colour reserved for state. Someone who grew up
# with this machine should read the result as a C64 screen with its border
# used, not as a capture tool's overlay.
CHROME = 11        # the darkest neutral in the palette, so screens dominate
CHROME_TEXT = 15   # light grey, legible on the chrome without being white
# Fixed rather than taken from the border, so two runs of one suite produce
# byte-identical stamps.
STAMP_BACKGROUND = 0
FAILURE_COLOUR = 2     # red
WARNING_COLOUR = 8     # orange
PASSED_COLOUR = 15     # light grey, the neutral a finished segment gets
RUNNING_COLOUR = 1     # white, the brighter outline on the current segment

# The failure edge is the outermost two rows and columns. The C64 border is 32
# pixels at the sides and at least 20 at the top, so the marking never touches
# the 320x200 picture area.
EDGE_PIXELS = 2
# Long enough that a reader dragging a timeline cannot step over it between two
# frames.
EDGE_DWELL_SECONDS = 3.0

# The progress bar along the bottom border: full width, a few pixels high, with
# a gap above it so it never touches the picture area.
BAR_HEIGHT = 4
BAR_GAP = 2
CHROME_BOTTOM_PIXELS = EDGE_PIXELS + BAR_HEIGHT + BAR_GAP

# The stamp is two rows of 8-pixel glyphs in the top border, which is 20 lines
# at the top on NTSC and 35 on PAL, so it fits on every geometry.
STAMP_ROWS = 2
# The pane labels go on the row under the stamp. They used to share the
# stamp's second row, where a caption long enough to reach the right of a pane
# was overwritten by the word MENU. One row is reserved for them and nothing
# else is ever drawn on it, so the two cannot collide whatever either says.
LABEL_ROW = STAMP_ROWS
# How much of the top of a frame the stamp and the labels own, and how much of
# the bottom the progress bar and the edge own. Between the two is the band no
# annotation is ever drawn into, which is what a still has to reproduce.
CHROME_TOP_PIXELS = (STAMP_ROWS + 1) * glyphs.GLYPH_HEIGHT

# How long the recording thread is given to finish after it is asked to stop.
# One slot's work plus one encoder write, with room for a host under load.
THREAD_JOIN_SECONDS = 30.0

# How long the recorder waits for a video frame before opening the audio file.
# One frame is 20 ms on a device that is sending, so this only ever expires on
# a device that is not.
TIMING_WAIT_SECONDS = 2.0

# How long a closing encoder is given to finish its file before it is killed.
# An mp4 is unplayable without the trailer ffmpeg writes on exit, and writing
# it costs a fraction of a second, so this only ever expires on a process that
# is not going to finish at all. Below the join budget of `Recorder.stop`, so
# the finishing pass cannot be what makes the join time out.
ENCODER_EXIT_SECONDS = 20.0

# How long each card is held. The opening card is a structured overview of
# three groups, which is more than two seconds of reading; the closing card is
# a verdict and a count, which is not. They are two constants because one
# would make the summary as long as the overview.
OVERVIEW_SECONDS = 5.0
SUMMARY_SECONDS = 2.0

# The device sends 50 frames a second on PAL. The recording does not need them:
# the material is a screen that is static for seconds at a time and then
# changes completely in one frame.
# Whole frames per second, and an integer on purpose: the decimation
# accumulator below counts in source frames, and the same accumulator in
# floating point loses a frame a second at 10 fps, where 5 x 0.2 sums to just
# under one.
SOURCE_FPS = 50

# How short a slot's audio has to be before the shortfall is treated as a
# stream that stopped rather than as packets that have not arrived yet. Two
# packets is 8 ms, which is more jitter than a LAN produces and less than any
# real gap.
SILENCE_AFTER_PACKETS = 2

# Why a stream's sequence continuity ended. Every one of these is something
# the run or the device did on purpose, and none of them is a packet the
# network lost, so the receiver is told to start its counters again rather
# than measure across the gap.
SUITE_STOPPED = "suite-stopped"
SUITE_STARTED = "suite-started"
RECORDER_REARM = "recorder-rearm"
STREAM_QUIET = "stream-quiet"

# How long a stream may deliver nothing before the packet after the gap cannot
# be compared with the packet before it. Longer than any jitter a LAN
# produces and shorter than the shortest interval a suite holds a stream for.
QUIET_DISCONTINUITY_SECONDS = 2.0

# How long a stream may be silent before the recorder asks for it again. Longer
# than the gap a suite leaves between its own stop and start, so the recorder
# does not fight a suite for the stream mid-check.
REARM_AFTER_SECONDS = 6.0
REARM_BACKOFF_CEILING_SECONDS = 120.0
# Both requests the recorder makes of the device are issued from the slot loop,
# so each is bounded well under the loop's own budget and neither is retried:
# a failure is an answer here, and the next slot will ask again.
REARM_TIMEOUT_SECONDS = 2.0
MENU_TIMEOUT_SECONDS = 2.0

# How long the screen tap may be silent before the recorder reads the menu
# itself. Its own interval rather than the stream's: the two answer different
# questions, and a suite that is navigating publishes a screen far more often
# than this.
MENU_QUIET_SECONDS = 6.0

# The two ranks every piece of metadata on a frame belongs to. What the frame
# is of comes first and in white; what it was produced by comes after it and in
# grey. A field is truncated from the right, so a narrow file loses the second
# rank before it loses any of the first.
PRIMARY_TEXT = 1     # white
SECONDARY_TEXT = 15  # light grey

# The opening card. Its background is the darkest thing in the palette so the
# values on it carry the contrast, and its own three ranks are separate from
# the frame stamp's two: a group heading and a field label say what a value is
# and are never the value.
CARD_BACKGROUND = 0
CARD_HEADING = 15   # light grey
CARD_LABEL = 12     # medium grey
CARD_VALUE = 1      # white
CARD_SECONDARY = 15
# One blank column at each side, and the gap between two columns of fields.
CARD_MARGIN_COLUMNS = 1
CARD_COLUMN_GAP = 4

# What the placeholder cards say, which is as much as the run knows.
NO_SCREEN_YET = "no screen has been read yet"
NO_MENU_OPEN = "no menu is open"
NO_VIDEO = "waiting for the device's video"
# Drawn above a harness pane showing a screen older than the poll that should
# have replaced it.
STALE_MARK = "STALE"

# How long the health-warning edge is held after the sweep that raised it. The
# edge answers "was the device in trouble around here", and a device is
# recovered between suites, so holding it to the end of a run would mark the
# rest of the recording for a condition that had already been dealt with.
UNHEALTHY_DWELL_SECONDS = 60.0
# How long a screen may go unrefreshed before the pane is marked stale. Longer
# than the interval a suite polls the menu at, so an ordinary quiet moment in
# a suite is not marked.
STALE_AFTER_SECONDS = 15.0


@dataclass
class Options:
    """Everything the recorder was configured with, and its defaults.

    Every one of these reaches the `capture` record, so a reader who finds a
    blurry recording learns from the file why it is blurry rather than guessing.
    """

    video: bool = True
    audio: bool = True
    menu: bool = True
    stamp: bool = True
    layout: str = "combined"
    quality: str = "lossless"
    scale: int = 1
    fps: int = 10
    keyint: float = 1.0
    menu_min_interval_ms: int = 0
    ffmpeg_args: str = ""

    def as_record(self) -> Dict[str, object]:
        return {"video": self.video, "audio": self.audio, "menu": self.menu,
                "stamp": self.stamp, "layout": self.layout,
                "quality": self.quality, "scale": self.scale, "fps": self.fps,
                "keyint": self.keyint,
                "menu_min_interval_ms": self.menu_min_interval_ms,
                "ffmpeg_args": self.ffmpeg_args}


@dataclass
class Geometry:
    """The canvas one output file is composed on."""

    width: int
    height: int
    harness_x: int
    screen_x: int

    @property
    def columns(self) -> int:
        """How many 8-pixel glyphs fit across, which is what the stamp fits to."""
        return self.width // glyphs.GLYPH_WIDTH


def decimate(phase: int, fps: int) -> Tuple[int, bool]:
    """Whether this source frame is one of the ones the output keeps.

    Deterministic decimation, and the reason it is a phase accumulator rather
    than a threshold on elapsed time: the accumulator gains the output rate on
    every source frame and a frame is taken when the total crosses the source
    rate, which is exact at simple ratios and bounded at every other. A
    threshold on elapsed time drifts, and two runs of one suite would then not
    decimate the same way, which is what makes two recordings comparable frame
    by frame.

    Counted in whole frames rather than in a fraction of one, because the same
    accumulator in floating point loses a frame a second at 10 fps: five
    additions of 0.2 sum to just under one.
    """
    phase += fps
    if phase < SOURCE_FPS:
        return phase, False
    return phase - SOURCE_FPS, True


def geometry_for(video: bool, menu: bool, layout: str) -> Dict[str, Geometry]:
    """The canvas for each output file this run will write.

    Dropping a source changes the canvas rather than leaving a blank pane, and
    `separate` gives each pane a file of its own with no gutter.
    """
    made: Dict[str, Geometry] = {}
    if layout == "separate":
        if menu:
            made["harness"] = Geometry(HARNESS_PANE_WIDTH, PANE_HEIGHT, 0, -1)
        if video:
            made["screen"] = Geometry(SCREEN_PANE_WIDTH, PANE_HEIGHT, -1, 0)
        return made
    width = 0
    harness_x = screen_x = -1
    if menu:
        harness_x = 0
        width = HARNESS_PANE_WIDTH
    if video:
        screen_x = width + (GUTTER_WIDTH if menu else 0)
        width = screen_x + SCREEN_PANE_WIDTH
    made["combined"] = Geometry(width, PANE_HEIGHT, harness_x, screen_x)
    return made


# ---------------------------------------------------------------------------
# What the run is doing, read from the records it is already writing
# ---------------------------------------------------------------------------


@dataclass
class RunState:
    """The suite, scenario and check the stamp and the markings name."""

    suite: str = ""
    label: str = ""
    attempt: int = 1
    scenario: str = ""
    check: str = ""
    failed_at: float = 0.0
    unhealthy: bool = False
    unhealthy_at: float = 0.0
    segments: List[str] = field(default_factory=list)
    verdicts: Dict[str, str] = field(default_factory=dict)
    current_segment: int = -1
    # Whether the last thing the run recorded was a suite closing. Between
    # suites there is no Telnet session and the overlay is all there is, which
    # is the only place a menu_screen request of the recorder's own is the
    # right thing to make.
    between_suites: bool = True

    def caption(self) -> str:
        """What the stamp's second row says: which test, at this moment."""
        parts = [part for part in (self.label, self.suite, self.scenario,
                                   self.check) if part]
        return " / ".join(parts)

    @property
    def key(self) -> str:
        """This suite run's identity key, which names its files."""
        return f"{self.label}/{self.suite}/{self.attempt}" if self.suite else ""

    @property
    def stem(self) -> str:
        """The file-name form of the key: the one substitution, no target."""
        return f"{self.label}-{self.suite}-{self.attempt}" if self.suite else ""


class JsonlTail:
    """Follow the records the runner and the suites are already writing.

    No new channel between the processes and nothing for the runner to know
    about the recorder: every record it needs is already being appended, with a
    wall-clock time, to files this can open and read to the end of.

    Three properties already hold: the files are append-only within a suite
    run, each record is one complete line written under O_APPEND, and a partial
    final line means the writer is mid-write rather than that anything is
    wrong, so a reader keeps the partial line and retries.

    The fourth is a trap rather than a property. A per-suite file is truncated
    on the first attempt, and for every suite that reuses a file name, which is
    every retried suite and every mode pass. A tailer holding an offset across
    that truncation reads nothing until the file grows past its old offset and
    then reads from the middle of a record, so this compares the file's size
    against its own offset on every tick and starts again when the size has
    gone backwards. The same rule covers a file that was replaced.
    """

    def __init__(self, directory: str) -> None:
        self.directory = directory
        self.state = RunState()
        self._offsets: Dict[str, int] = {}
        self._partial: Dict[str, str] = {}

    def poll(self) -> RunState:
        """Read whatever has been appended since the last look."""
        for name in self._files():
            for record in self._read(os.path.join(self.directory, name)):
                self._apply(record, name)
        return self.state

    def _entered_suite(self, source: str) -> None:
        """A suite is running from its first record, not from its first check.

        Deriving this from checks would leave the recorder believing it was
        between suites for everything before the first one closed, which for
        `av/stream_test.py` and `api/input_test.py` is the part of the suite
        that owns the stream. The recorder would then re-arm into it and
        change what the suite sees, which OBS-15.1 forbids.
        """
        if source == "run.jsonl":
            return
        self.state.between_suites = False

    def _files(self) -> List[str]:
        try:
            names = sorted(name for name in os.listdir(self.directory)
                           if name.endswith(".jsonl"))
        except OSError:
            return []
        # The screen spool shares the suffix and is not a record file: its
        # name carries no label, so reading a suite name out of it would leave
        # every artefact named after that suite with an empty one.
        return [name for name in names if name != "screens.jsonl"]

    def _read(self, path: str) -> List[dict]:
        try:
            size = os.path.getsize(path)
        except OSError:
            return []
        offset = self._offsets.get(path, 0)
        if size < offset:
            # Truncated or replaced. Reading from the old offset would land in
            # the middle of a record.
            offset = 0
            self._partial[path] = ""
        if size == offset:
            return []
        try:
            with open(path, encoding="utf-8", errors="replace") as handle:
                handle.seek(offset)
                text = handle.read()
        except OSError:
            return []
        self._offsets[path] = offset + len(text.encode("utf-8", "replace"))
        text = self._partial.pop(path, "") + text
        *lines, partial = text.split("\n")
        self._partial[path] = partial
        found = []
        for line in lines:
            if not line.strip():
                continue
            try:
                decoded = json.loads(line)
            except ValueError:
                continue
            if isinstance(decoded, dict):
                found.append(decoded)
        return found

    def _apply(self, record: dict, source: str) -> None:
        kind = record.get("kind")
        if kind != "suite":
            self._entered_suite(source)
        if record.get("attempt"):
            try:
                self.state.attempt = int(record["attempt"])
            except (TypeError, ValueError):
                pass
        if kind == "plan":
            sequence = record.get("sequence") or []
            self.state.segments = [
                f"{entry.get('label')}/{entry.get('suite')}"
                for entry in sequence if isinstance(entry, dict)]
        elif kind == "check":
            self.state.check = f"check {record.get('index')}"
            self.state.scenario = str(record.get("scenario") or "")
            if record.get("verdict") == "FAIL":
                self.state.failed_at = float(record.get("time") or 0.0)
        elif kind == "suite":
            name = str(record.get("name") or "")
            label = str(record.get("mode") or "")
            self.state.verdicts[f"{label}/{name}"] = str(record.get("verdict") or "")
            self.state.check = ""
            self.state.scenario = ""
            self.state.between_suites = True
        elif kind == "health":
            self.state.unhealthy = not record.get("ok", True)
            self.state.unhealthy_at = float(record.get("time") or 0.0)
        if record.get("suite") and source != "run.jsonl":
            # The per-suite file's own name is the only thing that carries the
            # label, which for an e2e suite is its UI mode and for a perf or
            # soak suite is its category.
            suite = str(record["suite"])
            stem = source[:-len(".jsonl")]
            self.state.suite = suite
            self.state.label = (stem[:-(len(suite) + 1)]
                                if stem.endswith("-" + suite) else "")
        # Matched on label and suite together. Under `--mode all` one suite
        # has a segment per mode, and matching on the name alone would mark
        # the first mode's segment for every pass over it.
        wanted = f"{self.state.label}/{self.state.suite}"
        for index, segment in enumerate(self.state.segments):
            if segment == wanted:
                self.state.current_segment = index
                break


# ---------------------------------------------------------------------------
# Stage 4: composing one canvas per slot
# ---------------------------------------------------------------------------


def _payload(value: object) -> bytes:
    """The `raw` field of a spooled screen, which is hex, as bytes."""
    if not isinstance(value, str):
        return b""
    try:
        return bytes.fromhex(value)
    except ValueError:
        return b""


def _why_no_video(cause: Optional[Dict[str, str]]) -> str:
    """What the card in an empty screen pane says.

    A gap the run can explain names the suite that caused it. A gap it cannot
    explain says only that there is no video, which is itself the answer: an
    unexplained gap is a device that went quiet on its own.
    """
    if not cause or cause.get("action") != "stop":
        return NO_VIDEO
    suite = cause.get("suite") or "a suite"
    return truncate(f"{suite} stopped this stream",
                    SCREEN_PANE_WIDTH // glyphs.GLYPH_WIDTH)


class Composer:
    """Draw one output frame.

    Four annotations share one coordinate system, and none of them ever
    touches a pane's 320x200 picture area: the C64 border is 32 pixels at the
    sides and at least 20 at the top and bottom, and every figure here fits
    inside it.

        the stamp        two rows across the canvas's top border
        pane labels      each pane's top border, right aligned, on the row
                         under the stamp, which nothing else is drawn on
        the failure edge the outermost two rows and columns
        the progress bar the bottom border, full width

    They are drawn in one fixed order: the panes, then the state edge, then
    the metadata, then the pane labels, then the progress bar. The edge is a
    state marking rather than a reading, so it goes under everything a reader
    reads; drawn last it painted over the first two pixels of the stamp and
    over both ends of the progress bar.

    Colour is reserved for state. The chrome is the darkest neutral in the
    palette so the screens dominate, and a failure colour is then the only
    colour on the frame, which is exactly what makes it findable in a
    thumbnail.
    """

    def __init__(self, geometry: Geometry, options: Options,
                 identity: Dict[str, str]) -> None:
        self.geometry = geometry
        self.options = options
        self.identity = identity
        self.canvas = glyphs.Canvas(geometry.width, geometry.height, CHROME)
        # The last frame composed without the annotations. See compose.
        self.plain = b""

    def compose(self, screen: Optional[Tuple[int, int, bytes]],
                harness: Optional[Sequence[str]], harness_kind: str,
                state: RunState, position: float, wall: float,
                harness_raw: bytes = b"", stale: bool = False,
                cause: Optional[Dict[str, str]] = None) -> bytes:
        """One canvas, as RGB bytes, for one slot.

        `self.plain` is left holding the same frame without the annotations,
        which is what the stills are written from: a still is what the machine
        showed, and a stamp burned into it is this module's own text on top of
        the evidence.
        """
        self.canvas.fill(0, 0, self.geometry.width, self.geometry.height, CHROME)
        if self.geometry.screen_x >= 0:
            self._draw_screen(screen, cause)
        if self.geometry.harness_x >= 0:
            self._draw_harness(harness, harness_kind, harness_raw, stale)
        self.plain = self.canvas.to_rgb()
        if self.options.stamp:
            self._draw_edge(state, wall)
            self._draw_stamp(state, position, wall)
            self._draw_labels(harness_kind)
            self._draw_bar(state)
            return self.canvas.to_rgb()
        return self.plain

    def _draw_screen(self, screen: Optional[Tuple[int, int, bytes]],
                     cause: Optional[Dict[str, str]] = None) -> None:
        x = self.geometry.screen_x
        if screen is None:
            self._card(x, SCREEN_PANE_WIDTH, _why_no_video(cause))
            return
        width, height, indices = screen
        # A frame of a different height is padded to the fixed geometry rather
        # than dropped: PAL and NTSC differ by 32 lines, a device can change
        # mode mid-run, and a recording that silently changed geometry would
        # desynchronise every timecode after it.
        top = (PANE_HEIGHT - height) // 2 if height < PANE_HEIGHT else 0
        self.canvas.blit_indices(x, max(top, 0), width,
                                 min(height, PANE_HEIGHT), indices)

    def _draw_harness(self, rows: Optional[Sequence[str]], kind: str,
                      raw: bytes = b"", stale: bool = False) -> None:
        """The four states of OBS-8.20, each saying something different."""
        x = self.geometry.harness_x
        top = (PANE_HEIGHT - HARNESS_TEXT_HEIGHT) // 2
        if not rows:
            self._card(x, HARNESS_PANE_WIDTH,
                       NO_MENU_OPEN if kind == "closed" else NO_SCREEN_YET)
            return
        if raw and kind == screen_spool.MENU:
            # The payload carries the colour plane and the reverse-video bit
            # that marks the selected row, so the menu is drawn from it and
            # the rows are the fallback for a screen that has no payload,
            # which is every Telnet one.
            glyphs.render_menu_screen(raw, self.canvas,
                                      x + menu_indent(HARNESS_PANE_WIDTH), top,
                                      background=CHROME)
        else:
            glyphs.render_text_screen(rows, self.canvas, x, top,
                                      CHROME_TEXT, CHROME)
        if stale:
            # The screen is the last one that was read rather than the current
            # one, and a reader looking at a still has no other way to know.
            self.canvas.draw_text(x, top - glyphs.GLYPH_HEIGHT, STALE_MARK,
                                  CHROME_TEXT, STAMP_BACKGROUND)

    def _card(self, x: int, width: int, text: str) -> None:
        """A pane with no source says why, which is itself the answer."""
        top = PANE_HEIGHT // 2 - glyphs.GLYPH_HEIGHT // 2
        left = x + max(0, (width - len(text) * glyphs.GLYPH_WIDTH) // 2)
        # Snapped to the 8-pixel grid: an element at an odd offset is the most
        # common way this kind of composition looks amateur.
        left -= left % glyphs.GLYPH_WIDTH
        self.canvas.draw_text(left, top, text, CHROME_TEXT, CHROME)

    def _draw_stamp(self, state: RunState, position: float, wall: float) -> None:
        """Every frame is self-describing, because a single frame travels.

        Somebody screenshots a failure into an issue, somebody shares the
        video, an agent is handed one still. Any of those has to answer which
        device, which firmware, which run and when, without the file it came
        from, and a title card at the start does not survive a screenshot of
        minute twelve.

        Two ranks, in one order. What the frame is of comes first and in
        white: where in the recording it is, which machine it is of, and which
        firmware that machine was running. What produced it follows in grey:
        the wall clock, the address and the build. A field is truncated from
        the right, so a file too narrow to hold both ranks loses the second.
        """
        self._draw_fields(0, (
            (format_position(position), PRIMARY_TEXT),
            (self.identity.get("target", ""), PRIMARY_TEXT),
            (self.identity.get("firmware", ""), PRIMARY_TEXT),
            (format_wall(wall), SECONDARY_TEXT),
            (self.identity.get("address", ""), SECONDARY_TEXT),
            (self.identity.get("ci", ""), SECONDARY_TEXT)))
        # The same rule on the second row: which test this is comes before
        # where inside it the run had got to.
        self._draw_fields(1, (
            (" / ".join(part for part in (state.label, state.suite) if part),
             PRIMARY_TEXT),
            (state.scenario, SECONDARY_TEXT),
            (state.check, SECONDARY_TEXT)))

    def _draw_fields(self, row: int, fields: Sequence[Tuple[str, int]]) -> None:
        """One stamp row, as fields laid out left to right in their own rank.

        Truncation is applied to the row rather than to each field, so the
        fields that fit are complete and the first one that does not is the
        one that carries the marker.
        """
        y = row * glyphs.GLYPH_HEIGHT
        used = 0
        for text, colour in fields:
            if not text:
                continue
            room = self.geometry.columns - used
            if room <= 0:
                return
            piece = truncate(("  " if used else "") + text, room)
            self.canvas.draw_text(used * glyphs.GLYPH_WIDTH, y, piece, colour,
                                  STAMP_BACKGROUND)
            used += len(piece)

    def _draw_labels(self, kind: str) -> None:
        """Which pane is which, for a viewer who did not build this.

        On a row of their own under the stamp. Sharing the stamp's second row
        put the word MENU on top of the caption whenever the caption reached
        the right of the pane, which a suite and scenario name together
        routinely do.
        """
        row = LABEL_ROW * glyphs.GLYPH_HEIGHT
        if self.geometry.screen_x >= 0:
            self._right_label(self.geometry.screen_x, SCREEN_PANE_WIDTH, row,
                              "SCREEN")
        if self.geometry.harness_x >= 0:
            self._right_label(self.geometry.harness_x, HARNESS_PANE_WIDTH, row,
                              "TELNET" if kind == "telnet" else "MENU")

    def _right_label(self, x: int, width: int, row: int, text: str) -> None:
        left = x + width - len(text) * glyphs.GLYPH_WIDTH
        left -= left % glyphs.GLYPH_WIDTH
        self.canvas.draw_text(left, row, text, CHROME_TEXT, STAMP_BACKGROUND)

    def _draw_edge(self, state: RunState, wall: float) -> None:
        """A colour at the edge, visible in a thumbnail at any scrub speed.

        Deliberately narrow. Failure and device trouble are the two things a
        reader is scanning for; marking WARN and SKIP as well would put a
        colour on most of the run and turn a diagnostic into wallpaper. A
        clean run is unmarked, which is itself a statement.
        """
        colour = None
        if state.failed_at and wall - state.failed_at <= EDGE_DWELL_SECONDS:
            colour = FAILURE_COLOUR
        elif state.unhealthy and (not state.unhealthy_at
                                  or wall - state.unhealthy_at
                                  <= UNHEALTHY_DWELL_SECONDS):
            colour = WARNING_COLOUR
        if colour is None:
            return
        width, height = self.geometry.width, self.geometry.height
        self.canvas.fill(0, 0, width, EDGE_PIXELS, colour)
        self.canvas.fill(0, height - EDGE_PIXELS, width, EDGE_PIXELS, colour)
        self.canvas.fill(0, 0, EDGE_PIXELS, height, colour)
        self.canvas.fill(width - EDGE_PIXELS, 0, EDGE_PIXELS, height, colour)

    def _draw_bar(self, state: RunState) -> None:
        """One segment per planned suite run, filling left to right.

        Equal widths, computed once from the plan: sizing them by duration
        would be more truthful and would make the bar's shape change every
        suite, which is the opposite of glanceable. Segments to the right of
        the current one stay dark, because their outcome is not known yet.
        """
        if not state.segments:
            return
        top = self.geometry.height - EDGE_PIXELS - BAR_HEIGHT
        width = self.geometry.width // len(state.segments)
        if width < 1:
            return
        for index, segment in enumerate(state.segments):
            verdict = state.verdicts.get(segment, "")
            if index > state.current_segment >= 0 or not verdict:
                colour = CHROME
            elif verdict == "FAIL":
                colour = FAILURE_COLOUR
            elif verdict in ("WARN", "SKIP"):
                colour = WARNING_COLOUR
            else:
                colour = PASSED_COLOUR
            left = index * width
            self.canvas.fill(left, top, width - 1, BAR_HEIGHT, colour)
            if index == state.current_segment:
                # Outlined rather than filled, so the segment still carries
                # whatever verdict its previous attempt reached.
                self.canvas.fill(left, top, width - 1, 1, RUNNING_COLOUR)
                self.canvas.fill(left, top + BAR_HEIGHT - 1, width - 1, 1,
                                 RUNNING_COLOUR)
                self.canvas.fill(left, top, 1, BAR_HEIGHT, RUNNING_COLOUR)
                self.canvas.fill(left + width - 2, top, 1, BAR_HEIGHT,
                                 RUNNING_COLOUR)

    def card(self, lines: Sequence[str]) -> bytes:
        """A closing card, composed like any other frame.

        A viewer who reaches the end knows how it went without opening the
        report. Kept flat because it carries a verdict and a count: grouping
        two facts is structure for its own sake.
        """
        self.canvas.fill(0, 0, self.geometry.width, self.geometry.height, CHROME)
        top = max(0, (self.geometry.height
                      - len(lines) * glyphs.GLYPH_HEIGHT) // 2)
        top -= top % glyphs.GLYPH_HEIGHT
        for row, text in enumerate(lines):
            self.canvas.draw_text(glyphs.GLYPH_WIDTH,
                                  top + row * glyphs.GLYPH_HEIGHT,
                                  truncate(text, self.geometry.columns - 2),
                                  CHROME_TEXT, CHROME)
        return self.canvas.to_rgb()

    def overview(self, groups: Sequence[Tuple[str, Sequence[Tuple[str, str, bool]]]]
                 ) -> bytes:
        """The opening card: what this recording is, grouped and aligned.

        A viewer who opens the file has three questions in this order: which
        machine is this, what was on it, and which run is this. The card is
        those three groups, and a flat list of `name: value` lines answered
        none of them faster than reading all of them.

        Three ranks of text rather than the frame stamp's two, because a card
        has room for a group heading and a field label as well as a value.
        The values are white, the labels and headings grey, and colour is
        still reserved for state, so the card carries none.

        A group is `(heading, [(label, value, primary)])`. The layout falls
        out of the canvas: a canvas wide enough for two columns of the widest
        group gets two, and anything narrower gets one, which is what a
        `separate` recording of the 384-pixel screen pane is.
        """
        width, height = self.geometry.width, self.geometry.height
        self.canvas.fill(0, 0, width, height, CARD_BACKGROUND)
        blocks = [self._card_block(heading, fields) for heading, fields in groups
                  if fields]
        if not blocks:
            return self.canvas.to_rgb()
        room = self.geometry.columns - 2 * CARD_MARGIN_COLUMNS
        widest = max(_block_width(block) for block in blocks)
        columns = 2 if widest * 2 + CARD_COLUMN_GAP <= room else 1
        laid = _split_blocks(blocks, columns)
        tallest = max(_column_height(column) for column in laid)
        top = max(0, (height - tallest * glyphs.GLYPH_HEIGHT) // 2)
        top -= top % glyphs.GLYPH_HEIGHT
        per_column = widest if columns > 1 else room
        left = CARD_MARGIN_COLUMNS
        for column in laid:
            self._draw_column(column, left, top, per_column)
            left += widest + CARD_COLUMN_GAP
        return self.canvas.to_rgb()

    @staticmethod
    def _card_block(heading: str,
                    fields: Sequence[Tuple[str, str, bool]]
                    ) -> List[List[Tuple[str, int]]]:
        """One group as rows of coloured runs, its labels aligned to one width."""
        label_width = max(len(label) for label, _value, _primary in fields)
        rows: List[List[Tuple[str, int]]] = [[(heading, CARD_HEADING)]]
        for label, value, primary in fields:
            rows.append([("  " + label.ljust(label_width) + "  ", CARD_LABEL),
                         (value, CARD_VALUE if primary else CARD_SECONDARY)])
        return rows

    def _draw_column(self, blocks: Sequence[List[List[Tuple[str, int]]]],
                     left: int, top: int, room: int) -> None:
        row = 0
        for index, block in enumerate(blocks):
            if index:
                # One blank row between two groups, which is what makes them
                # read as groups rather than as one list with headings in it.
                row += 1
            for runs in block:
                used = 0
                for text, colour in runs:
                    space = room - used
                    if space <= 0:
                        break
                    piece = truncate(text, space)
                    self.canvas.draw_text((left + used) * glyphs.GLYPH_WIDTH,
                                          top + row * glyphs.GLYPH_HEIGHT,
                                          piece, colour, CARD_BACKGROUND)
                    used += len(piece)
                row += 1


Block = List[List[Tuple[str, int]]]


def _block_width(block: Block) -> int:
    """How many columns one group needs, at its widest row."""
    return max((sum(len(text) for text, _colour in row) for row in block),
               default=0)


def _column_height(column: Sequence[Block]) -> int:
    """How many rows one column of groups needs, blank rows included."""
    return sum(len(block) for block in column) + max(0, len(column) - 1)


def _split_blocks(blocks: Sequence[Block], columns: int) -> List[List[Block]]:
    """Deal the groups into `columns` columns, in order and as evenly as it goes.

    In order, because the groups answer a reader's questions in the order they
    are asked and reordering them to pack better would lose that. The split
    point is the one that makes the taller column as short as it can be, and
    the last such point when several tie, which fills the left column first.
    """
    if columns <= 1 or len(blocks) < 2:
        return [list(blocks)]
    best = None
    for split in range(1, len(blocks)):
        left, right = list(blocks[:split]), list(blocks[split:])
        tallest = max(_column_height(left), _column_height(right))
        if best is None or tallest <= best[0]:
            best = (tallest, [left, right])
    return best[1] if best else [list(blocks)]


def menu_indent(pane_width: int) -> int:
    """Where a 40-column menu screen starts inside the harness pane.

    The pane is sized for the widest screen either transport produces, which
    is a 60-column Telnet session at 480 pixels. A menu is 40 columns, so it
    occupies 320 of those pixels and is centred in the rest rather than left
    against the gutter. Derived from the two widths so the two cannot drift,
    and snapped to the 8-pixel grid because everything here is on it.
    """
    indent = max(0, (pane_width - glyphs.MENU_COLUMNS * glyphs.GLYPH_WIDTH) // 2)
    return indent - indent % glyphs.GLYPH_WIDTH


def annotation_free_area(geometry: Geometry) -> Tuple[int, int, int, int]:
    """The rectangle of a composed frame no annotation is ever drawn into.

    The stamp and the pane labels own the top three character rows, and the
    progress bar and the state edge own the bottom of the frame. Everything
    between them is the panes as they were composed, which is what a still is
    written from, so a still and the frame it was taken from have to be
    identical here and are allowed to differ everywhere else.
    """
    return (EDGE_PIXELS, CHROME_TOP_PIXELS,
            max(0, geometry.width - 2 * EDGE_PIXELS),
            max(0, geometry.height - CHROME_TOP_PIXELS - CHROME_BOTTOM_PIXELS))


def truncate(text: str, columns: int) -> str:
    """`text` cut to fit, with a marker so a reader knows it was cut."""
    if columns <= 0:
        return ""
    if len(text) <= columns:
        return text
    return text[:max(0, columns - 1)] + ">"


def stamp_position(slots: int, fps: int) -> float:
    """Where the frame about to be written sits in the file, in seconds.

    `slots` counts every frame written, the title card's included, so at a
    constant output rate this is exactly the position a player reports. That
    also makes it the lead-in at the first frame after the cards, which is what
    `position_of` maps a wall-clock moment onto, so the timecode a reader seeks
    to and the timecode stamped there are the same number.

    Adding the lead-in to this counted it twice, and every frame in every
    recording was stamped that much late.
    """
    return slots / max(1, fps)


def format_position(seconds: float) -> str:
    """A position in the file, which is what every timecode here is.

    Exactly the position a player reports for the frame, because the output is
    constant rate and this counts from the first frame of the file including
    the title card.
    """
    whole = int(seconds)
    return (f"{whole // 3600:02d}:{whole % 3600 // 60:02d}:{whole % 60:02d}"
            f".{int((seconds - whole) * 1000):03d}")


def format_wall(when: float) -> str:
    """The stamp's wall clock, in the zone the run's other timestamps are in.

    Local rather than UTC, because every other time in the bundle is
    `time.time()` rendered by the report on this host, and a frame whose clock
    disagreed with the report's would be worse than useless for joining one to
    the other.
    """
    return time.strftime("%Y-%m-%d %H:%M:%S", time.localtime(when))


# ---------------------------------------------------------------------------
# Stage 5: the encoder
# ---------------------------------------------------------------------------


def encoder_command(path: str, geometry: Geometry, options: Options,
                    binary: str = "ffmpeg") -> List[str]:
    """The command for one output file.

    Fed raw, not encoded: the frames enter as rgb24 at the composed geometry
    and the fixed frame rate, which is exactly what the composition already
    has in memory, so nothing is converted twice and nothing is encoded twice.

    One stream per process, each with one pipe. Handing one process both the
    frames and the audio deadlocks at this frame size, and it does so in
    ffmpeg rather than in anything here: a composed frame is 872x272x3, which
    is eleven times a pipe buffer, so a writer that has to interleave the two
    from one thread blocks inside the frame write before it can write the
    audio the encoder is waiting for. Measured directly, with no code of this
    module involved: the first frame is taken and the second never is. The
    audio is encoded by a process of its own and muxed in with a stream copy,
    which touches no frame.

    Five properties, in the order they matter, and each of them is why one of
    these arguments is what it is:

    1 Pixel exact. The encode is lossless with no chroma subsampling. A lossy
      encode spends its bits on the transitions and blurs the 8x8 glyphs,
      which destroys the one thing the artefact is for, and 4:2:0 alone would
      smear the edge of every character. It is cheap here rather than
      expensive: sixteen palette colours on a screen that mostly does not
      change is close to the best case a lossless codec ever sees.
    2 A keyframe at every transition. The frame where the screen changes is
      the frame a reader is looking for and it has to be decodable on its own.
      x264's scene-cut detection puts an IDR frame exactly there and costs
      nothing to leave enabled; a fixed GOP would disable it, which is the
      mistake to avoid.
    3 Bounded backward seeking, through a keyframe interval of about a second
      of recorded time, for a static stretch where no scene cut fires.
    4 Constant frame rate, because the timecode arithmetic is a subtraction
      and is only correct if presentation time advances linearly.
    5 Seekable without reading the whole file: the index goes at the front, so
      a reader can seek in a partly downloaded artifact.
    """
    command = [binary, "-hide_banner", "-loglevel", "error", "-y",
               "-f", "rawvideo", "-pixel_format", "rgb24",
               "-video_size", f"{geometry.width}x{geometry.height}",
               "-framerate", str(options.fps), "-i", "pipe:0"]
    if options.scale > 1:
        # Integer factors only, nearest neighbour: a fractional scale resamples
        # pixel art and gives up the property the lossless encode was chosen
        # for.
        command += ["-vf", f"scale=iw*{options.scale}:ih*{options.scale}"
                            ":flags=neighbor"]
    if options.quality == "lossless":
        # libx264rgb encodes the RGB planes directly, so a decoded frame is the
        # composed frame byte for byte. libx264 would have to convert to YUV
        # first, and that conversion is not reversible: `-qp 0` is lossless in
        # the space it encodes, which is not the space the frames are in.
        command += ["-c:v", "libx264rgb", "-pix_fmt", "gbrp", "-qp", "0"]
    else:
        command += ["-c:v", "libx264", "-pix_fmt", "yuv444p",
                    "-crf", str(options.quality)]
    # A fixed GOP would disable scene-cut detection, which is what puts a
    # keyframe on the frame a reader is looking for. `-g` is the interval for a
    # static stretch where no cut fires, not a replacement for it.
    command += ["-g", str(max(1, int(options.keyint * options.fps))),
                "-preset", "veryfast"]
    command += ["-movflags", "+faststart", path]
    if options.ffmpeg_args:
        # Appended, so it overrides anything chosen above, including the pixel
        # exactness. --help says so.
        command += options.ffmpeg_args.split()
    return command


def encoder_available(binary: str = "ffmpeg") -> str:
    """"" when the encoder is usable, else why it is not.

    Checked at startup, because the feature is opt-in and its absence is only
    an error when recording was asked for. A build of ffmpeg without the
    encoder the lossless default needs fails here rather than after thirty
    minutes of capture.
    """
    if shutil.which(binary) is None:
        return f"{binary} is not on PATH"
    try:
        completed = subprocess.run([binary, "-hide_banner", "-encoders"],
                                   capture_output=True, text=True, timeout=20)
    except (OSError, subprocess.TimeoutExpired) as exc:
        return f"{binary} could not be run: {exc}"
    if "libx264rgb" not in completed.stdout:
        return (f"{binary} has no libx264rgb encoder, which the lossless "
                "default needs to keep the frames pixel exact")
    return ""


def audio_command(path: str, rate: float, binary: str = "ffmpeg") -> List[str]:
    """The command for the run's one audio track.

    The rate declared is the device's own rounded value rather than 48000: it
    is derived from the video clock, it differs between PAL and NTSC, and
    declaring 48000 would let a 356 ppm error accumulate into a second of
    drift over a long run.
    """
    return [binary, "-hide_banner", "-loglevel", "error", "-y",
            "-f", "s16le", "-ar", str(int(round(rate))), "-ac", "2",
            "-i", "pipe:0", "-c:a", "aac", "-b:a", "128k", path]


class Encoder:
    """One ffmpeg process writing one stream.

    Under `separate` there are two video encoders, each fed the pane it wants
    from the same slot loop, and one audio encoder for the run whose track is
    muxed into both. That is what makes the files aligned by construction
    rather than by synchronisation afterwards.
    """

    def __init__(self, path: str, command: Sequence[str]) -> None:
        self.path = path
        self.frames = 0
        self.shed = 0
        self.problem = ""
        # Set when the slot loop closed the process without waiting for it.
        self.finishing: "subprocess.Popen | None" = None
        try:
            self.process = subprocess.Popen(
                list(command), stdin=subprocess.PIPE,
                stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, bufsize=0)
        except OSError as exc:
            self.process = None
            self.problem = f"{path}: the encoder could not be started: {exc}"

    def write(self, payload: bytes, budget: float = 1.0) -> bool:
        """Hand one frame or one slot's audio to the encoder, or shed it.

        Never an unbounded write. A write to a full pipe would stall the slot
        loop, and a stalled loop stops draining the sockets, which loses
        packets from both streams at once and corrupts frames rather than
        merely thinning them. A frame the encoder could not take inside the
        budget is dropped and counted, and the count is in the capture record
        so a reader can see that the host was too slow rather than that the
        device was not sending.
        """
        if self.process is None or self.process.stdin is None:
            return False
        # With bufsize=0 this is already the raw descriptor; with any other it
        # is a buffered wrapper whose own buffer would defeat the budget.
        stream = getattr(self.process.stdin, "raw", self.process.stdin)
        # The descriptor has to be non-blocking for the budget to mean
        # anything. A blocking write of more than PIPE_BUF returns only once
        # every byte has been taken, so one frame of 711 KB against a 64 KB
        # pipe would sit in the kernel until the encoder had drained all of it,
        # however much budget was left. Non-blocking, the same write returns
        # what it could take and the loop re-checks the deadline.
        self._unblock(stream)
        view = memoryview(payload)
        deadline = time.monotonic() + budget
        while view:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                self.shed += 1
                return False
            ready = select.select((), (stream,), (), remaining)[1]
            if not ready:
                continue
            try:
                written = stream.write(view)
            except BlockingIOError:
                # The pipe filled between select saying it was writable and
                # the write. Not an error, and not progress either.
                continue
            except (BrokenPipeError, OSError) as exc:
                self.problem = f"{self.path}: the encoder stopped: {exc}"
                # Closed without waiting for the process, because this runs on
                # the slot loop: a dead encoder must cost the loop nothing.
                # The finishing pass reaps it.
                self.close(wait=False)
                return False
            if written:
                view = view[written:]
        self.frames += 1
        return True

    @staticmethod
    def _unblock(stream) -> None:
        """Put the encoder's input into non-blocking mode, if it has an fd."""
        try:
            os.set_blocking(stream.fileno(), False)
        except (OSError, AttributeError, ValueError):
            # A stand-in stream in a test, or a descriptor already gone. The
            # budget then behaves as it did before, which is the failure this
            # is protecting against rather than a new one.
            pass

    def close(self, wait: bool = True) -> None:
        """Shut the encoder down. `wait` is false on the slot loop's path.

        The finishing pass waits, because an ffmpeg still writing its moov
        atom needs to finish or the file is unplayable. The slot loop does
        not, because waiting there is the stall this class exists to avoid.
        """
        if self.process is None:
            return
        process, self.process = self.process, None
        self.finishing = process
        try:
            if process.stdin is not None:
                process.stdin.close()
        except OSError:
            pass
        if not wait:
            return
        try:
            process.wait(timeout=ENCODER_EXIT_SECONDS)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait()
        self.finishing = None

    def reap(self) -> None:
        """Wait for an encoder the slot loop closed without waiting."""
        process, self.finishing = self.finishing, None
        if process is None:
            return
        try:
            process.wait(timeout=ENCODER_EXIT_SECONDS)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait()


# -------------------------------------------------------------------------
# Which frames are worth keeping as stills
# -------------------------------------------------------------------------


# Halves the comparison cost against a full byte-for-byte compare. Screen
# changes worth surfacing (a menu redraw, a mode switch) touch runs of
# adjacent bytes, not isolated ones, so a change that matters still shows up
# strongly at every second byte; a change that only shows up at odd offsets
# and vanishes at even ones is not the kind of change this module needs to
# find.
SAMPLE_STRIDE = 2

# A report reads as "at a glance" only if the stills fit on one screen next
# to first/last without scrolling. 6 transitions plus first and last is 8
# thumbnails, which fits a 4-wide grid in two rows.
MAX_TRANSITIONS = 6

# A single blinking cursor is one byte changing in a 52224-byte frame, i.e.
# roughly one sampled byte in 26112 (0.00004 as a fraction of sampled
# bytes). A threshold of 2% of sampled bytes is three orders of magnitude
# above that noise floor, so a blinking cursor never qualifies, while a menu
# redraw or a mode switch (which rewrites a large fraction of the screen)
# clears it easily.
TRANSITION_THRESHOLD = 0.02


def difference(previous: bytes, current: bytes, stride: int = SAMPLE_STRIDE) -> int:
    """Count sampled bytes that differ between two frames.

    Frames of different lengths are treated as differing entirely (the
    larger length is returned), since a length change means the capture
    itself changed shape and no byte-position comparison is meaningful.

    Chosen implementation: pack each stride-sampled slice into one big
    integer with int.from_bytes, XOR the two integers, convert the XOR
    result back to bytes, and count the non-zero bytes with bytes.count(0).
    A byte in the XOR result is zero exactly where the two inputs matched,
    so len - count(0) is the count of differing bytes. Measured on two
    52224-byte frames with a 2-stride sample (26112 sampled bytes each,
    about 5% of them differing): this ran at about 120 microseconds per
    call, versus about 590 microseconds for sum(map(operator.ne, ...)) over
    zip(s1[::stride], s2[::stride]) and about 690 microseconds for a
    zip generator expression with sum(). The int/XOR/count(0) route wins
    because from_bytes, the XOR and count(0) all run as single C-level
    calls over the whole buffer, where the zip-based routes pay a Python
    bytecode step per sampled byte.
    """
    if len(previous) != len(current):
        return max(len(previous), len(current)) // stride
    sampled_previous = previous[::stride]
    sampled_current = current[::stride]
    length = len(sampled_previous)
    if length == 0:
        return 0
    xor_value = int.from_bytes(sampled_previous, "big") ^ int.from_bytes(
        sampled_current, "big"
    )
    xor_bytes = xor_value.to_bytes(length, "big")
    return length - xor_bytes.count(0)


# ---------------------------------------------------------------------------
# Stage 3: the slot loop, which is the spine
# ---------------------------------------------------------------------------


class SpoolTail:
    """Follow the screens the suites are already publishing.

    The recorder makes no `machine:menu_screen` request of its own while a
    suite is driving the UI. The suites read that screen before every key,
    repeatedly while a redraw starts and repeatedly again until it stops, so a
    recording that shows the menu after every navigation step needs no new
    device traffic at all: it needs the screens the harness is already looking
    at, and the spool is where they are.
    """

    def __init__(self, path: str) -> None:
        self.path = path
        self.offset = 0
        self.rows: Optional[List[str]] = None
        # The menu payload the screen was rendered from. The pane is drawn
        # from this rather than from the rows: the selected row is bit 7 of a
        # character byte and the colours are the colour plane, and neither
        # survives into text, so a pane drawn from the rows shows a menu with
        # no cursor in it.
        self.raw: bytes = b""
        self.kind = ""
        self.last_at = 0.0
        self.taken = 0
        # The last thing a suite did to a device stream, so a pane with no
        # source can say who took it rather than that it is unavailable.
        self.stream_event: Dict[str, str] = {}
        # Every one of those since the last look, for a reader that has to act
        # on each rather than only show the latest. Drained by `take_events`.
        self.stream_events: List[Dict[str, str]] = []

    def poll(self, now: float) -> None:
        try:
            size = os.path.getsize(self.path)
        except OSError:
            return
        if size < self.offset:
            self.offset = 0
        if size == self.offset:
            return
        try:
            with open(self.path, encoding="utf-8", errors="replace") as handle:
                handle.seek(self.offset)
                text = handle.read()
        except OSError:
            return
        self.offset += len(text.encode("utf-8", "replace"))
        for line in text.split("\n"):
            if not line.strip():
                continue
            try:
                record = json.loads(line)
            except ValueError:
                continue
            if not isinstance(record, dict):
                continue
            kind = record.get("kind")
            if kind in (screen_spool.MENU, screen_spool.TELNET):
                self.rows = [str(row) for row in record.get("text") or []]
                self.raw = _payload(record.get("raw"))
                self.kind = str(kind)
                self.last_at = now
                self.taken += 1
            elif kind == "stream":
                # Published by tests/e2e/lib/streams.py for every arm and
                # every stop a suite made, which is where the answer to "why
                # did the video stop here" already is.
                self.stream_event = {
                    "action": str(record.get("action") or ""),
                    "stream": str(record.get("stream") or ""),
                    "suite": str(record.get("suite") or ""),
                }
                self.stream_events.append(self.stream_event)

    def take_events(self) -> List[Dict[str, str]]:
        """Every stream event since the last call, oldest first."""
        found, self.stream_events = self.stream_events, []
        return found


class AudioCursor:
    """One slot's worth of audio, every slot, whatever arrived.

    The audio input has to advance with the slot loop rather than with the
    packets: an encoder muxing two inputs consumes them together, so a video
    frame handed in without the audio that belongs beside it stalls the whole
    process. A stream that has stopped is a very long gap, and concealing it
    keeps the audio track the same length as the video track without
    pretending the missing seconds were silence.
    """

    def __init__(self, timeline: "streams.AudioTimeline", rate: float,
                 fps: int) -> None:
        self.timeline = timeline
        self.rate = rate
        self.fps = max(1, fps)
        # Carried as a fraction of a frame rather than rounded away. At 47983
        # Hz and 10 slots a second a slot is 4798.29 frames, and rounding each
        # slot to 4798 runs the track about a tenth of a second short over
        # half an hour, which is drift the file cannot be corrected for
        # afterwards.
        self.owed = 0.0
        self.buffer = bytearray()
        self.concealed_bytes = 0
        # Bytes filled because the buffer was a few packets short of a slot,
        # which is ordinary jitter rather than loss. Counted apart from the
        # timeline's own loss figures so a healthy run does not report packets
        # the device never failed to send.
        self.filled_bytes = 0

        # Bytes the run knows the device was not sending, because a suite
        # stopped the stream or the recorder could not get it back. Counted
        # apart from both of the above: nothing failed to arrive.
        self.unavailable_bytes = 0
        # Whether the run believes the device should be sending right now.
        # Set by the recorder from what the suites did to the stream.
        self.available = True

    def push(self, pcm: bytes) -> None:
        self.buffer += pcm

    def wanted(self) -> int:
        """How many bytes this slot is owed, remainder included."""
        self.owed += self.rate / self.fps
        frames = int(self.owed)
        self.owed -= frames
        return frames * streams.FRAME_BYTES

    def take(self) -> bytes:
        """The bytes that belong in this slot, topped up when the device is quiet."""
        want = self.wanted()
        if len(self.buffer) >= want:
            out = bytes(self.buffer[:want])
            del self.buffer[:want]
            return out
        out = bytes(self.buffer)
        self.buffer.clear()
        short = want - len(out)
        packets = -(-short // streams.PAYLOAD_BYTES)
        if short < streams.PAYLOAD_BYTES * SILENCE_AFTER_PACKETS:
            # A packet or two behind, which is jitter rather than a stream
            # that stopped.
            filler = self.timeline.fill(packets)[:short]
            self.filled_bytes += len(filler)
        elif not self.available:
            # The run stopped this stream, or could not get it back. The track
            # still needs a slot's audio to stay the same length as the video,
            # and none of it is a packet the device failed to deliver.
            filler = self.timeline.absent(packets)[:short]
            self.unavailable_bytes += len(filler)
        else:
            # Enough missing from a stream that should be running that the
            # device stopped sending on its own, which is the case the
            # concealment counters are about.
            filler = self.timeline.silence(packets)[:short]
            self.concealed_bytes += len(filler)
        return out + filler


@dataclass
class Sources:
    """What each pane is showing, and how long each has had nothing new."""

    frame: Optional[Tuple[int, int, bytes]] = None
    frame_at: float = 0.0
    video_at: float = 0.0
    audio_at: float = 0.0


class Recorder:
    """One target's recording, from the streams and the spool to the files.

    Runs as a thread in the process that owns one target. Its output is per
    target, multicast is delivered to every socket that joined, and one per
    target needs no coordination.
    """

    def __init__(self, directory: str, target, api, options: Options,
                 identity: Optional[Dict[str, str]] = None,
                 clock: Callable[[], float] = time.monotonic,
                 wall_clock: Callable[[], float] = time.time,
                 encoder_binary: str = "ffmpeg") -> None:
        self.directory = directory
        self.target = targets_lib.resolve(target)
        self.api = api
        self.options = options
        self.identity = dict(identity or {})
        self.clock = clock
        self.wall_clock = wall_clock
        self.encoder_binary = encoder_binary

        self.started_wall = 0.0
        self.lead_in = 0.0
        self.slots = 0
        self.shed = 0
        self.padded = 0
        self.foreign: Dict[str, int] = {}
        self.rearms: Dict[str, int] = {"video": 0, "audio": 0}
        self.menu_requests = 0
        self.menu_failures = 0
        # Which video timing the audio rate was taken from. See _observe_pal.
        self.timing = ""
        self.audio_rate = 0.0
        self.problems: List[str] = []
        self.files: List[str] = []
        self.geometry: Dict[str, Geometry] = {}

        self._assembler = streams.FrameAssembler()
        self._audio = streams.AudioTimeline()
        self._arming = streams.Arming(api, self.target)
        self._tail = JsonlTail(directory)
        self._spool = SpoolTail(os.path.join(directory, "screens.jsonl"))
        self._sources = Sources()
        self._cursor: Optional[AudioCursor] = None
        self._audio_encoder: Optional[Encoder] = None
        self._audio_path = ""
        self._encoders: Dict[str, Encoder] = {}
        self._composers: Dict[str, Composer] = {}
        self._sockets: List = []
        self._addresses: set = set()
        self._running = False
        self._thread: Optional[threading.Thread] = None
        self._last_canvas: Dict[str, bytes] = {}
        # The same frames without the annotations, which is what a still is.
        self._plain_canvas: Dict[str, bytes] = {}
        # The decimation phase of OBS-8.27, and what it dropped.
        self._phase = 0
        self.decimated = 0
        self._rearm_wait = {"video": REARM_AFTER_SECONDS,
                            "audio": REARM_AFTER_SECONDS}
        # Whether the run believes the device should be sending each stream,
        # and why the next packet of it cannot be compared with the last one.
        self._available = {"video": True, "audio": True}
        self._discontinuity = {"video": "", "audio": ""}
        # One picker per suite run, so a long suite gets the same number of
        # stills as a short one and the set stays readable.
        self._picker = StillPicker()
        self._picking = ""
        self._picking_identity: Dict[str, object] = {}
        self.stills: List[dict] = []

    # -- lifecycle --

    def start(self) -> str:
        """Open everything this needs, or say why it could not. Never raises.

        Reported once, here, before any suite runs, rather than late in a run
        that has already cost 15 to 30 minutes.
        """
        problem = encoder_available(self.encoder_binary)
        if problem:
            self.problems.append(problem)
            return problem
        self.geometry = geometry_for(self.options.video, self.options.menu,
                                     self.options.layout)
        if not self.geometry:
            return "no source is enabled, so there is nothing to record"
        self.started_wall = self.wall_clock()
        self._addresses = streams.source_addresses(self.target.video_host)
        try:
            if self.options.video:
                self._sockets.append(("video", streams.stream_socket(
                    self.target.video_group, self.target.video_port,
                    timeout=None)))
            if self.options.audio:
                self._sockets.append(("audio", streams.stream_socket(
                    self.target.audio_group, self.target.audio_port,
                    timeout=None)))
        except OSError as exc:
            self.problems.append(f"a stream socket could not be opened: {exc}")
            self.close_sockets()
            return self.problems[-1]

        # Both streams are armed before the audio encoder is built, because
        # the encoder has to declare a sample rate and the rate follows the
        # device's video timing.
        for stream in ("video", "audio"):
            if getattr(self.options, stream):
                self._arming.start(stream)

        if self.options.audio:
            rate = streams.rate_for(self._observe_pal())
            self.audio_rate = rate
            self._cursor = AudioCursor(self._audio, rate, self.options.fps)
            self._audio_path = os.path.join(self.directory, AUDIO_NAME)
            self._audio_encoder = Encoder(
                self._audio_path,
                audio_command(self._audio_path, rate, self.encoder_binary))
            if self._audio_encoder.problem:
                self.problems.append(self._audio_encoder.problem)
                self._audio_encoder = None
                self._cursor = None
        for name, geometry in sorted(self.geometry.items()):
            path = os.path.join(self.directory, output_name(name))
            encoder = Encoder(path, encoder_command(path, geometry,
                                                    self.options,
                                                    self.encoder_binary))
            if encoder.problem:
                self.problems.append(encoder.problem)
                continue
            self._encoders[name] = encoder
            self._composers[name] = Composer(geometry, self.options,
                                             self.identity)
            self.files.append(os.path.basename(path))
        if not self._encoders:
            self.close_sockets()
            return "no encoder could be started"

        self._running = True
        self._thread = threading.Thread(target=self._loop, name="recorder",
                                        daemon=True)
        self._thread.start()
        return ""

    def stop(self) -> Dict[str, object]:
        """End the recording and return its own health record."""
        self._running = False
        alive = False
        if self._thread is not None:
            self._thread.join(timeout=THREAD_JOIN_SECONDS)
            alive = self._thread.is_alive()
        self._arming.stop_all()
        self.close_sockets()
        if alive:
            # The loop is still composing into the same canvases and writing
            # to the same encoders. Writing the cards and the stills here
            # would interleave with it, and every count below would be read
            # while it was being changed, so the recording is left as it
            # stands and the record says why it is short.
            self.problems.append(
                f"the recording thread was still running after "
                f"{THREAD_JOIN_SECONDS:.0f}s, so the file has no summary card")
        else:
            self._write_stills(self._picking)
            if self.options.stamp:
                self._write_cards_tail()
        for encoder in list(self._encoders.values()) + (
                [self._audio_encoder] if self._audio_encoder else []):
            encoder.close()
            encoder.reap()
            if encoder.problem:
                self.problems.append(encoder.problem)
        return self.record()

    def _observe_pal(self) -> bool:
        """Whether this device is sending PAL frames, from the stream itself.

        The audio sample clock is derived from the video clock, so the rate
        the audio file declares depends on which timing the device is in. The
        stream is the only place that says: no route reports it, and a wrong
        rate is drift the file cannot be corrected for afterwards.

        A device that sends nothing inside the wait is recorded as PAL, which
        is what every device on this LAN is, and the wait is bounded because a
        device that is not sending is the case this runs into most.
        """
        video = [sock for name, sock in self._sockets if name == "video"]
        if not video:
            return True
        deadline = self.clock() + TIMING_WAIT_SECONDS
        while self.clock() < deadline:
            for sock, data, mine in streams.receive(
                    video, self._addresses, deadline - self.clock()):
                if not mine:
                    continue
                frame = self._assembler.push(data)
                if frame is None:
                    continue
                # Kept rather than dropped: this is a complete frame, and the
                # first one the recording shows.
                self._sources.frame = (frame.width, frame.height,
                                       streams.unpack(frame.packed))
                self._sources.frame_at = self.clock()
                self._sources.video_at = self._sources.frame_at
                self.timing = ("pal" if frame.height > streams.HEIGHT_NTSC
                               else "ntsc")
                return self.timing == "pal"
        # Not a problem: a device that is not sending yet is the ordinary case
        # for a run whose first suite has not started. The record says the
        # rate was assumed rather than observed, and `frames_completed` says
        # whether any video arrived at all.
        self.timing = "assumed-pal"
        return True

    def close_sockets(self) -> None:
        for _name, sock in self._sockets:
            try:
                sock.close()
            except OSError:
                pass
        self._sockets = []

    # -- the loop --

    def _loop(self) -> None:
        """The slot loop, and the one place a recording can stop on its own.

        A recording that simply stops with nothing anywhere saying why is the
        failure that makes a reader distrust every other artefact in the
        bundle, so whatever ends this is written down and the run carries on.
        """
        try:
            self._slots()
        except Exception as exc:  # noqa: BLE001 - a recording may not end a run
            self.problems.append(
                f"the recording stopped after {self.slots} frames: "
                f"{type(exc).__name__}: {exc}")
            self._running = False

    def _slots(self) -> None:
        interval = 1.0 / max(1, self.options.fps)
        if self.options.stamp:
            self._write_cards_head(interval)
        next_slot = self.clock()
        while self._running:
            self._drain(min(interval, max(0.0, next_slot - self.clock())))
            now = self.clock()
            self._spool.poll(now)
            self._apply_stream_events()
            if now < next_slot:
                continue
            behind = int((now - next_slot) / interval)
            state = self._tail.poll()
            self._emit(state, recompose=True)
            for _ in range(min(behind, self.options.fps)):
                # Falling behind sheds the composition, not the slot: every
                # output still receives one frame per slot, so the files stay
                # aligned with each other and with the clock, and what is given
                # up is the work rather than the timeline.
                self._emit(state, recompose=False)
            next_slot += interval * (1 + min(behind, self.options.fps))
            self._maybe_rearm(now)
            self._maybe_read_menu(now)

    def _drain(self, budget: float) -> None:
        if not self._sockets or budget <= 0:
            time.sleep(min(budget if budget > 0 else 0.002, 0.05))
            return
        sockets = [sock for _name, sock in self._sockets]
        kinds = {id(sock): name for name, sock in self._sockets}
        now = self.clock()
        for sock, data, mine in streams.receive(sockets, self._addresses, budget):
            if not mine:
                # Another machine streaming into the same group. Counted and
                # named, never stopped: on a multi-target run it is another
                # target of the same run, and stopping it would break that
                # recording.
                self.foreign[str(id(sock))] = self.foreign.get(str(id(sock)), 0) + 1
                continue
            if kinds.get(id(sock)) == "audio":
                self._settle_continuity("audio", now)
                self._sources.audio_at = now
                if self._cursor is not None:
                    self._cursor.push(self._audio.push(data).pcm)
                continue
            self._settle_continuity("video", now)
            self._sources.video_at = now
            frame = self._assembler.push(data)
            if frame is not None:
                self._phase, keep = decimate(self._phase, self.options.fps)
                if not keep:
                    self.decimated += 1
                    continue
                if (self._sources.frame is not None
                        and frame.height != self._sources.frame[1]):
                    # A device can change video mode mid-run, and a raw stream
                    # has no way to say the size changed, so a later frame of a
                    # different height is padded to the fixed geometry rather
                    # than dropped and the count is reported.
                    self.padded += 1
                self._sources.frame = (frame.width, frame.height,
                                       streams.unpack(frame.packed))
                self._sources.frame_at = now

    # -- the streams' lifecycle, which is not the streams' health --

    def _mark_discontinuity(self, stream: str, reason: str) -> None:
        """Say that the next packet of `stream` starts a new timeline.

        Recorded rather than acted on immediately, because the receiver has
        nothing to reanchor onto until a packet arrives. The first reason
        wins: a suite that stops a stream and starts it again has ended the
        continuity once, not twice.
        """
        if not self._discontinuity.get(stream):
            self._discontinuity[stream] = reason

    def _settle_continuity(self, stream: str, now: float) -> None:
        """Act on a pending discontinuity, or find one nobody declared.

        Called with each arriving packet, before the receiver sees it. A
        stream that has delivered nothing for seconds has ended its own
        continuity whether or not the run knows why, and counting the missing
        sequence numbers as transport loss is what put 14187 lost frames on a
        recording where nothing was lost.
        """
        last = (self._sources.audio_at if stream == "audio"
                else self._sources.video_at)
        reason = self._discontinuity.get(stream) or ""
        if not reason and last and now - last >= QUIET_DISCONTINUITY_SECONDS:
            reason = STREAM_QUIET
        if not reason:
            return
        self._discontinuity[stream] = ""
        if stream == "audio":
            self._audio.reanchor(reason)
        else:
            self._assembler.reanchor(reason)

    def _apply_stream_events(self) -> None:
        """Take what the suites did to the device's streams and act on it.

        A suite stopping a stream and starting it again is the ordinary case,
        and both ends of it break the sequence continuity: the device stops
        counting where the recorder was and starts again wherever it is.
        """
        for event in self._spool.take_events():
            stream = event.get("stream") or ""
            if stream not in self._available:
                continue
            action = event.get("action") or ""
            if action == "stop":
                self._available[stream] = False
                self._mark_discontinuity(stream, SUITE_STOPPED)
            elif action == "start":
                self._available[stream] = True
                self._mark_discontinuity(stream, SUITE_STARTED)
        if self._cursor is not None:
            self._cursor.available = self._available["audio"]

    def _emit(self, state: RunState, recompose: bool) -> None:
        # The real clock rather than a slot count: under sustained shedding a
        # count of slots falls behind the records the stamp is there to be
        # joined to.
        wall = self.wall_clock()
        position = stamp_position(self.slots, self.options.fps)
        budget = 1.0 / max(1, self.options.fps)
        if self._audio_encoder is not None and self._cursor is not None:
            self._audio_encoder.write(self._cursor.take(), budget=budget)
        stale = bool(self._spool.last_at
                     and self.clock() - self._spool.last_at
                     > STALE_AFTER_SECONDS)
        for name, encoder in self._encoders.items():
            if recompose or name not in self._last_canvas:
                composer = self._composers[name]
                canvas = composer.compose(
                    self._sources.frame, self._spool.rows,
                    self._spool.kind or "closed", state, position, wall,
                    harness_raw=self._spool.raw, stale=stale,
                    cause=self._spool.stream_event)
                self._last_canvas[name] = canvas
                self._plain_canvas[name] = composer.plain
            # One per encoder per slot, so a separate layout counts two for a
            # slot neither file took. Whether the encoder was slow or has
            # died is in `problems`, which is where the difference is.
            if not encoder.write(self._last_canvas[name], budget=budget):
                self.shed += 1
        # The slot this frame was written into, which is what its position was
        # computed from above and what a still taken from it carries.
        frame = self.slots
        self.slots += 1
        if recompose:
            self._offer_still(state, frame, position)

    def _offer_still(self, state: RunState, frame: int, position: float) -> None:
        """Give the current frame to the suite run's still picker.

        A suite boundary closes the set and writes it, so a suite that failed
        has its first and last frames beside its failing checks in the report
        without anybody opening the recording.

        `frame` and `position` are where this canvas went in the file, passed
        in rather than read back, so a still that is kept carries the position
        of the frame it is and not of the suite it belongs to.
        """
        key = state.stem
        if key != self._picking:
            self._write_stills(self._picking)
            self._picker = StillPicker()
            self._picking = key
            # Held beside the stem because the set is written when the next
            # suite run has already started, so the state has moved on by then.
            self._picking_identity = {"label": state.label, "suite": state.suite,
                                      "attempt": state.attempt}
        if not key:
            return
        name = self._still_pane()
        if not name:
            return
        ranked = (self._sources.frame[2] if self._sources.frame
                  else "\n".join(self._spool.rows or []).encode())
        # The frame without the stamp, the edge and the bar. A still is what
        # the machine showed, and this module's own text burned across it is
        # not evidence of anything.
        self._picker.offer(self._plain_canvas[name], self._spool.rows, ranked,
                           frame=frame, position=position)

    def _still_pane(self) -> str:
        """Which output file the stills of this run are frames of.

        One pane rather than all of them: the report inlines one still per
        moment, and a `separate` layout's two files are the same moment twice.
        The first by name, so the choice is the same for the same run.
        """
        return sorted(self._plain_canvas)[0] if self._plain_canvas else ""

    def _write_stills(self, stem: str) -> None:
        if not stem:
            return
        chosen = self._picker.stills()
        if not chosen:
            return
        pane = self._still_pane() or sorted(self.geometry)[0]
        geometry = self.geometry[pane]
        for entry in write_stills(os.path.join(self.directory, "capture"),
                                  stem, geometry.width, geometry.height,
                                  chosen, pane=output_name(pane)):
            # The suite run each still belongs to, so a reader joins a still to
            # a check without parsing its file name back apart.
            entry["stem"] = stem
            entry.update(self._picking_identity)
            entry["target"] = self.target.token
            self.stills.append(entry)

    def _maybe_rearm(self, now: float) -> None:
        """Ask for a stream again when it has gone quiet.

        Three suites stop or restart the device's video stream during a run,
        and two of them stop it when they finish, which leaves the device
        sending nothing. Without this the recording is a placeholder card from
        the first such suite to the end of the run, which is most of it.

        Never into a suite: while one is running the spool says the stream is
        that suite's, and the suite wins.
        """
        if not self._tail.state.between_suites:
            return
        for stream, last in (("video", self._sources.video_at),
                             ("audio", self._sources.audio_at)):
            if not getattr(self.options, stream):
                continue
            wait = self._rearm_wait[stream]
            if last and now - last < wait:
                continue
            if not last and now - (self.slots / max(1, self.options.fps)) < wait:
                continue
            # One PUT, one attempt, with a timeout well under a slot, so a
            # device that accepts a connection and never answers cannot stall
            # the loop that has to keep draining the sockets.
            self._arming.started.discard(stream)
            if self._arming.start(stream, timeout=REARM_TIMEOUT_SECONDS,
                                  retries=1):
                self.rearms[stream] += 1
                self._rearm_wait[stream] = REARM_AFTER_SECONDS
                # The device starts sending from wherever its counter is now,
                # which has nothing to do with where it was when it stopped.
                self._available[stream] = True
                self._mark_discontinuity(stream, RECORDER_REARM)
            else:
                # A device that is off the network is the case this runs into
                # most, and asking it every six seconds for ten minutes is a
                # hundred requests that all fail the same way.
                self._rearm_wait[stream] = min(
                    REARM_BACKOFF_CEILING_SECONDS, wait * 2)
                # Nothing is going to arrive, and what the file is then short
                # of is not something the device failed to deliver.
                self._available[stream] = False
            if self._cursor is not None:
                self._cursor.available = self._available["audio"]
            if stream == "video":
                self._sources.video_at = now
            else:
                self._sources.audio_at = now

    def _maybe_read_menu(self, now: float) -> None:
        """Read the menu only where no suite is reading one.

        Between suites there is no session and the overlay is all there is,
        which is why the condition is written this way. Under `--mode telnet` a
        poll made while a suite was running would put a screen in the pane that
        nobody was looking at.
        """
        if not self.options.menu or not self._tail.state.between_suites:
            return
        if self._spool.last_at and now - self._spool.last_at < MENU_QUIET_SECONDS:
            return
        floor = self.options.menu_min_interval_ms / 1000.0
        if floor and now - getattr(self, "_last_menu_at", 0.0) < floor:
            return
        self._last_menu_at = now
        try:
            body = self.api.machine.menu_screen(
                timeout=MENU_TIMEOUT_SECONDS, retries=1)
        except Exception:  # noqa: BLE001 - a read may not end a recording
            self.menu_failures += 1
            return
        self.menu_requests += 1
        if body is None:
            self._spool.rows = None
            self._spool.raw = b""
            self._spool.kind = "closed"
            return
        # Decoded from the payload this already holds. Asking the device for
        # the rows would be a second request for the screen it just answered
        # with, against a device that serves about four connections at once.
        self._spool.rows = self.api.machine.rows_of(body)
        self._spool.raw = body
        self._spool.kind = screen_spool.MENU
        self._spool.last_at = now

    # -- stage 6: what the run leaves behind --

    def overview_groups(self) -> List[Tuple[str, List[Tuple[str, str, bool]]]]:
        """What the opening card says, as three groups of labelled fields.

        In the order a viewer asks the questions in: which machine is this,
        what was it built from, and which run is this. The card carries every
        field the per-frame stamp has to truncate, which is why the stamp's
        own rows are truncated from the right rather than shortened.
        """
        planned = len(self._tail.poll().segments)
        device = [("target", self.identity.get("target", ""), True),
                  ("product", self.identity.get("firmware", ""), True),
                  ("address", self.identity.get("address", ""), False),
                  ("fpga", self.identity.get("fpga", ""), False)]
        source = [("branch", self.identity.get("branch", ""), True),
                  ("commit", self.identity.get("commit", ""), True),
                  ("tree", self.identity.get("dirty", ""), False)]
        run = [("started", format_wall(self.started_wall), False),
               ("build", self.identity.get("ci", ""), False),
               ("host", self.identity.get("host", ""), False),
               ("suite runs", str(planned) if planned else "", True)]
        groups = []
        for heading, fields in (("DEVICE", device), ("SOURCE", source),
                                ("RUN", run)):
            present = [field for field in fields if field[1]]
            if present:
                groups.append((heading, present))
        return groups

    def _write_cards_head(self, interval: float) -> None:
        """The opening overview, held for exactly OVERVIEW_SECONDS."""
        groups = self.overview_groups()
        self._hold(lambda name: self._composers[name].overview(groups),
                   OVERVIEW_SECONDS, interval)
        self.lead_in = self.slots * interval

    def _write_cards_tail(self) -> None:
        """The summary card: how it went, without opening the report."""
        state = self._tail.poll()
        counts: Dict[str, int] = {}
        for verdict in state.verdicts.values():
            counts[verdict] = counts.get(verdict, 0) + 1
        failed = sorted(name for name, verdict in state.verdicts.items()
                        if verdict == "FAIL")
        lines = ["E2E GATE RUN: " + ("FAIL" if failed else "OK"),
                 "  ".join(f"{verdict.lower()}={count}"
                           for verdict, count in sorted(counts.items()))]
        lines += [f"failed: {name}" for name in failed[:8]]
        self._hold(lambda name: self._composers[name].card(lines),
                   SUMMARY_SECONDS, 1.0 / max(1, self.options.fps))

    def _hold(self, render: Callable[[str], bytes], seconds: float,
              interval: float) -> None:
        """Hold one card for `seconds`, one frame per slot.

        Rounded rather than truncated: at 10 frames a second `2.0 / 0.1` is
        19.999999999999996 in binary floating point, so the card that was
        meant to be held for two seconds was held for 1.9.
        """
        for _ in range(max(1, int(round(seconds / interval)))):
            if self._audio_encoder is not None and self._cursor is not None:
                self._audio_encoder.write(self._cursor.take(), budget=interval)
            for name, encoder in self._encoders.items():
                encoder.write(render(name), budget=interval)
            self.slots += 1

    def audio_path(self) -> str:
        """Where the run's audio track is, until the finishing pass folds it in."""
        return self._audio_path

    def record(self) -> Dict[str, object]:
        """The recording's own health, for the `kind=capture` record.

        A file with thousands of padded frames or hundreds of re-arms is
        telling a reader that the run fought the recorder for the stream, which
        is worth knowing before drawing conclusions from what it shows.
        """
        found: Dict[str, object] = {
            "target": self.target.token,
            "files": sorted(self.files),
            "started": self.started_wall,
            "lead_in": self.lead_in,
            "fps": self.options.fps,
            # The canvas the recorder composed, and beside it the frame size
            # the file actually carries. --record-scale multiplies one and not
            # the other, so a reader comparing the record with ffprobe on the
            # file needs both to agree with what they see.
            "geometry": {name: f"{g.width}x{g.height}"
                         for name, g in sorted(self.geometry.items())},
            "output_geometry": {
                name: (f"{g.width * self.options.scale}"
                       f"x{g.height * self.options.scale}")
                for name, g in sorted(self.geometry.items())},
            "options": self.options.as_record(),
            "frames": self.slots,
            "frames_shed": self.shed,
            "frames_padded": self.padded,
            "frames_decimated": self.decimated,
            "rearms": dict(self.rearms),
            "menu_from_tap": self._spool.taken,
            "menu_requested": self.menu_requests,
            "menu_failed": self.menu_failures,
            "foreign_senders": sum(self.foreign.values()),
            # Why each stream's counters were started again, and how often.
            # A reader comparing this with the loss figures can tell a run
            # that competed for the stream from a link that dropped packets;
            # without it the two were one number.
            "stream_lifecycle": {
                name: {reason: count for reason, count in sorted(reasons.items())}
                for name, reasons in (("video", self._assembler.discontinuities),
                                      ("audio", self._audio.discontinuities))
                if reasons},
        }
        if self.stills:
            # In the order they were taken, each naming its suite run, its
            # kind, both its files and the frame of the recording it is. This
            # is the only place that frame exists: a suite record says when a
            # suite ran, not which of its frames was kept.
            found["stills"] = list(self.stills)
        if self.audio_rate:
            found["audio_rate"] = self.audio_rate
            found["timing"] = self.timing
        found.update(self._assembler.counts())
        found.update({f"audio_{name}": value
                      for name, value in self._audio.counts().items()})
        if self._cursor is not None:
            # Apart from the loss figures above: filled is the bytes a slot
            # was short because a packet had not arrived yet, which is jitter
            # rather than a stream that stopped; concealed is a stream that
            # should have been running and was not; unavailable is a stream
            # the run had stopped, which owes the file audio and owes it no
            # explanation about the network.
            found["audio_filled_bytes"] = self._cursor.filled_bytes
            found["audio_concealed_bytes"] = self._cursor.concealed_bytes
            found["audio_unavailable_bytes"] = self._cursor.unavailable_bytes
        if self.problems:
            found["problems"] = list(self.problems)
        return found


# The run's one audio track, encoded once and muxed into every video file.
# Removed by the finishing pass, so it is never an artefact of its own.
AUDIO_NAME = "audio.m4a"


def output_name(pane: str) -> str:
    """The file one pane's canvas is written to, per the run's one layout."""
    return {"combined": "video.mp4", "harness": "video-harness.mp4",
            "screen": "video-screen.mp4"}[pane]


# ---------------------------------------------------------------------------
# Stage 6: finding a test in the result
# ---------------------------------------------------------------------------
#
# Three ways, and a reader who has the report needs none of the others: the
# chapter list in the player, a grep of the subtitles, and the mm:ss the report
# prints beside every failing check.
#
# Both are generated after the run, from the finished JSONL, where every
# interval is known. That is what the live marking on the frames cannot do, and
# the two are complementary: the marking is what a reader sees while scrubbing,
# and a chapter is where a reader jumps to.


def position_of(when: float, started: float, lead_in: float) -> float:
    """Where a wall-clock moment is in the file.

    Every mm:ss anywhere in this design is a position in the file rather than
    an elapsed time in the run. The JSONL is where wall-clock time lives, the
    recording is where file positions live, and these two numbers convert
    between them.
    """
    return max(0.0, lead_in + (when - started))


# How long a cue is held on screen when the check it names was shorter. Below
# this a cue flashes past unreadably; above it, a run of quick checks would be
# one long cue naming the first of them.
MINIMUM_CUE_SECONDS = 0.5
MINIMUM_CUE_MS = int(MINIMUM_CUE_SECONDS * 1000)


def milliseconds(seconds: float) -> int:
    """A file position as the whole milliseconds an `.srt` field carries.

    The unit an `.srt` is written in, so every decision about a cue is made in
    it. Deciding in seconds and rounding at the end produced cues whose two
    fields were 0.2 ms apart and quantised to the same millisecond, which is a
    cue a player shows for no time at all.
    """
    return max(0, int(seconds * 1000.0 + 0.5))


def srt_stamp(total_ms: int) -> str:
    """One `.srt` timestamp field, from whole milliseconds."""
    whole, remainder = divmod(max(0, int(total_ms)), 1000)
    return (f"{whole // 3600:02d}:{whole % 3600 // 60:02d}:{whole % 60:02d}"
            f",{remainder:03d}")


def srt_time(seconds: float) -> str:
    return srt_stamp(milliseconds(seconds))


def cue_times(cues: Sequence[Tuple[float, float, str]]) -> List[Tuple[int, int]]:
    """The start and end each cue is written with, in whole milliseconds.

    Separate from the formatting because the property that matters is about
    the numbers a player parses rather than about the string: every cue ends
    strictly after it starts, and no cue overlaps the next.

    Two rules produce that, in this order:

    - A cue starts at least one millisecond after the cue before it. Checks
      measured in microseconds land several to a millisecond, and cues sharing
      one start cannot be given distinct non-overlapping intervals at all. The
      later cues of such a group are moved forward by a millisecond each,
      which is below one output frame at any usable frame rate.
    - A cue ends at its check's own end, extended to the minimum dwell where
      there is room, and never past the next cue's start. A check followed
      immediately by another is what a burst of sub-millisecond checks is, and
      it gets the millisecond between the two starts rather than nothing.

    `cues` is expected in start order, which `cues_and_chapters` sorts it into.
    """
    starts: List[int] = []
    previous = -1
    for start, _end, _text in cues:
        at = max(milliseconds(start), previous + 1)
        starts.append(at)
        previous = at
    times: List[Tuple[int, int]] = []
    for index, (_start, end, _text) in enumerate(cues):
        at = starts[index]
        until = max(milliseconds(end), at + MINIMUM_CUE_MS)
        if index + 1 < len(starts):
            until = min(until, starts[index + 1])
        times.append((at, max(until, at + 1)))
    return times


def subtitles(cues: Sequence[Tuple[float, float, str]]) -> str:
    """One `.srt`, which regenerates without touching the video.

    Never a burned-in overlay: a sidecar costs no re-encode and is plain text,
    so it can be read and searched without opening the video at all. Each cue
    carries the check's identity key and its verdict, in that order, so a grep
    for a suite name or for FAIL returns the timecodes to seek to.

    A player stacks two overlapping cues, so a dwell that ran into the
    following check would put two identity keys on screen at once and leave a
    reader unable to tell which one the frame belonged to. `cue_times` is what
    keeps them apart.
    """
    parts = []
    for index, ((start, end), cue) in enumerate(zip(cue_times(cues), cues),
                                                start=1):
        parts.append(f"{index}\n{srt_stamp(start)} --> "
                     f"{srt_stamp(end)}\n{cue[2]}\n")
    return "\n".join(parts)


def chapter_metadata(chapters: Sequence[Tuple[float, float, str]]) -> str:
    """An ffmpeg metadata file, for a stream copy that touches no frame."""
    lines = [";FFMETADATA1"]
    for start, end, title in chapters:
        lines += ["[CHAPTER]", "TIMEBASE=1/1000",
                  f"START={int(start * 1000)}",
                  f"END={int(max(end, start + 0.5) * 1000)}",
                  f"title={title}"]
    return "\n".join(lines) + "\n"


def finish_file(path: str, metadata: str, audio: str = "",
                binary: str = "ffmpeg") -> str:
    """Put the audio and the chapters into a finished file.

    One stream-copy pass, so the frames the encoder wrote are the frames in
    the result and nothing about the encode changes. It costs seconds rather
    than the length of the run, which is why the chapters are exact: they are
    generated from the finished JSONL, where every interval is known, rather
    than from what the recorder knew while it was recording.

    Returns "" or the reason it did not.
    """
    meta_path = path + ".chapters"
    temporary = path + ".finished.mp4"
    with_audio = bool(audio) and os.path.exists(audio)
    inputs = [path] + ([audio] if with_audio else []) + [meta_path]
    metadata_index = str(len(inputs) - 1)
    command = [binary, "-hide_banner", "-loglevel", "error", "-y"]
    for name in inputs:
        command += ["-i", name]
    command += ["-map", "0:v"]
    if with_audio:
        command += ["-map", "1:a"]
    command += ["-map_metadata", metadata_index,
                "-map_chapters", metadata_index,
                "-c", "copy", "-movflags", "+faststart", temporary]
    try:
        with open(meta_path, "w", encoding="utf-8") as handle:
            handle.write(metadata)
        completed = subprocess.run(command, capture_output=True, text=True,
                                   timeout=300)
    except (OSError, subprocess.TimeoutExpired) as exc:
        return f"the audio and the chapters could not be added: {exc}"
    finally:
        if os.path.exists(meta_path):
            os.remove(meta_path)
    if completed.returncode != 0 or not os.path.exists(temporary):
        if os.path.exists(temporary):
            os.remove(temporary)
        return ("the audio and the chapters could not be added: "
                + completed.stderr.strip()[:200])
    os.replace(temporary, path)
    return ""


@dataclass
class Still:
    """One chosen frame, and exactly where in the recording it came from.

    `frame` is the slot the recorder wrote that frame into and `position` is
    where that slot sits in the file, both taken when the frame was composed
    rather than derived afterwards from the suite's timing. Deriving it was
    what made a still's stated position wrong by up to 4.7 seconds: a suite
    record says when a suite ran, not which frame of it was kept.
    """

    kind: str
    canvas: bytes
    rows: List[str]
    frame: int
    position: float


class StillPicker:
    """Keep the frames of one suite run that are worth writing out.

    Online rather than over a stored sequence, because a suite run is minutes
    of frames and holding them all would cost more memory than the whole
    recording. It keeps the first, the last, and the largest changes so far,
    Ranked on the device's packed frame when there is one and on the harness
    screen when there is not, so a run with `--no-record-video` still gets the
    set the report inlines.
    """

    def __init__(self, limit: int = MAX_TRANSITIONS,
                 threshold: float = TRANSITION_THRESHOLD) -> None:
        self.limit = limit
        self.threshold = threshold
        self.first: Optional[Still] = None
        self.last: Optional[Still] = None
        self.changes: List[Tuple[int, Still]] = []
        self._previous: Optional[bytes] = None
        self._index = 0

    def offer(self, canvas: bytes, rows: Optional[Sequence[str]],
              ranked_on: bytes, frame: int = 0, position: float = 0.0) -> None:
        """One composed frame, with what it was composed from and where it is.

        `frame` and `position` describe the slot this canvas was written into,
        so the still that comes out of the picker carries the position of the
        frame it actually is rather than of the suite it belongs to.
        """
        text = [str(row) for row in (rows or [])]
        candidate = Still("", canvas, text, frame, position)
        if self.first is None:
            self.first = candidate
        else:
            self.last = candidate
        if self._previous is not None and ranked_on:
            changed = difference(self._previous, ranked_on)
            sampled = max(1, len(ranked_on) // SAMPLE_STRIDE)
            if changed / sampled >= self.threshold:
                self.changes.append((changed, candidate))
                # Largest first, earlier frame on a tie, so the set is the same
                # for the same run.
                self.changes.sort(key=lambda item: (-item[0], item[1].frame))
                del self.changes[self.limit:]
        self._previous = ranked_on
        self._index += 1

    def stills(self) -> List[Still]:
        """The chosen stills, in capture order, each naming its own kind."""
        # By the frame each was taken at, so the set reads in capture order
        # whatever order the transitions were ranked in. The second key keeps
        # first before a transition and a transition before last when a suite
        # run was short enough that two of them fall on one frame.
        found: List[Tuple[int, int, str, Still]] = []
        if self.first is not None:
            found.append((self.first.frame, 0, "first", self.first))
        for _changed, still in self.changes:
            found.append((still.frame, 1, "change", still))
        if self.last is not None:
            found.append((self.last.frame, 2, "last", self.last))
        found.sort(key=lambda item: (item[0], item[1]))
        return [Still(kind, still.canvas, still.rows, still.frame,
                      still.position)
                for _frame, _rank, kind, still in found]


def write_stills(directory: str, stem: str, width: int, height: int,
                 chosen: Sequence[Still], pane: str = "") -> List[dict]:
    """Write each still as a pair of files sharing one name.

    The pair exists because the two readers need different things: the image
    is what a person opens, and the text is what the report inlines and what a
    program or an agent can match on. A still taken when no menu was open
    writes a text file saying so rather than omitting one, so the pair is never
    half present.

    Never stamped: a still is evidence of what was on a screen, and a caption
    drawn over the border is a caption drawn over evidence.

    Returns one entry per still, naming both files, the pane the canvas was
    composed for and the frame it was taken at. That entry is what reaches the
    `capture` record, so the report reads the position rather than inferring
    one, and an entry is returned only for a still whose text file was written.
    """
    try:
        from PIL import Image
    except ImportError:
        Image = None
    written: List[dict] = []
    try:
        os.makedirs(directory, exist_ok=True)
    except OSError:
        return written
    for index, still in enumerate(chosen, start=1):
        name = f"{stem}-{index}-{still.kind}"
        text_path = os.path.join(directory, name + ".txt")
        try:
            with open(text_path, "w", encoding="utf-8") as handle:
                handle.write("\n".join(still.rows) if still.rows
                             else NO_MENU_OPEN)
                handle.write("\n")
        except OSError:
            continue
        entry = {"index": index, "kind": still.kind, "text": name + ".txt",
                 "frame": still.frame, "position": round(still.position, 4)}
        if pane:
            # Which output file the frame and the position are positions in.
            # A separate layout writes two files and a still is composed for
            # one of them.
            entry["pane"] = pane
        if Image is not None:
            try:
                image = Image.frombytes("RGB", (width, height), still.canvas)
                image.save(os.path.join(directory, name + ".png"))
                entry["image"] = name + ".png"
            except (OSError, ValueError):
                pass
        written.append(entry)
    return written


def records_in(directory: str) -> List[Tuple[str, dict]]:
    """Every record a target's directory holds, with the file it came from."""
    found: List[Tuple[str, dict]] = []
    try:
        names = sorted(name for name in os.listdir(directory)
                       if name.endswith(".jsonl"))
    except OSError:
        return found
    for name in names:
        try:
            with open(os.path.join(directory, name), encoding="utf-8",
                      errors="replace") as handle:
                for line in handle:
                    if not line.strip():
                        continue
                    try:
                        record = json.loads(line)
                    except ValueError:
                        continue
                    if isinstance(record, dict):
                        found.append((name, record))
        except OSError:
            continue
    return found


def cues_and_chapters(directory: str, target: str, started: float,
                      lead_in: float) -> Tuple[List, List]:
    """The subtitle cues and the chapter marks for a finished run.

    Generated after the run, from the same JSONL, where every interval is
    known. That is what the live marking on the frames cannot do: a failing
    check's chapter starts at the start of that check rather than when the
    recorder learned about it.

    Every cue and every chapter is titled with the identity key, so a reader
    who has the report open and a reader who has the player open are looking at
    the same strings.
    """
    cues: List[Tuple[float, float, str]] = []
    chapters: List[Tuple[float, float, str]] = []
    for name, record in records_in(directory):
        if name == "run.jsonl":
            if record.get("kind") == "suite":
                label = str(record.get("mode") or "")
                key = (f"{target}/{label}/{record.get('name')}/"
                       f"{record.get('attempt', 1)}")
                end = position_of(float(record.get("time") or 0.0), started,
                                  lead_in)
                start = end - float(record.get("seconds") or 0.0)
                chapters.append((max(0.0, start), end,
                                 f"{key} {record.get('verdict')}"))
                if record.get("recoveries"):
                    chapters.append((max(0.0, start), end, "recovery"))
            continue
        if record.get("kind") != "check":
            continue
        stem = name[:-len(".jsonl")]
        suite = str(record.get("suite") or "")
        label = stem[:-(len(suite) + 1)] if stem.endswith("-" + suite) else ""
        key = (f"{target}/{label}/{suite}/{record.get('attempt', 1)}/"
               f"{record.get('index')}")
        end = position_of(float(record.get("time") or 0.0), started, lead_in)
        start = end - float(record.get("seconds") or 0.0)
        verdict = str(record.get("verdict") or "")
        cues.append((max(0.0, start), end, f"{key} {verdict}"))
        if verdict == "FAIL":
            chapters.append((max(0.0, start), end,
                             f"{key} {record.get('label')}"))
    cues.sort()
    chapters.sort()
    return cues, chapters


def finish(directory: str, target: str, started: float, lead_in: float,
           files: Sequence[str], audio: str = "",
           binary: str = "ffmpeg") -> List[str]:
    """Write the subtitles and put the chapters in. Returns what went wrong.

    One sidecar per video file, sharing its stem: players load a sidecar by
    matching the video's name, so a single `video.srt` would be found by
    neither of the files a separate layout writes. They are byte identical,
    generated once and written N times, which is a copy of a derived artefact
    rather than a second authored one.
    """
    problems: List[str] = []
    cues, chapters = cues_and_chapters(directory, target, started, lead_in)
    text = subtitles(cues)
    metadata = chapter_metadata(chapters)
    for name in files:
        path = os.path.join(directory, name)
        if not os.path.exists(path):
            continue
        try:
            with open(path[:-len(".mp4")] + ".srt", "w",
                      encoding="utf-8") as handle:
                handle.write(text)
        except OSError as exc:
            problems.append(f"the subtitles could not be written: {exc}")
        problem = finish_file(path, metadata, audio=audio, binary=binary)
        if problem:
            problems.append(problem)
    if audio and os.path.exists(audio):
        # Never an artefact of its own: it is inside every video file now.
        try:
            os.remove(audio)
        except OSError:
            pass
    return problems
