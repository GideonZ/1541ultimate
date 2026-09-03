#!/usr/bin/env python3
# Tier 4: the generated report, compared against the document checked in beside it.

"""The golden cases of the observability suite.

The generated report, compared against the document checked in beside it.

Importing this module registers its cases in support.CASES. The entry
point, tests/lib/observability_test.py, imports all four tiers and runs
them in the order TIERS names.
"""

from report import Failure
from selftest import expect
import json
import os
import re
import tempfile
from support import (EXPECTED, MALFORMED_RECORDS, ROOT, ScriptedRun,
                     fixture_tree,
    Skipped, canonicalize_document, case, generated_document,
    load_report_tool, require_fixture)
import collections
import shutil
import subprocess


@case(4, "OBS-5.3", "OBS-3.24")
def a_failure_whose_menu_had_closed_still_shows_what_it_was_driving() -> str:
    """The C64's own screen is not what a menu suite was looking at.

    When no menu is open the capture reads $0400, which is right for a suite
    that was driving the C64 and useless for one that was driving the menu and
    whose session closed on the way out. Seen on a real run: a check failed
    with a context menu open, the suite closed its session, and the report
    showed the BASIC prompt.

    The screen it was driving is in the spool the run already wrote, so this
    costs no device read, which is the same argument OBS-5.9 makes for the
    Telnet capture.
    """

    require_fixture()
    generator = load_report_tool()
    marker = "A WINDOW THE SUITE HAD OPEN"
    with tempfile.TemporaryDirectory() as directory:
        tree = os.path.join(directory, "run")
        shutil.copytree(fixture_tree(), tree)
        # `broken` fails, and its capture in the fixture is a readmem one.
        state_path = os.path.join(tree, "127.0.0.1", "capture",
                                  "overlay-broken-1-state.json")
        with open(state_path, encoding="utf-8") as handle:
            state = json.load(handle)
        state["source"] = "readmem"
        with open(state_path, "w", encoding="utf-8") as handle:
            json.dump(state, handle)
        with open(os.path.join(tree, "127.0.0.1", "screens.jsonl"), "a",
                  encoding="utf-8") as handle:
            handle.write(json.dumps({
                "kind": "menu", "suite": "broken", "attempt": 1,
                "time": 1.0, "cols": 40, "rows": 25,
                "text": [marker]}) + "\n")
        generator.write_report(tree)
        with open(os.path.join(tree, generator.INDEX_NAME),
                  encoding="utf-8") as handle:
            document = handle.read()
    if marker not in document:
        raise Failure("the screen the suite was driving is not in the report")
    if "The last menu screen this suite read" not in document:
        raise Failure("the spooled screen is not named as such")
    return "the spooled screen is shown beside the C64's"


@case(4, "OBS-7.6", "OBS-3.11")
def the_report_says_who_sent_the_lines_nobody_claimed() -> str:
    """A non-empty unknown-sender file is told to the reader, per sender.

    The file exists to make the omission visible rather than silent, and a
    reader who has to list the directory to find it is not being told. Seen on
    a real run: 132 lines from a second device on the same network, which is
    exactly the case the reader has to be able to dismiss. What lets them
    dismiss it is the address, the count and the reason attribution failed,
    per sender, so two senders are not one line saying "some lines arrived".
    """

    require_fixture()
    generator = load_report_tool()
    with tempfile.TemporaryDirectory() as directory:
        tree = os.path.join(directory, "run")
        shutil.copytree(fixture_tree(), tree)
        with open(os.path.join(tree, generator.UNKNOWN_SENDER_NAME), "w",
                  encoding="utf-8") as handle:
            for _ in range(3):
                handle.write("1786000000.000 192.0.2.99 a line nobody claimed\n")
            for _ in range(2):
                handle.write("1786000001.000 192.0.2.7 another one\n")
        generator.write_report(tree)
        with open(os.path.join(tree, generator.INDEX_NAME),
                  encoding="utf-8") as handle:
            document = handle.read()
    for wanted in ("5 line(s) arrived from a sender no target in this run is "
                   "known by",
                   "| `192.0.2.99` | 3     |", "| `192.0.2.7`  | 2     |",
                   "no machine of any target in this run resolves to it",
                   "No target is guessed for them",
                   "U64_LOG_ADDRESSES"):
        if wanted not in document:
            raise Failure(f"the report does not say {wanted!r}")
    # It is never dismissed by a conclusion drawn somewhere else in the
    # document: the section that carries it is in the report either way.
    if "## Device log" not in document:
        raise Failure("the unknown senders are not in a section of their own")
    # And the file table tells it apart from a device's own log.
    if "the device's own log, as the collector received it" in document.split(
            generator.UNKNOWN_SENDER_NAME)[1][:200]:
        raise Failure(f"{generator.UNKNOWN_SENDER_NAME} is described as a "
                      "device's own log")
    return "two senders, each with its count and its reason"


@case(4, "OBS-3.13", "OBS-3.21")
def the_document_is_byte_identical_to_the_expected_one() -> str:
    """The report for the fixture matches the document checked in beside it,
    once both are put through the same substitution for what a live build
    cannot hold still.

    A diff of that document is exactly the diff a reader of a real report would
    see, which is what makes a rendering change visible in review rather than
    only in production.
    """
    require_fixture()
    if not os.path.exists(EXPECTED):
        raise Failure(f"{os.path.relpath(EXPECTED, ROOT)} is missing")
    with open(EXPECTED, encoding="utf-8") as handle:
        wanted = canonicalize_document(handle.read())
    made = canonicalize_document(generated_document())
    if made != wanted:
        import difflib

        diff = list(difflib.unified_diff(wanted.splitlines(), made.splitlines(),
                                         "expected", "generated", lineterm=""))
        raise Failure("the document changed: " + " | ".join(diff[:6]))
    return f"{len(wanted.splitlines())} lines"


@case(4, "OBS-3.21")
def the_document_is_reproducible() -> str:
    """Two runs of the generator over one tree produce identical bytes."""
    require_fixture()
    first = generated_document()
    second = generated_document()
    if first != second:
        raise Failure("two runs over the same tree disagreed")
    return "identical twice"


@case(4, "OBS-3.15")
def the_document_is_markdown_and_nothing_else() -> str:
    """No HTML element but the detail marker, and no external reference.

    This is the property that lets one file serve the job summary, an editor,
    pandoc and a model with no per-target rendering.
    """

    require_fixture()
    document = generated_document()
    expect("one detail marker", document.count("<!-- detail -->"), 1)
    # Fenced blocks and code spans carry whatever the device and the suites
    # said, which is not markup and is not this document's to police.
    prose = re.sub(r"```.*?```", "", document, flags=re.S)
    prose = re.sub(r"`[^`]*`", "", prose)
    elements = [tag for tag in re.findall(r"<[^>]+>", prose)
                if tag != "<!-- detail -->"]
    if elements:
        raise Failure(f"HTML elements outside code: {elements[:5]}")
    external = re.findall(r"https?://\S+", prose)
    if external:
        raise Failure(f"external references: {external[:5]}")
    return "GFM only"


@case(4, "OBS-3.15")
def every_table_is_padded() -> str:
    """Every pipe table in the document has columns of one width."""
    require_fixture()
    document = generated_document()
    inside_fence = False
    block: list[str] = []
    tables = 0
    for line in [*document.splitlines(), ""]:
        if line.startswith("```"):
            inside_fence = not inside_fence
            continue
        if not inside_fence and line.startswith("|"):
            block.append(line)
            continue
        if block:
            widths = {len(row) for row in block}
            if len(widths) != 1:
                raise Failure(f"a table is ragged: {sorted(widths)} "
                              f"at {block[0][:60]!r}")
            tables += 1
            block = []
    if not tables:
        raise Failure("the document has no tables")
    return f"{tables} tables"


@case(4, "OBS-3.22")
def the_status_line_agrees_with_the_run_records() -> str:
    """One greppable line, first after the title, counting what the runner counted."""
    require_fixture()
    generator = load_report_tool()
    run = generator.load_tree(fixture_tree())
    document = generated_document()
    lines = [line for line in document.splitlines() if line.strip()]
    expect("the title first", lines[0], "# E2E gate run: FAIL")
    status = lines[1]
    if not status.startswith("RESULT: "):
        raise Failure(f"the status line is not second: {status!r}")
    fields = dict(part.split("=", 1) for part in status.split("  ")[1:])
    counts = run.counts()
    for name in ("targets", "suites", "ok", "fail", "warn", "skip",
                 "recoveries", "retried"):
        expect(name, fields[name], str(counts[name]))
    expect("exit", fields["exit"], str(run.exit_code))
    # The order is part of the contract, not only the keys: this line is
    # parsed by position by anything that does not want a parser of its own.
    expect("the keys, in order", list(fields),
           ["targets", "suites", "ok", "fail", "warn", "skip", "recoveries",
            "retried", "exit"])
    return status


@case(4, "OBS-3.20")
def a_failure_carries_the_command_and_the_log() -> str:
    """Each failing check names how to run it again and how its suite ended."""
    require_fixture()
    document = generated_document()
    if "./run-tests -H 127.0.0.1 -s broken --mode overlay" not in document:
        raise Failure("a failing check carries no reproduce command")
    if "RuntimeError: the device stopped answering mid-check" not in document:
        raise Failure("the traceback the suite ended on is not in the document")
    if "suites/broken.py" not in document:
        raise Failure("the failing suite's source path is not named")
    return "command, source and log tail"


@case(4, "OBS-3.27", "OBS-3.10")
def a_failure_carries_what_the_run_already_knows() -> str:
    """Facts the run recorded about a failure, and never a diagnosis."""
    require_fixture()
    document = generated_document()
    for wanted in ("Failed elsewhere:", "Passed elsewhere:",
                   "Passed on retry:", "First failure:"):
        if wanted not in document:
            raise Failure(f"{wanted!r} is missing from the failing entries")
    for forbidden in ("probably", "likely", "suggests", "the cause"):
        if forbidden in document:
            raise Failure(f"the document guesses: {forbidden!r}")
    return "4 kinds of fact, no guess"


@case(4, "OBS-3.18", "OBS-3.17")
def a_killed_run_is_rendered_as_what_it_is() -> str:
    """A truncated line and a missing closing record are stated, not refused."""
    require_fixture()
    document = generated_document()
    if "incomplete" not in document:
        raise Failure("the suite with no closing record is not marked")
    if "could not be read and were skipped" not in document:
        raise Failure("the truncated line is not counted in the header")
    if "wrote no closing record" not in document:
        raise Failure("the target with no run record is not named")
    return "incomplete, counted and named"


@case(4, "OBS-3.25", "OBS-2.14")
def coverage_says_what_the_run_did_not_do() -> str:
    """Planned against completed, what did not run, and what skipped."""
    require_fixture()
    document = generated_document()
    if "planned suite runs completed" not in document:
        raise Failure("the planned count is missing")
    if "| missing-file" not in document.split("## Failing checks")[0]:
        raise Failure("the suite whose file is absent is not in the verdicts")
    if "needs the ftp-listing-full-length fix" not in document:
        raise Failure("a skipped check's reason is not carried")
    return "planned, absent and skipped"


@case(4, "OBS-3.14")
def every_path_the_document_names_exists() -> str:
    """A file the report points at is a file in the tree."""

    require_fixture()
    document = generated_document()
    section = document.split("## Files in this run", 1)[1].split("\n\n", 2)[2]
    named = re.findall(r"^\| `([^`]+)`", section, flags=re.M)
    if len(named) < 10:
        raise Failure(f"the file index is too short: {named}")
    for relative in named:
        if relative == "index.md":
            continue
        if not os.path.exists(os.path.join(fixture_tree(), relative)):
            raise Failure(f"{relative} is named and is not there")
    return f"{len(named)} files"


@case(4, "OBS-3.17")
def a_record_of_the_wrong_shape_costs_that_record_only() -> str:
    """Every one of these still produces a document and exits zero."""

    require_fixture()
    generator = load_report_tool()
    for label, record, name in MALFORMED_RECORDS:
        with tempfile.TemporaryDirectory() as directory:
            tree = os.path.join(directory, "run")
            shutil.copytree(fixture_tree(), tree)
            path = os.path.join(tree, "127.0.0.1", name)
            if record is None:
                open(path, "w").close()
            else:
                with open(path, "a", encoding="utf-8") as handle:
                    handle.write(json.dumps(record) + "\n")
            try:
                generator.write_report(tree)
            except Exception as exc:  # noqa: BLE001 - the point of the case
                raise Failure(f"{label}: {type(exc).__name__}: {exc}") from exc
            with open(os.path.join(tree, generator.INDEX_NAME),
                      encoding="utf-8") as handle:
                if not handle.read().startswith("# E2E gate run:"):
                    raise Failure(f"{label}: no document was written")
    return f"{len(MALFORMED_RECORDS)} shapes"


@case(4, "OBS-15.11", "OBS-3.26")
def the_timeline_shows_a_gap_at_both_of_its_ends() -> str:
    """A gap is two events on the timeline, or one that says it never closed.

    Both ends rather than one line naming two times, so a reader sees what was
    running when the resource went away and what was running when it came
    back. The fixture has no gap of its own, since nothing in it goes quiet
    for long enough, so the two shapes are appended to a copy of it.
    """

    require_fixture()
    generator = load_report_tool()
    with tempfile.TemporaryDirectory() as directory:
        tree = os.path.join(directory, "run")
        shutil.copytree(fixture_tree(), tree)
        anchor_time = 0.0
        for record in ScriptedRun(tree, 0, "").records("127.0.0.1", "run.jsonl"):
            if record.get("kind") == "run":
                anchor_time = float(record.get("time") or 0.0)
        # The closed one in the target's own file, the open one in the
        # parent's: the collector runs in the process that owns the whole run,
        # so on a run of several targets its records are the parent's and name
        # the target each belongs to, and both have to reach the timeline.
        with open(os.path.join(tree, "127.0.0.1", "run.jsonl"), "a",
                  encoding="utf-8") as handle:
            handle.write(json.dumps({
                "kind": "gap", "time": anchor_time, "suite": "run-tests",
                "target": "127.0.0.1", "component": "syslog",
                "started": anchor_time - 30.0, "ended": anchor_time - 18.0,
                "reason": "the device stopped logging"}) + "\n")
        with open(os.path.join(tree, "run.jsonl"), "a",
                  encoding="utf-8") as handle:
            handle.write(json.dumps({
                "kind": "gap", "time": anchor_time, "suite": "run-tests",
                "target": "127.0.0.1", "component": "recorder",
                "started": anchor_time - 10.0,
                "reason": "the video stream stopped"}) + "\n")
        generator.write_report(tree)
        with open(os.path.join(tree, generator.INDEX_NAME),
                  encoding="utf-8") as handle:
            document = handle.read()
    for wanted in ("syslog gap opened: the device stopped logging",
                   "syslog gap closed after 12.0s",
                   "recorder gap opened: the video stream stopped",
                   "recorder gap still open when the run ended"):
        if wanted not in document:
            raise Failure(f"the timeline does not say {wanted!r}")
    return "one closed, one still open"


@case(4, "OBS-3.23", "OBS-8.28", "OBS-8.11")
def the_report_shows_the_stills_and_the_timecode() -> str:
    """A reader who never opens the recording still sees what a suite saw."""
    require_fixture()
    # Whichever suite runs the recorder caught: which those are depends on
    # where the output frames landed, and the report names what is in the tree.
    captures = os.path.join(fixture_tree(), "127.0.0.1", "capture")
    stills = [name for name in sorted(os.listdir(captures))
              if name.endswith("-first.txt")]
    if not stills:
        raise Skipped("the fixture was recorded without a recording")
    document = generated_document()
    if "## Screens" not in document:
        raise Failure("the stills are not in the report")
    section = document.split("## Screens", 1)[1].split("## Device log", 1)[0]
    for name in stills:
        if f"capture/{name}" not in section:
            raise Failure(f"{name} is in the tree and not in the report")
    if "-first.png" not in section:
        raise Failure("the image beside the text is not named")
    # A failing check carries where it is in the file, which is what makes the
    # recording usable from the report rather than only from a player.
    failing = document.split("## Failing checks", 1)[1].split("## Device health",
                                                              1)[0]
    if "into the recording" not in failing:
        raise Failure("a failing check carries no position in the file")
    return f"{len(stills)} suite runs with stills"


@case(4, "OBS-8.12", "OBS-8.34")
def the_recording_can_be_navigated_three_ways() -> str:
    """Chapters, a greppable sidecar, and the mm:ss the report prints.

    A reader who already has the report needs none of the others, which is why
    all three exist rather than one.
    """

    require_fixture()
    video = os.path.join(fixture_tree(), "127.0.0.1", "video.mp4")
    subtitles = os.path.join(fixture_tree(), "127.0.0.1", "video.srt")
    if not os.path.exists(video):
        raise Skipped("the fixture was recorded without a recording")
    with open(subtitles, encoding="utf-8") as handle:
        cues = handle.read()
    if "FAIL" not in cues:
        raise Failure("grep FAIL over the sidecar finds no failing check")
    if "127.0.0.1/overlay/broken/1/1" not in cues:
        raise Failure("a cue does not carry the identity key")
    probe = subprocess.run(
        ["ffprobe", "-v", "error", "-show_chapters", "-of", "default=nw=1",
         video], capture_output=True, text=True, check=False)
    if probe.returncode != 0:
        raise Skipped("ffprobe is not installed")
    if "overlay/broken" not in probe.stdout:
        raise Failure(f"no chapter names a suite run: {probe.stdout[:200]}")
    if "FAIL" not in probe.stdout:
        raise Failure("no chapter names a failing check")
    return f"{cues.count('-->')} cues, {probe.stdout.count('[CHAPTER]')} chapters"


@case(4, "OBS-3.4", "OBS-3.6")
def the_verdict_table_names_every_attempt() -> str:
    """One row per attempt, and one identity key shape for both labels."""
    require_fixture()
    document = generated_document()
    verdicts = document.split("## Verdict", 1)[1].split("## Coverage", 1)[0]
    rows = [line for line in verdicts.splitlines() if line.startswith("| 127.")]
    retried = [line for line in rows if "| flaky" in line]
    expect("both attempts", len(retried), 4)
    if not any("| 1       |" in line for line in retried) or \
            not any("| 2       |" in line for line in retried):
        raise Failure(f"the two attempts are not distinguished: {retried}")
    # A perf suite is named by its category and an e2e suite by its mode, and
    # each has exactly one row, not one per label the two could be read as.
    # One row, not one per label a reader could take the record's mode for.
    # The second target never reaches the perf category: a suite before it
    # kills that run.
    benchmark = [line for line in rows if "| a-benchmark" in line]
    expect("one row for the benchmark", len(benchmark), 1)
    for line in benchmark:
        if "| perf" not in line:
            raise Failure(f"a perf suite is not named by its category: {line}")
    if "127.0.0.1/perf/a-benchmark/1" not in document:
        raise Failure("the perf suite's identity key is not in the document")
    return f"{len(rows)} suite runs"


@case(4, "OBS-3.6")
def the_identity_key_names_the_files_it_says_it_does() -> str:
    """The substitution the preamble states produces the names in the tree."""
    require_fixture()
    document = generated_document()
    generator = load_report_tool()
    run = generator.load_tree(fixture_tree())
    for made in run.all_suites():
        target = next(t for t in run.targets if t.token == made.target)
        for relative in (f"{made.stem}.log",
                         f"capture/{made.stem}-{made.attempt}-screen.txt"):
            path = os.path.join(fixture_tree(), target.slug, relative)
            if os.path.exists(path) and relative not in document:
                raise Failure(f"{relative} is in the tree and not in the report")
    return "log and capture names"


@case(4, "OBS-3.7")
def the_health_table_is_every_sweep_in_order() -> str:
    """One row per sweep, one column per check, in the console's own words."""
    require_fixture()
    generator = load_report_tool()
    run = generator.load_tree(fixture_tree())
    document = generated_document()
    section = document.split("## Device health", 1)[1].split("## Files", 1)[0]
    for target in run.targets:
        rows = [line for line in section.splitlines()
                if line.startswith("| ") and not line.startswith("| ---")]
        if len(rows) < len(target.health):
            raise Failure(f"{len(rows)} rows for {len(target.health)} sweeps")
    for wanted in ("| ping ", "| DEGRADED ", "| FAIL ", "| skip "):
        if wanted not in section:
            raise Failure(f"{wanted!r} is missing from the health tables")
    return "every sweep, degraded and skipped shown"


@case(4, "OBS-3.24")
def the_preamble_is_short_and_fixed() -> str:
    """Fifteen lines at most, and the same fifteen in every report."""
    generator = load_report_tool()
    body = generator.PREAMBLE.split("\n", 1)[1].strip().splitlines()
    if len(body) > 15:
        raise Failure(f"the preamble is {len(body)} lines")
    for wanted in ("Verdict", "Coverage", "Failing checks", "Device health",
                   "Files in this run", ".log", "capture/"):
        if not any(wanted in line for line in body):
            raise Failure(f"the preamble does not mention {wanted!r}")
    return f"{len(body)} lines"


@case(4, "OBS-3.26", "OBS-15.11")
def the_timeline_is_the_whole_run_in_order() -> str:
    """Suites, sweeps, failures, captures, recoveries and device requests."""
    require_fixture()
    document = generated_document()
    section = document.split("## Timeline", 1)[1].split("## Checks", 1)[0]
    for wanted in ("started", "sweep", "FAIL", "device state captured",
                   "was recovered", "GET /v1/machine:menu_screen"):
        if wanted not in section:
            raise Failure(f"{wanted!r} is not on the timeline")
    offsets = [line.split()[0] for line in section.splitlines()
               if line.startswith("+")]
    if offsets != sorted(offsets):
        raise Failure("the timeline is not in wall-clock order")
    # One request the harness makes once per sweep would otherwise sit between
    # every pair of events for the length of the run.

    requests = collections.Counter(
        line.split("  ", 1)[1] for line in section.splitlines()
        if line.startswith("+") and " GET " in line)
    worst = requests.most_common(1)
    if worst and worst[0][1] > 4:
        raise Failure(f"one request fills {worst[0][1]} timeline lines: "
                      f"{worst[0][0][:60]}")
    return f"{len(offsets)} events"


@case(4, "OBS-3.29")
def the_time_section_names_the_slow_ones() -> str:
    """The slowest suites and checks, by name rather than as a count."""
    require_fixture()
    document = generated_document()
    section = document.split("## Where the time went", 1)[1]
    if "Slowest suite runs" not in section or "Slowest checks" not in section:
        raise Failure("one of the two tables is missing")
    if "127.0.0.1/overlay/" not in section:
        raise Failure("the slowest rows do not name a suite run")
    return "both tables"


@case(4, "OBS-3.30")
def the_report_says_what_the_run_left_behind() -> str:
    """A mount with no unmount and a setting the run wrote, with their suite."""
    require_fixture()
    document = generated_document()
    section = document.split("## What this run changed", 1)[1] \
        .split("## Failing checks", 1)[0]
    if "mount not undone" not in section:
        raise Failure("a drive mounted and not unmounted is not reported")
    if "Log to Syslog Server" not in section:
        raise Failure("a setting the run wrote is not reported")
    if "leaves-things-behind" not in section:
        raise Failure("the suite that made the change is not named")
    if "/v1/machine:reset" in section:
        raise Failure("a reset is reported as something left behind")
    return "a mount and a setting"


@case(4, "OBS-3.11", "OBS-7.14")
def the_report_shows_the_device_log_around_a_failure() -> str:
    """The lines received during a failing check, and the restart on the timeline."""
    require_fixture()
    document = generated_document()
    if "## Device log" not in document:
        raise Failure("the collected log is not in the report")
    section = document.split("## Device log", 1)[1]
    if "best-effort and incomplete by construction" not in section:
        raise Failure("the report does not say what the log is worth")
    if "1541: no answer from the drive" not in section:
        raise Failure("the lines around the failure are not inlined")
    if "127.0.0.1/overlay/noisy/1/1" not in section:
        raise Failure("the slice is not attributed to the failing check")
    # Both sides of the comparison. The table exists to show a device whose log
    # arrived from an address its name does not resolve to, and it cannot show
    # that with the expected side blank, which is what it carried for every run
    # until the addresses were read as the list of strings they are.
    table = section.split("Expected from", 1)[1].split("\n\n", 1)[0]
    if table.count("`127.0.0.1`") < 2:
        raise Failure(f"the expected and observed addresses are not both "
                      f"named: {table!r}")
    # The failing check's window holds 83 lines, 80 of them the device's own
    # echo of requests this run made. A slice that is the last of everything
    # would be 60 of those and neither of the two lines about the drive.
    slice_text = section.split("127.0.0.1/overlay/noisy/1/1", 1)[1]
    slice_text = slice_text.split("```", 2)[1]
    if "HTTP GET /v1/machine:menu_screen" in slice_text:
        raise Failure("this run's own requests are inlined in the slice")
    if "Accept client" in slice_text:
        raise Failure("this run's own connections are inlined in the slice")
    if "1541: seek track 18" not in slice_text:
        raise Failure("the device's own lines were pushed out of the slice by "
                      "this run's requests")
    if "line(s) in the window" not in section:
        raise Failure("the slice does not say how wide the window was")
    if "this run's own requests, which are in the file and not here" not in section:
        raise Failure("the omitted lines are not counted")
    timeline = document.split("## Timeline", 1)[1].split("## Checks", 1)[0]
    if "restarted, seen in its own log" not in timeline:
        raise Failure("a device restart is not on the timeline")
    # Every check would multiply the document by the check count to answer a
    # question only ever asked about failures.
    if section.count("from the end of the check before it") > 4:
        raise Failure("the log is inlined for checks that did not fail")
    return "one slice, one restart"


@case(4, "OBS-4.1", "OBS-1.7", exclusive=True)
def the_job_summary_is_a_copy_of_part_of_the_report() -> str:
    """The summary is the report's own bytes, plus at most the two lines.

    Asserting this by construction is what keeps one authored report true:
    the summary is a copy, and there is no second generator to keep in step.
    """

    require_fixture()
    generator = load_report_tool()
    document = generated_document()
    with tempfile.TemporaryDirectory() as directory:
        with open(os.path.join(directory, generator.INDEX_NAME), "w",
                  encoding="utf-8") as handle:
            handle.write(document)
        summary = os.path.join(directory, "summary.md")
        previous = os.environ.get("E2E_ARTIFACT_URL")
        os.environ["GITHUB_STEP_SUMMARY"] = summary
        os.environ.pop("E2E_ARTIFACT_URL", None)
        try:
            expect("exit", generator.job_summary(directory), 0)
            with open(summary, encoding="utf-8") as handle:
                written = handle.read()
            wanted = document.split(generator.DETAIL_MARKER, 1)[0]
            expect("a byte copy", written, wanted)
            if len(written.encode()) >= 1024 * 1024:
                raise Failure("the summary is over GitHub's limit")
            if generator.DETAIL_MARKER in written:
                raise Failure("the detail part reached the summary")

            # With the artifact's URL in the environment, exactly one line is
            # appended, because the report is generated before the artifact
            # exists and cannot carry it.
            os.environ["E2E_ARTIFACT_URL"] = "https://example.invalid/artifact/1"
            os.remove(summary)
            generator.job_summary(directory)
            with open(summary, encoding="utf-8") as handle:
                linked = handle.read()
            extra = linked[len(wanted):].strip().splitlines()
            expect("one appended line", len(extra), 1)
            if "https://example.invalid/artifact/1" not in extra[0]:
                raise Failure(f"the artifact link is not the appended line: {extra}")
        finally:
            os.environ.pop("GITHUB_STEP_SUMMARY", None)
            if previous is None:
                os.environ.pop("E2E_ARTIFACT_URL", None)
            else:
                os.environ["E2E_ARTIFACT_URL"] = previous
    return f"{len(wanted.splitlines())} lines copied"
