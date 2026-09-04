#!/usr/bin/env python3
# The case registry and the helpers every observability tier shares.

"""What the four tier modules are written against.

observability_test.py was 7,796 lines in one module: 175 cases across four
independent tiers, 43 helpers, 250 function-local imports and the runner, so
every contributor to the harness edited one file and every change to it
conflicted with every other. The tiers were already independent - `TIERS` names
them and `run_cases` runs them in order - so they are four modules now, and
this holds what they share: `Case`, the `@case` decorator that registers into
`CASES`, `Skipped`, and the helpers the cases call.

Importing a tier module is what puts its cases in `CASES`. The entry point,
tests/lib/observability_test.py, imports all four.
"""

from __future__ import annotations

import contextlib
import fcntl
import json
import os
import re
import socket
import sys
import tempfile
import threading
import time
# Imported here for its side effect, not for a name. dataclasses reads
# sys.modules.get('typing') and then touches typing.ClassVar, so a worker
# applying @dataclass while another worker is part-way through the first
# `import typing` (tests/lib/wait.py has the only one left on the pure
# tier's path) finds the module present but that name not yet bound, and
# fails with "partially initialized module 'typing'". Loading it before
# the pool starts leaves no first import for two threads to race on.
import typing  # noqa: F401
from contextlib import contextmanager
from dataclasses import dataclass
from collections.abc import Callable, Sequence
from pathlib import Path

# The one stanza that puts the shared library on sys.path; see tests/lib/bootstrap.py.
sys.path.insert(0, str(next(p for p in Path(__file__).resolve().parents
                            if (p / "tests" / "lib").is_dir()) / "tests" / "lib"))
import bootstrap  # noqa: E402,F401
# The stream library, the recorder and the screen spool are shared E2E support
# rather than shared library code, so they live beside the suites that use them.

import report  # noqa: E402
import targets  # noqa: E402
from device_double import DeviceDouble  # noqa: E402
from report import Failure  # noqa: E402
from selftest import expect  # noqa: E402

# The checkout root. This module sits at tests/lib/observability/, so it is
# four levels up rather than the three it was when everything lived in
# tests/lib/observability_test.py. bootstrap knows where tests/ is, which
# is the same answer without counting.
ROOT = os.path.dirname(bootstrap.TESTS)
RUNNER_PATH = os.path.join(ROOT, "run-tests")
REPORT_TOOL = os.path.join(ROOT, "tools", "e2e_report.py")

# tests/lib/fixtures, which is where e2e-run.expected.md is checked in;
# this module moved into tests/lib/observability and the fixtures did not.
FIXTURES = os.path.join(bootstrap.LIB, "fixtures")
# A reduced real run, written by the runner rather than by hand, so a
# hand-written tree cannot diverge from what the runner actually writes. Built
# fresh into a scratch directory the first time a golden case needs it rather
# than checked in: nothing under fixtures/ but the document itself is a
# generated artefact of a real run, and generated artefacts, including the
# binary ones a recording would add, do not belong in git. `EXPECTED` is the
# one thing that is checked in, because it is what a reviewer reads to see a
# rendering change.
EXPECTED = os.path.join(FIXTURES, "e2e-run.expected.md")
FIXTURE: str | None = None
_FIXTURE_PROBLEM: str | None = None
# Guards building FIXTURE: several golden cases call require_fixture()
# concurrently, and only the first should pay for the real scripted run that
# builds it - the rest just wait and then read what it built.
_FIXTURE_LOCK = threading.Lock()

# The port overrides a case must not inherit from whoever ran it. Two cases
# assert the built-in defaults, and a developer who has exported one of these
# for a device of their own would otherwise turn this suite red.
PORT_VARIABLES = (targets.REST_PORT_ENV, targets.FTP_PORT_ENV,
                  targets.TELNET_PORT_ENV, targets.DMA_PORT_ENV)

TIERS = ((1, "pure"), (2, "component"), (3, "pipeline"), (4, "golden"))




class Skipped(Exception):
    """A case that cannot run here. The message is the reason it reports."""


@dataclass(frozen=True)
class Case:
    tier: int
    label: str
    requirements: tuple[str, ...]
    run: Callable[[], object]
    exclusive: bool = False


CASES: list[Case] = []


def case(tier: int, *requirements: str, label: str = "", exclusive: bool = False):
    """Register one case, naming the requirements it holds.

    The requirement numbers are on the case rather than only in a docstring so
    the registry check of OBS-16.7 can read them, and so a reviewer can read a
    list of requirement numbers with a test each instead of reading the
    specification against the code.

    `exclusive=True` marks a case that must not run at the same time as any
    other case's own reporting, or that touches state outside its own tempdir
    in a way another case running at the same time could observe. That covers
    a case that mutates process-global state directly (an os.environ entry,
    a module-level path like interactions.LOG_PATH), one that calls report.py
    itself - directly (report.detail(), for a coverage note) or by calling a
    run-tests function in-process that does (report.warn(), for instance) -
    instead of only returning its one-line summary, and one that edits a real
    file this checkout tracks rather than one under tempfile.TemporaryDirectory.
    Cases run concurrently by default; an exclusive one runs on the main
    thread instead, in its normal place in registration order, so it is never
    running at the same time as anything else that touches that same state.
    """

    def register(function: Callable[[], object]) -> Callable[[], object]:
        CASES.append(Case(tier, label or function.__name__.replace("_", " "),
                          tuple(requirements), function, exclusive))
        return function

    return register


def load_runner():
    """Import run-tests as a module. It has no .py suffix, so it needs a loader."""
    import importlib.machinery
    import importlib.util

    loader = importlib.machinery.SourceFileLoader("run_tests_observability",
                                                  RUNNER_PATH)
    spec = importlib.util.spec_from_loader("run_tests_observability", loader)
    module = importlib.util.module_from_spec(spec)
    sys.modules["run_tests_observability"] = module
    loader.exec_module(module)
    return module


def load_report_tool():
    """Import the report generator from tools/ by path, as its tests must."""
    import importlib.machinery
    import importlib.util

    loader = importlib.machinery.SourceFileLoader("e2e_report", REPORT_TOOL)
    spec = importlib.util.spec_from_loader("e2e_report", loader)
    module = importlib.util.module_from_spec(spec)
    # Registered before it runs, because a dataclass declared in it looks its
    # own module up by name while the class body is being processed.
    sys.modules["e2e_report"] = module
    loader.exec_module(module)
    return module


@contextmanager
def without_port_overrides():
    """Run a block with every port override out of the environment."""
    saved = {name: os.environ.pop(name, None) for name in PORT_VARIABLES}
    try:
        yield
    finally:
        for name, value in saved.items():
            if value is not None:
                os.environ[name] = value


def parse_srt(text: str) -> list[tuple[int, int, str]]:
    """One `.srt` as (start, end, caption) with both times in milliseconds.

    Parsed from the generated file rather than taken from the numbers that
    produced it: the property under test is about what a player reads, and a
    pair of floats that a player would round into one millisecond is exactly
    the defect these cases exist to catch.
    """
    found: list[tuple[int, int, str]] = []

    def stamp(field: str) -> int:
        clock, _, millis = field.strip().partition(",")
        hours, minutes, seconds = (int(part) for part in clock.split(":"))
        return ((hours * 3600 + minutes * 60 + seconds) * 1000) + int(millis)

    for block in text.strip().split("\n\n"):
        lines = block.splitlines()
        if len(lines) < 3:
            raise Failure(f"a cue is not three lines: {block!r}")
        start, separator, end = lines[1].partition(" --> ")
        if not separator:
            raise Failure(f"a cue has no timing line: {lines[1]!r}")
        found.append((stamp(start), stamp(end), "\n".join(lines[2:])))
    return found


def free_udp_port() -> int:
    """A UDP port nothing holds, for a test that needs a specific number."""

    holder = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    holder.bind(("0.0.0.0", 0))
    port = holder.getsockname()[1]
    holder.close()
    return port


# Every variable the runner exports that a scripted run must not inherit.
# Derived from what the runner actually exports rather than listed by hand:
# see `no_runner_variable_escapes_the_scrubbing_list` for why.
INHERITED_VARIABLES = {
    "E2E_ATTEMPT", "E2E_INTERACTIONS", "E2E_JSONL", "E2E_SCREENS", "E2E_SUITE",
    "E2E_SYSLOG_OWNED", "E2E_SYSLOG_PORT", "E2E_SYSLOG_PORTS", "E2E_TARGET",
    # Not a runner variable but a suite one: --assume-fix reaches every suite
    # through it, so a scripted run would otherwise inherit the gate's
    # assumptions.
    "E2E_ASSUME_FIX",
}


def runner_variables() -> set:
    """Every `E2E_` variable name the runner's own source mentions."""

    with open(RUNNER_PATH, encoding="utf-8") as handle:
        source = handle.read()
    return set(re.findall(r'"(E2E_[A-Z0-9_]+)"', source))


# The variables a scripted run may keep, with the reason each is harmless.
# Anything not here is scrubbed, so a new one is scrubbed by default and a
# deliberate exception has to be written down.
KEPT_VARIABLES: set = set()


@contextlib.contextmanager
def exclusive(name: str):
    """Hold a lock shared by every copy of this suite on this machine.

    The runner gives each target its own process, so three targets run three
    copies of this suite at the same time on one host. A case that measures
    something about the host, rather than about the device, cannot share it:
    the frame-exact recorder cases lose datagrams and drop frames under the
    other two copies' load, and the harness-hash case has one checkout to edit
    between them. Taking turns costs seconds and makes them mean something.
    """
    path = os.path.join(tempfile.gettempdir(), f"e2e-obs-{name}.lock")
    with open(path, "w", encoding="utf-8") as handle:
        fcntl.flock(handle, fcntl.LOCK_EX)
        try:
            yield
        finally:
            fcntl.flock(handle, fcntl.LOCK_UN)


def _harness_hash_edit(runner) -> str:
    """Hash, edit, hash, restore, hash. Called with the edit lock held."""
    import shutil
    import tempfile

    # Edit a tracked file the runner reads, and put it back. Appending a
    # comment is enough: what the field detects is a change in content, not a
    # change in which files are modified.
    victim = os.path.join(ROOT, "tests", "lib", "report.py")
    # A run killed between the append and the restore leaves the marker
    # behind, and two of them were committed that way. Taken out here, before
    # the first hash, so the case starts from the tree git has rather than
    # from one a dead run left.
    removed = _remove_edit_marker(victim)

    before = runner.harness_hash()
    if not before:
        raise Skipped("git does not answer in this checkout")
    expect("the same tree hashes the same twice", runner.harness_hash(), before)
    with tempfile.TemporaryDirectory() as scratch:
        keep = os.path.join(scratch, "report.py")
        shutil.copy2(victim, keep)
        try:
            with open(victim, "a", encoding="utf-8") as handle:
                handle.write(_EDIT_MARKER)
            after = runner.harness_hash()
            if after == before:
                raise Failure("an edited harness file hashed the same, so a "
                              "run edited under itself would report nothing")
            # And `git status --porcelain` says the same thing before and
            # after, which is why it cannot stand in for this.
            dirty = runner.git_answer("status", "--porcelain")
            if dirty is not None and victim.replace(ROOT, "") not in "".join(
                    line[3:] for line in dirty.splitlines()):
                pass  # the file may have been modified already, which is the point
        finally:
            shutil.copy2(keep, victim)
    expect("and the restored tree hashes as it did", runner.harness_hash(),
           before)
    left = f", {removed} stale marker(s) cleaned first" if removed else ""
    return f"{before} changed and came back{left}"


_EDIT_MARKER = "\n# written by a test, removed immediately\n"


def _remove_edit_marker(path: str) -> int:
    """Strip any marker a killed run left in `path`. How many it took out."""
    with open(path, encoding="utf-8") as handle:
        text = handle.read()
    count = text.count(_EDIT_MARKER)
    if count:
        with open(path, "w", encoding="utf-8") as handle:
            handle.write(text.replace(_EDIT_MARKER, ""))
    return count


@contextmanager
def interaction_log(directory):
    """Point the interaction log at a file of its own for one block."""
    import interactions

    previous_path = interactions.LOG_PATH
    previous_environment = os.environ.get(interactions.LOG_ENV)
    path = os.path.join(directory, "interactions.jsonl")
    interactions.set_path(path)
    os.environ[interactions.LOG_ENV] = path
    try:
        yield path
    finally:
        interactions.flush()
        interactions.set_path(previous_path)
        if previous_environment is None:
            os.environ.pop(interactions.LOG_ENV, None)
        else:
            os.environ[interactions.LOG_ENV] = previous_environment


def logged_interactions(path):
    import interactions

    interactions.flush()
    if not os.path.exists(path):
        return []
    with open(path, encoding="utf-8") as handle:
        return [json.loads(line) for line in handle if line.strip()]


def vic_frame(rows, width=384, height=272, background=6, foreground=14,
              scroll=0, columns=40, border=None):
    """One VIC frame of colour indices showing `rows`, as the hardware draws it.

    Rendered from the character ROM through `glyphs.rom_rows_for_index`, which
    is the same data the decoder matches against and is production code, so the
    only thing this restates is the pixel layout of a frame.

    `scroll` is `$D016`'s fine scroll, which moves the character grid 0 to 7
    pixels to the right of the centred origin. `columns` is `$D016` bit 3: at
    38 the VIC blanks one cell at each side of the window, over a grid that
    does not move, which is what a frame taken while the KERNAL scrolls the
    screen looks like.

    `border` is the colour outside the display window. Left unset the whole
    frame is the background colour, which is the simplest frame that carries
    the text. Set to a colour of its own the frame has the shape the hardware
    sends: a border, a window inside it, and ink clipped at the window edge, so
    a grid origin that reaches past the window meets border pixels rather than
    running off a frame that is background everywhere.
    """
    import glyphs
    import vic_text

    pixels = bytearray([background if border is None else border]) * (
        width * height)
    window, top = vic_text.picture_origin(width, height)
    if columns == 38:
        window_left = window + glyphs.GLYPH_WIDTH
        window_right = window + vic_text.TEXT_WIDTH - glyphs.GLYPH_WIDTH
    else:
        window_left, window_right = window, window + vic_text.TEXT_WIDTH
    if border is not None:
        for y in range(top, top + vic_text.TEXT_HEIGHT):
            for x in range(window_left, window_right):
                pixels[y * width + x] = background
    left = window + scroll
    for row, text in enumerate(rows[:vic_text.ROWS]):
        for column, character in enumerate(text[:vic_text.COLUMNS]):
            index = glyphs.screen_code_for(character)
            shape = glyphs.rom_rows_for_index(
                index if index != glyphs.NO_GLYPH else
                glyphs.screen_code_for(" "))
            for line in range(glyphs.GLYPH_HEIGHT):
                bits = shape[line]
                base = ((top + row * glyphs.GLYPH_HEIGHT + line) * width
                        + left + column * glyphs.GLYPH_WIDTH)
                for bit in range(glyphs.GLYPH_WIDTH):
                    if bits & (0x80 >> bit):
                        at = base + bit
                        x = left + column * glyphs.GLYPH_WIDTH + bit
                        if x < width and (border is None
                                          or window_left <= x < window_right):
                            pixels[at] = foreground
    if columns == 38 and border is None:
        blanked = list(range(window, window + glyphs.GLYPH_WIDTH))
        blanked += list(range(window + vic_text.TEXT_WIDTH - glyphs.GLYPH_WIDTH,
                              window + vic_text.TEXT_WIDTH))
        for y in range(height):
            for x in blanked:
                pixels[y * width + x] = background
    return bytes(pixels)


def screen_text_of(image, geometry):
    """The C64 screen a still carries, read back out of the written PNG.

    The screen pane is blitted into the canvas one pixel per pixel, so mapping
    the pane's colours back to VIC indices gives the decoder exactly what the
    device sent, after a round trip through the composer, the encoder and the
    file.
    """
    import glyphs
    import vic_text

    if geometry.screen_x < 0:
        return None
    width, height = 384, 272
    pane = image.crop((geometry.screen_x, 0, geometry.screen_x + width,
                       height)).convert("RGB")
    index_of = {glyphs.c64_rgb(index): index for index in range(16)}
    pixels = bytes(index_of.get(colour, 0) for colour in pane.getdata())
    return vic_text.decode(pixels, width, height)


def packets_of(frame, sequence=0):
    """One frame's datagrams, so a case can count them rather than state 68."""
    from device_double import video_packets

    return video_packets(frame, sequence)


def video_stream(assembler, first_frame, count, sequence=0, gap_frames=0):
    """Push `count` consecutive frames and return where the counters got to."""
    from device_double import video_packets

    packets_per_frame = 0
    for offset in range(count):
        packets = video_packets(first_frame + offset * (1 + gap_frames),
                                sequence)
        packets_per_frame = len(packets)
        sequence += len(packets)
        for packet in packets:
            assembler.push(packet)
    return sequence, packets_per_frame


def composed_pair(rows, kind="menu", state=None, raw=b"", identity=None,
                  layout="combined", screen_colour=6):
    """One composed frame and the same frame without its annotations.

    Both come out of one `compose` call, which is where the recorder writes
    the still from, so a case comparing the two is comparing what the file
    holds against what the still holds. The device pane is one flat colour so
    that a case looking for a colour the annotations use finds only them; blue
    by default, which is neither of the two ranks and not the chrome.
    """
    import recorder as recorder_lib

    geometry = recorder_lib.geometry_for(True, True, layout)[layout]
    composer = recorder_lib.Composer(geometry, recorder_lib.Options(),
                                     identity or {"target": "u64"})
    frame = (384, 272, bytes([screen_colour]) * (384 * 272))
    stamped = composer.compose(frame, rows, kind,
                               state or recorder_lib.RunState(), 61.0,
                               1786700000.0, harness_raw=raw)
    return geometry, composer, stamped, composer.plain


def glyph_columns(rgb: bytes, width: int, row: int, background=(0, 0, 0)):
    """Which 8-pixel columns of one glyph row hold anything but `background`."""
    import recorder as recorder_lib

    found = []
    top = row * recorder_lib.glyphs.GLYPH_HEIGHT
    for column in range(width // recorder_lib.glyphs.GLYPH_WIDTH):
        left = column * recorder_lib.glyphs.GLYPH_WIDTH
        for y in range(top, top + recorder_lib.glyphs.GLYPH_HEIGHT):
            for x in range(left, left + recorder_lib.glyphs.GLYPH_WIDTH):
                at = (y * width + x) * 3
                if tuple(rgb[at:at + 3]) != background:
                    found.append(column)
                    break
            else:
                continue
            break
    return found


def occupied_span(rgb: bytes, width: int, y: int, background):
    """The first and last pixel on scanline `y` that is not `background`."""
    found = [x for x in range(width)
             if tuple(rgb[((y * width) + x) * 3:((y * width) + x) * 3 + 3])
             != background]
    return (found[0], found[-1]) if found else (None, None)


def glyph_rows(rgb: bytes, width: int, height: int, background):
    """Which glyph rows of a canvas hold anything but `background`, and where."""
    import recorder as recorder_lib

    found = {}
    for row in range(height // recorder_lib.glyphs.GLYPH_HEIGHT):
        columns = glyph_columns(rgb, width, row, background=background)
        if columns:
            found[row] = (min(columns), max(columns))
    return found


# The double answers in microseconds, so the pacing the suites use against real
# hardware is all wait and no information here. Every one of these is a
# documented override in tests/lib/pacing.py, and a run that used the hardware
# values would spend most of this suite's budget waiting for a screen that had
# already settled.
SCRIPTED_PACING = {
    "U64_UI_POLL_INTERVAL": "0.005",
    "U64_UI_SETTLE_TIMEOUT": "0.5",
    "U64_UI_KEY_CHANGE_TIMEOUT": "0.1",
    "U64_UI_OVERLAY_DRAW_TIMEOUT": "0.2",
    "U64_UI_KEY_SETTLE": "0.005",
    "U64_UI_KEY_DRAIN": "0.002",
    "U64_UI_MENU_TOGGLE_SETTLE": "0.02",
    "U64_UI_MENU_TOGGLE_TIMEOUT": "0.5",
    # There is no C64 behind the double, so the runner's post-reset wait for
    # one to finish its cold start has nothing to wait for.
    "U64_UI_RESET_SETTLE": "0.005",
}


WRAPPER = '''\
"""Run the real `run-tests` over a scripted registry against the double."""
import importlib.machinery
import importlib.util
import json
import os
import sys

loader = importlib.machinery.SourceFileLoader("run_tests_scripted",
                                              os.environ["OBS_RUNNER"])
spec = importlib.util.spec_from_loader("run_tests_scripted", loader)
runner = importlib.util.module_from_spec(spec)
loader.exec_module(runner)

with open(os.environ["OBS_REGISTRY"], encoding="utf-8") as handle:
    # The shallowest profile, so a scripted registry is never filtered by the
    # profile the fixture happens to run under. These stubs stand in for the
    # whole tree; which of them run is the fixture's business, not a bundle's.
    runner.SUITES = tuple(
        runner.Suite(**dict(entry, profile=runner.profiles.SMOKE))
        for entry in json.load(handle))

# The double serves REST, FTP, Telnet and the DMA control port. It does not
# fake the on-device UI object stack, which is what this gate drives.
runner.ui_state_gate = lambda action, options, label="", quiet=False, attempt=None: True

_child_command = runner.child_command


def child_command(args, target, output_dir):
    """The real command, with this wrapper in place of the runner's own path."""
    command = _child_command(args, target, output_dir)
    command[1] = os.path.abspath(__file__)
    return command


runner.child_command = child_command

try:
    status = runner.main(sys.argv[1:])
finally:
    runner.stop_console_capture()
sys.exit(status)
'''


STUB_PREAMBLE = '''\
"""One scripted suite, standing in for a real one."""
import argparse
import os
import sys
import threading
import time

sys.path.insert(0, {library!r})
sys.path.insert(0, {e2e_library!r})
import report

_parser = argparse.ArgumentParser()
_parser.add_argument("-H", "--host", default="")
_parser.add_argument("-p", "--password", default="")
_parser.add_argument("--mode", default="")
ARGS, _rest = _parser.parse_known_args()
'''


@dataclass(frozen=True)
class Stub:
    """One suite in the scripted registry, and what it does when it runs."""

    name: str
    body: str = "report.check_start('it held'); report.check_ok('20 rows')\n"
    category: str = "e2e"
    args: str = "-H @HOST@ -p @PASS@ --mode @MODE@"
    manual: bool = False
    # Registered but absent from disk, which is how run_suite reports SKIP.
    missing: bool = False


@dataclass
class ScriptedRun:
    """The tree one scripted run wrote, and the status it exited with."""

    directory: str
    status: int
    stdout: str

    def path(self, *parts: str) -> str:
        return os.path.join(self.directory, *parts)

    def records(self, *parts: str) -> list[dict]:
        import json

        found = []
        with open(self.path(*parts), encoding="utf-8") as handle:
            for line in handle:
                if line.strip():
                    try:
                        found.append(json.loads(line))
                    except ValueError:
                        pass
        return found

    def tree(self) -> list[str]:
        """Every file under the output directory, by relative path, sorted."""
        found = []
        for base, _dirs, files in os.walk(self.directory):
            for name in files:
                found.append(os.path.relpath(os.path.join(base, name),
                                             self.directory))
        return sorted(found)


def scripted_run(double: DeviceDouble, stubs: Sequence[Stub],
                 tokens: Sequence[str] = ("127.0.0.1",),
                 arguments: Sequence[str] = (),
                 workspace: str = "",
                 extra_environment: dict | None = None) -> ScriptedRun:
    """Drive the real runner over `stubs` against `double`, and return the tree."""
    import json
    import subprocess

    # The wrapper answers the UI-state gate as satisfied, so the double agrees
    # with it: between suites no menu is open, and 404 on this endpoint is
    # what that state looks like.
    double.faults.menu_screen_404 = True

    # The scripted suites import report and the rest of the library, which is
    # tests/lib and not this module's own directory: support.py sits in
    # tests/lib/observability.
    library = bootstrap.LIB
    e2e_library = bootstrap.E2E_LIB
    suites = os.path.join(workspace, "suites")
    os.makedirs(suites, exist_ok=True)
    registry = []
    for stub in stubs:
        path = os.path.join(suites, f"{stub.name.replace('-', '_')}.py")
        if not stub.missing:
            with open(path, "w", encoding="utf-8") as handle:
                handle.write(STUB_PREAMBLE.format(
                    library=library, e2e_library=e2e_library) + stub.body)
        registry.append({"category": stub.category, "name": stub.name,
                         "path": path, "args": stub.args, "manual": stub.manual})
    registry_path = os.path.join(workspace, "registry.json")
    with open(registry_path, "w", encoding="utf-8") as handle:
        json.dump(registry, handle)
    wrapper = os.path.join(workspace, "wrapper.py")
    with open(wrapper, "w", encoding="utf-8") as handle:
        handle.write(WRAPPER)

    output = os.path.join(workspace, "run")
    environment = dict(os.environ, OBS_RUNNER=RUNNER_PATH,
                       OBS_REGISTRY=registry_path, OBS_WORKSPACE=workspace,
                       NO_COLOR="1")
    environment.update(double.environment())
    environment.update(SCRIPTED_PACING)
    environment.update(extra_environment or {})
    environment.pop("FORCE_COLOR", None)
    # A scripted run is a run of its own, not a continuation of whatever
    # started this process. When this suite runs inside the gate it inherits
    # the gate's own state, and every one of these means something to the
    # runner: E2E_SYSLOG_OWNED tells it another process already holds the
    # collector's port, so it starts none and writes no `log` record, and
    # three cases about the device log then fail for a reason that has nothing
    # to do with them. Measured under `run-tests u64 u2@c64u c64u`, where every
    # target's copy of this suite failed the same three.
    for inherited in sorted(INHERITED_VARIABLES | {"GITHUB_STEP_SUMMARY"}):
        environment.pop(inherited, None)
    completed = subprocess.run(
        [sys.executable, wrapper, "-o", output, *arguments, *tokens],
        env=environment, capture_output=True, text=True, check=False)
    return ScriptedRun(output, completed.returncode,
                       completed.stdout + completed.stderr)


def records_from_a_stub_suite(environment: dict, body: str = "") -> list[dict]:
    """Run one throwaway suite in a child process and read what it recorded.

    A child rather than a reload: report.py reads its environment at import,
    and reloading it inside a running suite would reset the check counter this
    suite is itself numbering with.
    """
    import json
    import subprocess
    import tempfile

    with tempfile.TemporaryDirectory() as directory:
        path = os.path.join(directory, "records.jsonl")
        script = (
            "import sys\n"
            # bootstrap.LIB, not this file's directory: this module used to live in
            # tests/lib and now lives one below it, and the child needs the
            # library rather than the tier package.
            f"sys.path.insert(0, {bootstrap.LIB!r})\n"
            "import report\n"
            "report.check_start('a check'); report.check_ok('20 rows')\n"
            + body)
        child = dict(os.environ, E2E_JSONL=path)
        child.pop("E2E_TARGET", None)
        child.pop("E2E_ATTEMPT", None)
        child.pop("E2E_SUITE", None)
        child.update(environment)
        completed = subprocess.run([sys.executable, "-c", script], env=child,
                                   capture_output=True, text=True, check=False)
        if completed.returncode != 0:
            raise Failure(f"the stub suite exited {completed.returncode}: "
                          f"{completed.stderr.strip()[:200]}")
        with open(path, encoding="utf-8") as handle:
            return [json.loads(line) for line in handle if line.strip()]


# A suite that leaves the device unhealthy and fails, so the runner recovers
# it and runs the suite again. The flag file is what the double watches and
# what the recovery command removes: the three processes involved share no
# memory, and a retry cannot be scripted any other way without a device.
RETRY_BODY = """\
import pathlib
report.check_start('the device is well')
if os.environ.get("E2E_ATTEMPT") == "1":
    pathlib.Path(os.environ["OBS_FLAG"]).touch()
    report.check_fail('the listener is gone')
    sys.exit(1)
report.check_ok('recovered')
"""


FIXTURE_STUBS = (
    Stub("held", body=(
        "report.section('the ordinary case')\n"
        "report.check_start('the listing is complete')\n"
        "report.check_ok('20 rows')\n"
        "report.check_start('the first row is the header')\n"
        "report.check_ok()\n")),
    # Fails a check and skips another, so the report has both a failure to
    # explain and a coverage gap to name.
    Stub("broken", body=(
        "report.check_start('the row survives a redraw')\n"
        "report.check_fail('0 rows, expected 20')\n"
        "report.check_start('the name is listed in full')\n"
        "report.check_skip('needs the ftp-listing-full-length fix, which this "
        "machine does not have')\n"
        "sys.exit(1)\n")),
    # Ends in a traceback, which is what the captured console carries and the
    # JSONL does not.
    Stub("raised", body=(
        "report.check_start('the device answers')\n"
        "raise RuntimeError('the device stopped answering mid-check')\n")),
    # Fails once on a device the sweep then reports degraded, so the runner
    # recovers it and runs the suite again.
    Stub("flaky", body=RETRY_BODY),
    # Leaves a menu open, so the sweep before the next suite skips the two
    # checks that need the C64 running.
    # Sends what a device's own log looks like, so the collected log and the
    # report's slices around a failure are in the fixture. A real device sends
    # these unprompted; here a suite stands in for one.
    Stub("noisy", body=(
        "import socket\n"
        "port = int(os.environ['E2E_SYSLOG_PORT'])\n"
        "sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)\n"
        "def say(text):\n"
        "    sock.sendto(text.encode(), ('127.0.0.1', port))\n"
        "    time.sleep(0.05)\n"
        "import time\n"
        "say('All linked modules have been initialized and are now running.')\n"
        "report.check_start('the drive answers')\n"
        "say('1541: seek track 18')\n"
        # What the device writes because the harness asked it something. A real
        # device produces far more of these than of anything else: over one
        # sequential run of the gate they were 15882 of 22930 lines. Here they
        # are enough to push the two lines that matter out of the slice if the
        # slice were the last of everything.
        "for n in range(40):\n"
        "    sock.sendto(('Accept client 0 on socket 7.  '\n"
        "                 '192.168.1.185:4%04d' % n).encode(),\n"
        "                ('127.0.0.1', port))\n"
        "    time.sleep(0.01)\n"
        "    sock.sendto(b'HTTP GET /v1/machine:menu_screen',\n"
        "                ('127.0.0.1', port))\n"
        "    time.sleep(0.01)\n"
        "say('1541: no answer from the drive')\n"
        "report.check_fail('the drive did not answer')\n"
        "sys.exit(1)\n")),
    # Drives the menu through the shared UI backend, so the spool of every
    # screen the harness read is in the fixture too.
    Stub("browse", body=(
        "import pathlib\n"
        "import ui_backend\n"
        "pathlib.Path(os.environ['OBS_MENU']).touch()\n"
        # 20s rather than the suites' usual few seconds: this run shares one
        # CPU with a second target racing the same double, on hosts ranging
        # from an idle workstation to a loaded CI runner, and closing the
        # menu is polled real time against a loopback double, not hardware.
        "backend = ui_backend.make_backend('overlay', ARGS.host,\n"
        "                                  ARGS.password, 20.0)\n"
        "report.check_start('the cursor moves')\n"
        "backend.send_key('DOWN')\n"
        "report.check_ok('one row')\n"
        "pathlib.Path(os.environ['OBS_MENU']).unlink(missing_ok=True)\n")),
    Stub("menu-left-open", body=(
        "import pathlib\n"
        "report.check_start('the menu opens')\n"
        "pathlib.Path(os.environ['OBS_MENU']).touch()\n"
        "report.check_ok('on screen')\n")),
    Stub("menu-closed-again", body=(
        "import pathlib\n"
        "pathlib.Path(os.environ['OBS_MENU']).unlink(missing_ok=True)\n"
        "report.check_start('the menu closes')\n"
        "report.check_ok()\n")),
    # Mounts an image and writes a setting, and puts neither back, which is
    # what the report's account of what a run changed is built from.
    Stub("leaves-things-behind", body=(
        "from api import UltimateApi\n"
        "device = UltimateApi(ARGS.host, ARGS.password)\n"
        "report.check_start('the image mounts')\n"
        "device.drives.mount('a', '/Usb0/game.d64')\n"
        "report.check_ok()\n"
        "report.check_start('the setting takes')\n"
        "device.configs.set('Network Settings', 'Log to Syslog Server',\n"
        "                   '192.168.1.2:5514')\n"
        "report.check_ok()\n")),
    # Registered and absent from disk, which is how the runner reports SKIP for
    # a whole suite.
    Stub("missing-file", missing=True),
    # Needs an operator decision, so an ordinary run leaves it out and the
    # report says which suites it did not run and why.
    Stub("operator-only", manual=True),
    # A category rather than a mode, so the identity key's other label form is
    # in the fixture too.
    Stub("a-benchmark", category="perf", args="-H @HOST@ -p @PASS@", body=(
        "report.check_start('typing reaches the field')\n"
        "report.check_ok('11.2 characters a second')\n")),
    # Kills the runner from underneath itself, on the second target only. That
    # leaves a suite with no closing record, a JSONL file cut mid-line, and a
    # target with no run record at all, which is the tree a killed run leaves
    # and the one the evidence matters most for.
    Stub("cut-short", body=(
        "import signal\n"
        "import time as _time\n"
        "report.check_start('the first half')\n"
        "report.check_ok()\n"
        "if os.environ.get('E2E_TARGET') == '127.0.0.1@localhost':\n"
        "    with open(os.environ['E2E_JSONL'], 'a') as handle:\n"
        "        handle.write('{\"kind\": \"check\", \"index\": 2, \"lab')\n"
        "        handle.flush()\n"
        # The runner copies this suite's console into its log line by line, so
        # killing it the moment the line is printed is a race with that copy:
        # the fixture then has an empty log and the document loses the log
        # tail block. Waiting for the line to reach the log is what the kill
        # is synchronised against, rather than a pause that would be the same
        # race with a longer odds.
        "    _log = os.environ['E2E_JSONL'][:-len('.jsonl')] + '.log'\n"
        "    _deadline = _time.monotonic() + 10.0\n"
        "    while _time.monotonic() < _deadline:\n"
        "        try:\n"
        "            if 'the first half' in open(_log, encoding='utf-8',\n"
        "                                        errors='replace').read():\n"
        "                break\n"
        "        except OSError:\n"
        "            pass\n"
        "        _time.sleep(0.005)\n"
        "    os.kill(os.getppid(), signal.SIGKILL)\n"
        "    os.kill(os.getpid(), signal.SIGKILL)\n"
        "report.check_start('the second half')\n"
        "report.check_ok()\n")),
)


def build_fixture() -> ScriptedRun:
    """Drive a real, scripted run and return the `-j` tree it wrote.

    No `--record`: the golden cases are about the document the generator
    renders from JSONL, logs and captured screens, none of which need a video
    encoder, so the fixture asks for none and needs neither `ffmpeg` nor `PIL`
    to build. The two cases that are genuinely about a recording, `the report
    shows the stills and the timecode` and `the recording can be navigated
    three ways`, see no `video.mp4` here and report SKIP through the same path
    they already use for a fixture recorded without one.
    """
    workspace = tempfile.mkdtemp(prefix="e2e-observability-fixture-")
    unhealthy = os.path.join(workspace, "unhealthy")
    menu = os.path.join(workspace, "menu-open")
    # No port of its own: the collector binds 0, gets one nobody else has, and
    # exports it to the suites. Choosing one here and handing it on is a race
    # that two runs starting at the same moment lose together.
    with DeviceDouble() as double:
        double.menu_open_flag = menu
        double.withhold_ftp_banner_while(unhealthy)
        made = scripted_run(
            double, FIXTURE_STUBS,
            tokens=("127.0.0.1", "127.0.0.1@localhost"),
            # Both categories, so the fixture carries a suite run named by its
            # mode and one named by its category. The two are the only two
            # shapes an identity key has, and a generator that gets the second
            # one wrong renders every perf and soak suite twice.
            arguments=("--e2e", "--perf", "--syslog",
                       "--syslog-port", "0",
                       "--recover-command", f"rm -f {unhealthy}"),
            workspace=workspace,
            extra_environment={"OBS_FLAG": unhealthy, "OBS_MENU": menu,
                               })
    return made


def _fixture_is_well_formed(made: ScriptedRun) -> bool:
    """Every suite whose scripted body cannot itself fail came back OK.

    `browse` shares one flag file, standing in for a device's menu, with
    whichever other suite the other target is running at the same instant:
    real cross-target interference over a device two targets are both driving,
    which is exactly what the fixture is for. It also means the outcome is
    racy exactly here, so a build where it lost the race is rebuilt rather
    than becoming the tree 22 cases and a checked-in document are measured
    against.
    """
    for target in ("127.0.0.1", "127.0.0.1-at-localhost"):
        for record in made.records(target, "overlay-browse.jsonl"):
            if record.get("kind") == "suite" and record.get("verdict") != "OK":
                return False
    return True


def fixture_tree() -> str:
    """The built fixture tree, after require_fixture() has made it.

    An accessor rather than the `FIXTURE` global itself: the tier modules
    import from this one, and a name bound at import time would stay at the
    None it had before the first case built the tree.
    """
    if FIXTURE is None:
        raise Failure("the fixture tree has not been built; call require_fixture()")
    return FIXTURE


def require_fixture() -> None:
    """Build the fixture tree once per process and cache its path in `FIXTURE`.

    A case that needs the tree calls this before touching `FIXTURE` rather
    than building its own copy, so 22 golden cases pay the cost of one real
    run rather than one each. Rebuilt, bounded, when the build itself lost the
    one race it contains; see `_fixture_is_well_formed`.

    A builder that cannot build fails every case that needed it, and never
    skips them. Every acceptance criterion for the report generator lives in
    tier 4, so a builder that stopped working turned all of them into skipped
    cases while this suite still reported a pass, and the whole section
    stopped being tested without anything going red. That is a larger hole
    than any rendering defect those cases exist to catch.
    """
    global FIXTURE, _FIXTURE_PROBLEM

    if _FIXTURE_PROBLEM is not None:
        raise Failure(_FIXTURE_PROBLEM)
    if FIXTURE is not None:
        return
    with _FIXTURE_LOCK:
        # Checked again with the lock held: another case may have built it, or
        # learned it cannot be built, while this one was waiting for the lock.
        if _FIXTURE_PROBLEM is not None:
            raise Failure(_FIXTURE_PROBLEM)
        if FIXTURE is not None:
            return
        try:
            made = build_fixture()
            for _attempt in range(2):
                if _fixture_is_well_formed(made):
                    break
                made = build_fixture()
            FIXTURE = made.directory
        except Failure:
            raise
        except Exception as error:  # noqa: BLE001
            _FIXTURE_PROBLEM = f"the fixture could not be built: {error}"
            raise Failure(_FIXTURE_PROBLEM) from error


def prewarm_fixture() -> None:
    """Build FIXTURE, if needed, before tier 4's cases run concurrently.

    build_fixture() drives a real scripted run, so its own content - a retry
    count among the things it can carry - depends on whatever else the
    machine is doing while it runs. `require_fixture()`'s lock keeps two
    golden cases from building it at once, but a case that already holds the
    lock still has roughly twenty others doing their own real work beside it.
    Called here instead, before that tier's pool exists, the build has the
    same quiet machine a sequential run would have given the first case that
    needed it.
    """
    try:
        require_fixture()
    except Failure:
        pass  # Each case that needs FIXTURE raises this again on its own line.


def record_fixture() -> int:
    """Rewrite the checked-in expected document from a freshly built fixture.

    A deliberate act, not a side effect: the expected document is what a
    reviewer reads to see a rendering change, so regenerating it has to be
    something somebody chose to do. Only the document is written; the tree it
    was rendered from is scratch space and is not kept.

    Written canonical, through the same substitutions the comparison applies.
    The raw document carries the durations, timestamps, scratch directory,
    branch and commit of the machine and moment that built it, so re-recording
    it after a rendering change produced a diff of several hundred lines with
    the handful of changed lines somewhere inside. Canonical, the diff of a
    re-record is the rendering change and nothing else, which is what this
    tier is for.
    """
    require_fixture()
    with open(EXPECTED, "w", encoding="utf-8") as handle:
        handle.write(canonicalize_document(generated_document()))
    report.detail(f"recorded {EXPECTED} from a fixture built at {FIXTURE}")
    return 0


def generated_document() -> str:
    """The report the generator writes for the built fixture.

    Generated in a copy so the fixture tree is never written into: the
    generator writes index.md beside the records it read.
    """
    import shutil
    import tempfile

    generator = load_report_tool()
    with tempfile.TemporaryDirectory() as directory:
        tree = os.path.join(directory, "run")
        shutil.copytree(FIXTURE, tree)
        generator.write_report(tree)
        with open(os.path.join(tree, generator.INDEX_NAME),
                  encoding="utf-8") as handle:
            return handle.read()


def synthetic_run(generator, **overrides):
    """A one-target run built in memory, for a rendering rule the fixture lacks.

    The fixture is a real run and stays one, so a rule that needs a value no
    scripted run produces, such as a check slow enough to be marked, is
    exercised over the same renderer with records built by hand.
    """
    check = generator.Check(
        target="u64", label="overlay", suite="prg-context-menu", attempt=1,
        index=26, check_label="the file loads", verdict="OK",
        extra="20 rows", seconds=overrides.get("seconds", 0.5),
        time=1000.0, scenario="mount and run")
    made = generator.SuiteRun(
        target="u64", label="overlay", suite="prg-context-menu", attempt=1,
        verdict="OK", seconds=1.0, note="", mode="overlay", recoveries=0,
        time=1000.0, checks=[check])
    target = generator.TargetRun(token="u64", slug="u64", suites=[made],
                                 health=overrides.get("health", []),
                                 run={"kind": "run", "verdict": "OK", "suites": 1,
                                      "passed": 1, "failed": 0, "skipped": 0,
                                      "dirty": 0, "recoveries": 0, "exit_code": 0,
                                      "started": 999.0})
    return generator.Run(directory=overrides.get("directory", "/nowhere"),
                         targets=[target])


def records_in_fixture():
    """Every record in the fixture tree, as (file name, record)."""
    import json

    for root, _dirs, names in os.walk(FIXTURE):
        for name in sorted(names):
            if not name.endswith(".jsonl"):
                continue
            with open(os.path.join(root, name), encoding="utf-8",
                      errors="replace") as handle:
                for line in handle:
                    if not line.strip():
                        continue
                    try:
                        record = json.loads(line)
                    except ValueError:
                        continue
                    if isinstance(record, dict):
                        yield name, record


def canonicalize_document(text: str) -> str:
    """Replace what a fresh, live build cannot hold still, with a fixed stand-in.

    The fixture is a real, scripted run against two targets that race each
    other rather than hand-written data, so its commit, host, timings, scratch
    directory and byte counts are whatever they are on the machine and moment
    that built it, and two events a millisecond apart can land in either order
    or either side of a rounded second boundary. None of that is what this
    tier proves; it proves the renderer's wording, structure and alignment,
    so both this document and the checked-in one are put through the same
    substitutions before they are compared. The two sections built entirely
    from that race, the timeline and the slow-check summary, are reduced to
    their length: `the_timeline_is_the_whole_run_in_order` and
    `the_time_section_names_the_slow_ones` already prove their content and
    order directly against a live document, so nothing is lost by not also
    diffing them here. Table padding is collapsed everywhere rather than
    reasoned about column by column, because a placeholder is rarely the same
    width as the real value it replaces; `every_table_is_padded` is what
    proves alignment, not this.
    """

    text = re.sub(r"[ \t]+\|", " |", text)
    text = re.sub(r"\|[ \t]+", "| ", text)
    # A table's separator rule is as wide as its widest cell, so a value that
    # got shorter moves every dash in the rule above it and a comparison about
    # wording fails over arithmetic. Every rule collapses to one form; the
    # padding itself is what `every_table_is_padded` proves.
    text = re.sub(r"(?m)^\|[-:| ]+\|$",
                  lambda m: "| " + " | ".join(
                      "---" for _ in m.group(0).strip("|").split("|")) + " |",
                  text)
    text = re.sub(r"\b[0-9a-f]{40}\b", "0" * 40, text)
    # The collector binds an ephemeral port in a fixture, so the number it got
    # is whatever the kernel had free at that moment.
    text = re.sub(r"(?<=UDP )\d{4,5}\b", "0", text)
    text = re.sub(r"`(\d{4,5})`", "`0`", text)
    text = re.sub(r"\d+\.\d+s\b", "0.000s", text)
    text = re.sub(r"(?<=[=\s])\d+ms\b", "0ms", text)
    text = re.sub(r"\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}", "0000-00-00 00:00:00",
                  text)
    text = re.sub(r"/tmp/e2e-observability-fixture-\S+?(?=[\s`'\"/])", "/FIXTURE",
                  text)
    # Where this checkout lives on disk, wherever a real traceback names one
    # of its own files, and which line of it the frame landed on. The file and
    # the function are what the frame says; the line number moves whenever
    # anything above it in that file changes, so a fixture recorded before an
    # unrelated edit to ui_backend.py would otherwise have to be re-recorded
    # for a rendering that did not change.
    text = re.sub(r'File "[^"]*?(?=/tests/(?:e2e|lib)/)', 'File "/REPO', text)
    text = re.sub(r'(File "/REPO[^"]*", line )\d+', r"\g<1>N", text)
    # Python 3.11 added a caret line under a traceback frame pinpointing the
    # failing sub-expression; the CI image runs 3.10 and has no such line.
    # Neither this substitution nor the line count beside it is what this
    # tier proves.
    text = re.sub(r"(?m)^[ \t]*\^+[ \t]*\n", "", text)
    text = re.sub(r"Last \d+ line\(s\) of", "Last N line(s) of", text)
    text = re.sub(r"--syslog-port \d+", "--syslog-port N", text)
    text = re.sub(r"(?m)^\| (host|python|branch|worktree) \|.*\|$",
                  lambda m: f"| {m.group(1)} | CANONICAL |", text)
    # The byte count of a file the fixture wrote: real, and off by a handful
    # of bytes between builds purely from how many digits a measured duration
    # happened to serialize as, which is exactly the kind of noise this
    # substitution exists to remove.
    text = re.sub(r"(?m)^(\| `[^`]+` \| )(?:\d+|-)( \| )", r"\1N\2", text)
    # Whether the health sweep's ping check can run at all depends on whether
    # a `ping` binary is on the machine that built the fixture, not on
    # anything this tier renders.
    text = re.sub(r"(?ms)(^\| Sweep \| Verdict \| ping \|.*?\n)(.*?)(?=\n\n)",
                  lambda m: m.group(1) + re.sub(
                      r"(?m)^(\| [^|]+ \| (?:OK|DEGRADED|FAIL) \| )\S+( \| )",
                      r"\1N\2", m.group(2)),
                  text)
    text = re.sub(r"(?ms)^## Timeline\n.*?(?=\n## Checks)",
                  lambda m: _section_length(m.group(0), "## Timeline"), text)
    text = re.sub(r"(?ms)^## Where the time went\n.*?(?=\n## Device log)",
                  lambda m: _section_length(m.group(0), "## Where the time went"),
                  text)
    return text


_REDUCED_SECTION_RE = re.compile(
    r"\n\n\d+ line\(s\), order not compared here\n?\Z")


def _section_length(section: str, heading: str) -> str:
    """`heading`, followed by how many lines it held rather than which.

    A section that has already been reduced is returned unchanged, which is
    what makes canonicalize_document idempotent: the checked-in document is
    stored canonical and is then put through the same substitutions again
    before it is compared, so a second pass has to be a no-op.
    """
    if _REDUCED_SECTION_RE.search(section):
        return section if section.endswith("\n") else section + "\n"
    return f"{heading}\n\n{len(section.splitlines()) - 1} line(s), order not " \
           f"compared here\n"


# Requirements this suite deliberately does not name a test for, and why. A
# reason here is a decision somebody took; a requirement in neither this table
# nor a test is one nobody has decided about.
UNTESTED_REQUIREMENTS = {
    "OBS-1.4": "a rule about what may be invented, held by every case that "
               "joins two artefacts with no identifier of its own",
    "OBS-1.5": "a rule about whose clock is used; the harness reads no clock "
               "of the device's anywhere",
    "OBS-1.9": "a rule about which artefacts exist, held by the tests for each "
               "of them",
    "OBS-4.4": "a statement that no URL serves one file out of a zipped "
               "artifact, which is GitHub's behaviour rather than this code's",
    "OBS-4.5": "the artifact URL comes from the upload action's own output, "
               "which only a real workflow run produces",
    "OBS-4.7": "a decision to put nothing binary in the summary, held by the "
               "case that renders the summary from the fixture",
    "OBS-4.8": "the three hops from a build result to an artefact, which are "
               "the workflow's shape rather than a behaviour",
    "OBS-5.8": "a decision not to capture per check, which is the absence of "
               "a call rather than a behaviour",
    "OBS-6.7": "a decision that the heap series is never an assertion, held "
               "by the case that proves the heap check can only SKIP or OK",
    "OBS-6.8": "a decision not to build a heap sampler on a timer",
    "OBS-7.1": "a deployment step on the devices, not code in this repository",
    "OBS-7.2": "the firmware's own parse of the configured value",
    "OBS-7.11": "loss the receiving side cannot measure, which is why the "
                "collector reports what it received rather than what was sent",
    "OBS-7.12": "an unbounded lag between printing and receipt, which is why "
                "attribution by interval is approximate rather than exact",
    "OBS-7.13": "what the firmware prints before the syslog is initialised",
    "OBS-7.15": "whether an assertion failure reaches the collector, which is "
                "the firmware item at OBS-9.1",
    "OBS-7.16": "the firmware's behaviour when nothing is listening",
    "OBS-8.15": "the property this suite's whole first tier demonstrates",
    "OBS-8.18": "the cost of multicast on the LAN, which is an operator's "
                "decision rather than a behaviour",
    "OBS-15.3": "a rule that no capability was removed, held by the suites "
                "that still use each resource",
    "OBS-15.9": "a rule that nothing here touches the device debug log, which "
                "is the absence of a call",
    "OBS-16.5": "the three injected seams, used by every case that passes a "
                "clock, an encoder name or an address",
    "OBS-2.6": "the interval rule, exercised by every case that joins a record "
               "to a time",
    "OBS-2.7": "attribution of the gaps between checks, which only the syslog "
               "slices consume",
    "OBS-3.16": "one documented pandoc command, run by hand and never in CI",
    "OBS-9.1": "optional firmware work, proven red then green on hardware",
    "OBS-9.3": "optional firmware work, proven red then green on hardware",
    "OBS-14.1": "a statement about which host platforms are supported",
    "OBS-14.4": "a host requirement, documented rather than executable",
    "OBS-16.1": "the shape of this suite, which running it demonstrates",
    "OBS-16.4": "the three ways this suite is invoked, each of which invokes it",
    "OBS-16.8": "the suite's own budget, measured by running it",
    "OBS-16.9": "the suite's own constraint, held by every case in it",
    "OBS-16.10": "where the device-free acceptance criteria live",
}


def specified_requirements() -> dict:
    """Every requirement the specification defines, with its priority."""

    path = os.path.join(ROOT, "tests", "e2e", "doc", "observability-spec.md")
    try:
        with open(path, encoding="utf-8") as handle:
            text = handle.read()
    except OSError:
        return {}
    found = {}
    for number, priority in re.findall(
            r"^\*\*(OBS-\d+\.\d+)\*\*\s*\[(P\d)", text, flags=re.M):
        found[number] = priority
    return found


# One record of the wrong shape per case. A tree is written by a run that may
# have been killed mid-write, and by a version of the harness that may not be
# the one reading it, so a field of the wrong type has to cost that field
# rather than the whole document.
MALFORMED_RECORDS = (
    ("an attempt that is not a number",
     {"kind": "check", "index": 1, "attempt": "two", "seconds": 0, "time": 1,
      "suite": "held"}, "overlay-held.jsonl"),
    ("a duration that is not a number",
     {"kind": "suite", "name": "held", "seconds": "fast", "attempt": 1,
      "time": 1}, "run.jsonl"),
    ("a time that is not a number",
     {"kind": "check", "index": 1, "attempt": 1, "seconds": 0, "time": "now",
      "suite": "held"}, "overlay-held.jsonl"),
    ("health checks that are strings",
     {"kind": "health", "label": "x", "ok": True, "time": 1,
      "checks": ["ping"]}, "run.jsonl"),
    ("a heap entry that is not an object",
     {"kind": "health", "label": "x", "ok": True, "time": 1,
      "checks": [{"name": "heap", "state": "ok", "heap": "lots"}]},
     "run.jsonl"),
    ("a health check with no name",
     {"kind": "health", "label": "x", "ok": True, "time": 1,
      "checks": [{"state": "ok", "ms": 1}]}, "run.jsonl"),
    ("a plan whose suites are strings",
     {"kind": "plan", "suites": ["a", "b"], "sequence": [], "time": 1},
     "run.jsonl"),
    ("action parameters that are a list",
     {"kind": "action", "method": "PUT", "path": "/v1/x", "params": ["a"],
      "status": 200, "time": 1}, "overlay-held.jsonl"),
    ("a count that is not a number",
     {"kind": "run", "verdict": "OK", "suites": "many", "exit_code": 0,
      "time": 1}, "run.jsonl"),
    ("a per-suite file with nothing in it", None, "overlay-empty.jsonl"),
)


def selected(tiers: Sequence[int], only: Sequence[str]) -> list[Case]:
    chosen = [c for c in CASES if not tiers or c.tier in tiers]
    if only:
        chosen = [c for c in chosen
                  if any(name in c.label or name in c.requirements for name in only)]
    return chosen


@dataclass
class _Outcome:
    """A non-exclusive case's result, computed on a worker thread."""
    verdict: str
    message: str
    elapsed: float


def _execute_case(entry: Case) -> _Outcome:
    """Run a non-exclusive case's body on a worker thread and time it.

    Never raises: a case must not end the suite. Calls nothing in report.py -
    only an exclusive case does that (report.detail(), for a coverage note),
    which is exactly why a case that does is marked exclusive and runs through
    `run_case` instead, never through here.
    """
    started = time.monotonic()
    try:
        extra = entry.run() or ""
    except Skipped as exc:
        return _Outcome(report.SKIP, str(exc), time.monotonic() - started)
    except Failure as exc:
        return _Outcome(report.FAIL, str(exc), time.monotonic() - started)
    except Exception as exc:  # noqa: BLE001 - a case must not end the suite
        return _Outcome(report.FAIL,
                        f"{type(exc).__name__}: {report.format_exception(exc)}",
                        time.monotonic() - started)
    return _Outcome(report.OK, str(extra), time.monotonic() - started)


def _report_outcome(entry: Case, outcome: _Outcome) -> str:
    """Print one non-exclusive case's already-computed outcome as its check line."""
    report.check_start(entry.label)
    if outcome.verdict == report.SKIP:
        report.check_skip(outcome.message, elapsed=outcome.elapsed)
    elif outcome.verdict == report.FAIL:
        report.check_fail(outcome.message, elapsed=outcome.elapsed)
    else:
        report.check_ok(outcome.message, elapsed=outcome.elapsed)
    return outcome.verdict
