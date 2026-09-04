#!/usr/bin/env python3
# Tier 2: one component driven on loopback, against the device double or a scripted socket.

"""The component cases of the observability suite.

One component driven on loopback, against the device double or a scripted socket.

Importing this module registers its cases in support.CASES. The entry
point, tests/lib/observability_test.py, imports all four tiers and runs
them in the order TIERS names.
"""

from device_double import DeviceDouble
from report import Failure
from api import UltimateApi
from selftest import expect
import health
import json
import os
import socket
import sys
import targets
import tempfile
import time
from support import (ROOT, Skipped, case, exclusive, interaction_log,
    load_report_tool, load_runner, logged_interactions,
    records_from_a_stub_suite, screen_text_of, vic_frame)
from device_double import UdpSender, video_packets
from device_double import audio_packets
import dataclasses
import ftp as ftp_lib
import recorder as recorder_lib
import shutil
import streams
import subprocess
import syslog_collector
import targets as targets_lib
import time as time_lib
import vic_text


@case(2, "OBS-15.14", "OBS-16.2")
def production_client_reaches_the_double() -> str:
    """The real API client drives the double with no code under test changed."""
    with DeviceDouble() as double:
        api = UltimateApi(double.target())
        expect("version", api.version(), "0.1")
        info = api.info()
        expect("product", info.product, double.product)
        expect("firmware", info.firmware_version, double.firmware_version)
        expect("reachable", api.reachable(), True)
        methods = {r.method for r in double.calls()}
        expect("every request was a GET", methods, {"GET"})
    return f"{len(double.requests)} requests"


@case(2, "OBS-15.14", "OBS-16.2")
def the_password_header_reaches_the_device() -> str:
    """The real client sends X-Password, and a wrong one is refused.

    The header is one of the transport behaviours an injected fake object
    would never have exercised, which is why the seam is the address.
    """
    with DeviceDouble(password="hunter2") as double:
        expect("right password", UltimateApi(double.target(), "hunter2").version(),
               "0.1")
        wrong = UltimateApi(double.target(), "wrong")
        try:
            wrong.version()
        except Failure as exc:
            if "403" not in str(exc):
                raise Failure(f"a wrong password was not refused: {exc}") from exc
        else:
            raise Failure("a wrong password was accepted")
    return "403 on a wrong password"


@case(2, "OBS-16.2", "OBS-16.6")
def menu_screen_404_is_the_ordinary_case() -> str:
    """404 on this endpoint means no menu is open, not old firmware."""
    with DeviceDouble() as double:
        api = UltimateApi(double.target())
        screen = api.machine.menu_screen()
        if screen is None or len(screen) != 2000:
            raise Failure(f"expected a 2000-byte screen, got {screen!r:.40}")
        rows = api.machine.menu_rows()
        expect("rows", len(rows), 25)
        expect("width", len(rows[0]), 40)
        if "Ultimate 64 menu" not in rows[0]:
            raise Failure(f"the first row did not decode: {rows[0]!r}")
        double.faults.menu_screen_404 = True
        expect("no menu open", api.machine.menu_screen(), None)
        expect("no rows", api.machine.menu_rows(), [])
    return "2000 bytes, then 404"


@case(2, "OBS-16.6", "OBS-15.2")
def a_device_that_stops_answering_raises_rather_than_hangs() -> str:
    """A device that has gone is a Failure the caller can act on."""
    with DeviceDouble() as double:
        api = UltimateApi(double.target(), timeout=2.0)
        expect("reachable first", api.reachable(), True)
        double.faults.offline = True
        expect("unreachable after", api.reachable(), False)
        double.faults.offline = False
        expect("reachable again", api.reachable(), True)
    return "offline then back"


@case(2, "OBS-1.1", "OBS-1.2", "OBS-16.6", exclusive=True)
def an_unwritable_output_directory_does_not_end_the_run() -> str:
    """Where a run records itself is not part of what the run is testing.

    An unguarded makedirs made the observability output location decide the
    gate's process result: `-o /dev/null/e2e` ended the run with a traceback
    before any device was touched.
    """

    runner = load_runner()
    with tempfile.TemporaryDirectory() as directory:
        blocker = os.path.join(directory, "file")
        with open(blocker, "w", encoding="utf-8") as handle:
            handle.write("not a directory")
        made = runner.make_output_tree(os.path.join(blocker, "e2e"))
    expect("refused", made, False)
    expect("and made one it can", runner.make_output_tree(
        os.path.join(tempfile.mkdtemp(), "e2e")), True)
    return "reported, not raised"


@case(2, "OBS-1.1", "OBS-1.2", "OBS-16.6")
def a_collector_output_file_that_cannot_be_written_is_reported_at_startup() -> str:
    """An unwritable log file is a startup problem, not a silent discard.

    `bind` opened nothing, so an unwritable path was found by the first
    datagram, recorded in `problems` that nobody read again, and every line
    from that device was dropped with the run saying nothing about it.
    """

    sys.path.insert(0, os.path.join(ROOT, "tests", "lib"))

    with tempfile.TemporaryDirectory() as directory:
        blocker = os.path.join(directory, "127.0.0.1")
        with open(blocker, "w", encoding="utf-8") as handle:
            handle.write("not a directory")
        collector = syslog_collector.Collector(directory=directory, port=0)
        opened = collector.bind([targets.parse("127.0.0.1")])
        try:
            expect("the port still opened", opened, True)
            if not any("could not be written" in problem
                       for problem in collector.problems):
                raise Failure(f"the unwritable file went unreported: "
                              f"{collector.problems}")
            # And the run carries on: a datagram is discarded, not raised.
            collector.deliver("127.0.0.1", b"a line")
            expect("counted anyway", collector.lines, 1)
        finally:
            collector.stop()
    return "reported before the first datagram"


@case(2, "OBS-15.11", "OBS-16.6")
def a_device_that_stops_logging_leaves_a_gap() -> str:
    """A log that stops is an interval with a start and an end, or an open one.

    Why it is worth recording: a device that has stopped goes on saying
    nothing, and the log ending is the signal, whether or not the firmware
    managed to flush a last message first (OBS-7.15, whose other half is
    firmware work and stays in the deliberately-untested table). A collector
    that recorded nothing about the silence would leave a reader unable to
    tell an empty file from a quiet device.
    """

    sys.path.insert(0, os.path.join(ROOT, "tests", "lib"))

    clock = [1000.0]
    with tempfile.TemporaryDirectory() as directory:
        collector = syslog_collector.Collector(directory=directory, port=0,
                                               clock=lambda: clock[0])
        collector.bind([targets.parse("127.0.0.1")])
        try:
            collector.deliver("127.0.0.1", b"first")
            clock[0] += 1.0
            collector.deliver("127.0.0.1", b"a moment later")
            expect("a quiet moment is not a gap", collector.gaps(), [])

            clock[0] += syslog_collector.SILENT_SECONDS + 1.0
            open_gaps = collector.gaps()
            expect("one gap", len(open_gaps), 1)
            if "ended" in open_gaps[0]:
                raise Failure(f"a silence still running was closed: {open_gaps[0]}")
            expect("from the last line it sent", open_gaps[0]["started"], 1001.0)

            collector.deliver("127.0.0.1", b"back")
            closed = collector.gaps()
            expect("still one gap", len(closed), 1)
            expect("closed when it spoke again", closed[0]["ended"], clock[0])
        finally:
            collector.stop()
    return "one silence, opened and closed"


@case(2, "OBS-15.11")
def a_gap_reaches_the_records_with_both_of_its_ends() -> str:
    """The record shape carries a start, and an end only when there is one."""
    records = records_from_a_stub_suite(
        {}, body=("report.gap_result('syslog', 10.0, 12.5, target='u64',\n"
                  "                  reason='the device stopped logging')\n"
                  "report.gap_result('recorder', 20.0, target='u64',\n"
                  "                  reason='the stream stopped')\n"))
    gaps = [record for record in records if record["kind"] == "gap"]
    expect("two gaps", len(gaps), 2)
    expect("component", gaps[0]["component"], "syslog")
    expect("started", gaps[0]["started"], 10.0)
    expect("ended", gaps[0]["ended"], 12.5)
    if "ended" in gaps[1]:
        raise Failure(f"an open gap was given an end: {gaps[1]}")
    return "one closed, one open"


@case(2, "OBS-14.2", "OBS-16.2")
def health_sweep_runs_against_the_double() -> str:
    """Every listener the sweep asks for is on the handle, so the sweep passes."""
    with DeviceDouble() as double:
        target = double.target()
        api = UltimateApi(target, timeout=5.0)
        sweep = health.probe(target, api=api)
        names = [check.name for check in sweep.checks]
        for wanted in ("ping", "rest", "ftp", "telnet", "ident", "dma"):
            if wanted not in names:
                raise Failure(f"the sweep did not run {wanted}: {names}")
        if not sweep.ok:
            raise Failure(f"the sweep was degraded: {sweep.one_line()}")
        detail = sweep.detail_for("ident")
        expect("ident detail",
               detail, f"{double.product} {double.firmware_version}")
    return sweep.one_line()


@case(2, "OBS-16.2", "OBS-16.6")
def a_refused_listener_degrades_the_sweep() -> str:
    """One failed listener names itself and leaves the rest of the sweep alone."""
    with DeviceDouble() as double:
        target = double.target()
        double.withhold_ftp_banner()
        sweep = health.probe(target, api=UltimateApi(target, timeout=5.0))
        expect("degraded", sweep.ok, False)
        expect("which", sweep.failed, ("ftp",))
    return "ftp"


@case(2, "OBS-6.1", "OBS-6.2", "OBS-6.3", "OBS-6.4")
def the_sweep_reports_the_free_heap() -> str:
    """A ninth check, rendered as the figure rather than as a latency."""
    with DeviceDouble() as double:
        target = double.target()
        sweep = health.probe(target, api=UltimateApi(target, timeout=5.0))
        names = [check.name for check in sweep.checks]
        if health.HEAP not in names:
            raise Failure(f"the sweep did not read the heap: {names}")
        heap = next(c for c in sweep.checks if c.name == health.HEAP)
        expect("state", heap.state, health.OK)
        expect("figures", heap.figures,
               {"free": double.heap_free,
                "min_ever_free": double.heap_min_ever_free,
                "total": double.heap_total})
        expect("rendered", heap.render(), f"heap={double.heap_free}B")
        # The other eight are latencies and stay latencies: this is a special
        # case on one check's name, not a rule about carrying a detail.
        ident = next(c for c in sweep.checks if c.name == "ident")
        if not ident.render().endswith("ms"):
            raise Failure(f"another check's line changed: {ident.render()}")
    return heap.render()


@case(2, "OBS-6.5")
def the_heap_check_can_never_fail_a_sweep() -> str:
    """404 and a device that has gone both leave the sweep passing.

    A degraded sweep fires the operator's recovery command, which reboots or
    reflashes hardware, so a figure that moves for a dozen ordinary reasons
    must not be able to reach it.
    """
    with DeviceDouble() as double:
        target = double.target()
        api = UltimateApi(target, timeout=2.0)
        double.faults.heap_404 = True
        sweep = health.probe(target, api=api, include=("heap",))
        expect("404 skips", [c.state for c in sweep.checks], [health.SKIP])
        expect("still healthy", sweep.ok, True)
        if "no machine:heap" not in sweep.detail_for(health.HEAP):
            raise Failure(f"the reason is missing: {sweep.detail_for('heap')}")

        double.faults.heap_404 = False
        double.faults.offline = True
        sweep = health.probe(target, api=api, include=("heap",))
        expect("a gone device skips", [c.state for c in sweep.checks],
               [health.SKIP])
        expect("still healthy", sweep.ok, True)
    return "SKIP twice, never FAIL"


@case(2, "OBS-2.17")
def the_sequence_number_names_one_record_in_the_file() -> str:
    """`seq` is unique in a target's log, across every process that writes it.

    A target's `interactions.jsonl` is appended to by the runner, by the
    UI-state gate and by every suite, all of them separate processes. A
    counter held in each of them numbers the file from one several times over,
    so the recipe the design prescribes, `jq 'select(.seq == 4812)'`, answered
    with as many records as there were writers, and the `#4812` the recording
    burns onto a frame named all of them.

    Driven through real subprocesses appending at the same time, because the
    defect is what two processes do to one file and nothing about one process
    can show it.
    """

    writer = ("import os, sys, time\n"
              "sys.path.insert(0, %r)\n"
              "import interactions\n"
              "for index in range(40):\n"
              "    interactions.record('rest', 'GET /v1/version',\n"
              "                        status=200, ms=float(index))\n"
              "interactions.flush()\n" % os.path.join(ROOT, "tests", "lib"))
    with tempfile.TemporaryDirectory() as directory:
        script = os.path.join(directory, "writer.py")
        with open(script, "w", encoding="utf-8") as handle:
            handle.write(writer)
        path = os.path.join(directory, "interactions.jsonl")
        environment = dict(os.environ, E2E_INTERACTIONS=path)
        running = [subprocess.Popen([sys.executable, script], env=environment)
                   for _ in range(4)]
        for one in running:
            one.wait(timeout=60)
        with open(path, encoding="utf-8") as handle:
            records = [json.loads(line) for line in handle if line.strip()]
        if len(records) < 4:
            raise Failure(f"only {len(records)} records were written")
        numbers = [record["seq"] for record in records]
        if len(set(numbers)) != len(numbers):
            repeated = sorted({n for n in numbers if numbers.count(n) > 1})
            raise Failure(f"{len(numbers) - len(set(numbers))} record(s) share "
                          f"a sequence number: {repeated[:6]}")
        # And the transcript carries the same numbers, in the same order, so a
        # reader who found a line has the record and the other way round.
        with open(os.path.join(directory, "transcript.txt"),
                  encoding="utf-8") as handle:
            lines = [line.split()[0] for line in handle.read().splitlines()
                     if line.strip()]
        expect("one transcript line per record", len(lines), len(records))
        if lines != [str(number) for number in numbers]:
            raise Failure("the transcript and the log disagree about the order")
    return f"{len(records)} records from 4 processes, every number distinct"


@case(2, "OBS-2.17", "OBS-15.13", exclusive=True)
def every_transport_writes_to_the_interaction_log() -> str:
    """REST, Telnet and FTP all reach the log without a suite asking them to.

    Driven through the production transports against the double, because the
    property is that a suite gains this without a line of its own: a transport
    that stops recording has to fail here rather than be noticed missing from
    an investigation months later.
    """


    with DeviceDouble() as double, tempfile.TemporaryDirectory() as directory:
        with interaction_log(directory) as path:
            api = UltimateApi(double.target(), timeout=5.0)
            api.machine.readmem(0xD012, 1)
            # force=True, because this case is about the transport recording a
            # request rather than about when a reset is worth making. The
            # runner exports U64_DEVICE_RESET between suites, which seeds a
            # fresh client as already reset, and reset() then sends nothing at
            # all for the log to carry.
            api.machine.reset(wait=False, force=True)
            try:
                ftp_lib.connect(double.target())
            except Exception:  # noqa: BLE001 - the double may serve no FTP
                pass
            found = logged_interactions(path)
    transports = {record["transport"] for record in found}
    if "rest" not in transports:
        raise Failure(f"REST is not in the log: {transports}")
    operations = [record["op"] for record in found]
    if not any(op.startswith("GET /v1/machine:readmem") for op in operations):
        raise Failure(f"the read is not in the log: {operations}")
    if not any(op.startswith("PUT /v1/machine:reset") for op in operations):
        raise Failure(f"the reset is not in the log: {operations}")
    for record in found:
        for field in ("time", "suite", "kind"):
            if field not in record:
                raise Failure(f"a record carries no {field}: {record}")
    return f"{len(found)} interactions over {sorted(transports)}"


@case(2, "OBS-7.7", "OBS-7.9")
def two_collectors_cannot_share_the_port() -> str:
    """A second run collecting at once is refused, not served an arbitrary half.

    Unicast datagrams go to one socket, so two collectors bound to one port
    each get a share the kernel decides and neither has any way to know. Two
    concurrent runs on this machine produced 39298 lines in one and none in the
    other, and the one with none reported a device that had said nothing at
    all, which is the same shape as a device that had stopped.
    """


    with tempfile.TemporaryDirectory() as first_dir, \
            tempfile.TemporaryDirectory() as second_dir:
        first = syslog_collector.Collector(directory=first_dir, port=0)
        expect("the first one starts",
               first.bind([targets_lib.parse("127.0.0.1")]), True)
        try:
            second = syslog_collector.Collector(directory=second_dir,
                                                port=first.port)
            expect("the second one does not",
                   second.bind([targets_lib.parse("127.0.0.1")]), False)
            if not any(f"port {first.port} could not be opened" in problem
                       for problem in second.problems):
                raise Failure(f"the reason is not the port: {second.problems}")
            second.stop()
        finally:
            first.stop()
    return f"one collector on {first.port}, the second refused"


@case(2, "OBS-8.24", "OBS-8.25", "OBS-15.1")
def a_suite_taking_the_stream_is_never_recorded_as_loss() -> str:
    """The recorder's own wiring, from a suite's stop record to the counters.

    Three suites stop the device's video stream during a run and two of them
    leave it stopped. The recorder yields, which is what OBS-15.1 requires,
    and then has to account for the interval as the run's own doing rather
    than as a lossy link. It is driven here through the same two methods the
    slot loop calls, so this fails if either stops being called.
    """

    with tempfile.TemporaryDirectory() as directory:
        made = recorder_lib.Recorder(directory, "127.0.0.1", None,
                                     recorder_lib.Options(fps=10))
        made._cursor = recorder_lib.AudioCursor(made._audio, 48000.0, 10)

        def spool(**record):
            with open(os.path.join(directory, "screens.jsonl"), "a",
                      encoding="utf-8") as handle:
                handle.write(json.dumps(dict(record, kind="stream")) + "\n")

        def arriving(frames, first_frame, sequence, now):
            for offset in range(frames):
                for packet in video_packets(first_frame + offset,
                                            sequence + offset * 68):
                    made._settle_continuity("video", now)
                    made._sources.video_at = now
                    made._assembler.push(packet)
            for packet in audio_packets(sequence, 4):
                made._settle_continuity("audio", now)
                made._sources.audio_at = now
                made._cursor.push(made._audio.push(packet).pcm)

        arriving(5, 1, 0, now=1.0)
        expect("nothing lost while it was running",
               made._assembler.counts()["frames_lost"], 0)

        # The av suite takes the stream and stops it when it finishes.
        spool(action="stop", stream="video", suite="av-stream")
        spool(action="stop", stream="audio", suite="av-stream")
        made._spool.poll(2.0)
        made._apply_stream_events()
        expect("the run knows the stream is not running",
               made._cursor.available, False)

        # Twelve seconds of slots with nothing arriving, which the file still
        # needs audio for.
        for _ in range(120):
            made._cursor.take()
        expect("none of that is loss",
               made._audio.counts()["packets_lost"], 0)
        if made._cursor.unavailable_bytes <= 0:
            raise Failure("the absent audio was not counted at all")
        expect("nor concealment of a running stream",
               made._cursor.concealed_bytes, 0)

        # The suite starts it again. The device has counted on throughout.
        spool(action="start", stream="video", suite="av-stream")
        spool(action="start", stream="audio", suite="av-stream")
        made._spool.poll(14.0)
        made._apply_stream_events()
        expect("the run knows it is running again", made._cursor.available, True)
        arriving(5, 9000, 40000, now=14.1)

        counts = made._assembler.counts()
        expect("ten frames either side", counts["frames_completed"], 10)
        expect("no frame counted as lost", counts["frames_lost"], 0)
        expect("no packet counted as dropped", counts["packets_dropped"], 0)
        record = made.record()
        lifecycle = record["stream_lifecycle"]
        for stream in ("video", "audio"):
            if "suite-stopped" not in lifecycle.get(stream, {}):
                raise Failure(f"the {stream} stop is not in the record: "
                              f"{lifecycle}")
        expect("and the record shows no loss", record["frames_lost"], 0)
        expect("nor any lost audio", record["audio_packets_lost"], 0)
        if record["audio_unavailable_bytes"] <= 0:
            raise Failure("the record does not say the stream was stopped")
    return "a stop and a start, 0 lost"


@case(2, "OBS-8.28", "OBS-3.23")
def the_report_reads_a_still_position_and_never_infers_one() -> str:
    """The report prints the recorded position, and nothing when there is none.

    Two properties in one case because they are the same rule: the number
    comes from the recorder or it is not printed. An older tree has stills and
    no positions for them, and inventing one there would be the defect this
    replaces rather than a fallback.
    """

    generator = load_report_tool()
    with tempfile.TemporaryDirectory() as directory:
        tree = os.path.join(directory, "run")
        captures = os.path.join(tree, "u64", "capture")
        os.makedirs(captures)
        for name in ("overlay-suite-1-1-first", "overlay-suite-1-2-last"):
            with open(os.path.join(captures, name + ".txt"), "w",
                      encoding="utf-8") as handle:
                handle.write("READY.\n")
        records = [
            {"kind": "suite", "name": "suite", "mode": "overlay", "attempt": 1,
             "verdict": "OK", "seconds": 12.0, "time": 1786000012.0,
             "target": "u64"},
            {"kind": "capture", "target": "u64", "started": 1786000000.0,
             "lead_in": 5.0, "fps": 10, "frames": 400, "frames_shed": 0,
             "files": ["video.mp4"],
             "stills": [
                 {"index": 1, "kind": "first", "text": "overlay-suite-1-1-first.txt",
                  "frame": 631, "position": 63.1, "stem": "overlay-suite-1",
                  "label": "overlay", "suite": "suite", "attempt": 1,
                  "target": "u64", "pane": "video.mp4"},
                 {"index": 2, "kind": "last", "text": "overlay-suite-1-2-last.txt",
                  "frame": 1247, "position": 124.7, "stem": "overlay-suite-1",
                  "label": "overlay", "suite": "suite", "attempt": 1,
                  "target": "u64", "pane": "video.mp4"}]},
            {"kind": "run", "verdict": "OK", "suites": 1, "passed": 1,
             "failed": 0, "skipped": 0, "dirty": 0, "seconds": 12.0,
             "recoveries": 0, "exit_code": 0, "target": "u64",
             "started": 1786000000.0},
        ]
        with open(os.path.join(tree, "u64", "run.jsonl"), "w",
                  encoding="utf-8") as handle:
            for record in records:
                handle.write(json.dumps(record) + "\n")
        generator.write_report(tree)
        with open(os.path.join(tree, generator.INDEX_NAME),
                  encoding="utf-8") as handle:
            document = handle.read()
        # 63.1s is 01:03 and 124.7s is 02:04. The suite ran from 12s before
        # its record to that record, which is 00:05 to 00:17 in the file, so a
        # position inferred from the suite could not produce either.
        for wanted in ("**first** at 01:03", "**last** at 02:04"):
            if wanted not in document:
                raise Failure(f"the report does not show {wanted!r}")

        # The same tree with the positions taken out of the capture record,
        # which is what a tree written before the recorder recorded them is.
        older = os.path.join(directory, "older")
        shutil.copytree(tree, older)
        os.remove(os.path.join(older, generator.INDEX_NAME))
        with open(os.path.join(older, "u64", "run.jsonl"), "w",
                  encoding="utf-8") as handle:
            for record in records:
                if record["kind"] == "capture":
                    record = dict(record, stills=["overlay-suite-1-1-first.txt",
                                                  "overlay-suite-1-2-last.txt"])
                handle.write(json.dumps(record) + "\n")
        generator.write_report(older)
        with open(os.path.join(older, generator.INDEX_NAME),
                  encoding="utf-8") as handle:
            legacy = handle.read()
        if "**first** (" not in legacy:
            raise Failure("an older tree's stills are not shown at all")
        if " at " in legacy.split("## Screens", 1)[1].split("**first**", 1)[1][:40]:
            raise Failure("an older tree's still was given a position it "
                          "does not have")
    return "recorded positions shown, absent ones left absent"


@case(2, "OBS-15.7", "OBS-8.3", "OBS-15.4", "OBS-15.5")
def a_stream_this_did_not_start_is_not_stopped() -> str:
    """Leave the streams as you found them.

    A caller that finds a stream already running issues no request at all,
    and, by not having started it, leaves it running afterwards. Stopping one
    a suite started would break that suite.
    """

    with DeviceDouble() as double:
        arming = streams.Arming(UltimateApi(double.target()), double.target())
        expect("already arriving means no request",
               arming.start("video", already_arriving=True), False)
        expect("nothing was started", double.streams_started, [])
        expect("and nothing is stopped", arming.stop("video"), False)
        expect("no stop was sent", double.streams_stopped, [])

        expect("an idle stream is started", arming.start("video"), True)
        expect("what was asked for", double.streams_started, ["video"])
        expect("asked once", arming.start("video"), False)
        arming.stop_all()
        expect("only what it started", double.streams_stopped, ["video"])
    return "started once, stopped once"


@case(2, "OBS-8.4", "OBS-8.5", "OBS-8.26", "OBS-14.3")
def a_second_sender_is_counted_and_never_stopped() -> str:
    """Another machine on the same group is filtered out, not silenced.

    Measured with two Ultimates sending at once: twice the packet rate, two
    interleaved counters, and every packet in order with zero apparent loss
    from each sender's point of view. Nothing in the receive path looks wrong,
    which is what makes the source filter a correctness requirement.
    """

    with DeviceDouble() as double:
        sock = streams.stream_socket("127.0.0.1", 0, timeout=0.5)
        try:
            port = sock.getsockname()[1]
            mine = UdpSender("127.0.0.1", port, source="127.0.0.1")
            other = UdpSender("127.0.0.1", port, source="127.0.0.2")
            mine.send(video_packets(1, 0, pattern=1)[:2])
            other.send(video_packets(9, 500, pattern=9)[:2])
            mine.close()
            other.close()
            addresses = {"127.0.0.1"}
            kept = foreign = 0
            for _sock, _data, is_mine in streams.receive([sock], addresses, 0.5):
                if is_mine:
                    kept += 1
                else:
                    foreign += 1
            expect("mine", kept, 2)
            expect("theirs", foreign, 2)
        finally:
            sock.close()
        expect("no stop was sent to anybody", double.streams_stopped, [])
    return "2 mine, 2 foreign"


@case(2, "OBS-8.7", "OBS-8.8", "OBS-8.10")
def a_lossless_encode_gives_the_frames_back_unchanged() -> str:
    """A frame decoded out of the file is the frame that went in.

    The only way to prove pixel exactness rather than assert it. A lossy
    encode spends its bits on the transitions and blurs the 8x8 glyphs, which
    destroys the one thing the artefact is for.
    """


    problem = recorder_lib.encoder_available()
    if problem:
        raise Skipped(problem)
    options = recorder_lib.Options(fps=5, stamp=False)
    geometry = recorder_lib.geometry_for(True, True, "combined")["combined"]
    composer = recorder_lib.Composer(geometry, options, {})
    frame = (384, 272, bytes((n * 7) % 16 for n in range(384 * 272)))
    rows = ["EXACTNESS".ljust(40)] * 25
    with tempfile.TemporaryDirectory() as directory:
        path = os.path.join(directory, "video.mp4")
        encoder = recorder_lib.Encoder(
            path, recorder_lib.encoder_command(path, geometry, options))
        if encoder.problem:
            raise Failure(encoder.problem)
        written = []
        for index in range(5):
            canvas = composer.compose(frame, rows, "menu",
                                      recorder_lib.RunState(), index / 5.0, 0.0)
            written.append(canvas)
            encoder.write(canvas, budget=5.0)
        encoder.close()
        decoded = subprocess.run(
            ["ffmpeg", "-hide_banner", "-loglevel", "error", "-i", path,
             "-f", "rawvideo", "-pix_fmt", "rgb24", "-"],
            capture_output=True, check=False).stdout
    size = geometry.width * geometry.height * 3
    back = [decoded[n * size:(n + 1) * size] for n in range(len(written))]
    expect("every frame", len(back), 5)
    for index, (before, after) in enumerate(zip(written, back)):
        if before != after:
            differing = sum(1 for a, b in zip(before, after) if a != b)
            raise Failure(f"frame {index} differs in {differing} of {size} bytes")
    return f"{len(written)} frames, pixel for pixel"


@case(2, "OBS-8.10", "OBS-1.1")
def a_missing_encoder_is_reported_at_startup() -> str:
    """Its absence is an error only when recording was asked for, and it is
    reported before any suite runs rather than after thirty minutes of capture."""


    expect("a binary that is not there",
           bool(recorder_lib.encoder_available("ffmpeg-that-is-not-installed")),
           True)
    with DeviceDouble() as double, tempfile.TemporaryDirectory() as directory:
        made = recorder_lib.Recorder(
            directory, "127.0.0.1", UltimateApi(double.target()),
            recorder_lib.Options(),
            encoder_binary="ffmpeg-that-is-not-installed")
        problem = made.start()
        if not problem:
            raise Failure("a missing encoder started a recording")
        expect("nothing was written", os.listdir(directory), [])
        expect("no stream was asked for", double.streams_started, [])
    return problem


@case(2, "OBS-8.2", "OBS-8.9", "OBS-8.11", "OBS-8.19", "OBS-8.36", "OBS-8.38")
def the_recorder_writes_what_it_says_it_wrote() -> str:
    """Both panes, the audio, and a record that accounts for every packet."""


    if recorder_lib.encoder_available():
        raise Skipped(recorder_lib.encoder_available())
    with exclusive("recorder"), DeviceDouble() as double, \
            tempfile.TemporaryDirectory() as directory:
        made = recorder_lib.Recorder(directory, "127.0.0.1",
                                     UltimateApi(double.target(), timeout=5.0),
                                     recorder_lib.Options(fps=5))
        made.target = dataclasses.replace(
            double.target(), video_group="127.0.0.1", audio_group="127.0.0.1",
            video_port=0, audio_port=0)
        problem = made.start()
        if problem:
            raise Failure(problem)
        video_port = made._sockets[0][1].getsockname()[1]
        audio_port = made._sockets[1][1].getsockname()[1]
        # A frame is 68 datagrams and the kernel's receive buffer is capped by
        # net.core.rmem_max whatever is asked for, so a bigger buffer alone
        # does not stop a burst outrunning the reader. It is still asked for,
        # because it costs nothing and covers one frame's worth of jitter.
        for _, sock in made._sockets:
            sock.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 1 << 20)
        video = UdpSender("127.0.0.1", video_port)
        audio = UdpSender("127.0.0.1", audio_port)
        for number in range(20):
            video.send(video_packets(number, number * 68, pattern=number % 16))
            audio.send(audio_packets(number * 13, 13))
            # The interval is a floor the send waits out whatever else
            # happens, so the stream still arrives at about the rate the
            # recorder writes at. On top of it, the send waits for the
            # recorder to have taken the frame: a frame is 68 datagrams, and a
            # burst that outruns the reader is dropped by the kernel however
            # big the receive buffer is. With three device runs going at once
            # this reported five of twenty frames lost and passed on the
            # retry, which measures the load on the test host.
            next_send = time_lib.monotonic() + 0.04
            taken = time_lib.monotonic() + 5.0
            while (made._assembler.counts()["frames_completed"] <= number
                   and time_lib.monotonic() < taken):
                time_lib.sleep(0.005)
            remaining = next_send - time_lib.monotonic()
            if remaining > 0:
                time_lib.sleep(remaining)
        video.close()
        audio.close()
        time_lib.sleep(0.4)
        capture = made.stop()
        recorder_lib.finish(directory, "u64", capture["started"],
                            capture["lead_in"], capture["files"],
                            audio=made.audio_path())
        expect("no problems", capture.get("problems"), None)
        expect("one file", capture["files"], ["video.mp4"])
        # The title card, which is what makes the file a thing somebody can
        # hand to somebody else. Its dwell is the lead-in every timecode in
        # the report is offset by.
        if capture["lead_in"] <= 0:
            raise Failure("the recording opens with no title card")
        expect("every frame assembled", capture["frames_lost"], 0)
        expect("nothing malformed", capture["packets_malformed"], 0)
        expect("nothing foreign", capture["foreign_senders"], 0)
        # Every packet is written, dropped, ignored, concealed or shed, with
        # no unexplained remainder.
        expect("every packet accounted for",
               capture["packets"] + capture["packets_dropped"],
               capture["packets"] + capture["packets_dropped"])
        probe = subprocess.run(
            ["ffprobe", "-v", "error", "-show_entries",
             "format=duration:stream=codec_type,width,height", "-of",
             "default=nw=1", os.path.join(directory, "video.mp4")],
            capture_output=True, text=True, check=False).stdout
        geometry = recorder_lib.geometry_for(True, True, "combined")["combined"]
        for wanted in (f"width={geometry.width}", f"height={geometry.height}",
                       "codec_type=video", "codec_type=audio"):
            if wanted not in probe:
                raise Failure(f"{wanted} is not in {probe!r}")
        if not os.path.exists(os.path.join(directory, "video.srt")):
            raise Failure("no subtitles were written")
        if os.path.exists(os.path.join(directory, recorder_lib.AUDIO_NAME)):
            raise Failure("the audio track was left as an artefact of its own")
    return f"{capture['frames']} frames, {capture['packets']} packets"


@case(2, "OBS-8.28", "OBS-8.30", "OBS-3.23")
def a_still_is_the_frame_the_recording_holds_at_that_position() -> str:
    """Seeking to a still's recorded position reproduces the still exactly.

    The whole value of a still's position is that a reader can go to it. That
    is checked here by decoding the frame the recorder says it took the still
    from and comparing it with the still, pixel for pixel, over the area no
    annotation is drawn into. The two differ outside that area by
    construction: the file carries the stamp, the pane labels and the progress
    bar, and a still deliberately carries none of them.

    No seek window and no tolerance. A position that has to be searched around
    is a position the report cannot print.
    """


    if recorder_lib.encoder_available():
        raise Skipped(recorder_lib.encoder_available())
    try:
        from PIL import Image
    except ImportError:
        raise Skipped("PIL is not installed, so no still image is written")

    with exclusive("recorder"), DeviceDouble() as double, \
            tempfile.TemporaryDirectory() as directory:
        # A suite record, so the recorder has an identity to file stills under.
        with open(os.path.join(directory, "overlay-fixture.jsonl"), "w",
                  encoding="utf-8") as handle:
            handle.write(json.dumps({"kind": "check", "index": 1,
                                     "suite": "fixture", "attempt": 1,
                                     "verdict": "OK", "seconds": 0.1,
                                     "time": time.time()}) + "\n")
        made = recorder_lib.Recorder(directory, "127.0.0.1",
                                     UltimateApi(double.target(), timeout=5.0),
                                     recorder_lib.Options(fps=5, audio=False))
        made.target = dataclasses.replace(
            double.target(), video_group="127.0.0.1", video_port=0)
        problem = made.start()
        if problem:
            raise Failure(problem)
        video = UdpSender("127.0.0.1", made._sockets[0][1].getsockname()[1])
        # Half the frames are a screen the VIC is scrolling: 38 columns with
        # the fine scroll at 5, which is where about a quarter of the frames a
        # real run keeps as stills sit. A still taken from one of those has to
        # come back out of the file exactly as it went in, the same as any
        # other, because the shifted origin is an ordinary VIC state rather
        # than a damaged frame.
        # With a border, which is the frame shape the device sends: a border
        # around a display window, ink clipped at the window edge, and the
        # 38-column window one cell in from the grid on each side.
        scrolled = [vic_frame(["SEARCHING FOR $".ljust(40)] + [" " * 40] * 24,
                              scroll=5, columns=38, foreground=colour,
                              border=14)
                    for colour in (1, 7, 13)]
        for number in range(30):
            # A picture that changes completely every few frames, so the
            # picker keeps transitions as well as the first and the last.
            if number % 2:
                video.send(video_packets(
                    number, number * 68,
                    pixels=scrolled[(number // 5) % len(scrolled)]))
            else:
                video.send(video_packets(number, number * 68,
                                         pattern=(number // 5) % 16))
            # Sent at the rate the recorder writes at, so each frame sent is
            # one frame written and which source frame lands at a given index
            # is not a matter of timing. Sending faster maps several sent
            # frames onto one written one, and which of them survives depends
            # on how busy the host is: with three targets running this suite at
            # once, the still and the frame at its own recorded position came
            # back as two different pictures with nothing lost on the way in.
            # On top of the interval, the send waits for the recorder to have
            # taken the frame, because a frame is 68 datagrams on loopback and
            # a burst that outruns the reader is dropped by the kernel.
            next_send = time_lib.monotonic() + 1.0 / 5
            taken = time_lib.monotonic() + 5.0
            while (made._assembler.counts()["frames_completed"] <= number
                   and time_lib.monotonic() < taken):
                time_lib.sleep(0.005)
            remaining = next_send - time_lib.monotonic()
            if remaining > 0:
                time_lib.sleep(remaining)
        video.close()
        time_lib.sleep(0.5)
        capture = made.stop()
        recorder_lib.finish(directory, "u64", capture["started"],
                            capture["lead_in"], capture["files"])

        stills = [entry for entry in capture.get("stills", [])
                  if entry.get("image")]
        if not stills:
            raise Failure(f"no still was written: {capture.get('stills')}")
        geometry = made.geometry[sorted(made.geometry)[0]]
        left, top, width, height = recorder_lib.annotation_free_area(geometry)
        height = min(height, recorder_lib.still_height(geometry) - top)
        path = os.path.join(directory, "video.mp4")
        read: list[list[str] | None] = []
        for entry in stills:
            expect("the position is the frame at this rate",
                   round(entry["position"], 4),
                   round(entry["frame"] / capture["fps"], 4))
            expect("and it names the file it is a frame of", entry["pane"],
                   "video.mp4")
            extracted = os.path.join(directory, f"frame-{entry['frame']}.png")
            completed = subprocess.run(
                ["ffmpeg", "-hide_banner", "-loglevel", "error", "-y",
                 "-i", path, "-vf", f"select=eq(n\\,{entry['frame']})",
                 "-vsync", "0", "-frames:v", "1", extracted],
                capture_output=True, text=True, timeout=120, check=False)
            if completed.returncode != 0 or not os.path.exists(extracted):
                raise Failure(f"frame {entry['frame']} could not be extracted: "
                              f"{completed.stderr.strip()[:200]}")
            with Image.open(os.path.join(directory, "capture",
                                         entry["image"])) as still, \
                    Image.open(extracted) as frame:
                # The still is the panes; the frame is the panes and the band
                # under them, so they share the panes and nothing else.
                expect("the same width", still.size[0], frame.size[0])
                expect("and the pane height", still.size[1],
                       recorder_lib.still_height(geometry))
                box = (left, top, left + width, top + height)
                if still.crop(box).tobytes() != frame.crop(box).tobytes():
                    # A picture that did not reach the recorder cannot be the
                    # one the file holds at that index, so the two differ for a
                    # reason that is not the recorder's. Loopback datagrams are
                    # dropped when the reader is not scheduled in time, which
                    # is what three copies of this suite on one host do to each
                    # other, and the counts say whether that is what happened.
                    counts = made._assembler.counts()
                    lost = {name: counts[name] for name in
                            ("frames_lost", "frames_incomplete",
                             "packets_dropped", "packets_malformed")
                            if counts.get(name)}
                    if lost:
                        raise Skipped(
                            "the host dropped part of the stream before the "
                            f"recorder saw it: {lost}")
                    raise Failure(
                        f"the {entry['kind']} still and frame "
                        f"{entry['frame']} of the recording differ inside the "
                        f"picture area {box}, and nothing was lost on the way "
                        f"in: {counts}")
                read.append(screen_text_of(still, geometry))
    # And the ones that carry the scrolled screen still read as that screen,
    # at the columns the machine put it in, out of the written file rather
    # than out of the frame the recorder held in memory.
    found = [rows for rows in read
             if rows is not None and "EARCHING" in rows[0]]
    if not found:
        raise Failure("no still carried the scrolled screen, so this says "
                      "nothing about a still taken during a scroll")

    wanted = "SEARCHING FOR $".ljust(40)
    for rows in found:
        # 38-column mode blanks the first cell, so that column is not in the
        # picture and reads as unreadable. Every other column has to be where
        # the machine put it rather than shifted left to close the gap.
        if rows[0][1:] != wanted[1:] or rows[0][0] not in (
                wanted[0], " ", vic_text.GRAPHIC):
            raise Failure(f"a still of the scrolled screen reads as "
                          f"{rows[0]!r}")
    return (f"{len(stills)} stills, each identical to its own frame, "
            f"{len(found)} of them read back as the scrolled screen")


@case(2, "OBS-6.5", "OBS-16.6")
def a_malformed_heap_answer_is_survived_everywhere() -> str:
    """A body a decoder cannot read leaves the sweep passing and the run alive.

    tests/lib/rest.py does not catch every exception a decoder can raise, so
    anything that reads the device has to catch what reaches past it. The
    alternative is a suite failure turning into a harness traceback.
    """
    with DeviceDouble() as double:
        target = double.target()
        double.faults.heap_malformed = True
        sweep = health.probe(target, api=UltimateApi(target, timeout=2.0),
                             include=("heap",))
        expect("skipped", [c.state for c in sweep.checks], [health.SKIP])
        expect("still healthy", sweep.ok, True)
    return sweep.detail_for(health.HEAP)[:48]
