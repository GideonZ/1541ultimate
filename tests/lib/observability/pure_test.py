#!/usr/bin/env python3
# Tier 1: no device, no subprocess and no clock: rules, parsers and policies checked directly.

"""The pure cases of the observability suite.

No device, no subprocess and no clock: rules, parsers and policies checked directly.

Importing this module registers its cases in support.CASES. The entry
point, tests/lib/observability_test.py, imports all four tiers and runs
them in the order TIERS names.
"""

from device_double import DeviceDouble
from report import Failure
from api import UltimateApi
import contextlib
from selftest import expect
import health
import io
import json
import os
import re
import report
import socket
import struct
import sys
import targets
import tempfile
import threading
import time
from support import (CASES, INHERITED_VARIABLES, KEPT_VARIABLES, REPORT_TOOL,
    ROOT, RUNNER_PATH, Skipped, UNTESTED_REQUIREMENTS, _harness_hash_edit,
    case, composed_pair, exclusive, free_udp_port, glyph_columns,
    interaction_log, load_report_tool, load_runner, logged_interactions,
    occupied_span, packets_of, parse_srt, runner_variables,
    specified_requirements, synthetic_run, vic_frame, video_stream,
    without_port_overrides)
from device_double import audio_packets
from device_double import video_packets
import ast
import av_stream
import band as band_lib
import dataclasses
import ftplib
import glyphs
import importlib.machinery
import interactions
import recorder as recorder_lib
import rest as rest_lib
import screens as screen_spool
import streams
import subprocess
import sys as sys_lib
import syslog_collector
import targets as targets_lib
import types
import ui_backend
import vic_text


@case(1, "OBS-15.13", "OBS-8.14", "OBS-7.18", exclusive=True)
def target_carries_every_port() -> str:
    """A parsed target answers where each of the device's surfaces is."""
    with without_port_overrides():
        target = targets.parse("u64")
        cartridge = targets.parse("u2@c64u")
    expect("rest", target.rest_port, 80)
    expect("ftp", target.ftp_port, 21)
    expect("telnet", target.telnet_port, 23)
    expect("dma", target.dma_port, 64)
    # A cartridge has no VIC and no streams route, so the picture and the
    # audio are the computer's, and so is the request that starts them.
    expect("the video is the computer's", cartridge.video_host, "c64u")
    expect("and so is the stream it asks for",
           cartridge.host_for("/v1/streams/video:start"), "c64u")
    expect("the keyboard too", cartridge.host_for("/v1/machine:input"), "c64u")
    expect("everything else is the cartridge's",
           cartridge.host_for("/v1/machine:menu_screen"), "u2")
    # Both machines of a cartridge target log, and the order says which is
    # the device under test.
    expect("both logs, the cartridge first", cartridge.log_hosts,
           ("u2", "c64u"))
    expect("a whole machine logs once", target.log_hosts, ("u64",))
    return "4 ports, and which machine is which"


@case(1, "OBS-15.13", "OBS-15.14", exclusive=True)
def rest_port_environment_override() -> str:
    """U64_REST_PORT moves the REST port the way U64_TELNET_PORT moves Telnet."""
    with without_port_overrides():
        os.environ[targets.REST_PORT_ENV] = "8080"
        expect("overridden", targets.parse("u64").rest_port, 8080)
        os.environ[targets.REST_PORT_ENV] = "not a port"
        expect("malformed value ignored", targets.parse("u64").rest_port, 80)
        os.environ.pop(targets.REST_PORT_ENV)
    return "8080"


@case(1, "OBS-15.14", exclusive=True)
def rest_url_names_the_port_only_when_it_moved() -> str:
    """A URL against a real device reads exactly as it does with no port field."""

    with without_port_overrides():
        plain = rest_lib.RestClient("u64")
    expect("default", plain.url("/v1/version"), "http://u64/v1/version")
    moved = rest_lib.RestClient(
        targets.Target(token="u64", device="u64", computer="u64", rest_port=8080))
    expect("moved", moved.url("/v1/version"), "http://u64:8080/v1/version")
    return "80 stays implicit"


@case(1, exclusive=True)
def a_failed_teardown_is_recorded_and_does_not_raise() -> str:
    """report.best_effort keeps the verdict and stops losing the evidence.

    The suites used to end with `try: ... except Exception: pass`, so a settings
    restore that failed left the device changed with nothing anywhere saying so,
    and the next suite failed for a reason its report could not explain. This
    pins the three parts of the replacement: the caller carries on, the console
    gets a line, and the JSONL gets a record naming the step and the exception.
    """

    report_module = sys.modules["report"]
    previous = report_module.JSONL_PATH
    with tempfile.TemporaryDirectory() as workspace:
        path = os.path.join(workspace, "run.jsonl")
        report_module.set_jsonl_path(path)
        try:
            expect("a step that works reports success",
                   report_module.best_effort("restore Drive A", lambda: None), True)

            def refuse():
                raise OSError("device did not answer")

            expect("a step that fails reports failure, rather than raising",
                   report_module.best_effort("restore Drive A", refuse), False)

            def refuse_ftp():
                raise ftplib.error_perm("550 no such file")

            expect("an ftplib error is a device fault too",
                   report_module.best_effort("delete the fixture", refuse_ftp),
                   False)

            # A bug in the teardown itself is not something to carry on from:
            # a TypeError here means the caller is wrong, not the device.
            raised = False
            try:
                report_module.best_effort("bad call", lambda: "x" + 1)
            except TypeError:
                raised = True
            expect("a caller bug still propagates", raised, True)
            with open(path, encoding="utf-8") as handle:
                records = [json.loads(line) for line in handle if line.strip()]
        finally:
            report_module.set_jsonl_path(previous)

    teardowns = [r for r in records if r["kind"] == "teardown"]
    expect("one record per failed step, and none for the ones that worked",
           len(teardowns), 2)
    expect("the record names the step", teardowns[0]["label"], "restore Drive A")
    expect("and says it did not happen", teardowns[0]["ok"], False)
    expect("and carries the exception",
           teardowns[0]["error"], "OSError: device did not answer")
    expect("the ftplib one too", teardowns[1]["label"], "delete the fixture")
    if "550 no such file" not in teardowns[1]["error"]:
        raise Failure(f"the ftplib reason was lost: {teardowns[1]['error']!r}")
    return "2 teardown records, 1 caller bug re-raised"


@case(1)
def a_reset_part_way_through_a_body_is_not_retried() -> str:
    """A PUT the device may already have seen is not sent a second time.

    urllib wraps every OSError raised inside HTTPConnection.request in a
    URLError, and that one call both connects and sends. Classifying by
    exception type alone therefore read a reset part-way through a body as
    "never left the client" and repeated a non-idempotent PUT: a files:* upload
    or a machine:writemem applied twice, which the soak suites then see as a
    false heap delta or a duplicate file.

    Two servers, one per branch of the rule. One accepts the connection and
    resets the socket while the body is arriving, which the device may have
    acted on, so the PUT is sent once. The other refuses the connection, which
    it cannot have acted on, so the PUT is repeated: three attempts, and the
    pauses between them are what makes that observable without counting
    connections nothing accepted.
    """

    # Larger than a socket buffer, so the reset lands with part of it sent.
    body = b"x" * 200000
    accepted = []

    def reset_while_reading(listener: socket.socket) -> None:
        """Accept, read a little, then send RST rather than FIN."""
        while True:
            try:
                connection, _ = listener.accept()
            except OSError:
                return
            accepted.append(1)
            try:
                connection.recv(4096)
                # SO_LINGER with a zero timeout makes close() send RST, which
                # is what a device that has gone away produces.
                connection.setsockopt(socket.SOL_SOCKET, socket.SO_LINGER,
                                      struct.pack("ii", 1, 0))
            except OSError:
                pass
            finally:
                connection.close()

    listener = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    listener.bind(("127.0.0.1", 0))
    listener.listen(8)
    resetting_port = listener.getsockname()[1]
    server = threading.Thread(target=reset_while_reading, args=(listener,),
                              daemon=True)
    server.start()

    # A port nothing listens on: bound to find a free one, then closed.
    probe = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    probe.bind(("127.0.0.1", 0))
    refused_port = probe.getsockname()[1]
    probe.close()

    def client(port: int):
        return rest_lib.RestClient(
            targets.Target(token="127.0.0.1", device="127.0.0.1",
                           computer="127.0.0.1", rest_port=port),
            timeout=5.0)

    try:
        with without_port_overrides():
            try:
                client(resetting_port).request("PUT", "/v1/machine:writemem",
                                               body=body)
            except Failure:
                pass
            expect("a body that was part-way out is sent once, not again",
                   len(accepted), 1)

            started = time.monotonic()
            try:
                client(refused_port).request("PUT", "/v1/machine:writemem",
                                             body=b"small")
            except Failure:
                pass
            else:
                raise Failure("a PUT to a closed port should not have succeeded")
            elapsed = time.monotonic() - started
    finally:
        listener.close()
        server.join(timeout=5)

    # Three attempts means two pauses, 0.5s then 1.5s. A refused connection
    # that was not repeated would come back at once, so the time is what says
    # the retry happened rather than a count of connections nothing accepted.
    pauses = rest_lib.retry_pause(0) + rest_lib.retry_pause(1)
    if elapsed < pauses * 0.9:
        raise Failure(f"a refused PUT came back in {elapsed:.2f}s, too fast to "
                      f"have waited the {pauses:.2f}s of retry pauses")

    # The rule itself, stated directly, so both branches are readable without
    # reconstructing them from the sockets above.
    expect("not sent: repeat whatever the method",
           rest_lib.may_retry("PUT", False), True)
    expect("sent: a PUT may not be repeated", rest_lib.may_retry("PUT", True), False)
    expect("sent: a GET may", rest_lib.may_retry("GET", True), True)
    return f"1 attempt after a reset, {elapsed:.1f}s of retries after a refusal"


@case(1, exclusive=True)
def the_reporter_refuses_a_second_thread_writing_to_a_line() -> str:
    """One Reporter, and a line only the thread that opened it may write to.

    report.py used to be nine `global` statements over a dozen module
    variables, and the rule that kept this file's own case pool working was a
    docstring saying only the main thread reports. Nothing enforced it: a case
    calling report.detail() from a worker printed under whichever check
    happened to be open, and its check_ok closed another check's line, because
    `depth` counts nesting rather than concurrency.

    The state is one object now, a lock covers opening a line, closing it and
    queueing a detail under it, and a write from another thread is refused
    with a message naming the rule.
    """
    report_module = sys.modules["report"]

    expect("one instance holds the state",
           isinstance(report_module._default, report_module.Reporter), True)
    # The names other modules read off this one - interactions.py, screens.py,
    # rest.py - still answer, from the Reporter.
    expect("SUITE_NAME still reads", report_module.SUITE_NAME,
           report_module._default.suite_name)
    expect("JSONL_PATH still reads", report_module.JSONL_PATH,
           report_module._default.jsonl_path)
    unknown = "NOT_A_NAME"  # not a literal, so this reads as a lookup
    try:
        getattr(report_module, unknown)
    except AttributeError as exc:
        expect("an unknown name is still an AttributeError",
               unknown in str(exc), True)
    else:
        raise Failure("an unknown module attribute did not raise")

    captured = io.StringIO()
    refused: list[str] = []

    def write_from_a_worker(action) -> None:
        try:
            action()
        except Failure as exc:
            refused.append(str(exc))

    # A Reporter of its own, because this case runs inside a check of the
    # harness's and would otherwise be opening a nested line rather than a
    # line of its own. That the state can be swapped like this is the point of
    # it being one object.
    outer = report_module._default
    # jsonl_path="": a fresh Reporter would otherwise take the path from
    # E2E_JSONL and write this case's throwaway check into the run's own
    # records file, where it would be read back as a check that never ran.
    report_module._default = report_module.Reporter(jsonl_path="")
    try:
        with contextlib.redirect_stdout(captured):
            report_module.check_start("a check owned by this thread")
            for action in (lambda: report_module.detail("from a worker"),
                           lambda: report_module.check_start("a worker's check"),
                           report_module.check_ok):
                thread = threading.Thread(target=write_from_a_worker,
                                          args=(action,))
                thread.start()
                thread.join()
            report_module.check_ok("still ours")
            owner_after = report_module._default.owner
    finally:
        report_module._default = outer

    expect("every write from another thread was refused", len(refused), 3)
    for message in refused:
        if "one thread" not in message:
            raise Failure(f"the message does not name the rule: {message!r}")
    line = captured.getvalue().strip()
    expect("the line is the one this thread opened and closed",
           line.count("still ours"), 1)
    if "a worker's check" in line:
        raise Failure(f"a worker's label reached the console: {line!r}")

    # The owner is cleared once the line closes, so the next check is free to
    # be opened by whichever thread collects it.
    expect("the line is released", owner_after, None)
    return "3 writes refused, the owner's line intact"


@case(1, "OBS-14.2")
def ping_uses_each_platform_own_unit() -> str:
    """`ping -W` is seconds on Linux and milliseconds on the BSD stack.

    The wrong unit makes every ping fail before a device on a LAN could
    answer, and a failed ping makes the sweep degraded, which fires the
    operator's recovery command.
    """
    linux = health.ping_command("u64", "linux")
    darwin = health.ping_command("u64", "darwin")
    expect("linux", linux, ["ping", "-c", "1", "-W",
                            str(health.PING_TIMEOUT_SECONDS), "u64"])
    expect("darwin", darwin, ["ping", "-c", "1", "-W",
                              str(int(health.PING_TIMEOUT_SECONDS * 1000)), "u64"])
    if linux[4] == darwin[4]:
        raise Failure("both platforms were given the same -W value")
    return f"linux -W {linux[4]}, darwin -W {darwin[4]}"


@case(1, "OBS-2.1", "OBS-3.17")
def every_registered_suite_reports_a_closing_line() -> str:
    """A suite's own records have to carry its verdict, not only the runner's.

    The report reads a run's directory, and a suite file with no closing
    record is what a killed run leaves, which is how the completeness section
    describes it. A suite that simply never reported one is then
    indistinguishable from one that died: measured here, ftp-client, printer
    and the network soak all passed and left no verdict in their own files.

    Static, so it catches a suite that never reports a verdict at all. It
    cannot catch one that reports on its failing path and not its passing one,
    which is what ftp-client did; that came out of reading a real run's
    directory, and `the report says what the run left behind` is the case that
    would notice it again.
    """
    runner = load_runner()
    closing = ("suite_ok", "suite_fail", "suite_skip", "suite_warn")
    silent = []
    for suite in runner.SUITES:
        path = os.path.join(ROOT, suite.path)
        if not os.path.exists(path):
            continue
        with open(path, encoding="utf-8") as handle:
            text = handle.read()
        if not any(call in text for call in closing):
            silent.append(suite.name)
    if silent:
        raise Failure("suites that report no closing line: " + ", ".join(silent))
    return f"{len(runner.SUITES)} suites"


@case(1, "OBS-8.30", "OBS-8.12")
def the_stamped_check_is_the_check_the_subtitle_names() -> str:
    """The caption on a frame names the same check as the cue over it.

    Two artefacts of one run disagreeing about one frame is worse than either
    being absent, and this pair disagreed for the whole of every run: a check
    record is written when the check closes, so the tail that feeds the stamp
    named the check that had just finished while the cue, generated afterwards
    from the same records with the whole interval known, named the one that was
    running. Both advanced on the same record, so the two counters moved
    together and stayed exactly one apart, which reads as a rendering choice
    rather than as a defect.
    """

    sys.path.insert(0, os.path.join(ROOT, "tests", "e2e", "lib"))

    with tempfile.TemporaryDirectory() as directory:
        path = os.path.join(directory, "overlay-browse.jsonl")
        # Three checks, each closing at the moment the next begins.
        records = [{"kind": "check", "suite": "browse", "index": index,
                    "verdict": "OK", "seconds": 1.0, "time": 100.0 + index,
                    "attempt": 1}
                   for index in (1, 2, 3)]
        with open(path, "w", encoding="utf-8") as handle:
            for record in records:
                handle.write(json.dumps(record) + "\n")

        # What the stamp says once each record has been read. After check N
        # closes, the check running is N + 1.
        tail = recorder_lib.JsonlTail(directory)
        stamped = []
        with open(path, "w", encoding="utf-8") as handle:
            pass
        tail = recorder_lib.JsonlTail(directory)
        for record in records:
            with open(path, "a", encoding="utf-8") as handle:
                handle.write(json.dumps(record) + "\n")
            stamped.append(tail.poll().check)
        expect("the stamp names the check now running", stamped,
               ["check 2", "check 3", "check 4"])

        # And the cue covering the interval each check actually ran in.
        cues, _chapters = recorder_lib.cues_and_chapters(directory, "u64",
                                                         started=100.0,
                                                         lead_in=0.0)
        named = [text.split()[0].rsplit("/", 1)[1] for _s, _e, text in cues]
        expect("the cues name every check once", named, ["1", "2", "3"])
        # The frame in the middle of check 2's interval: the cue over it says
        # check 2, and so must the stamp, which is the state after check 1's
        # record and before check 2's.
        tail = recorder_lib.JsonlTail(directory)
        with open(path, "w", encoding="utf-8") as handle:
            handle.write(json.dumps(records[0]) + "\n")
        covering = [text for start, end, text in cues
                    if start <= 1.5 <= end]
        expect("one cue covers that moment", len(covering), 1)
        expect("the cue and the stamp agree",
               covering[0].split()[0].rsplit("/", 1)[1],
               tail.poll().check.split()[-1])
    return "3 checks, stamp and cue agree on every one"


@case(1, "OBS-8.30", "OBS-8.11")
def the_stamped_timecode_is_where_the_frame_is() -> str:
    """The timecode burned into a frame is the one a player reports for it.

    A single frame travels: somebody screenshots a failure into an issue, and
    what makes that screenshot usable is that its timecode seeks back to it.
    The report maps a check's wall-clock moment to a file position with
    `position_of`, and the stamp at that position has to be the same number.

    It was not. `slots` counts every frame written, the title card's included,
    so it already carries the lead-in, and the stamp added the lead-in to it
    again: measured on a real recording, every frame was stamped exactly 2.0s
    late, the whole length of the card.
    """
    sys.path.insert(0, os.path.join(ROOT, "tests", "e2e", "lib"))

    fps = 10
    interval = 1.0 / fps
    cards = max(1, round(recorder_lib.OVERVIEW_SECONDS / interval))
    lead_in = cards * interval

    expect("the first frame of the file",
           recorder_lib.stamp_position(0, fps), 0.0)
    # The frame the capture proper starts on is the lead-in, by construction:
    # this is the identity the old arithmetic broke.
    expect("the first frame after the cards",
           recorder_lib.stamp_position(cards, fps), lead_in)

    # And what the report tells a reader to seek to is what they find there.
    started = 1000.0
    for elapsed in (0.0, 1.0, 12.3, 600.0):
        where = recorder_lib.position_of(started + elapsed, started, lead_in)
        slots = round(where * fps)
        stamped = recorder_lib.stamp_position(slots, fps)
        if abs(stamped - where) > 1e-9:
            raise Failure(f"the report seeks to {where:.3f}s and the stamp "
                          f"there reads {stamped:.3f}s")
    expect("and it is formatted as a position",
           recorder_lib.format_position(lead_in), "00:00:05.000")
    return "the stamp is the file position"


@case(1, "OBS-8.2", "OBS-8.12")
def two_subtitle_cues_never_cover_the_same_moment() -> str:
    """One identity key on screen at a time, whatever the check durations.

    A cue for a check shorter than the minimum dwell is held on screen so it
    can be read. Held past the next check's start it overlaps it, and a player
    stacks overlapping cues, so the viewer sees two identity keys at once and
    cannot tell which one the frame belongs to.
    """
    sys.path.insert(0, os.path.join(ROOT, "tests", "e2e", "lib"))

    cues = [(3.705, 3.725, "one"), (3.725, 4.385, "two"),
            (4.385, 4.390, "three"), (4.390, 4.400, "four"),
            (10.0, 10.05, "last")]
    spans = parse_srt(recorder_lib.subtitles(cues))
    expect("one cue each", len(spans), len(cues))
    expect("in the order they were given", [span[2] for span in spans],
           [cue[2] for cue in cues])
    for index in range(1, len(spans)):
        if spans[index - 1][1] > spans[index][0]:
            raise Failure(f"cue {index} ends at {spans[index - 1][1]}ms and cue "
                          f"{index + 1} starts at {spans[index][0]}ms")
    for (start, end, text) in spans:
        if end <= start:
            raise Failure(f"the cue {text!r} is serialized from {start}ms to "
                          f"{end}ms, which a player shows for no time at all")
    # The last cue has nothing after it, so it keeps the whole dwell.
    expect("the last cue is held", spans[-1][1] - spans[-1][0],
           recorder_lib.MINIMUM_CUE_MS)
    return f"{len(spans)} cues, none overlapping"


@case(1, "OBS-8.2", "OBS-8.12")
def a_cue_for_a_sub_millisecond_check_is_still_shown() -> str:
    """Checks too short to separate in milliseconds still get distinct cues.

    A `.srt` field is whole milliseconds. A check that measured 200 us has a
    start and an end that round to the same one, and a burst of such checks
    has starts that do too. Both were serialized as `--> ` with identical
    fields, which is a cue a player displays for no time: about a fifth of the
    cues of a real 23-suite sweep were written that way.

    The identity keys and their order have to survive that, because the
    sidecar is what a reader greps.
    """
    sys.path.insert(0, os.path.join(ROOT, "tests", "e2e", "lib"))

    # Six checks inside one millisecond, then two more inside the next, then
    # one with room after it.
    cues = [(12.0000, 12.0002, "a/1 OK"), (12.0002, 12.0004, "a/2 OK"),
            (12.0004, 12.0006, "a/3 FAIL"), (12.0006, 12.0008, "a/4 OK"),
            (12.0008, 12.00095, "a/5 OK"), (12.00095, 12.0010, "a/6 OK"),
            (12.0010, 12.0012, "a/7 OK"), (12.0012, 12.0014, "a/8 OK"),
            (12.5000, 12.5100, "a/9 OK")]
    spans = parse_srt(recorder_lib.subtitles(cues))
    expect("one cue each", len(spans), len(cues))
    expect("every identity key survives", [span[2] for span in spans],
           [cue[2] for cue in cues])
    for start, end, text in spans:
        if end <= start:
            raise Failure(f"the cue {text!r} is serialized from {start}ms to "
                          f"{end}ms, which a player shows for no time at all")
    for index in range(1, len(spans)):
        if spans[index][0] < spans[index - 1][0]:
            raise Failure("the cues are not in start order")
        if spans[index - 1][1] > spans[index][0]:
            raise Failure(f"cue {index} ends at {spans[index - 1][1]}ms and "
                          f"cue {index + 1} starts at {spans[index][0]}ms")
    # The eight crowded cues occupy the two milliseconds their checks did,
    # extended by the one millisecond each needs to be a cue at all.
    expect("the burst starts where the first check did", spans[0][0], 12000)
    expect("and is a millisecond apart", [span[0] for span in spans[:8]],
           list(range(12000, 12008)))
    expect("the check with room keeps the full dwell",
           spans[-1][1] - spans[-1][0], recorder_lib.MINIMUM_CUE_MS)
    return f"{len(spans)} cues, none of them zero length"


@case(1, "OBS-8.22", "OBS-8.20")
def the_harness_pane_draws_the_firmware_own_character_set() -> str:
    """A window frame on the device is a window frame in the recording.

    The menu payload carries the byte the firmware drew, and the firmware
    draws with its own character set, whose first 32 entries are the UI's
    shapes: box corners and edges, a filled block, a selection diamond. Drawn
    through a printable character and a stock C64 ROM instead, every one of
    them is blank, so a recorded menu pane lost every frame on screen while
    the device pane beside it showed them.
    """
    sys.path.insert(0, os.path.join(ROOT, "tests", "e2e", "lib"))

    for label, code in (("top-left corner", 0x01), ("horizontal edge", 0x02),
                        ("vertical edge", 0x04), ("filled block", 0x0B)):
        rows = glyphs._MENU_ROM_ROWS[code]
        if not any(rows):
            raise Failure(f"the {label} at ${code:02X} has no pixels")
    # And lower case is lower case: the firmware's set has both, where the
    # stock ROM's unshifted half folds a-z onto A-Z.
    if glyphs._MENU_ROM_ROWS[ord("a")] == glyphs._MENU_ROM_ROWS[ord("A")]:
        raise Failure("lower case draws the upper-case shape")

    # One cell, end to end: the payload byte reaches the canvas as its own
    # shape rather than as a blank.
    payload = bytearray(2000)
    payload[0] = 0x01
    payload[1000] = 0x01  # white on black
    canvas = glyphs.Canvas(glyphs.GLYPH_WIDTH, glyphs.GLYPH_HEIGHT, 0)
    glyphs.render_menu_screen(bytes(payload), canvas, 0, 0)
    if not any(canvas._pixels):
        raise Failure("a corner glyph drew nothing onto the canvas")
    return "frames, blocks and lower case all draw"


@case(1, "OBS-7.5", "OBS-7.6", "OBS-7.7", "OBS-7.8")
def the_collector_attributes_a_datagram_to_its_device() -> str:
    """One datagram is one line, stamped on receipt and filed by its sender."""


    ticks = iter([100.0 + n for n in range(10)])
    with tempfile.TemporaryDirectory() as directory:
        collector = syslog_collector.Collector(
            directory=directory, port=0, clock=lambda: next(ticks))
        wanted = [targets_lib.parse("127.0.0.2"),
                  targets_lib.parse("127.0.0.3@127.0.0.4")]
        if not collector.bind(wanted):
            raise Failure(f"the collector did not start: {collector.problems}")
        try:
            collector.deliver("127.0.0.2", b"the device says something")
            collector.deliver("127.0.0.4", b"the computer says something")
            collector.deliver("10.9.9.9", b"a machine nobody expected")
        finally:
            collector.stop()

        expect("the device's own file",
               syslog_collector.read(os.path.join(directory, "127.0.0.2",
                                                  "syslog.txt")),
               [(101.0, "the device says something")])
        # A cartridge target's computer logs as well, and both are kept: one is
        # the firmware under test and the other is the machine it is in.
        expect("the computer's file",
               syslog_collector.read(
                   os.path.join(directory, "127.0.0.3-at-127.0.0.4",
                                "syslog-127.0.0.4.txt")),
               [(102.0, "the computer says something")])
        with open(os.path.join(directory, "syslog-unknown-sender.txt"),
                  encoding="utf-8") as handle:
            unmapped = handle.read()
        if "10.9.9.9" not in unmapped:
            raise Failure(f"the sender's address was not kept: {unmapped!r}")
        expect("counted", (collector.lines, collector.unmapped), (2, 1))
    return "3 datagrams, 3 files"


@case(1, "OBS-7.8", "OBS-2.4")
def the_unknown_sender_file_exists_only_when_it_has_something_in_it() -> str:
    """Absence means every line was attributed, not that nothing was checked.

    The file is opened at startup, so an output that cannot be written is an
    operator's problem in the first seconds rather than a discovery at the end
    of a run that has already cost 15 to 30 minutes. That check costs an empty
    file in every healthy run, and an artifact present in every run says
    nothing by being there, so it is removed at close when nothing went into
    it.

    A target's own `syslog.txt` is deliberately not treated the same way. Empty
    there is a finding rather than noise: the device was expected to log and
    said nothing, which is what the runner warns about and what a reader has to
    be able to tell from a collector that never started.
    """


    name = syslog_collector.UNKNOWN_SENDER_NAME
    with tempfile.TemporaryDirectory() as directory:
        collector = syslog_collector.Collector(directory=directory, port=0)
        if not collector.bind([targets_lib.parse("127.0.0.2"),
                               targets_lib.parse("127.0.0.3")]):
            raise Failure(f"the collector did not start: {collector.problems}")
        path = os.path.join(directory, name)
        if not os.path.exists(path):
            raise Failure(
                f"{name} was not opened at startup, so an unwritable one "
                f"would be found by the first datagram instead of now")
        collector.deliver("127.0.0.2", b"a line from the device")
        collector.stop()
        if os.path.exists(path):
            raise Failure(
                f"{name} was left behind holding "
                f"{os.path.getsize(path)} byte(s) after a run in which every "
                f"line was attributed")
        # The target that said nothing keeps its empty file, because that is
        # the record of a device that was collected from and stayed silent.
        silent = os.path.join(directory, "127.0.0.3", "syslog.txt")
        if not os.path.exists(silent):
            raise Failure(
                "a silent target's own log file was removed as well; empty "
                "there is a finding, not noise")
        expect("and it is empty", os.path.getsize(silent), 0)

    with tempfile.TemporaryDirectory() as directory:
        collector = syslog_collector.Collector(directory=directory, port=0)
        if not collector.bind([targets_lib.parse("127.0.0.2")]):
            raise Failure(f"the collector did not start: {collector.problems}")
        collector.deliver("10.9.9.9", b"a machine nobody expected")
        collector.stop()
        path = os.path.join(directory, name)
        if not os.path.exists(path):
            raise Failure(f"{name} was removed although a datagram was "
                          f"attributed to no target")
        with open(path, encoding="utf-8") as handle:
            kept = handle.read()
        if "10.9.9.9" not in kept:
            raise Failure(f"the sender's address was not kept: {kept!r}")
    return "absent when every line was attributed, kept with the sender when not"


@case(1, "OBS-1.2", "OBS-15.2", "OBS-7.17")
def a_collector_that_cannot_start_says_so_once() -> str:
    """A busy port is one warning at startup and nothing else."""


    holder = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    holder.bind(("127.0.0.1", 0))
    try:
        with tempfile.TemporaryDirectory() as directory:
            collector = syslog_collector.Collector(
                directory=directory, port=holder.getsockname()[1])
            expect("did not start", collector.bind(
                [targets_lib.parse("127.0.0.2")]), False)
            # Bound above 1023, so the collector needs no privilege: 514
            # would need root or a capability on Linux and root on macOS,
            # and the devices are configured to the port it does bind.
            if syslog_collector.DEFAULT_PORT <= 1023:
                raise Failure(f"port {syslog_collector.DEFAULT_PORT} is "
                              f"privileged")
            if not any("could not be opened" in problem
                       for problem in collector.problems):
                raise Failure(f"no reason was given: {collector.problems}")
            collector.stop()
    finally:
        holder.close()
    return collector.problems[-1]


@case(1, "OBS-7.8", "OBS-7.9")
def a_port_one_machine_sends_to_identifies_it() -> str:
    """Attribution by the receiving socket, and never a guess.

    A device with two interfaces answers REST on one and can send its log from
    the other, so its datagrams arrive from an address no target claims. A
    port exactly one machine sends to identifies that machine whatever address
    the datagram came from; a port more than one machine sends to identifies
    nothing, so its datagrams fall back to the source address and an
    unrecognised one is still never guessed at.
    """


    with tempfile.TemporaryDirectory() as directory:
        collector = syslog_collector.Collector(directory=directory, port=0)
        # A port for the first target and nothing for the second, which is
        # what an unprovisioned machine looks like.
        exclusive = free_udp_port()
        # Two machines left on the run's default port, so that port owns
        # nothing and its datagrams are attributed by source address, which is
        # what an unprovisioned bench looks like.
        wanted = [targets_lib.parse("127.0.0.2"),
                  targets_lib.parse("127.0.0.3"),
                  targets_lib.parse("127.0.0.4")]
        if not collector.bind(wanted, {"127.0.0.2": exclusive}):
            raise Failure(f"the collector did not start: {collector.problems}")
        try:
            expect("one machine owns that port",
                   collector.owners.get(exclusive), "127.0.0.2")
            # From an address that belongs to no target at all, on the port
            # only that machine sends to. This is the WiFi case.
            collector.deliver("192.0.2.71", b"from the other interface",
                              exclusive)
            # And from an address no target claims, on the shared default
            # port, which identifies nothing.
            collector.deliver("192.0.2.99", b"a stranger", collector.port)
        finally:
            collector.stop()
        attributed = syslog_collector.read(
            os.path.join(directory, "127.0.0.2", "syslog.txt"))
        expect("the wireless line was attributed", [text for _t, text in attributed],
               ["from the other interface"])
        expect("by the port it arrived on",
               collector.attribution_of("127.0.0.2"), {"port": 1})
        # The address it actually came from is still recorded, so a device
        # logging from somewhere unexpected stays visible rather than being
        # absorbed by the port match.
        expect("the observed sender", collector.observed("127.0.0.2"),
               {"192.0.2.71": 1})
        if "192.0.2.71" in collector.addresses_of("127.0.0.2"):
            raise Failure("an address the run never expected is listed as "
                          "expected")
        with open(os.path.join(directory,
                               syslog_collector.UNKNOWN_SENDER_NAME),
                  encoding="utf-8") as handle:
            unknown = handle.read()
        if "192.0.2.99" not in unknown or "a stranger" not in unknown:
            raise Failure(f"the unclaimed sender was not kept: {unknown!r}")
        if "192.0.2.71" in unknown:
            raise Failure("a datagram the port identified was still treated "
                          "as unattributable")
    return "1 attributed by port, 1 unattributable, neither guessed"


@case(1, "OBS-1.8", "OBS-3.5")
def the_mask_hides_a_password_and_nothing_else() -> str:
    """Every shape a password reaches a console log in, and no ordinary word.

    The mask fires on the words around a password rather than on a password,
    so it also fires on a sentence using one of those words. A check recorded
    `--password not supplied` and the document rendered `--password ***
    supplied`, which is not a redaction but a reversal: the report stated the
    opposite of the record, on six lines of a real run.
    """
    generator = load_report_tool()
    secret = "Sw0rdf1sh-Hunter2"
    for text in (f"./run-tests --password {secret} u64",
                 f"env U64_PASS={secret}",
                 f"curl -u admin:{secret} http://192.0.2.1/v1/info",
                 f"X-Password: {secret}",
                 f'{{"password": "{secret}"}}'):
        masked = generator.redact(text)
        if secret in masked:
            raise Failure(f"the password survived: {masked!r}")
        if "***" not in masked:
            raise Failure(f"nothing was masked in {text!r}")
    for kept in ("--password not supplied",
                 "-p none was given",
                 "the password is required"):
        if generator.redact(kept) != kept:
            raise Failure(f"an ordinary word was masked: "
                          f"{generator.redact(kept)!r}")
    return "5 shapes masked, 3 sentences left alone"


@case(1, "OBS-2.20", exclusive=True)
def a_harness_edited_mid_run_is_reported() -> str:
    """A run whose own files change under it says so on its record.

    Every suite is a separate process started from the working tree, so an
    edit to `tests/`, `tools/` or `run-tests` while a run is in progress means
    the later suites ran different code from the earlier ones. That is two
    runs reported as one and nothing in the artefacts said it.

    It is not hypothetical. Building this branch, a gate was invalidated
    exactly that way: two targets ran a suite of 169 cases and passed, the
    third ran 170 and failed two, because a script that was believed to be a
    dry run rewrote four files thirteen minutes in. Nothing but the case
    counts, buried in three console logs, distinguished that run from a run of
    one revision.

    `worktree_dirty` cannot answer it, which is why this field exists beside
    it: `git status --porcelain` reports which files are modified and not what
    is in them, so editing a file that was already modified leaves its output
    identical. The edit that caused the incident was to files that were
    already modified.
    """

    runner = load_runner()
    # One target per process, three targets at once, one checkout between
    # them, and every copy appends the same line to the same file. Without the
    # lock a copy reads its "before" hash while another copy has the file
    # edited, then makes the identical edit itself and hashes the same thing
    # twice. The whole check is held, first hash included, because that first
    # hash is what the rest is compared against. Taking turns costs
    # milliseconds: each copy edits and restores at once.
    with exclusive("harness-hash"):
        return _harness_hash_edit(runner)


@case(1, "OBS-3.15")
def a_device_error_cannot_be_read_as_a_tag() -> str:
    """Text the device or the transport produced never becomes markup.

    The timeline quotes what a failed request answered, and those words are
    whatever they are: `urllib` reports a failed lookup as
    `<urlopen error [Errno -2] Name or service not known>`. Interpolated bare
    into Markdown a renderer reads the angle brackets as a tag and swallows
    the line, which is both the one thing OBS-3.15 forbids and the loss of the
    one sentence a reader needs. Found in this branch's own gate report, where
    two timeline lines carried it.
    """
    generator = load_report_tool()
    text = generator.describe_action({
        "method": "GET", "path": "/v1/machine:menu_screen", "status": 500,
        "retries": 3,
        "error": "<urlopen error [Errno -2] Name or service not known>"})
    if "`<urlopen error" not in text:
        raise Failure(f"the error is not in a code span: {text!r}")
    stripped = re.sub(r"`[^`]*`", "", text)
    tags = re.findall(r"<(?!!--)[a-zA-Z/][^>]*>", stripped)
    if tags:
        raise Failure(f"the line still reads as markup: {tags}")
    # And it stays one line, so the timeline's one-line-per-event rule holds
    # even when the device answers with a pretty-printed body.
    many = generator.describe_action({
        "method": "PUT", "path": "/v1/machine:reset", "status": 500,
        "error": "{\n  \"errors\" : [ \"no\" ]\n}"})
    expect("one line", many.count("\n"), 0)
    return "the error is quoted, and it is one line"


@case(1, "OBS-17.3", "OBS-3.22")
def the_report_says_what_the_runner_says_about_each_exit_status() -> str:
    """The generator's ladder is the runner's, word for word.

    The ladder is defined in `run-tests` and restated in the generator, which
    has to keep its own copy because it renders a tree written by some version
    of the runner and cannot depend on the one installed beside it. A copy
    that nothing compares is a copy that rots: the ladder was renumbered and
    the generator kept the old words, so it would have told a reader that a
    retried run failed and that a failed run was a usage error.

    Comparing the KEYS would not have caught that. Every key was already
    present; it was the prose against each that was wrong. So this compares
    the words.
    """

    runner = load_runner()
    generator = load_report_tool()
    help_text = runner.build_parser().format_help()
    block = re.search(r"exit status.*?\n((?:  \d+ .*\n(?:      .*\n)*)+)",
                      help_text, re.S)
    if not block:
        raise Failure("the runner's --help no longer states its exit statuses, "
                      "so this case is not checking anything")
    stated = {}
    for line in block.group(1).splitlines():
        found = re.match(r"  (\d+)  (.+)", line)
        if found:
            stated[int(found.group(1))] = found.group(2).strip()
    if not stated:
        raise Failure(f"no status was parsed out of: {block.group(1)!r}")
    for status, words in sorted(stated.items()):
        # The runner's own wording, cut at the first sentence, because its
        # help adds a note after the usage status that a table cell does not
        # want.
        wanted = words.split(". ")[0].rstrip(".")
        given = generator.EXIT_MEANING.get(status)
        if given is None:
            raise Failure(f"the generator has no meaning for status {status}, "
                          f"which the runner documents as {wanted!r}")
        if given != wanted:
            raise Failure(f"status {status}: the runner says {wanted!r} and "
                          f"the generator says {given!r}")
    extra = sorted(set(generator.EXIT_MEANING) - set(stated))
    if extra:
        raise Failure(f"the generator documents {extra}, which the runner "
                      f"does not define")
    # And the verdict the report prints at each status, which is the thing a
    # reader acts on. Each run is built self-consistent with its status, so a
    # recovered run carries a recovery and a retried one carries a retry, the
    # way the runner writes them.
    for status in sorted(stated):
        record = {"kind": "run", "suites": 1, "passed": 1,
                  "recoveries": 1 if status == runner.EXIT_RECOVERED else 0,
                  "retried": 1 if status == runner.EXIT_RETRIED else 0,
                  "failed": 1 if status == runner.EXIT_SUITE_FAILED else 0}
        run = generator.Run(directory="runs",
                            parent={"kind": "run", "exit_code": status},
                            targets=[generator.TargetRun(
                                token="u64", slug="u64", run=record)])
        verdict = generator.overall_verdict(run)
        if status > runner.EXIT_RECOVERED:
            wanted = "FAIL"
        elif status == runner.EXIT_OK:
            wanted = "OK"
        else:
            # A retry and a recovery are one state: passed, with a caveat.
            wanted = "WARN"
        if verdict != wanted:
            raise Failure(f"exit {status}: the report says {verdict}, "
                          f"expected {wanted}")
    return f"{len(stated)} statuses, words and verdicts both"


@case(1, "OBS-16.6", "OBS-15.2")
def no_runner_variable_escapes_the_scrubbing_list() -> str:
    """A variable the runner exports cannot be forgotten by the scrubbing list.

    A scripted run is a run of its own, and every `E2E_` variable the gate has
    exported means something to the runner it starts. One that survives makes
    the fixture behave as part of the gate: measured live, a case that clears
    the collector-port variable and asserts a run with no collector compares
    nothing compared itself against the four ports the gate was collecting on,
    because a second port variable had been added to the runner and not to the
    list.

    Fixing the two names would have left the third occurrence available, and
    the list already carried a comment about a previous instance of the same
    thing. So the list is derived from the runner's own source and this case
    fails when the two disagree, which is what retires the defect class rather
    than another instance of it.
    """
    exported = runner_variables()
    if not exported:
        raise Failure("no E2E_ variable was found in the runner's source, so "
                      "this case is not checking anything")
    missing = sorted(exported - INHERITED_VARIABLES - KEPT_VARIABLES)
    if missing:
        raise Failure(f"the runner exports {missing}, which a scripted run "
                      f"would inherit. Add each to INHERITED_VARIABLES, or to "
                      f"KEPT_VARIABLES with the reason it is harmless.")
    # `E2E_ASSUME_FIX` is machine.py's, reached through the environment rather
    # than named in the runner, so it is expected not to appear there.
    stale = sorted(INHERITED_VARIABLES - exported - {"E2E_ASSUME_FIX"})
    if stale:
        raise Failure(f"the scrubbing list carries {stale}, which the runner "
                      f"no longer exports")
    return f"{len(exported)} variables, every one scrubbed"


@case(1, "OBS-2.17", exclusive=True)
def identical_interactions_collapse_and_bodies_are_kept_once() -> str:
    """An exhaustive log is only affordable if repetition costs nothing.

    A settle loop reads the same screen until it stops changing, which is the
    same request with the same 2000-byte answer thirty times. Written out that
    is thirty records and sixty thousand bytes of body for eight bytes of
    information; collapsed and content-addressed it is one record and one body.
    """

    with tempfile.TemporaryDirectory() as directory:
        with interaction_log(directory) as path:
            screen = bytes(range(256)) * 8
            for _ in range(30):
                interactions.record("rest", "GET /v1/machine:menu_screen",
                                    status=200, ms=11.0, body=screen)
            interactions.record("rest", "PUT /v1/machine:reset", status=200,
                                ms=9.0)
            for _ in range(3):
                interactions.record("rest", "GET /v1/machine:menu_screen",
                                    status=200, ms=12.0, body=screen)
            found = logged_interactions(path)
        bodies = sorted(os.listdir(os.path.join(directory, "bodies")))
        expect("three records for 34 interactions", len(found), 3)
        expect("the first is the whole settle loop", found[0]["repeat"], 30)
        if "until" not in found[0]:
            raise Failure("a collapsed record does not say when it stopped")
        expect("the mutation between them is its own record",
               found[1]["op"], "PUT /v1/machine:reset")
        expect("and the loop after it is not merged with the one before",
               found[2]["repeat"], 3)
        expect("one body on disk for both", len(bodies), 1)
        expect("named by the digest the records carry",
               bodies[0], found[0]["body_sha256"] + ".bin")
        expect("and its size is recorded", found[0]["body_bytes"], len(screen))
        with open(os.path.join(directory, "bodies", bodies[0]), "rb") as handle:
            expect("the body on disk is the body that arrived", handle.read(),
                   screen)
    return "34 interactions, 3 records, 1 body"


@case(1, "OBS-2.17", exclusive=True)
def a_short_answer_is_in_the_record_itself() -> str:
    """A reader should not have to open a file to see a 30-character error."""

    with tempfile.TemporaryDirectory() as directory:
        with interaction_log(directory) as path:
            interactions.record("rest", "GET /v1/machine:menu_screen",
                                status=404, ms=3.0, body=b"no menu is open")
            found = logged_interactions(path)
        stored = os.path.exists(os.path.join(directory, "bodies"))
    expect("inline", found[0]["body"], "no menu is open")
    expect("and nothing on disk", stored, False)

    # And a short answer that is not text, which is what a one-byte read is.
    with tempfile.TemporaryDirectory() as directory:
        with interaction_log(directory) as path:
            interactions.record("rest", "GET /v1/machine:readmem", status=200,
                                ms=13.0, body=b"\x1f")
            found = logged_interactions(path)
        stored = os.path.exists(os.path.join(directory, "bodies"))
    expect("the byte itself", found[0]["body_hex"], "1f")
    expect("and nothing on disk for it", stored, False)
    return "inline under the threshold, as text and as hex"


@case(1, "OBS-2.18")
def the_c64_screen_is_read_back_out_of_the_recorded_frame() -> str:
    """The device's own screen, as text, from the picture already in hand.

    The alternative is a `machine:readmem` of screen memory per screen against
    a device the suites are driving, which is device load this layer is not
    allowed to add. The frame arrived because the recording asked for the
    stream, and the C64 draws text as fixed shapes from a ROM this module
    already holds, so reading it back is exact rather than approximate.
    """
    sys.path.insert(0, os.path.join(ROOT, "tests", "e2e", "lib"))

    wanted = ["**** COMMODORE 64 BASIC V2 ****".ljust(40),
              " ".ljust(40),
              " 64K RAM SYSTEM  38911 BASIC BYTES FREE ",
              " ".ljust(40),
              "READY.".ljust(40)] + [" ".ljust(40)] * 20
    read = vic_text.decode(vic_frame(wanted), 384, 272)
    if read is None:
        raise Failure("a plain text screen was not read as one")
    expect("every row", len(read), vic_text.ROWS)
    for index, (was, now) in enumerate(zip(wanted, read)):
        if was != now:
            raise Failure(f"row {index} read back as {now!r}, not {was!r}")

    # Reverse video, which is how the machine marks a selection.
    inverted = bytearray(vic_frame(["ABC".ljust(40)] + [" " * 40] * 24))
    left, top = vic_text.picture_origin(384, 272)
    for y in range(top, top + 8):
        for x in range(left, left + 24):
            at = y * 384 + x
            inverted[at] = 6 if inverted[at] == 14 else 14
    read = vic_text.decode(bytes(inverted), 384, 272)
    if read is None or not read[0].startswith("ABC"):
        raise Failure(f"a reverse video row read back as {read[0]!r}"
                      if read else "a reverse video row was not read at all")

    # An NTSC frame, which is 32 lines shorter and centres the picture area
    # somewhere else.
    read = vic_text.decode(vic_frame(wanted, height=240), 384, 240)
    if read is None or read[0] != wanted[0]:
        raise Failure("an NTSC frame was not read")

    # A logo made of PETSCII graphics is still a text screen: the shapes are
    # in the ROM and have no ASCII form, which is a different answer from a
    # cell that is not a ROM shape at all. The C64's own boot screen has one,
    # and rejecting the frame for it would lose the text beside it.

    graphic = bytearray(vic_frame(["READY.".ljust(40)] + [" " * 40] * 24))
    shape = glyphs.rom_rows_for_index(100)
    for row in range(1, 20):
        for column in range(40):
            for line in range(8):
                bits = shape[line]
                base = ((top + row * 8 + line) * 384 + left + column * 8)
                for bit in range(8):
                    graphic[base + bit] = 14 if bits & (0x80 >> bit) else 6
    read = vic_text.decode(bytes(graphic), 384, 272)
    if read is None:
        raise Failure("a screen with a graphic logo on it was rejected")
    expect("the text beside it is still read", read[0], "READY.".ljust(40))
    expect("and the logo is marked rather than named",
           set(read[1]), {vic_text.GRAPHIC})

    # And something that is not a text screen at all says so rather than
    # returning a screen of question marks.
    noise = bytes((x * 7 + y * 13) % 16 for y in range(272) for x in range(384))
    expect("a frame that is not text", vic_text.decode(noise, 384, 272), None)
    return f"{vic_text.ROWS} rows, PAL and NTSC, reverse video, and a refusal"


@case(1, "OBS-2.18")
def a_scrolling_screen_is_read_at_the_column_it_is_really_in() -> str:
    """Every `$D016` state, both column modes, all eight fine scroll values.

    A frame taken while the KERNAL scrolls the screen is a correct picture in
    an ordinary VIC state, not a damaged one, and about a quarter of the frames
    a run keeps as stills are in it. Measured on a C64 Ultimate and on an
    Ultimate II+L in one: the picture area was 304 pixels wide at x=39 rather
    than 320 at x=32, which is 38-column mode with the fine scroll at 7.

    The decoder used to anchor its columns on the first pixel that is not the
    border. In 38-column mode that pixel is the window edge, which sits one
    cell inside the grid, so every cell was read one column to the left of
    where it really is. The shifted reading still matched the character ROM
    everywhere, because the cell it invents at the edge is blank, so it was
    returned as if it were right: READY. came back as EADY.
    """
    sys.path.insert(0, os.path.join(ROOT, "tests", "e2e", "lib"))

    wanted = ["READY.".ljust(40),
              'LOAD"$",8'.ljust(40),
              "SEARCHING FOR $".ljust(40)] + [" ".ljust(40)] * 22
    # Twice over: once on a frame that is the background colour everywhere, and
    # once on a frame with a real border around a real display window, which is
    # the shape the device sends. The second is not a variation of the first.
    # A grid origin the fine scroll has moved right reaches past the window at
    # some scroll values, and what it reaches is the border, whose colour is
    # not the background and is therefore read as ink. Eight pixel columns of
    # it make one cell that matches no ROM shape on every row of a 25-row
    # screen, which is 25 unreadable cells against a tolerance of 24, so a
    # frame the machine drew correctly was refused. On the boot screen the
    # border and the text are both light blue, which is why this uses one
    # colour for both.
    for border in (None, 14):
        shape = "with a border" if border else "without a border"
        for scroll in range(8):
            read = vic_text.decode(
                vic_frame(wanted, scroll=scroll, border=border), 384, 272)
            if read is None:
                raise Failure(f"40 columns {shape} at fine scroll {scroll} was "
                              "not read")
            for index, (was, now) in enumerate(zip(wanted, read)):
                if was != now:
                    raise Failure(f"40 columns {shape} at fine scroll {scroll}: "
                                  f"row {index} read back as {now!r}, not "
                                  f"{was!r}")

        # 38 columns: the VIC blanks one cell at each side, so the first column
        # is not in the picture at all and no decode can recover it. What
        # matters is that everything else keeps its own column number rather
        # than sliding one to the left to fill the gap.
        for scroll in range(8):
            read = vic_text.decode(
                vic_frame(wanted, scroll=scroll, columns=38, border=border),
                384, 272)
            if read is None:
                raise Failure(f"38 columns {shape} at fine scroll {scroll} was "
                              "not read")
            for index, (was, now) in enumerate(zip(wanted, read)):
                if was[1:] != now[1:]:
                    raise Failure(f"38 columns {shape} at fine scroll {scroll}: "
                                  f"row {index} read back as {now!r}, not "
                                  f"{was!r}")
                if now[0] not in (was[0], " ", vic_text.GRAPHIC):
                    raise Failure(f"38 columns {shape} at fine scroll {scroll}: "
                                  f"the blanked column read as {now[0]!r}")
        # The one the hardware was actually seen in reads completely: at fine
        # scroll 7 the blanked cell covers one pixel column of the first
        # character, and no character of the set carries ink there.
        read = vic_text.decode(
            vic_frame(wanted, scroll=7, columns=38, border=border), 384, 272)
        expect(f"the measured state reads in full {shape}", read[0], wanted[0])
    return ("40 and 38 columns, fine scroll 0 to 7, with and without a border, "
            "every column in place")


@case(1, "OBS-2.18")
def a_frame_the_decoder_cannot_read_is_counted_rather_than_dropped() -> str:
    """A refused frame leaves a number behind, not an absence.

    The decoder writes nothing for a frame that is not a text screen it can
    read, which is the right record to write. It is the wrong thing to leave as
    the only trace: a device drawing a bitmap, a screen in the shifted
    character set and a device whose screen simply did not change all produce
    the same silence in `screen-text.jsonl`, and the capture record's
    `screen_texts` counts only the successes. `screens_unreadable` is the other
    half, so a reader can see the proportion rather than infer it.

    Driven through the recorder's own screen reader rather than through a
    stream, because the interval between reads and the arrival of a frame are
    independent and a test that waits on both proves whichever one it happened
    to catch.
    """
    sys.path.insert(0, os.path.join(ROOT, "tests", "e2e", "lib"))


    wanted = ["READY.".ljust(40)] + [" ".ljust(40)] * 24
    noise = bytes((x * 7 + y * 13) % 16
                  for y in range(272) for x in range(384))
    with DeviceDouble() as double, tempfile.TemporaryDirectory() as directory:
        made = recorder_lib.Recorder(directory, "127.0.0.1",
                                     UltimateApi(double.target(), timeout=5.0),
                                     recorder_lib.Options(fps=5, audio=False))
        made.target = dataclasses.replace(
            double.target(), video_group="127.0.0.1", video_port=0)
        state = recorder_lib.RunState(suite="fixture", label="overlay")

        made._sources.frame = (384, 272, noise)
        made._maybe_read_screen(100.0, state)
        expect("the frame it could not read is counted",
               made.screens_unreadable, 1)
        expect("and no screen was written for it", made.screen_texts, 0)

        made._sources.frame = (384, 272, vic_frame(wanted, border=14))
        made._maybe_read_screen(200.0, state)
        expect("the frame it could read is written", made.screen_texts, 1)
        expect("and the refusals stay where they were",
               made.screens_unreadable, 1)

        made._sources.frame = (384, 272, noise)
        made._maybe_read_screen(300.0, state)
        expect("a second refusal counts again", made.screens_unreadable, 2)

        path = os.path.join(directory, "screen-text.jsonl")
        with open(path, encoding="utf-8") as handle:
            written = [json.loads(line) for line in handle]
        expect("one record, for the one frame that could be read",
               len(written), 1)
        expect("and it is the screen that was there", written[0]["text"][0],
               wanted[0])
    return "two refusals counted, one screen written"


@case(1, "OBS-2.17", exclusive=True)
def the_transcript_and_the_records_share_one_sequence() -> str:
    """One line a person reads, one record a program reads, one number joining.

    Two files that a reader has to align by timestamp are two files that
    disagree the moment two interactions land in one millisecond.
    """

    with tempfile.TemporaryDirectory() as directory:
        with interaction_log(directory) as path:
            interactions.note_menu(True)
            interactions.note_screen("READY.")
            interactions.record("rest", "POST /v1/machine:input", status=200,
                                ms=41.2, params="{'keys': 'J C003'}",
                                body=b'{"errors":[]}')
            interactions.note_menu(False)
            for _ in range(4):
                interactions.record("rest", "GET /v1/machine:menu_screen",
                                    status=404, ms=3.0)
            interactions.record("telnet", "connect u2:23", ms=12.0,
                                fault="refused", error="connection refused")
            found = logged_interactions(path)
            with open(os.path.join(directory,
                                   interactions.TRANSCRIPT_NAME),
                      encoding="utf-8") as handle:
                lines = [line for line in handle.read().splitlines() if line]

    expect("one line per record", len(lines), len(found))
    expect("numbered from one", [record["seq"] for record in found], [1, 2, 3])
    for record, line in zip(found, lines):
        if not line.split()[0] == str(record["seq"]):
            raise Failure(f"line {line!r} does not open with its own seq")
        if record["op"].split()[0] not in line:
            raise Failure(f"line {line!r} does not name {record['op']!r}")
    # The injection says what was on screen and whether the menu was open,
    # which is what tells a key the machine ignored from one an open menu
    # swallowed while answering 200.
    expect("the menu was open for the injection", found[0]["menu_open"], True)
    if not found[0].get("screen"):
        raise Failure("the injection does not say what was on screen")
    expect("and closed for the reads after it", found[1]["menu_open"], False)
    expect("the settle loop collapsed", found[1]["repeat"], 4)
    expect("the connection fault is one word", found[2]["fault"], "refused")
    if "menu=open" not in lines[0] or "menu=closed" not in lines[1]:
        raise Failure("the transcript does not carry the menu state")
    return f"{len(lines)} lines, {len(found)} records, one sequence"


@case(1, "OBS-2.17", "OBS-1.1")
def the_interaction_log_never_ends_a_run() -> str:
    """Nothing about recording an interaction may reach the caller.

    An observability component that fails a run it was watching is worse than
    one that is missing, and this one sits in the path of every device call in
    the tree.
    """

    class Awkward:
        def __repr__(self):
            raise RuntimeError("this object refuses to be described")

    previous = interactions.LOG_PATH
    interactions.set_path("/proc/this/cannot/be/written/interactions.jsonl")
    try:
        interactions.record("rest", "GET /v1/info", params=Awkward(),
                            body=Awkward())
        interactions.record("rest", "GET /v1/info", status=200)
        interactions.flush()
    finally:
        interactions.set_path(previous)
    # And with no destination at all, which is every run without -o.
    interactions.set_path("")
    interactions.record("rest", "GET /v1/info", status=200)
    interactions.flush()
    interactions.set_path(previous)
    return "an unwritable path and an undescribable object, both survived"


@case(1, "OBS-7.7", exclusive=True)
def a_device_pointed_at_another_port_is_named() -> str:
    """A run that will collect nothing says so before it collects nothing.

    The collector binds a port and the device sends to one, and comparing the
    two is the whole of it. A device configured with a bare address sends to
    514, because that is the firmware's default, and the collector binds 5514
    because 514 needs root. Measured on the C64 Ultimate here: 23 suites,
    `syslog.txt` empty, no warning anywhere, and a report that says the device
    said nothing, which is what a device that had stopped also looks like.
    """
    runner = load_runner()
    # Both variables, because this case runs inside the gate as a registered
    # suite and the gate's own collector exports them. Reading whichever one
    # the environment happened to carry is how this case came to compare a
    # fixture against the four ports a live run was collecting on.
    names = (runner.SYSLOG_PORT_ENV, runner.SYSLOG_PORTS_ENV)
    saved = {name: os.environ.get(name) for name in names}
    os.environ[runner.SYSLOG_PORT_ENV] = "5514"
    os.environ[runner.SYSLOG_PORTS_ENV] = "5514"
    try:
        expect("the right port is no problem",
               runner.syslog_setting_problem("192.168.1.185:5514"), "")
        bare = runner.syslog_setting_problem("192.168.1.185")
        if "port 514" not in bare or "5514" not in bare:
            raise Failure(f"a bare address is not named: {bare!r}")
        if "192.168.1.185:5514" not in bare:
            raise Failure(f"the warning does not say what to set: {bare!r}")
        wrong = runner.syslog_setting_problem("192.168.1.185:9999")
        if "port 9999" not in wrong:
            raise Failure(f"another port is not named: {wrong!r}")
        # Several ports, which is a bench that gives each machine one. A
        # device sending to any of them is collected.
        os.environ[runner.SYSLOG_PORTS_ENV] = "5514,5515,5516"
        expect("a port this run collects on is no problem",
               runner.syslog_setting_problem("192.168.1.185:5516"), "")
        outside = runner.syslog_setting_problem("192.168.1.185:5599")
        if "port 5599" not in outside or "5514, 5515, 5516" not in outside:
            raise Failure(f"a port outside the set is not named: {outside!r}")
        # A run with no collector compares nothing: the setting is then the
        # operator's business and not this run's.
        for name in names:
            os.environ.pop(name, None)
        expect("and with no collector there is nothing to compare",
               runner.syslog_setting_problem("192.168.1.185"), "")
    finally:
        for name, value in saved.items():
            if value is None:
                os.environ.pop(name, None)
            else:
                os.environ[name] = value
    return "a bare address, a wrong port, a port set and the right one"


@case(1, "OBS-15.8")
def a_reader_sees_every_line_the_collector_wrote() -> str:
    """A suite reads the file rather than the port, and in order.

    The last datagram carries several lines. This firmware's forwarding task
    sends one line per datagram, but nothing in the protocol requires that and
    the collector must not depend on it. Written as one output line it would
    be one timestamp followed by raw newlines, and every line after the first
    would be dropped by `read`.
    """


    ticks = iter([100.0 + n for n in range(10)])
    with tempfile.TemporaryDirectory() as directory:
        collector = syslog_collector.Collector(
            directory=directory, port=0, clock=lambda: next(ticks))
        collector.bind([targets_lib.parse("127.0.0.2")])
        try:
            for text in (b"the RTC says something",
                         b"ASSERTION FAIL: some_file.cc:42\na task list follows"):
                collector.deliver("127.0.0.2", text)
        finally:
            collector.stop()
        path = os.path.join(directory, "127.0.0.2", "syslog.txt")
        found = syslog_collector.read(path)
        expect("in order", [text for _when, text in found],
               ["the RTC says something", "ASSERTION FAIL: some_file.cc:42",
                "a task list follows"])
        expect("counted as lines, not as datagrams", collector.lines, 3)
    return "3 lines from 2 datagrams"


@case(1, "OBS-8.6", "OBS-8.24")
def a_frame_is_assembled_from_its_headers() -> str:
    """The payload belongs where the header says, not where it arrived."""


    made = streams.FrameAssembler()
    packets = video_packets(1, 0, pattern=5)
    out_of_order = [packets[3], *packets[:3], *packets[4:]]
    frame = None
    for packet in out_of_order:
        frame = made.push(packet) or frame
    if frame is None:
        raise Failure("no frame completed")
    expect("geometry", (frame.width, frame.height), (384, 272))
    pixels = streams.unpack(frame.packed)
    expect("one index per pixel", len(pixels), 384 * 272)
    # Low nibble first: the first pixel on the wire is the low half of the
    # first byte.
    expect("every pixel", set(pixels), {5})
    return f"{len(pixels)} pixels, out of order"


@case(1, "OBS-8.24")
def loss_and_reordering_are_counted_wrap_safely() -> str:
    """A 16-bit counter wrapping is one packet, not sixty-five thousand."""


    made = streams.FrameAssembler()
    # 65535 then 0, which a raw subtraction reads as a 65535-packet loss.
    wrapping = video_packets(1, 65535)
    made.push(wrapping[0])
    made.push(wrapping[1])
    expect("a wrap is not a loss", made.counts()["packets_dropped"], 0)

    lost = streams.FrameAssembler()
    packets = video_packets(2, 0)
    for packet in packets[:2] + packets[4:]:
        lost.push(packet)
    expect("the gap", lost.counts()["packets_dropped"], 2)
    expect("an incomplete frame never completes",
           lost.counts()["frames_completed"], 0)
    return "wrap 0, gap 2"


@case(1, "OBS-8.24", "OBS-8.25", "OBS-8.26")
def video_loss_is_told_apart_from_the_stream_not_running() -> str:
    """Nine cases over one counter, because they were one number.

    `frames_lost` counted every gap in the frame counter as a frame the
    network lost. The counter runs whether anything is receiving or not, so a
    suite that took the stream for a minute added a minute of frames to it: a
    green 23-suite sweep reported 14187 lost frames against 55409 completed
    ones on a link that dropped 253 packets.
    """


    # 1. An uninterrupted stream loses nothing.
    clean = streams.FrameAssembler()
    video_stream(clean, 1, 20)
    counts = clean.counts()
    expect("clean frames", counts["frames_completed"], 20)
    for name in ("frames_lost", "packets_dropped", "frames_incomplete",
                 "frames_reordered", "stream_discontinuities"):
        expect(f"clean {name}", counts[name], 0)

    # 2. Packets that were sent and did not arrive are loss.
    lossy = streams.FrameAssembler()
    for offset in range(4):
        packets = video_packets(offset + 1, offset * len(packets_of(1)))
        for index, packet in enumerate(packets):
            if offset == 2 and index in (10, 11, 12):
                continue
            lossy.push(packet)
    counts = lossy.counts()
    expect("the packets are counted as dropped", counts["packets_dropped"], 3)
    expect("the frame they broke never completes",
           counts["frames_completed"], 3)
    expect("not as a discontinuity", counts["stream_discontinuities"], 0)

    # 3. Frames whose last packet never came are incomplete, not lost. The
    #    assembler holds two at a time, so the third one pushes the first out.
    partial = streams.FrameAssembler()
    width = len(packets_of(1))
    for offset in range(4):
        for packet in video_packets(offset + 1, offset * width)[:-1]:
            partial.push(packet)
    counts = partial.counts()
    expect("none of them completed", counts["frames_completed"], 0)
    expect("two were given up as incomplete", counts["frames_incomplete"], 2)
    expect("and the missing packets are loss", counts["packets_dropped"], 3)
    expect("no frame is called lost", counts["frames_lost"], 0)

    # 4. A frame that finishes assembling after a later one is reordering.
    reordered = streams.FrameAssembler()
    first, second = video_packets(1, 0), video_packets(2, width)
    for packet in first[:-1] + second + first[-1:]:
        reordered.push(packet)
    counts = reordered.counts()
    expect("both frames arrived", counts["frames_completed"], 2)
    expect("the late one is reordering", counts["frames_reordered"], 1)
    expect("and not loss", counts["frames_lost"], 0)
    expect("nor a discontinuity", counts["stream_discontinuities"], 0)

    # 5. A 16-bit counter wrapping is one step forward.
    wrapping = streams.FrameAssembler()
    video_stream(wrapping, 65534, 4, sequence=65500)
    counts = wrapping.counts()
    expect("four frames across both wraps", counts["frames_completed"], 4)
    expect("no loss at the wrap", counts["frames_lost"], 0)
    expect("and none in the sequence", counts["packets_dropped"], 0)

    # 6. A suite stopping and starting the stream, which the caller declares.
    #    The device counts on while nothing is listening, so the jump is the
    #    length of the pause.
    declared = streams.FrameAssembler()
    video_stream(declared, 1, 5)
    declared.reanchor("suite-stopped")
    video_stream(declared, 9000, 5, sequence=40000)
    counts = declared.counts()
    expect("every frame either side", counts["frames_completed"], 10)
    expect("nothing counted as lost across it", counts["frames_lost"], 0)
    expect("nothing counted as dropped either", counts["packets_dropped"], 0)
    expect("one discontinuity", counts["stream_discontinuities"], 1)
    expect("with its reason", declared.discontinuities,
           {"suite-stopped": 1})

    # 7. The recorder asking for the stream again, which is the same shape
    #    with a different reason.
    rearmed = streams.FrameAssembler()
    video_stream(rearmed, 1, 3)
    rearmed.reanchor("recorder-rearm")
    video_stream(rearmed, 4000, 3, sequence=12000)
    expect("no loss for a rearm", rearmed.counts()["frames_lost"], 0)
    expect("named as a rearm", rearmed.discontinuities, {"recorder-rearm": 1})

    # 8. A device that restarted, which nobody declared and which the
    #    assembler has to find for itself.
    restarted = streams.FrameAssembler()
    video_stream(restarted, 30000, 4, sequence=40000)
    video_stream(restarted, 1, 4, sequence=0)
    counts = restarted.counts()
    expect("every frame", counts["frames_completed"], 8)
    expect("no loss across the restart", counts["frames_lost"], 0)
    expect("found and named", restarted.discontinuities.get("device-restart", 0)
           >= 1, True)

    # 9. And a gap small enough to be loss is still loss.
    small = streams.FrameAssembler()
    video_stream(small, 1, 2)
    video_stream(small, 12, 2, sequence=1000)
    expect("nine frames missing", small.counts()["frames_lost"], 9)
    expect("not called a discontinuity",
           small.counts()["stream_discontinuities"], 0)
    return "9 cases, loss and lifecycle apart"


@case(1, "OBS-8.24", "OBS-8.25")
def audio_loss_is_told_apart_from_the_stream_not_running() -> str:
    """The same nine distinctions on the audio timeline.

    Its concealment is what made the difference invisible: every slot the
    stream was stopped for was filled, and every filled slot was counted as
    packets the device had failed to send. A green sweep reported 29759 lost
    audio packets over a stream that lost none.
    """


    def timeline(packets):
        made = streams.AudioTimeline()
        for packet in packets:
            made.push(packet)
        return made

    # 1. Uninterrupted.
    clean = timeline(audio_packets(0, 20))
    counts = clean.counts()
    expect("written", counts["packets_written"], 20)
    for name in ("packets_lost", "packets_concealed", "packets_absent",
                 "resyncs", "stream_discontinuities", "late_dropped"):
        expect(f"clean {name}", counts[name], 0)

    # 2. Real loss is concealed and counted.
    lossy = timeline(audio_packets(0, 5) + audio_packets(8, 5))
    counts = lossy.counts()
    expect("three packets lost", counts["packets_lost"], 3)
    expect("and concealed", counts["packets_concealed"], 3)
    expect("none of it absent", counts["packets_absent"], 0)

    # 3. Reordering and duplication move nothing.
    packets = audio_packets(0, 6)
    shuffled = timeline([*packets[:3], packets[2], packets[4], packets[3], packets[5]])
    counts = shuffled.counts()
    expect("one duplicate", counts["duplicates"], 1)
    expect("one late", counts["late_dropped"], 1)
    expect("and the one that really was missing", counts["packets_lost"], 1)

    # 4. A wrap is one step.
    wrapping = timeline(audio_packets(65534, 4))
    expect("no loss at the wrap", wrapping.counts()["packets_lost"], 0)

    # 5. A declared stop and start.
    declared = streams.AudioTimeline()
    for packet in audio_packets(0, 5):
        declared.push(packet)
    declared.reanchor("suite-stopped")
    for packet in audio_packets(40000, 5):
        declared.push(packet)
    counts = declared.counts()
    expect("nothing lost across it", counts["packets_lost"], 0)
    expect("nothing concealed for it", counts["packets_concealed"], 0)
    expect("one discontinuity", counts["stream_discontinuities"], 1)
    expect("named", declared.discontinuities, {"suite-stopped": 1})

    # 6. A rearm.
    rearmed = streams.AudioTimeline()
    for packet in audio_packets(0, 3):
        rearmed.push(packet)
    rearmed.reanchor("recorder-rearm")
    for packet in audio_packets(9000, 3):
        rearmed.push(packet)
    expect("no loss for a rearm", rearmed.counts()["packets_lost"], 0)
    expect("named", rearmed.discontinuities, {"recorder-rearm": 1})

    # 7. A device restart, which the timeline finds for itself.
    restarted = timeline(audio_packets(60000, 4) + audio_packets(0, 4))
    counts = restarted.counts()
    expect("re-anchored", counts["resyncs"], 1)
    expect("nothing called lost", counts["packets_lost"], 0)

    # 8. A stream the run knows is not running owes the file audio and owes
    #    it no loss.
    absent = streams.AudioTimeline()
    for packet in audio_packets(0, 2):
        absent.push(packet)
    pcm = absent.absent(120)
    counts = absent.counts()
    expect("counted as absent", counts["packets_absent"], 120)
    expect("and not as loss", counts["packets_lost"], 0)
    expect("the file still gets its audio", len(pcm),
           120 * streams.PAYLOAD_BYTES)

    # 9. A stream that should have been running and was not is loss.
    quiet = streams.AudioTimeline()
    for packet in audio_packets(0, 2):
        quiet.push(packet)
    quiet.silence(7)
    counts = quiet.counts()
    expect("counted as loss", counts["packets_lost"], 7)
    expect("and not as absent", counts["packets_absent"], 0)

    # 10. A stream that has not started yet has lost nothing.
    fresh = streams.AudioTimeline()
    expect("nothing has arrived", fresh.anchored, False)
    for packet in audio_packets(0, 1):
        fresh.push(packet)
    expect("and now something has", fresh.anchored, True)
    return "10 cases, loss and lifecycle apart"


@case(1, "OBS-8.25")
def the_card_at_the_start_is_not_lost_audio() -> str:
    """The seconds before the first packet are not packets that went missing.

    The file opens on a card held for five seconds while the device is still
    being asked for the stream. The audio track has to be the same length as
    the video track, so those slots are filled, and filling them was counted
    as loss: 1275 lost audio packets on a run that lost about 25.
    """

    timeline = streams.AudioTimeline()
    cursor = recorder_lib.AudioCursor(timeline, 48000.0, 10)
    for _ in range(50):
        cursor.take()
    counts = timeline.counts()
    expect("nothing lost before the stream started", counts["packets_lost"], 0)
    expect("and it is counted as absent", counts["packets_absent"] > 0, True)
    if cursor.unavailable_bytes <= 0:
        raise Failure("the opening card's audio was not counted at all")
    expect("nothing concealed", cursor.concealed_bytes, 0)

    # Once the stream is running, a gap in it is loss again.
    for packet in audio_packets(0, 200):
        cursor.push(timeline.push(packet).pcm)
    for _ in range(20):
        cursor.take()
    if timeline.counts()["packets_lost"] <= 0:
        raise Failure("a gap in a running stream is no longer counted as loss")
    return "50 slots before the first packet, 0 lost"


@case(1, "OBS-8.24", "OBS-8.26")
def a_malformed_packet_is_dropped_and_counted() -> str:
    """A packet failing a format field is not from this stream."""


    made = streams.FrameAssembler()
    good = video_packets(1, 0)[0]
    wrong_width = struct.pack("<HHHHBBH", 0, 1, 0, 320, 4, 4, 0) + good[12:]
    wrong_depth = struct.pack("<HHHHBBH", 0, 1, 0, 384, 4, 8, 0) + good[12:]
    for packet in (wrong_width, wrong_depth, good[:100]):
        expect("ignored", made.push(packet), None)
    expect("counted", made.counts()["packets_malformed"], 3)
    expect("nothing written", made.counts()["packets_dropped"], 0)
    return "3 malformed"


@case(1, "OBS-8.17")
def a_geometry_change_is_carried_rather_than_dropped() -> str:
    """PAL and NTSC differ by 32 lines and a device can change mid-run."""


    made = streams.FrameAssembler()
    heights = []
    for number, height in ((1, 272), (2, 240)):
        for packet in video_packets(number, number * 100, height=height):
            frame = made.push(packet)
            if frame is not None:
                heights.append(frame.height)
    expect("both heights", heights, [272, 240])
    return "272 then 240"


@case(1, "OBS-8.25")
def audio_loss_is_concealed_rather_than_zero_filled() -> str:
    """Four outcomes, and the one that matters is what a duplicate does."""


    timeline = streams.AudioTimeline()
    packets = audio_packets(0, 4, sample=1000)
    straight = b"".join(timeline.push(packet).pcm for packet in packets)

    repeated = streams.AudioTimeline()
    with_duplicate = b"".join(
        repeated.push(packet).pcm
        for packet in [packets[0], packets[1], packets[1], packets[2],
                       packets[3]])
    # A duplicate that advanced the index would shift the audio against the
    # video by that packet's 4 ms, permanently, once per occurrence.
    expect("a duplicate changes nothing", with_duplicate, straight)
    expect("counted", repeated.counts()["duplicates"], 1)

    gapped = streams.AudioTimeline()
    gapped.push(packets[0])
    written = gapped.push(audio_packets(4, 1, sample=1000)[0])
    expect("the gap is filled", written.concealed_packets, 3)
    filler = written.pcm[:-streams.PAYLOAD_BYTES]
    if set(filler) == {0}:
        raise Failure("the gap was zero filled, which clicks at both ends")
    return "duplicate, late and gap"


@case(1, "OBS-8.25")
def a_large_jump_re_anchors_the_audio_timeline() -> str:
    """A backward jump of tens of thousands of packets is a device restart."""


    timeline = streams.AudioTimeline()
    timeline.push(audio_packets(30000, 1)[0])
    written = timeline.push(audio_packets(1, 1)[0])
    expect("nothing was concealed", written.concealed_packets, 0)
    expect("re-anchored", timeline.counts()["resyncs"], 1)
    return "one resync"


@case(1, "OBS-8.28", "OBS-8.31", "OBS-8.32", "OBS-8.33")
def the_stills_are_the_transitions_and_not_the_cursor() -> str:
    """A blinking cursor is not a screen change worth keeping."""

    def picked(frames):
        """The kinds the recorder's own picker chooses, in capture order.

        Driven through StillPicker rather than through a copy of the rule, so
        this fails when the code that ships changes.
        """
        picker = recorder_lib.StillPicker()
        for index, frame in enumerate(frames):
            picker.offer(frame, None, frame, frame=index, position=index / 10.0)
        return [still.kind for still in picker.stills()]

    blinking = [bytes([n % 2]) + bytes(52223) for n in range(20)]
    expect("no transitions", picked(blinking), ["first", "last"])

    # Two changes that stay changed, which is what a menu redraw looks like:
    # a screen that changed and changed back would be two transitions.
    changing = [bytes([0]) * 52224 for _ in range(20)]
    for index in range(5, 12):
        changing[index] = bytes([9]) * 52224
    for index in range(12, 20):
        changing[index] = bytes([3]) * 52224
    kinds = picked(changing)
    expect("both transitions", kinds.count("change"), 2)
    expect("first and last as well", (kinds[0], kinds[-1]), ("first", "last"))
    return "2 of 20"


@case(1, "OBS-8.28", "OBS-3.23")
def a_still_carries_the_frame_it_was_taken_from() -> str:
    """The position a still reports is the frame it is, not the suite's timing.

    The report used to place a still by the suite record around it, which is
    the interval the suite ran in rather than the moment the frame was kept.
    Measured against the recordings of a 23-suite sweep that put stills up to
    4.7 seconds away from the frame they show, in both directions.
    """

    picker = recorder_lib.StillPicker()
    frames = [bytes([0]) * 4096 for _ in range(30)]
    for index in range(10, 20):
        frames[index] = bytes([7]) * 4096
    for index in range(20, 30):
        frames[index] = bytes([4]) * 4096
    # Slot 40 onwards at 10 frames a second, so the first frame of this suite
    # run is four seconds into a file that opened with a card.
    for index, frame in enumerate(frames):
        picker.offer(frame, ["row"], frame, frame=40 + index,
                     position=(40 + index) / 10.0)
    chosen = picker.stills()
    expect("first, two transitions and last", [s.kind for s in chosen],
           ["first", "change", "change", "last"])
    for still in chosen:
        expect(f"the {still.kind} still's position is its own frame",
               round(still.position, 4), round(still.frame / 10.0, 4))
    expect("the first still is the first frame offered", chosen[0].frame, 40)
    expect("the last still is the last frame offered", chosen[-1].frame, 69)
    expect("the transitions are where the picture changed",
           [s.frame for s in chosen[1:3]], [50, 60])
    return "4 stills, each at its own frame"


@case(1, "OBS-8.27")
def frames_are_decimated_by_a_phase_accumulator() -> str:
    """The output rate is reached exactly, and the same way every run."""

    def taken(fps: int, source_frames: int) -> int:
        phase = 0
        kept = 0
        for _ in range(source_frames):
            phase, keep = recorder_lib.decimate(phase, fps)
            kept += 1 if keep else 0
        return kept

    # One second of a PAL device at each rate the recorder offers.
    for fps in (1, 2, 5, 10, 25, 50):
        expect(f"{fps} of 50 kept", taken(fps, 50), fps)
    # A ratio that divides into nothing: 7 in 50 is 7 every second, where
    # "take one in N" would give 7 in the first second and drift after it.
    expect("no drift over a minute", taken(7, 50 * 60), 7 * 60)
    return "exact at every rate"


@case(1, "OBS-5.9")
def a_dropped_telnet_session_does_not_capture_a_blank_screen() -> str:
    """The screen before the session died is what the suite was working on."""

    sys.path.insert(0, os.path.join(ROOT, "tests", "e2e", "lib"))

    with tempfile.TemporaryDirectory() as directory:
        path = os.path.join(directory, "screens.jsonl")
        with open(path, "w", encoding="utf-8") as handle:
            for when, rows in ((1.0, ["READY."]), (2.0, ["", "  "])):
                handle.write(json.dumps({
                    "kind": screen_spool.TELNET, "suite": "browse",
                    "attempt": 1, "time": when, "text": rows}) + "\n")
        last = screen_spool.last_before(path, 9.0, screen_spool.TELNET,
                                        suite="browse", attempt=1)
        expect("the last screen is the blank one", last["text"], ["", "  "])
        earlier = screen_spool.last_before(path, 9.0, screen_spool.TELNET,
                                           suite="browse", attempt=1,
                                           non_blank=True)
        expect("and the one before it is not", earlier["text"], ["READY."])
    # The report has to say which of the two it is showing, or a reader takes
    # the second-to-last screen for the one the suite ended on.
    generator = load_report_tool()
    for source in ("telnet-spool", "telnet-spool-earlier"):
        if source not in generator.SCREEN_SOURCE:
            raise Failure(f"the report has no wording for source={source}")
    if "ended" in generator.SCREEN_SOURCE["telnet-spool-earlier"]:
        raise Failure("the earlier screen is described as the one the suite "
                      "ended on")
    return "the blank screen is not the only answer, and the report says so"


@case(1, "OBS-7.5", "OBS-7.6", exclusive=True)
def a_second_interface_can_be_declared() -> str:
    """A device logs from whichever interface the route picked.

    Measured on the U64 here: REST answers on the Ethernet address and the log
    arrives from the WiFi one, and no route on the device reports either.

    The machine is named `localhost` rather than `u64` because what is under
    test is the merge of a declared address into a resolved one, and the typo
    report beside it, neither of which is about name resolution. A device name
    resolves instantly on the LAN that has the device and costs a full lookup
    timeout, measured at about 113 seconds, on a CI runner that does not.
    `localhost` is in every host's own hosts file, so the resolved half of the
    merge is a real lookup that never reaches a name server.
    """
    sys.path.insert(0, os.path.join(ROOT, "tests", "lib"))

    previous = os.environ.get(syslog_collector.ADDRESS_ENV)
    os.environ[syslog_collector.ADDRESS_ENV] = ("localhost=192.0.2.71,"
                                                "c64u=192.0.2.9")
    try:
        found = syslog_collector.resolve("localhost")
        if "192.0.2.71" not in found:
            raise Failure(f"the declared address is not in {found}")
        if "127.0.0.1" not in found:
            raise Failure(f"the resolved address was lost: {found}")
        if "192.0.2.9" in found:
            raise Failure("another machine's declared address was taken")
        # A name no target has is a typo, and its symptom is the one this
        # variable exists to remove, so the run says so once at the start.
        import tempfile

        with tempfile.TemporaryDirectory() as directory:
            collector = syslog_collector.Collector(directory=directory, port=0)
            collector.bind([targets.parse("localhost")])
            collector.stop()
        if not any("is not a machine of any target" in problem
                   for problem in collector.problems):
            raise Failure(f"a declared name nobody has went unreported: "
                          f"{collector.problems}")
    finally:
        if previous is None:
            os.environ.pop(syslog_collector.ADDRESS_ENV, None)
        else:
            os.environ[syslog_collector.ADDRESS_ENV] = previous
    return "the second interface is attributed, a typo is reported"


@case(1, "OBS-2.5")
def every_record_kind_is_in_the_table() -> str:
    """The record-shape table names every kind and every new field.

    The table is what a reader of a JSONL file consults, and a kind that is
    written and not in it is a field nobody outside this repository can
    interpret.
    """
    path = os.path.join(ROOT, "tests", "lib", "README.md")
    with open(path, encoding="utf-8") as handle:
        text = handle.read()
    for kind in ("check", "scenario", "suite", "health", "warning", "log",
                 "capture", "plan", "action", "interaction", "vic", "run"):
        if f"| `{kind}` |" not in text:
            raise Failure(f"the table has no row for kind={kind}")
    for field in ("target", "attempt", "targets", "exit_code", "lead_in",
                  "stills", "seq", "fault", "menu_open"):
        if f"`{field}" not in text:
            raise Failure(f"the table does not name the {field} field")
    return "11 kinds, every new field"


@case(1, "OBS-4.9", "OBS-4.10")
def the_gate_workflow_is_the_one_described() -> str:
    """The workflow file, read as the facts the specification states about it."""
    path = os.path.join(ROOT, ".github", "workflows", "e2e.yml")
    with open(path, encoding="utf-8") as handle:
        text = handle.read()
    for wanted, why in (
            ("runs-on: [self-hosted, e2e]",
             "an unlabelled job lands on the build machine"),
            ("cancel-in-progress: false",
             "cancelling mid-suite leaves a device in an unknown state"),
            ("e2e-report-${{ github.run_id }}", "the report artifact"),
            ("e2e-video-${{ github.run_id }}", "the recordings artifact"),
            ("retention-days: 7", "the recordings have their own lifetime"),
            ("steps.gate.outputs.status",
             "the job decides on the gate's exit status, and a workflow can "
             "read a step's outcome but not the number it exited with"),
            ('GATE_OUTCOME" = "cancelled"',
             "a cancelled or timed-out gate has to fail the job, and it has "
             "no status to compare")):
        if wanted not in text:
            raise Failure(f"{wanted!r} is not in the workflow: {why}")

    # The job tolerates a caveat and fails an outcome. A workflow that failed
    # on a retry would cancel the point of retrying, because a flake would
    # still fail the gate; what keeps a retried pass from being ignored is
    # that it is loud rather than that it is red.
    runner = load_runner()
    decide = text.split("Decide on the gate's own status", 1)[1]
    for status in (runner.EXIT_OK, runner.EXIT_RETRIED, runner.EXIT_RECOVERED):
        if f"\n            {status})" not in decide:
            raise Failure(f"the workflow does not tolerate exit {status}, "
                          f"which means every suite passed")
    for status in (runner.EXIT_SUITE_FAILED, runner.EXIT_DEVICE_UNHEALTHY,
                   runner.EXIT_USAGE):
        if f"\n            {status})" in decide:
            raise Failure(f"the workflow tolerates exit {status}, which is an "
                          f"outcome rather than a caveat")
    if "${{ inputs" in text.split("run: |", 1)[1].split("- name", 1)[0]:
        raise Failure("an input is substituted into the script rather than "
                      "passed through the environment")

    # The gate asks for what it uploads. It uploaded a recordings artifact
    # while running `run-tests` with neither --syslog nor --record, so the
    # artifact was always empty and a scheduled failure left a console log and
    # nothing else.
    command = text.split("run: |", 1)[1].split("- name", 1)[0]
    for flag in ("$SYSLOG", "$RECORD"):
        if flag not in command:
            raise Failure(f"the gate never passes {flag[1:].lower()} to "
                          "run-tests, but uploads what it produces")
    for wanted, why in (
            ("&& '--syslog' || ''",
             "a variable holding the string false is not an empty variable"),
            ("&& '--record' || ''", "the same for the recorder"),
            ("github.event_name == 'schedule'",
             "the unattended run is the one that needs the evidence")):
        if wanted not in text:
            raise Failure(f"{wanted!r} is not in the workflow: {why}")
    # Every physical target the harness supports, because a gate that leaves
    # one out reports nothing about it.
    scheduled = text.split("TARGETS:", 1)[1].split("\n", 1)[0]
    for target in ("c64u", "u64", "u2@c64u"):
        if target not in scheduled:
            raise Failure(f"the scheduled gate does not run {target}")
    return "two artifacts, one runner label, three targets, both collectors"


@case(1, "OBS-1.6")
def the_only_mutation_an_observer_makes_is_the_arming() -> str:
    """Everything watching a run reads; only the recorder's arming writes.

    Checked in the components' own sources rather than in a run's records,
    because an action record does not say who made the request: a `PUT` from
    the suites driving the UI and a `PUT` from an observer look the same in
    the log, and it is the second that this rule is about.
    """
    components = (
        os.path.join(ROOT, "tests", "lib", "syslog_collector.py"),
        os.path.join(ROOT, "tests", "lib", "health.py"),
        os.path.join(ROOT, "tests", "e2e", "lib", "recorder.py"),
        os.path.join(ROOT, "tests", "e2e", "lib", "screens.py"),
        os.path.join(ROOT, "tools", "e2e_report.py"),
    )
    mutating = ('"PUT"', "'PUT'", '"POST"', "'POST'", '"DELETE"', "'DELETE'",
                ".reset(", ".streams.start(", ".streams.stop(")
    for path in components:
        with open(path, encoding="utf-8") as handle:
            for number, line in enumerate(handle, 1):
                if line.lstrip().startswith("#"):
                    continue
                for token in mutating:
                    if token in line:
                        raise Failure(f"{os.path.relpath(path, ROOT)}:{number} "
                                      f"makes a {token} request")
    # The exception, which is granted to exactly one class.
    arming = os.path.join(ROOT, "tests", "e2e", "lib", "streams.py")
    with open(arming, encoding="utf-8") as handle:
        text = handle.read()
    body = text.split("class Arming:", 1)[1].split("\ndef ", 1)[0]
    for call in ("self.api.streams.start(", "self.api.streams.stop("):
        if call not in body:
            raise Failure(f"{call} is not in Arming, so the exception has "
                          f"moved somewhere this does not check")
    return "five components read only, one class arms"


@case(1, "OBS-5.2")
def the_capture_is_taken_before_the_state_gate() -> str:
    """The screen a suite left is read before anything else drives the UI.

    `ui_state.verify` presses RUN/STOP, opens the file browser and closes the
    menu, so a capture taken after it shows the harness's own tidying rather
    than what the suite left. The order is a fact about one function, which is
    why it is checked as one.

    """
    with open(RUNNER_PATH, encoding="utf-8") as handle:
        text = handle.read()
    body = text.split("def run_one_attempt(", 1)[1].split("\ndef ", 1)[0]
    capture = body.find("capture_failure(")
    gate = body.find('ui_state_gate("verify"')
    if capture < 0 or gate < 0:
        raise Failure("run_one_attempt no longer calls both")
    if capture > gate:
        raise Failure("the capture is taken after the state gate, so it shows "
                      "the gate's own navigation")
    return "the capture comes first"


@case(1, "OBS-8.16", "OBS-15.1")
def a_rearm_waits_for_the_suite_and_backs_off() -> str:
    """A suite owns the stream from its first record, and a dead device is cheap."""


    with tempfile.TemporaryDirectory() as directory:
        tail = recorder_lib.JsonlTail(directory)
        expect("nothing has run yet", tail.poll().between_suites, True)
        # One action record, which is what a suite writes long before its
        # first check closes. av/stream_test.py and api/input_test.py own the
        # stream in exactly that window.
        path = os.path.join(directory, "overlay-stream.jsonl")
        with open(path, "w", encoding="utf-8") as handle:
            handle.write(json.dumps({"kind": "action", "suite": "stream",
                                     "time": 1.0}) + "\n")
        expect("a suite is running", tail.poll().between_suites, False)
        with open(path, "a", encoding="utf-8") as handle:
            handle.write(json.dumps({"kind": "suite", "name": "stream",
                                     "verdict": "OK", "time": 2.0}) + "\n")
        expect("and has finished", tail.poll().between_suites, True)

    # The back-off: a device that answers nothing is asked a handful of times
    # over ten minutes rather than a hundred.
    wait = recorder_lib.REARM_AFTER_SECONDS
    ceiling = recorder_lib.REARM_BACKOFF_CEILING_SECONDS
    asked, elapsed = 0, 0.0
    while elapsed < 600:
        asked += 1
        elapsed += wait
        wait = min(ceiling, wait * 2)
    if asked > 12:
        raise Failure(f"{asked} re-arms in ten minutes is not a back-off")
    return f"{asked} re-arms in ten minutes"


@case(1, "OBS-8.14", "OBS-8.20")
def a_file_that_is_not_a_suite_run_s_own_cannot_rename_the_run() -> str:
    """Only a file whose name carries the label may say what the label is.

    The suite run a frame belongs to is read from the name of the file the
    record was appended to, because that name is the only thing that carries
    the label. Several files in a target's directory carry a `suite` field and
    are not one suite run's records: the screen spool, the interaction log and
    the decoded screen text. Their names carry no label.

    Treating one of them as a suite run's file blanked the label on the next
    poll, so the identity flapped between `overlay-input-1` and `-input-1`
    every time the recorder wrote a line. Each flap is a change of suite run to
    the recorder: it wrote the stills it had, threw the picker away and started
    a new one. A 24-suite run produced 994 still records naming 290 files, and
    the frame each one named was the frame at some earlier flap rather than the
    frame the file on disk holds. Extracting the video at the recorded position
    then reproduced a different picture, which is the one property a still's
    position exists to have.
    """


    with tempfile.TemporaryDirectory() as directory:
        tail = recorder_lib.JsonlTail(directory)
        with open(os.path.join(directory, "overlay-input.jsonl"), "w",
                  encoding="utf-8") as handle:
            handle.write(json.dumps({"kind": "check", "index": 1,
                                     "suite": "input", "attempt": 1,
                                     "verdict": "OK", "time": 1.0}) + "\n")
        expect("the suite run is named after its own file",
               tail.poll().stem, "overlay-input-1")

        # Every file a target's directory holds that carries a suite name and
        # is not one suite run's records.
        for name, record in (
                ("screen-text.jsonl", {"kind": "vic", "suite": "input",
                                       "attempt": 1, "text": [], "time": 2.0}),
                ("interactions.jsonl", {"kind": "interaction", "seq": 1,
                                        "suite": "input", "attempt": 1,
                                        "time": 3.0}),
                ("screens.jsonl", {"kind": "screen", "suite": "input",
                                   "attempt": 1, "time": 4.0})):
            with open(os.path.join(directory, name), "a",
                      encoding="utf-8") as handle:
                handle.write(json.dumps(record) + "\n")
            if tail.poll().stem != "overlay-input-1":
                raise Failure(f"{name} renamed the suite run to "
                              f"{tail.state.stem!r}")

        # And a suite run's own file still names it, including the next one.
        with open(os.path.join(directory, "telnet-menu-screen.jsonl"), "w",
                  encoding="utf-8") as handle:
            handle.write(json.dumps({"kind": "check", "index": 1,
                                     "suite": "menu-screen", "attempt": 1,
                                     "verdict": "OK", "time": 5.0}) + "\n")
        expect("the next suite run renames it",
               tail.poll().stem, "telnet-menu-screen-1")
    return "three shared files, none of them a suite run"


@case(1, "OBS-8.27", "OBS-8.38")
def an_encoder_that_takes_nothing_cannot_stall_the_loop() -> str:
    """The frame is shed inside its budget rather than blocking on the pipe."""


    # A process that reads nothing at all, so its pipe fills and stays full.
    encoder = recorder_lib.Encoder.__new__(recorder_lib.Encoder)
    encoder.path = "stalled"
    encoder.frames = 0
    encoder.shed = 0
    encoder.problem = ""
    encoder.finishing = None
    encoder.process = subprocess.Popen(
        [sys.executable, "-c", "import time; time.sleep(30)"],
        stdin=subprocess.PIPE, stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL, bufsize=0)
    try:
        frame = b"\x00" * (872 * 272 * 3)
        started = time.monotonic()
        # The first write fills the pipe; the second has nowhere to go.
        for _ in range(2):
            encoder.write(frame, budget=0.2)
        took = time.monotonic() - started
    finally:
        # Killed rather than closed: this one is sleeping on purpose and
        # would hold the suite for the encoder's whole exit budget.
        if encoder.process is not None:
            encoder.process.kill()
            encoder.process.wait()
        encoder.close(wait=False)
    if took > 2.0:
        raise Failure(f"two writes of one budget took {took:.1f}s, so the "
                      f"loop blocks on a full pipe")
    expect("shed rather than written", encoder.shed >= 1, True)
    return f"{took:.2f}s for two 0.2s budgets"


@case(1, "OBS-8.20")
def the_harness_pane_has_four_states() -> str:
    """Each says something different, and the menu is drawn from its payload."""

    geometry = recorder_lib.geometry_for(False, True, "combined")["combined"]
    options = recorder_lib.Options(stamp=False, video=False)
    composer = recorder_lib.Composer(geometry, options, {})
    state = recorder_lib.RunState()

    def drawn(rows, kind, raw=b"", stale=False):
        return composer.compose(None, rows, kind, state, 0.0, 0.0,
                                harness_raw=raw, stale=stale)

    nothing = drawn(None, "")
    closed = drawn(None, "closed")
    if nothing == closed:
        raise Failure("a menu that is closed looks like one never read")
    # A payload with the selected row marked by bit 7, which is the only
    # thing that says where the cursor is.
    payload = bytearray(b" " * 1000 + b"\x01" * 1000)
    payload[40:48] = b"SELECTED"
    marked = bytearray(payload)
    for index in range(40, 48):
        marked[index] |= 0x80
    rows = [" " * 40] * 25
    plain = drawn(rows, "menu", bytes(payload))
    reversed_row = drawn(rows, "menu", bytes(marked))
    if plain == reversed_row:
        raise Failure("the selected row is not visible in the pane, so the "
                      "pane is drawn from the text rather than the payload")
    if drawn(rows, "menu", bytes(payload), stale=True) == plain:
        raise Failure("a stale screen is not marked")
    return "four states, and the cursor is in the picture"


@case(1, "OBS-8.19", "OBS-8.25")
def a_slot_of_audio_is_the_rate_it_declares() -> str:
    """The track and the video stay the same length, remainder included."""

    timeline = streams.AudioTimeline()
    rate = streams.RATE_PAL_HZ
    cursor = recorder_lib.AudioCursor(timeline, rate, 10)
    wanted = sum(cursor.wanted() for _ in range(600)) // streams.FRAME_BYTES
    # A minute at 10 slots a second. Rounding each slot down would lose about
    # 175 frames of it, which is drift the file cannot be corrected for.
    expect("a minute of frames", wanted, round(rate * 60))

    # A slot that is one packet short is jitter, not loss.
    cursor = recorder_lib.AudioCursor(timeline, rate, 10)
    cursor.push(b"\x00" * (cursor.wanted() - streams.PAYLOAD_BYTES))
    cursor.owed = 0.0
    cursor.take()
    expect("no loss reported", timeline.counts()["packets_lost"], 0)
    if not cursor.filled_bytes:
        raise Failure("the shortfall was not counted at all")
    return "exact length, jitter is not loss"


@case(1, "OBS-8.20", "OBS-8.29", "OBS-8.30", "OBS-8.35")
def the_canvas_is_the_shape_the_sources_make() -> str:
    """Dropping a source changes the canvas rather than leaving a blank pane."""


    # The panes, the band under them, and room under that for the progress bar
    # and the state edge. Derived from the parts rather than written down, so a
    # change to any of them moves this together with the thing it measures.
    height = (recorder_lib.PANE_HEIGHT + band_lib.HEIGHT
              + recorder_lib.EDGE_PIXELS + recorder_lib.BAR_HEIGHT
              + recorder_lib.BAR_GAP)
    combined = recorder_lib.geometry_for(True, True, "combined")["combined"]
    expect("both panes and a gutter", (combined.width, combined.height),
           (872, height))
    expect("everything below the panes is the recorder's own chrome",
           combined.height - recorder_lib.PANE_HEIGHT,
           recorder_lib.CHROME_BOTTOM_PIXELS)
    expect("and a still keeps only the panes",
           recorder_lib.still_height(combined), recorder_lib.PANE_HEIGHT)
    video_only = recorder_lib.geometry_for(True, False, "combined")["combined"]
    expect("video only", video_only.width, 384)
    harness_only = recorder_lib.geometry_for(False, True, "combined")["combined"]
    expect("harness only", harness_only.width, 480)
    separate = recorder_lib.geometry_for(True, True, "separate")
    expect("two files", sorted(separate), ["harness", "screen"])
    expect("no gutter in either",
           [separate["harness"].width, separate["screen"].width], [480, 384])
    for geometry in (combined, video_only, harness_only):
        if geometry.width % 2 or geometry.height % 2:
            raise Failure(f"{geometry} has an odd dimension")
    return f"872x{height}, 384x{height}, 480x{height}"


@case(1, "OBS-8.40")
def the_bands_columns_are_where_the_header_says_they_are() -> str:
    """Every field starts under its own name, in order, inside the band.

    A column that does not line up with its header is worse than no header:
    a reader takes the wrong number off the wrong line and does not know it.
    """

    layout = band_lib.layout_for(872)
    fields = layout.fields()
    names = ("time", "type", "interaction", "stat", "dur", "sent", "rcvd",
             "body", "ref")
    line = band_lib.header(layout)
    expect("the header is exactly the band's width", len(line), layout.columns)
    at = 0
    for (start, size), name in zip(fields, names):
        if len(name) > size:
            raise Failure(f"the {name} column is narrower than its own name")
        if line[start:start + len(name)] != name:
            raise Failure(f"the {name} header is not at column {start}: "
                          f"{line[start:start + size]!r}")
        if start < at:
            raise Failure(f"the {name} column overlaps the one before it")
        if start + size > layout.columns:
            raise Failure(f"the {name} column runs off the band")
        at = start + size
    # The interaction absorbs the slack, and it is the only field that does.
    narrow = band_lib.layout_for(872 - 20 * band_lib.glyphs.GLYPH_WIDTH)
    widths = [size for _start, size in fields]
    narrow_widths = [size for _start, size in narrow.fields()]
    moved = [i for i, (a, b) in enumerate(zip(widths, narrow_widths)) if a != b]
    expect("only the interaction column resizes", moved, [2])
    expect("and it lost exactly the columns the band lost",
           widths[2] - narrow_widths[2], 20)
    return f"{layout.columns} columns, interaction {widths[2]} wide"


@case(1, "OBS-8.40")
def the_band_formats_a_number_the_same_way_on_every_line() -> str:
    """Byte counts, durations and truncation, at the widths the columns allow."""

    # 1000 and 1023 are the two that say the units are binary: a decimal
    # thousand would call either of them a kilobyte.
    sizes = [(None, ""), (0, ""), (1, "1B"), (999, "999B"), (1000, "1000B"),
             (1023, "1023B"), (1024, "1.00K"),
             (10 * 1024, "10.0K"), (100 * 1024, "100K"),
             (1024 * 1024, "1.00M"), (int(2.7 * 1024 ** 3), "2.70G")]
    for count, wanted in sizes:
        expect(f"{count} bytes", band_lib.size_of(count), wanted)
        if len(band_lib.size_of(count)) > band_lib.BYTES_WIDTH:
            raise Failure(f"{count} bytes does not fit its column")
    durations = [(None, ""), (0.0005, "0ms"), (0.25, "250ms"), (1.0, "1.0s"),
                 (99.9, "99.9s"), (120.0, "120s")]
    for seconds, wanted in durations:
        expect(f"{seconds} seconds", band_lib.duration_of(seconds), wanted)
        if len(band_lib.duration_of(seconds)) > band_lib.DURATION_WIDTH:
            raise Failure(f"{seconds} seconds does not fit its column")
    # Truncation keeps both ends, because a path identifies itself at both.
    cut = band_lib.middle_truncate("/v1/drives:mount label=disks/game.d64", 20)
    expect("cut to the column", len(cut), 20)
    if not cut.startswith("/v1/drives") or not cut.endswith("game.d64"):
        raise Failure(f"the head or the tail was lost: {cut!r}")
    expect("nothing to cut", band_lib.middle_truncate("short", 20), "short")
    return f"{cut!r}"


@case(1, "OBS-8.40")
def the_band_shows_what_the_run_is_doing_and_counts_the_rest() -> str:
    """Polling is counted and never shown; work is shown; repeats collapse."""

    ticker = band_lib.Ticker()
    polling = [{"transport": "rest", "op": "GET /v1/machine:menu_screen",
                "status": 404, "clock": "00:00:01", "reference": "#1",
                "received": 40} for _ in range(50)]
    ticker.apply(polling, now=1.0)
    expect("no polling line", len(ticker.lines), 0)
    expect("but every one counted", ticker.counts.get("rest"), 50)
    ticker.apply([{"transport": "ftp", "op": "STOR game.prg", "status": 226,
                   "clock": "00:00:02", "reference": "#51", "sent": 8192}],
                 now=2.0)
    expect("work is shown", len(ticker.lines), 1)
    # Four identical calls are one line naming the range, not four copies.
    repeats = [{"transport": "rest", "op": "PUT /v1/machine:writemem",
                "status": 200, "clock": "00:00:03",
                "reference": f"#{60 + n}"} for n in range(4)]
    ticker.apply(repeats, now=3.0)
    expect("collapsed into one line", len(ticker.lines), 2)
    expect("naming the range it covers", ticker.lines[-1].reference,
           "#60-63")
    # And the band keeps only what it can show.
    ticker.apply([{"transport": "telnet", "op": f"send {n}", "status": "ok",
                   "clock": "00:00:04", "reference": f"#{80 + n}"}
                  for n in range(10)], now=4.0)
    expect("never more lines than rows", len(ticker.lines), ticker.rows)
    counters = ticker.counters(band_lib.layout_for(872))
    for wanted in ("ftp 1", "rest 54", "tel 10", "tx ", "rx ", "av "):
        if wanted not in counters:
            raise Failure(f"the counter row does not carry {wanted!r}: "
                          f"{counters!r}")
    return counters.strip()


@case(1, "OBS-2.17", "OBS-8.40", exclusive=True)
def sent_and_received_are_byte_counts_on_every_transport() -> str:
    """One meaning per field name, across REST and Telnet.

    The Telnet exchange wrote the payload text into `sent` while REST wrote a
    byte count into it, so a reader and the band both had to know which
    transport a record came from before they could read a field. The band added
    them up, and one Telnet keystroke, `send F5` with `\x1b[15~` in `sent`,
    stopped a recording 1168 frames into an 8850-frame run.
    """


    with tempfile.TemporaryDirectory() as directory:
        with interaction_log(directory) as path:
            session = types.SimpleNamespace(
                _sent=("F5", b"\x1b[15~", time.monotonic()))
            ui_backend.TelnetBackend._record_exchange(session, 753)
            session._sent = None
            ui_backend.TelnetBackend._record_exchange(session, 40)
            interactions.record("rest", "PUT /v1/machine:writemem",
                                status=200, ms=12.0, sent=184, received=175)
            found = logged_interactions(path)

    for record in found:
        for name in ("sent", "received"):
            value = record.get(name)
            if value is not None and not isinstance(value, (int, float)):
                raise Failure(f"{record['op']} wrote {name}={value!r}, which "
                              f"is not a byte count")
    exchange = next(r for r in found if r["op"] == "send F5")
    expect("the count of what went out", exchange.get("sent"), 5)
    expect("and of what came back", exchange.get("received"), 753)
    expect("with the keystroke itself in payload",
           "1b[15~" in str(exchange.get("payload")), True)

    # And the band, which is what broke, adds them up rather than stopping.
    ticker = band_lib.Ticker()
    ticker.apply(found, now=1.0)
    expect("every byte counted once", (ticker.sent, ticker.received),
           (5 + 184, 753 + 40 + 175))
    return "sent and received are counts, payload is what was sent"


@case(1, "OBS-8.40")
def the_band_never_stops_the_recording_over_one_record() -> str:
    """A field of the wrong shape costs a counter, not the rest of the run.

    The band is drawn from records the transports write while the run is
    happening. It watches the run; it may not be able to end the evidence of
    one.
    """

    ticker = band_lib.Ticker()
    ticker.apply([{"transport": "telnet", "op": "send F5",
                   "sent": "'\\x1b[15~'", "received": None,
                   "clock": "00:00:01", "reference": "#1"},
                  {"transport": "rest", "op": "PUT /v1/machine:writemem",
                   "status": 200, "sent": 184, "received": 12,
                   "clock": "00:00:02", "reference": "#2"}], now=1.0)
    expect("the good record still counted", (ticker.sent, ticker.received),
           (184, 12))
    expect("and both lines are on the band", len(ticker.lines), 2)
    # Drawn as well as counted: a line whose byte field was not a number has
    # nothing in that column rather than an exception in the composer.
    glyphs = recorder_lib.glyphs
    layout = band_lib.layout_for(872)
    canvas = glyphs.Canvas(872, band_lib.HEIGHT, 6)
    band_lib.draw(canvas, 0, 0, 872, ticker, layout, "SUITE X > CHECK Y",
                  band_lib.RUNNING,
                  {"background": 6, "primary": 1, "secondary": 15,
                   "failure": 2, "warning": 7, "accent": 3}, 1.0)
    return "one malformed field costs one counter"


@case(1, "OBS-8.40")
def a_line_is_stamped_when_it_is_issued_and_never_moves() -> str:
    """An interaction appears while it is in flight and is finalised in place."""

    ticker = band_lib.Ticker()
    ticker.apply([{"transport": "rest", "op": "PUT /v1/machine:reset",
                   "phase": "start", "clock": "00:00:05", "reference": "#7",
                   "seconds": 0.2}], now=5.0)
    expect("the line is there before the answer", len(ticker.lines), 1)
    expect("and says so", ticker.lines[0].status, "...")
    expect("the run is running", ticker.state(5.0, False, False),
           band_lib.RUNNING)
    # Something else lands while the first is still open.
    ticker.apply([{"transport": "ftp", "op": "LIST /", "status": 226,
                   "clock": "00:00:06", "reference": "#8"}], now=6.0)
    expect("which does not displace it", ticker.lines[0].reference, "#7")
    # Held long enough, the same line says it is stuck, without moving.
    ticker.lines[0].seconds = band_lib.STALL_SECONDS
    expect("stalled", ticker.state(6.0, False, False), band_lib.STALLED)
    ticker.apply([{"transport": "rest", "op": "PUT /v1/machine:reset",
                   "phase": "end", "status": 204, "clock": "00:00:07",
                   "reference": "#7", "seconds": 2.5, "received": 12}],
                 now=7.0)
    expect("still the first line", ticker.lines[0].reference, "#7")
    expect("now answered", ticker.lines[0].status, "204")
    expect("with its own duration", ticker.lines[0].seconds, 2.5)
    expect("and no extra line for the answer", len(ticker.lines), 2)
    expect("nothing in flight", ticker.state(7.0, False, True),
           band_lib.PASSED)
    expect("and a failing run says so", ticker.state(7.0, True, True),
           band_lib.FAILED)
    return "issued, held, finalised in place"


@case(1, "OBS-8.40")
def the_band_colours_a_stall_and_a_failure_and_nothing_else() -> str:
    """The drawn band, in pixels: colour marks the two states worth marking."""

    glyphs = recorder_lib.glyphs
    colours = {"background": 6, "primary": 1, "secondary": 15, "failure": 2,
               "warning": 7, "accent": 3}
    width = 872
    layout = band_lib.layout_for(width)

    def drawn(record, now, activity="SUITE X > CHECK Y", state=band_lib.RUNNING):
        ticker = band_lib.Ticker()
        ticker.apply([record], now)
        canvas = glyphs.Canvas(width, band_lib.HEIGHT, 6)
        band_lib.draw(canvas, 0, 0, width, ticker, layout, activity, state,
                      colours, now)
        return canvas.to_rgb() if hasattr(canvas, "to_rgb") else bytes(
            canvas._pixels)

    def colours_in(rgb, row, start, size):
        """Which colours a field of one glyph row was drawn in."""
        found = set()
        for line in range(row * glyphs.GLYPH_HEIGHT,
                          (row + 1) * glyphs.GLYPH_HEIGHT):
            base = line * width * 3
            for column in range(start * glyphs.GLYPH_WIDTH,
                                (start + size) * glyphs.GLYPH_WIDTH):
                at = base + column * 3
                found.add(tuple(rgb[at:at + 3]))
        return found

    row = band_lib.FIRST_TICKER_ROW + band_lib.TICKER_ROWS - 1
    warning = tuple(glyphs.c64_rgb(colours["warning"]))
    failure = tuple(glyphs.c64_rgb(colours["failure"]))
    ordinary = drawn({"transport": "rest", "op": "PUT /v1/machine:writemem",
                      "status": 200, "clock": "00:00:01", "reference": "#1",
                      "ms": 12.0}, now=1.0)
    seen = colours_in(ordinary, row, *layout.duration)
    if warning in seen or failure in seen:
        raise Failure("an ordinary interaction was coloured")
    stalled = drawn({"transport": "rest", "op": "PUT /v1/machine:writemem",
                     "phase": "start", "clock": "00:00:01", "reference": "#1",
                     "seconds": band_lib.STALL_SECONDS}, now=2.0)
    if warning not in colours_in(stalled, row, *layout.duration):
        raise Failure("a stalled interaction is not marked")
    failed = drawn({"transport": "rest", "op": "GET /v1/drives", "status": 500,
                    "clock": "00:00:01", "reference": "#1", "ms": 30.0},
                   now=3.0)
    if failure not in colours_in(failed, row, *layout.duration):
        raise Failure("a failed interaction is not marked")
    # The activity row's own state word, which is the other thing colour says.
    header_row = drawn({"transport": "rest", "op": "GET /v1/info",
                        "status": 200, "clock": "00:00:01", "reference": "#1"},
                       now=4.0, state=band_lib.FAILED)
    tail = layout.columns - len(band_lib.FAILED) - 1
    if failure not in colours_in(header_row, band_lib.ACTIVITY_ROW, tail,
                                 len(band_lib.FAILED)):
        raise Failure("a failing run is not marked in the activity row")
    return "stall in warning, failure in red, nothing else coloured"


@case(1, "OBS-8.35", "OBS-8.37")
def a_composed_frame_uses_the_machines_own_colours() -> str:
    """Sixteen colours, the character ROM, and every element on the 8-pixel grid."""

    sys.path.insert(0, os.path.join(ROOT, "tools", "api"))

    loader = importlib.machinery.SourceFileLoader(
        "menu_screen_tool", os.path.join(ROOT, "tools", "api",
                                         "menu_screen_tool.py"))
    spec = importlib.util.spec_from_loader("menu_screen_tool", loader)
    tool = importlib.util.module_from_spec(spec)
    sys.modules["menu_screen_tool"] = tool
    loader.exec_module(tool)
    palette = {tool.c64_rgb(index) for index in range(16)}

    geometry = recorder_lib.geometry_for(True, True, "combined")["combined"]
    composer = recorder_lib.Composer(geometry, recorder_lib.Options(),
                                     {"target": "u64"})
    frame = (384, 272, bytes(n % 16 for n in range(384 * 272)))
    rows = ["Ultimate 64 menu".ljust(40)] * 25
    rgb = composer.compose(frame, rows, "menu", recorder_lib.RunState(), 1.0,
                           1786700000.0)
    expect("the canvas", len(rgb), geometry.width * geometry.height * 3)
    used = {tuple(rgb[i:i + 3]) for i in range(0, len(rgb), 3)}
    outside = used - palette
    if outside:
        raise Failure(f"colours outside the sixteen: {sorted(outside)[:4]}")
    return f"{len(used)} colours, all VIC"


@case(1, "OBS-8.30", "OBS-8.20")
def the_pane_labels_are_on_a_row_of_their_own() -> str:
    """MENU and SCREEN can never land on top of the caption the stamp writes.

    They shared the stamp's second row. A caption is `label / suite / scenario
    / check`, which reaches the right of the harness pane on most suites, and
    the label was drawn over it: seen on every frame of a `freeze` suite whose
    scenario name was long enough.
    """

    def composed(scenario):
        state = recorder_lib.RunState(
            suite="browser-filesystem-refresh", label="overlay",
            scenario=scenario, check="check 17")
        return composed_pair(["x" * 40] * 25, state=state)

    expect("the labels are on the row under the stamp", recorder_lib.LABEL_ROW,
           recorder_lib.STAMP_ROWS)
    geometry, _c, short, _p = composed("a rename")
    _g, _c2, long_caption, _p2 = composed(
        "a rename of a file whose name is long enough to reach the far side "
        "of both panes and then some")
    row_bytes = geometry.width * recorder_lib.glyphs.GLYPH_HEIGHT * 3

    def band(rgb, row):
        return rgb[row * row_bytes:(row + 1) * row_bytes]

    if band(short, 1) == band(long_caption, 1):
        raise Failure("the caption did not change, so this proves nothing")
    labels = recorder_lib.LABEL_ROW
    if band(short, labels) != band(long_caption, labels):
        raise Failure("a longer caption changed the row the pane labels are "
                      "on, so the two can collide")
    drawn = glyph_columns(long_caption, geometry.width, labels,
                          background=recorder_lib.glyphs.c64_rgb(6))
    if not drawn:
        raise Failure("the pane labels are not drawn")
    # Right aligned in each pane, which is where the two labels are.
    panes = geometry.width // recorder_lib.glyphs.GLYPH_WIDTH
    if max(drawn) < panes - len("SCREEN"):
        raise Failure("the SCREEN label is not against the right of its pane")
    return f"stamp on rows 0..{recorder_lib.STAMP_ROWS - 1}, labels on {labels}"


@case(1, "OBS-8.20", "OBS-8.41")
def the_left_pane_is_sticky_and_says_which_surface_it_is() -> str:
    """One surface at a time, chosen by the interactions and by nothing else.

    The pane followed whichever screen the spool had published last. A suite
    that reads a Telnet session and the overlay menu in the same second made it
    flip between the two several times a second, which is unreadable and is
    also wrong half the time about which surface the harness was talking to.

    The oscillation case is the first one below: the same alternating stream
    that used to flip the pane must now leave it on one surface.
    """

    def apply(records, at=1.0):
        pane = recorder_lib.PaneMode()
        pane.apply(records, at)
        return pane

    def menu_read(open_now=True):
        return {"transport": "rest", "op": "GET /v1/machine:menu_screen",
                "status": 200 if open_now else 404, "menu_open": open_now}

    def telnet_send(key="DOWN"):
        return {"transport": "telnet", "op": f"send {key}"}

    def key_press(menu_open):
        return {"transport": "rest", "op": "POST /v1/machine:input",
                "status": 200, "menu_open": menu_open}

    # 1. The oscillation. A suite reading the menu between every Telnet key
    #    used to move the pane on every record; the surface being driven is
    #    the Telnet session throughout, because that is what is being sent to.
    alternating = []
    for _ in range(20):
        alternating += [telnet_send(), menu_read(False), menu_read(False)]
    pane = apply(alternating)
    expect("one surface for the whole burst", pane.mode,
           recorder_lib.PANE_TELNET)

    # 2. Telnet only.
    expect("telnet alone", apply([telnet_send(), telnet_send()]).mode,
           recorder_lib.PANE_TELNET)

    # 3. Menu only.
    expect("menu alone", apply([menu_read(), key_press(True)]).mode,
           recorder_lib.PANE_MENU)

    # 4. Both orderings, and the last surface actually driven wins.
    expect("telnet then menu", apply([telnet_send(), key_press(True)]).mode,
           recorder_lib.PANE_MENU)
    expect("menu then telnet", apply([key_press(True), telnet_send()]).mode,
           recorder_lib.PANE_TELNET)

    # 5. Keys into the C64's matrix with the menu closed, which is neither of
    #    the screens: the same call, told apart by whether the menu was open.
    expect("keys with the menu closed", apply([key_press(False)]).mode,
           recorder_lib.PANE_KEYS)
    expect("and the same call with it open is the menu",
           apply([key_press(True)]).mode, recorder_lib.PANE_MENU)
    keys = apply([key_press(False), key_press(False)])
    if len(keys.keys) != 2:
        raise Failure(f"the keys pane kept {keys.keys}")

    # 6. A genuinely alternating suite moves the pane, once per change, and
    #    the pane records when it changed rather than counting anything.
    pane = recorder_lib.PaneMode()
    pane.apply([telnet_send()], 10.0)
    expect("first surface at its own time", (pane.mode, pane.since),
           (recorder_lib.PANE_TELNET, 10.0))
    pane.apply([telnet_send(), telnet_send()], 11.0)
    expect("more of the same does not move it", pane.since, 10.0)
    pane.apply([key_press(True)], 12.0)
    expect("a real change does", (pane.mode, pane.since),
           (recorder_lib.PANE_MENU, 12.0))

    # 7. An interaction that identifies no surface never moves it.
    pane.apply([{"transport": "rest", "op": "GET /v1/info", "status": 200},
                {"transport": "ftp", "op": "RETR", "reply": "226 ok"}], 13.0)
    expect("an unrelated call leaves it alone", (pane.mode, pane.since),
           (recorder_lib.PANE_MENU, 12.0))
    expect("and a menu_screen that answered 404 does too",
           apply([key_press(True), menu_read(False)]).mode,
           recorder_lib.PANE_MENU)
    return "oscillation, both orderings, three surfaces"


@case(1, "OBS-8.20")
def a_stale_surface_never_looks_live() -> str:
    """The label says how old the pane is and the pane is drawn as its age.

    A viewer looking at a frame has to be able to tell a screen the harness is
    driving now from the last screen of a surface it left two minutes ago, and
    the pane is the only thing on the frame that can be either.
    """

    expect("current content says nothing", recorder_lib.format_age(0.4), "")
    expect("seconds", recorder_lib.format_age(12.0), " 12s")
    expect("and minutes", recorder_lib.format_age(180.0), " 3m")
    expect("the label names the surface",
           (recorder_lib.pane_label(recorder_lib.PANE_TELNET),
            recorder_lib.pane_label(recorder_lib.PANE_MENU),
            recorder_lib.pane_label(recorder_lib.PANE_KEYS)),
           ("TELNET", "MENU", "KEYS"))

    payload = (bytes([0x20]) * (recorder_lib.glyphs.MENU_COLUMNS
                                * recorder_lib.glyphs.MENU_ROWS)
               + bytes([0x77]) * (recorder_lib.glyphs.MENU_COLUMNS
                                  * recorder_lib.glyphs.MENU_ROWS))
    geometry = recorder_lib.geometry_for(True, True, "combined")["combined"]
    composer = recorder_lib.Composer(geometry, recorder_lib.Options(),
                                     {"target": "u64"})
    frame = (384, 272, bytes([6]) * (384 * 272))
    state = recorder_lib.RunState()
    live = composer.compose(frame, ["x" * 40] * 25, "menu", state, 1.0,
                            1786700000.0, harness_raw=payload, live=True)
    stale = composer.compose(frame, ["x" * 40] * 25, "menu", state, 1.0,
                             1786700000.0, harness_raw=payload, live=False,
                             age=" 12s")
    if live == stale:
        raise Failure("a surface no longer being driven looks exactly like one "
                      "that is")
    dim = recorder_lib.glyphs.c64_rgb(recorder_lib.DIM_TEXT)
    if dim not in {tuple(stale[i:i + 3]) for i in range(0, len(stale), 3)}:
        raise Failure("the stale pane is not drawn in the dimmer colour")
    return "age in the label, dimmer in the pane"


@case(1, "OBS-8.20")
def a_menu_is_centred_in_the_pane_and_a_session_is_not() -> str:
    """A 40-column menu gets the same margin on both sides of a 60-column pane.

    The pane is sized for the widest screen either transport produces. A menu
    is 320 of its 480 pixels and was drawn against the left edge, so it sat
    off centre beside a device pane that is centred. A Telnet session is the
    full 60 columns and has nowhere to move to.
    """

    expect("the indent is the two widths",
           recorder_lib.menu_indent(recorder_lib.HARNESS_PANE_WIDTH),
           (recorder_lib.HARNESS_PANE_WIDTH
            - recorder_lib.glyphs.MENU_COLUMNS
            * recorder_lib.glyphs.GLYPH_WIDTH) // 2)
    expect("which is 80 pixels each side", recorder_lib.menu_indent(480), 80)

    # A payload whose every cell has one colour in both nibbles, so each cell
    # is a solid block whichever nibble the decoder reads as the background
    # and the occupied pixels are exactly the menu's rectangle.
    cells = recorder_lib.glyphs.MENU_COLUMNS * recorder_lib.glyphs.MENU_ROWS
    payload = bytes([0x20]) * cells + bytes([0x77]) * cells
    geometry, _composer, _stamped, plain = composed_pair(
        ["x" * 40] * 25, kind="menu", raw=payload)
    chrome = recorder_lib.glyphs.c64_rgb(recorder_lib.CHROME)
    # A scanline through the middle of the menu, which starts 36 lines down in
    # a 272-line pane. Read in pixels rather than in glyph rows: the menu is
    # centred vertically and is not on the canvas's own 8-pixel row grid.
    harness = plain[:]
    first, _last = occupied_span(harness, geometry.width, 136, chrome)
    if first is None:
        raise Failure("the menu drew nothing")
    # Only the harness pane, since the device pane is on the same scanline.
    expect("the menu starts one indent in", first,
           recorder_lib.menu_indent(recorder_lib.HARNESS_PANE_WIDTH))
    menu_end = (recorder_lib.menu_indent(recorder_lib.HARNESS_PANE_WIDTH)
                + recorder_lib.glyphs.MENU_COLUMNS
                * recorder_lib.glyphs.GLYPH_WIDTH)
    for x in (menu_end - 1, menu_end):
        at = ((136 * geometry.width) + x) * 3
        inside = tuple(harness[at:at + 3]) != chrome
        expect(f"pixel {x} is inside the menu", inside, x < menu_end)
    expect("so the margins match",
           recorder_lib.HARNESS_PANE_WIDTH - menu_end, first)
    return f"40 columns centred in 60, {first}px each side"


@case(1, "OBS-8.30")
def the_state_edge_is_drawn_under_the_text_and_the_bar() -> str:
    """A failure marking never overwrites a reading.

    The edge is the outermost two rows and columns, and the stamp starts at
    the first column of the first row, so drawing the edge last painted red
    over the first two pixels of every timecode and over both ends of the
    progress bar. It is a marking for a reader scrubbing a timeline, not a
    reading, so it goes under everything that is read.
    """

    state = recorder_lib.RunState(suite="broken", label="overlay",
                                  failed_at=1786700000.0,
                                  segments=["overlay/broken"],
                                  verdicts={"overlay/broken": "FAIL"},
                                  current_segment=0)
    geometry, _composer, stamped, _plain = composed_pair(
        ["READY."] * 25, state=state)
    red = recorder_lib.glyphs.c64_rgb(recorder_lib.FAILURE_COLOUR)

    def at(x, y):
        return tuple(stamped[((y * geometry.width) + x) * 3:
                             ((y * geometry.width) + x) * 3 + 3])

    # The edge is there.
    expect("the edge is drawn", at(0, 100), red)
    expect("on both sides", at(geometry.width - 1, 100), red)
    # And the text on top of it is not red: the timecode starts at column 0.
    timecode = [at(x, y) for x in range(recorder_lib.EDGE_PIXELS)
                for y in range(recorder_lib.glyphs.GLYPH_HEIGHT)]
    if all(pixel == red for pixel in timecode):
        raise Failure("the edge painted over the start of the timecode")
    # And the progress bar, which reaches both ends of the frame.
    bar_y = geometry.height - recorder_lib.EDGE_PIXELS - 1
    if at(0, bar_y) == red and at(geometry.width - 2, bar_y) == red:
        raise Failure("the edge painted over both ends of the progress bar")
    return "edge under the stamp and the bar"


@case(1, "OBS-8.30")
def the_frame_metadata_is_ranked_and_ordered() -> str:
    """Primary fields first and in white, secondary after them and in grey.

    A frame that travels on its own has to answer which recording, which
    machine and which firmware before it answers which build produced it, and
    a narrow file has to lose the second question rather than the first.
    """

    identity = {"target": "u64", "firmware": "Ultimate 64 3.11",
                "address": "192.168.1.15", "ci": "17253361191"}
    geometry, _composer, stamped, _plain = composed_pair(
        ["READY."] * 25, identity=identity)
    white = recorder_lib.glyphs.c64_rgb(recorder_lib.PRIMARY_TEXT)
    grey = recorder_lib.glyphs.c64_rgb(recorder_lib.SECONDARY_TEXT)
    if white == grey:
        raise Failure("the two ranks are the same colour")

    def rank_of(column):
        top = 0
        for y in range(top, top + recorder_lib.glyphs.GLYPH_HEIGHT):
            for x in range(column * recorder_lib.glyphs.GLYPH_WIDTH,
                           (column + 1) * recorder_lib.glyphs.GLYPH_WIDTH):
                at = ((y * geometry.width) + x) * 3
                pixel = tuple(stamped[at:at + 3])
                if pixel == white:
                    return "primary"
                if pixel == grey:
                    return "secondary"
        return ""

    ranks = [rank_of(column) for column in range(geometry.columns)]
    seen = [rank for rank in ranks if rank]
    if not seen:
        raise Failure("no metadata was drawn")
    if "primary" in seen[seen.index("secondary"):]:
        raise Failure("a primary field is drawn after a secondary one")
    # The order itself: position, target, firmware, then the rest.
    expect("the timecode first",
           recorder_lib.format_position(61.0)[:8], "00:01:01")
    return f"{seen.count('primary')} primary then " \
           f"{seen.count('secondary')} secondary columns"


@case(1, "OBS-8.30", "OBS-8.9")
def the_opening_overview_is_five_seconds_at_any_rate() -> str:
    """The card a viewer lands on is held for exactly five seconds.

    Held in whole slots, so the figure has to come out right at every output
    rate the recorder offers rather than only at ten frames a second. Two
    seconds of a flat list was not long enough to read a grouped card, and the
    arithmetic that produced it truncated `2.0 / 0.1` to 19 slots, so the card
    it did produce was 1.9 seconds.
    """

    for fps in (5, 10, 20, 25):
        interval = 1.0 / fps
        slots = max(1, round(recorder_lib.OVERVIEW_SECONDS / interval))
        expect(f"the overview at {fps} fps", slots * interval,
               recorder_lib.OVERVIEW_SECONDS)
        summary = max(1, round(recorder_lib.SUMMARY_SECONDS / interval))
        expect(f"the summary at {fps} fps", summary * interval,
               recorder_lib.SUMMARY_SECONDS)
    if recorder_lib.SUMMARY_SECONDS >= recorder_lib.OVERVIEW_SECONDS:
        raise Failure("the closing card is as long as the opening one")
    return "5.0s opening, 2.0s closing"


@case(1, "OBS-8.30")
def the_opening_overview_is_grouped_and_degrades_to_one_column() -> str:
    """Three groups, aligned fields, two columns when there is room for two."""

    groups = [("DEVICE", [("target", "u64", True),
                          ("product", "Ultimate 64 3.11", True),
                          ("address", "192.168.1.15", False),
                          ("fpga", "1.4E", False)]),
              ("SOURCE", [("branch", "feat/e2e-observability", True),
                          ("commit", "5d4bea60", True),
                          ("tree", "clean", False)]),
              ("RUN", [("started", "2026-08-15 10:00:00", False),
                       ("build", "17253361191", False),
                       ("host", "bench", False),
                       ("suite runs", "23", True)])]

    def rows_used(layout, pane):
        geometry = recorder_lib.geometry_for(True, True, layout)[pane]
        composer = recorder_lib.Composer(geometry, recorder_lib.Options(), {})
        rgb = composer.overview(groups)
        background = recorder_lib.glyphs.c64_rgb(recorder_lib.CARD_BACKGROUND)
        used = {}
        for row in range(geometry.height // recorder_lib.glyphs.GLYPH_HEIGHT):
            columns = glyph_columns(rgb, geometry.width, row,
                                    background=background)
            if columns:
                used[row] = (min(columns), max(columns))
        return geometry, used

    wide, wide_rows = rows_used("combined", "combined")
    narrow, narrow_rows = rows_used("separate", "screen")
    if not wide_rows or not narrow_rows:
        raise Failure("the card drew nothing")
    # Two columns on the wide canvas: the second one starts far enough right
    # that no field of the first could have reached it.
    if max(end for _start, end in wide_rows.values()) < wide.columns // 2:
        raise Failure("the wide card did not use two columns")
    # One column on the narrow one, which cannot hold two: every row starts
    # within two columns of the first, which is the indent a field label
    # carries under its group heading.
    starts = sorted({start for start, _end in narrow_rows.values()})
    if max(starts) - min(starts) > 2:
        raise Failure(f"the narrow card is not one column: rows start at "
                      f"{starts}")
    # Nothing runs off either canvas, and the block is centred rather than
    # pushed against the left margin: the canvas is wider than the fields need.
    for geometry, rows in ((wide, wide_rows), (narrow, narrow_rows)):
        limit = geometry.columns - 1
        for row, (_start, end) in rows.items():
            if end > limit:
                raise Failure(f"row {row} reaches column {end} of {limit}")
        margins = (min(start for start, _end in rows.values()),
                   limit - max(end for _start, end in rows.values()))
        if abs(margins[0] - margins[1]) > 1:
            raise Failure(f"the card is not centred on a {geometry.columns} "
                          f"column canvas: margins {margins}")
    return f"{len(wide_rows)} rows over 2 columns, " \
           f"{len(narrow_rows)} over 1"


@case(1, "OBS-15.6")
def the_two_stream_modules_are_callers_of_one_library() -> str:
    """One wire format, one set of socket options, one source filter.

    Three implementations of one format is the state
    tests/lib/check_transport_usage.py exists because the HTTP client reached.
    The public names the suites import keep working, so no suite changes.
    """

    try:
        import vic_video
    except ImportError as error:
        raise Skipped(f"{error}, needed by vic_video.py") from error

    expect("the video group", (vic_video.MULTICAST_GROUP, av_stream.VIDEO_GROUP),
           (streams.VIDEO_GROUP, streams.VIDEO_GROUP))
    expect("the audio address", (av_stream.AUDIO_GROUP, av_stream.AUDIO_PORT),
           (streams.AUDIO_GROUP, streams.AUDIO_PORT))
    expect("the packet sizes",
           (av_stream.VIDEO_PACKET_BYTES, av_stream.AUDIO_PACKET_BYTES),
           (streams.VIDEO_PACKET_BYTES, streams.AUDIO_PACKET_BYTES))

    # Neither may open a socket of its own: the options that let a suite and a
    # recorder share the port are set in one place or in none.
    here = os.path.join(ROOT, "tests", "e2e", "lib")
    for name in ("vic_video.py", "av_stream.py"):
        with open(os.path.join(here, name), encoding="utf-8") as handle:
            tree = ast.parse(handle.read(), filename=name)
        for node in ast.walk(tree):
            if not isinstance(node, ast.Call):
                continue
            target = node.func
            if (isinstance(target, ast.Attribute) and target.attr == "socket"
                    and isinstance(target.value, ast.Name)
                    and target.value.id == "socket"):
                raise Failure(f"{name} opens a socket of its own at line "
                              f"{node.lineno}")
    return "constants and sockets, one place"


@case(1, "OBS-5.5")
def the_c64_screen_is_decoded_as_screen_codes() -> str:
    """Screen codes, not ASCII, and bit 7 is reverse video rather than nothing.

    The menu plane is literal printable ASCII and this one is not, and the two
    must not share a decode path.
    """
    runner = load_runner()
    rows = runner.c64_screen_rows(
        bytes([0x12, 0x05, 0x01, 0x04, 0x19, 0x2E])      # READY.
        + bytes([0x92, 0x85, 0x81])                      # the same, reversed
        + bytes([0x00, 0x1B, 0x1E])                      # @ [ up arrow
        + bytes([0x40, 0x5A, 0x7F])                      # graphics, no text form
        + bytes([0x20]) * 979)
    expect("rows", len(rows), 25)
    expect("columns", len(rows[0]), 40)
    expect("the prompt", rows[0][:6], "READY.")
    expect("reverse video is text", rows[0][6:9], "REA")
    expect("the punctuation codes", rows[0][9:12], "@[\u2191")
    expect("graphics have no text form", rows[0][12:15], "   ")
    return "6 code ranges"


@case(1, "OBS-3.15")
def a_table_is_padded_to_a_common_width() -> str:
    """Padded columns are what make the raw file aligned in a terminal."""
    generator = load_report_tool()
    lines = generator.table(["Suite", "Verdict"],
                            [["prg-context-menu", "OK"], ["input", "FAIL"]])
    widths = {len(line) for line in lines}
    if len(widths) != 1:
        raise Failure(f"the rows are not one width: {lines}")
    expect("every row", len(lines), 4)
    return f"{widths.pop()} columns wide"


@case(1, "OBS-3.5")
def a_slow_check_is_marked_the_way_the_console_marks_it() -> str:
    """A check past the shared threshold reads SLOW here as well as there."""
    generator = load_report_tool()
    quick = generator.render(synthetic_run(generator, seconds=0.5))
    slow = generator.render(synthetic_run(
        generator, seconds=report.SLOW_CHECK_SECONDS + 1))
    if "SLOW" in quick:
        raise Failure("a check under the threshold was marked SLOW")
    if "OK SLOW" not in slow:
        raise Failure("a check over the threshold was not marked SLOW")
    if "20 rows" not in quick:
        raise Failure("the check's own measurement is not in the document")
    return "marked over the threshold only"


@case(1, "OBS-3.19")
def a_target_with_no_identity_says_so() -> str:
    """A device the sweep never identified reads as unknown rather than blank."""
    generator = load_report_tool()
    unknown = generator.render(synthetic_run(generator))
    if "firmware unknown" not in unknown:
        raise Failure("a target with no ident record did not say so")
    known = generator.render(synthetic_run(generator, health=[
        {"kind": "health", "label": "before", "ok": True, "time": 999.5,
         "checks": [{"name": "ident", "state": "ok", "ms": 1.0,
                     "detail": "Ultimate 64 3.15"}]}]))
    if "Ultimate 64 3.15" not in known:
        raise Failure("the product and firmware are not in the header")
    return "named, or honestly absent"


@case(1, "OBS-3.17")
def the_generator_renders_a_tree_that_is_not_there() -> str:
    """A directory with nothing in it produces a document rather than an error."""

    generator = load_report_tool()
    with tempfile.TemporaryDirectory() as directory:
        path = generator.write_report(directory)
        with open(path, encoding="utf-8") as handle:
            document = handle.read()
        if not document.startswith("# E2E gate run:"):
            raise Failure(f"no document was written: {document[:80]!r}")
        if generator.DETAIL_MARKER not in document:
            raise Failure("the detail marker is missing")
    return f"{len(document.splitlines())} lines from an empty tree"


@case(1, "OBS-3.23", "OBS-8.39")
def the_recording_table_names_the_files_it_is_about() -> str:
    """A row about a recording says which file it is a row about.

    The column read `-` for every run, because the file names are a list of
    strings and the helper that read it keeps only the records in a list. The
    same helper read the device log's expected addresses.
    """
    generator = load_report_tool()
    run = generator.Run(directory="runs", targets=[generator.TargetRun(
        token="u2@c64u", slug="u2-at-c64u",
        capture={"files": ["video.mp4", "video-harness.mp4"], "frames": 100,
                 "fps": 10, "frames_lost": 3})])
    rendered = "\n".join(generator.recording_block(run))
    # Under the target's slug, which is not its token: every other path in the
    # document is relative to the tree, and a bare name left a reader to
    # compose one from a substitution the document never states.
    for name in ("video.mp4", "video-harness.mp4"):
        if f"`u2-at-c64u/{name}`" not in rendered:
            raise Failure(f"{name} is not named in the recording table")
        if f"`{name}`" in rendered:
            raise Failure(f"{name} is named without the directory it is in")
    if "3 frames lost" not in rendered:
        raise Failure("the loss column lost its figure")
    # And the one place a reader with a video is looking says how to get from
    # a frame to the record behind it and back, in both directions.
    if "jq 'select(.seq == 4812)'" not in rendered:
        raise Failure("the section does not say how to reach a record from a "
                      "frame")
    if "lead_in + (time - started)" not in rendered:
        raise Failure("the section does not say how to reach a frame from a "
                      "record")
    return "both files named, and the join in both directions"


@case(1, "OBS-2.18")
def the_report_says_how_much_of_the_screen_came_back_as_text() -> str:
    """Both halves of the decoder's accounting, or nothing at all.

    A recorder that refused three frames in four and one whose device sat on
    one screen for the whole run write the same number of records. Only the
    count of refusals separates them, so the report carries it beside the count
    of screens read.
    """
    generator = load_report_tool()

    def target(capture):
        return generator.Run(directory="runs", targets=[generator.TargetRun(
            token="u64", slug="u64", capture=capture)])

    said = generator.screen_text_lines(
        target({"screen_texts": 201, "screens_unreadable": 279}))
    joined = "\n".join(said)
    if "201 screen(s) read back as text" not in joined:
        raise Failure(f"the screens read are not reported: {joined!r}")
    if "279 frame(s) it could not read" not in joined:
        raise Failure(f"the frames refused are not reported: {joined!r}")

    # A run whose recorder read everything still says so, because zero
    # refusals is a fact and an absent line is not.
    none_refused = "\n".join(generator.screen_text_lines(
        target({"screen_texts": 12, "screens_unreadable": 0})))
    if "0 frame(s) it could not read" not in none_refused:
        raise Failure(f"a run with no refusals says nothing: {none_refused!r}")

    expect("a run with no recorder has nothing to say",
           generator.screen_text_lines(target(None)), [])
    return "read and refused, both always"


@case(1, "OBS-3.18", "OBS-3.22")
def a_shared_file_does_not_become_a_suite_run_in_the_table() -> str:
    """`incomplete` has to mean a suite that did not close, and nothing else.

    A target's directory holds files that carry a `suite` field and are not one
    suite run's records: the screen spool, the interaction log and the decoded
    screen text. Their names carry no label, so reading a suite run out of one
    invents a suite that never ran, with no runner record to close it, and the
    verdict table then carries a row reading `incomplete`. `screen-text.jsonl`
    did exactly that: a real run's table carried `u64 | screen | text | 1 |
    incomplete`, and the completeness note above it named that run as one this
    run did not finish.

    A reader cannot tell such a row from the one it exists to show, which is a
    suite the run really was killed in the middle of.
    """

    generator = load_report_tool()
    with tempfile.TemporaryDirectory() as directory:
        target = os.path.join(directory, "u64")
        os.makedirs(target)
        with open(os.path.join(directory, "run.jsonl"), "w",
                  encoding="utf-8") as handle:
            handle.write(json.dumps({"kind": "run", "verdict": "OK",
                                     "time": 9.0}) + "\n")
        with open(os.path.join(target, "run.jsonl"), "w",
                  encoding="utf-8") as handle:
            handle.write(json.dumps({"kind": "suite", "name": "input",
                                     "mode": "overlay", "attempt": 1,
                                     "verdict": "OK", "seconds": 1.0,
                                     "time": 2.0}) + "\n")
        with open(os.path.join(target, "overlay-input.jsonl"), "w",
                  encoding="utf-8") as handle:
            handle.write(json.dumps({"kind": "check", "index": 1,
                                     "suite": "input", "attempt": 1,
                                     "verdict": "OK", "seconds": 0.1,
                                     "time": 1.0}) + "\n")
        for name, record in (
                ("screen-text.jsonl", {"kind": "vic", "suite": "input",
                                       "attempt": 1, "text": [], "time": 1.5}),
                ("interactions.jsonl", {"kind": "interaction", "seq": 1,
                                        "suite": "input", "attempt": 1,
                                        "time": 1.6}),
                ("screens.jsonl", {"kind": "screen", "suite": "input",
                                   "attempt": 1, "time": 1.7})):
            with open(os.path.join(target, name), "w",
                      encoding="utf-8") as handle:
                handle.write(json.dumps(record) + "\n")

        run = generator.load_tree(directory)
        found = sorted((made.label, made.suite, made.attempt)
                       for made in run.targets[0].suites)
        expect("one suite run, the one that ran", found,
               [("overlay", "input", 1)])
        expect("and nothing is incomplete",
               [made.suite for made in run.targets[0].suites
                if made.verdict not in ("OK", "FAIL", "WARN", "SKIP")], [])
    return "three shared files, one suite run"


@case(1, "OBS-16.7", exclusive=True)
def every_requirement_with_logic_has_a_test() -> str:
    """Report which requirements no case names, and why, rather than failing.

    A reviewer cannot read three thousand lines of specification against three
    thousand lines of code. They can read a list of requirement numbers with a
    test each, which is what makes a one-pass implementation reviewable.

    It reports rather than fails on purpose: a check that fails on a
    deliberately untested requirement is one people learn to route around, and
    a routed-around check finds nothing.
    """
    specified = specified_requirements()
    if not specified:
        raise Skipped("the specification is not in this checkout")
    tested = {number for entry in CASES for number in entry.requirements}
    both = sorted(tested & set(UNTESTED_REQUIREMENTS))
    if both:
        raise Failure(f"a requirement is both tested and listed as deliberately "
                      f"untested: {both}")
    unknown = sorted(tested - set(specified))
    if unknown:
        raise Failure(f"a case names a requirement the specification does not "
                      f"define: {unknown}")
    missing = sorted(set(specified) - tested - set(UNTESTED_REQUIREMENTS),
                     key=lambda number: (specified[number], number))
    for number in missing:
        report.detail(f"    {specified[number]} {number}: no case names it")
    for number, reason in sorted(UNTESTED_REQUIREMENTS.items()):
        report.detail(f"    {number}: not tested, {reason}")
    return (f"{len(tested)} named, {len(missing)} unnamed, "
            f"{len(UNTESTED_REQUIREMENTS)} deliberately not")


@case(1, "OBS-3.23", "OBS-8.28")
def a_still_no_suite_claims_is_still_shown() -> str:
    """A run that ended between a retry and its records leaves one behind.

    The recorder writes a still under the identity it held when it took it,
    which is read from the records the run has written so far. A report that
    only showed stills belonging to a suite run it knows about would drop it,
    and the file index would then name a file the document never explains.
    """

    generator = load_report_tool()
    with tempfile.TemporaryDirectory() as directory:
        capture = os.path.join(directory, "u64", "capture")
        os.makedirs(capture)
        with open(os.path.join(capture, "overlay-noisy-2-1-first.txt"), "w",
                  encoding="utf-8") as handle:
            handle.write("READY.\n")
        target = generator.TargetRun(token="u64", slug="u64")
        run = generator.Run(directory=directory, targets=[target])
        document = "\n".join(generator.screens_section(run))
    if "overlay-noisy-2-1-first.txt" not in document:
        raise Failure("a still nobody claimed is not in the report")
    if "no suite record" not in document:
        raise Failure("it is shown without saying that no suite claimed it")
    return "shown, and named as unclaimed"


@case(1, "OBS-3.30")
def a_stream_started_twice_is_one_stream() -> str:
    """What a run left behind is the device's state, not a count of requests.

    The recorder re-arms a stream that has gone quiet, so a run can start one
    stream several times and stop it once, and the device is left with it
    stopped. Measured on the U64: one re-arm made the report say the audio
    stream had been left running when it had not.
    """
    generator = load_report_tool()

    def action(path, when):
        return {"kind": "action", "method": "PUT", "path": path,
                "status": 200, "time": when}

    target = generator.TargetRun(token="u64", slug="u64", actions=[
        action("/v1/streams/audio:start", 1.0),
        action("/v1/streams/audio:start", 2.0),
        action("/v1/streams/audio:stop", 3.0),
        action("/v1/drives/a:mount", 4.0),
    ])
    rows = generator.unmatched_changes(
        generator.Run(directory="", targets=[target]))
    left = [row[3] for row in rows]
    expect("only the mount is left", left, ["/v1/drives/a"])
    return "one start, one stop, one stream"


@case(1, "OBS-14.5")
def the_generator_adds_no_dependency() -> str:
    """It imports the standard library and this repository's own modules only.

    The same rule check_transport_usage.py applies to the HTTP client, applied
    to imports: a new package here is a package the CI host has to grow.
    """

    with open(REPORT_TOOL, encoding="utf-8") as handle:
        tree = ast.parse(handle.read(), filename=REPORT_TOOL)
    imported = set()
    for node in ast.walk(tree):
        if isinstance(node, ast.Import):
            imported |= {alias.name.split(".")[0] for alias in node.names}
        elif isinstance(node, ast.ImportFrom) and node.level == 0 and node.module:
            imported.add(node.module.split(".")[0])
    ours = {"report", "targets", "e2e_report"}
    outside = sorted(imported - ours - set(sys_lib.stdlib_module_names))
    if outside:
        raise Failure(f"the generator imports {outside}")
    return f"{len(imported)} modules, all standard or ours"


@case(1, "OBS-4.1", "OBS-4.3", exclusive=True)
def the_job_summary_stays_inside_its_limits() -> str:
    """No marker copies the whole file; an oversized one truncates and says so."""

    generator = load_report_tool()
    with tempfile.TemporaryDirectory() as directory:
        summary = os.path.join(directory, "summary.md")
        os.environ["GITHUB_STEP_SUMMARY"] = summary
        os.environ.pop("E2E_ARTIFACT_URL", None)
        try:
            whole = "# a report with no marker\n\nbody\n"
            with open(os.path.join(directory, generator.INDEX_NAME), "w",
                      encoding="utf-8") as handle:
                handle.write(whole)
            generator.job_summary(directory)
            with open(summary, encoding="utf-8") as handle:
                expect("the whole file", handle.read(), whole)

            oversized = ("x" * 200 + "\n") * 6000 + generator.DETAIL_MARKER + "\n"
            with open(os.path.join(directory, generator.INDEX_NAME), "w",
                      encoding="utf-8") as handle:
                handle.write(oversized)
            os.remove(summary)
            generator.job_summary(directory)
            with open(summary, encoding="utf-8") as handle:
                cut = handle.read()
            if len(cut.encode()) >= generator.SUMMARY_LIMIT_BYTES:
                raise Failure(f"the summary is {len(cut.encode())} bytes")
            if "Truncated at a line boundary" not in cut:
                raise Failure("the truncation was not stated")
            body = cut.split("\n\n_Truncated")[0]
            if set(body.split("\n")) - {"x" * 200, ""}:
                raise Failure("the cut did not land on a line boundary")
        finally:
            os.environ.pop("GITHUB_STEP_SUMMARY", None)
    return f"{len(cut.encode()) // 1024} KiB after truncation"


@case(1, "OBS-4.1", exclusive=True)
def the_job_summary_step_does_nothing_outside_ci() -> str:
    """With no GITHUB_STEP_SUMMARY the step exits zero and writes nothing."""

    generator = load_report_tool()
    previous = os.environ.pop("GITHUB_STEP_SUMMARY", None)
    try:
        with tempfile.TemporaryDirectory() as directory:
            expect("exit", generator.job_summary(directory), 0)
            expect("wrote nothing", os.listdir(directory), [])
    finally:
        if previous is not None:
            os.environ["GITHUB_STEP_SUMMARY"] = previous
    return "silent and zero"
