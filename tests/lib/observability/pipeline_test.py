#!/usr/bin/env python3
# Tier 3: a whole run of the harness against the device double, checked through the artefacts it writes.

"""The pipeline cases of the observability suite.

A whole run of the harness against the device double, checked through the artefacts it writes.

Importing this module registers its cases in support.CASES. The entry
point, tests/lib/observability_test.py, imports all four tiers and runs
them in the order TIERS names.
"""

from device_double import DeviceDouble
from report import Failure
from selftest import expect
import json
import os
import re
import report
import sys
import tempfile
from support import (REPORT_TOOL, RETRY_BODY, ROOT, RUNNER_PATH, fixture_tree,
    Stub, case, load_report_tool, records_from_a_stub_suite, require_fixture,
    scripted_run)
import shutil
import subprocess


@case(3, "OBS-16.6", "OBS-1.1", exclusive=True)
def a_scripted_run_does_not_inherit_the_gate_state() -> str:
    """This suite runs inside the gate, and a run of its own is not a child of it.

    Every one of these variables means something to the runner, and the suite
    inherits whatever started it. E2E_SYSLOG_OWNED tells the runner another
    process already holds the collector's port, so a scripted run started
    none, wrote no `log` record, and three cases about the device log failed
    for a reason that had nothing to do with them. Measured under
    `run-tests u64 u2@c64u c64u`: every target's copy of this suite failed the
    same three.
    """

    leaked = {"E2E_SYSLOG_OWNED": "1", "E2E_SUITE": "somebody-else",
              "E2E_TARGET": "somewhere-else", "E2E_ATTEMPT": "7"}
    saved = {name: os.environ.get(name) for name in leaked}
    os.environ.update(leaked)
    try:
        with DeviceDouble() as double, tempfile.TemporaryDirectory() as workspace:
            made = scripted_run(double, [Stub("held")], workspace=workspace,
                                arguments=("--syslog", "--syslog-port", "0"))
            logs = [r for r in made.records("127.0.0.1", "run.jsonl")
                    if r["kind"] == "log"]
            # One when collection starts and one when it ends.
            expect("the collector still started", len(logs), 2)
            suites = [r for r in made.records("127.0.0.1", "run.jsonl")
                      if r["kind"] == "suite"]
            expect("and the run is its own", suites[-1]["target"], "127.0.0.1")
    finally:
        for name, value in saved.items():
            if value is None:
                os.environ.pop(name, None)
            else:
                os.environ[name] = value
    return "the gate's state does not reach a scripted run"


@case(3, "OBS-2.1", "OBS-3.17")
def a_policy_fixture_never_writes_into_the_run_it_runs_inside() -> str:
    """runner_policy_test drives the runner, and none of that reaches the run.

    The suite exercises `Device.ensure_healthy` and `run_suite` against devices
    and suites that exist only inside it. Both report through the same module
    the suite itself reports through, so under the gate their synthetic records
    were appended to the run's real per-suite file: six `suite` records naming
    `fixture-suite`, one naming `signalled-suite`, and eight `health` records
    for a device called `device.invalid`. The report rendered them as suite
    runs with no closing verdict, so a fully green run showed six `incomplete`
    rows in the verdict table and six entries under `## Failing checks`.

    Asserted on the records rather than on the report, because the report is
    not allowed to know that any of these names are fixtures.
    """

    policy = os.path.join(ROOT, "tests", "lib", "runner_policy_test.py")
    with tempfile.TemporaryDirectory() as directory:
        path = os.path.join(directory, "overlay-runner-policy.jsonl")
        environment = dict(os.environ)
        environment.update({"E2E_JSONL": path, "E2E_SUITE": "runner-policy",
                            "E2E_TARGET": "127.0.0.1", "E2E_ATTEMPT": "1",
                            "NO_COLOR": "1"})
        completed = subprocess.run([sys.executable, policy], env=environment,
                                   capture_output=True, text=True, timeout=300, check=False)
        if completed.returncode != 0:
            raise Failure("the policy suite did not pass: "
                          + completed.stdout.strip().splitlines()[-1])
        with open(path, encoding="utf-8") as handle:
            records = [json.loads(line) for line in handle if line.strip()]
    named = [r.get("name") for r in records if r.get("kind") == "suite"]
    expect("the only suite record is the suite's own", named,
           ["runner_policy_test"])
    sweeps = [r for r in records if r.get("kind") == "health"]
    if sweeps:
        raise Failure(f"{len(sweeps)} health records for a device-free suite: "
                      + ", ".join(sorted({str(r.get('label')) for r in sweeps})))
    failed = [r for r in records if r.get("verdict") == report.FAIL]
    if failed:
        raise Failure(f"{len(failed)} records carry a FAIL verdict")
    return f"{len(records)} records, all the suite's own"


@case(3, "OBS-2.1")
def a_check_that_answers_for_itself_is_reported_once() -> str:
    """One check is one line and one record, whoever produced the verdict.

    A check whose body reports its own verdict, `check_skip` inside a
    `with check(...)` being the common one, closed the line itself and then
    the block closed it again: the console showed the verdict and a bare
    `OK (0.000s)` under it, and the records held two entries for one index,
    the second claiming OK for a check that had skipped. Counted from the
    records, a suite of 39 checks reported 43.
    """
    records = records_from_a_stub_suite(
        {"E2E_SUITE": "fixture"},
        body=("with report.check('answers for itself'):\n"
              "    report.check_skip('not applicable here')\n"
              "with report.check('answers for itself, failing'):\n"
              "    report.check_fail('no')\n"
              "with report.check('left to the block'):\n"
              "    pass\n"))
    checks = [r for r in records if r["kind"] == "check"]
    # The stub writes one check of its own before the body runs.
    labelled = [r for r in checks if r["label"] != "a check"]
    expect("one record each", len(labelled), 3)
    expect("indices", sorted(r["index"] for r in labelled), [2, 3, 4])
    expect("the skip stayed a skip",
           [r["verdict"] for r in labelled], ["SKIP", "FAIL", "OK"])
    return "three checks, three records"


@case(3, "OBS-6.4")
def the_heap_figures_reach_the_health_record() -> str:
    """The figures ride in the health record, not in a record of their own."""

    with DeviceDouble() as double, tempfile.TemporaryDirectory() as workspace:
        made = scripted_run(double, [Stub("held")], workspace=workspace)
        sweeps = [r for r in made.records("127.0.0.1", "run.jsonl")
                  if r["kind"] == "health"]
        if not sweeps:
            raise Failure("no sweep was recorded")
        entries = [c for c in sweeps[0]["checks"] if c["name"] == "heap"]
        expect("one heap entry", len(entries), 1)
        expect("free", entries[0]["figures"]["free"], double.heap_free)
        # `heap` is the only check that carries figures. Any other check that
        # grows them is either a new measurement nobody reads or a figure that
        # belongs in the check's own detail, and both are worth stopping at.
        for other in sweeps[0]["checks"]:
            if other["name"] != "heap" and "figures" in other:
                raise Failure(f"{other['name']} grew a figures entry")
        kinds = {r["kind"] for r in made.records("127.0.0.1", "run.jsonl")}
        if "heap" in kinds:
            raise Failure("the figures were given a record kind of their own")
        expect("and the sweep is still OK", sweeps[0]["ok"], True)
    return "inside the health record, and only on heap"


@case(3, "OBS-2.17", "OBS-3.6")
def a_run_writes_one_interaction_log_per_target() -> str:
    """The runner's own device calls and the suites' land in one file.

    The runner sweeps the device's health and drives its UI-state gate outside
    any suite, and a suite that fails takes a capture. All of those are device
    interactions and all of them belong beside each other, which is why the
    file is one per target rather than one per suite run.
    """

    reads_memory = (
        "from api import UltimateApi\n"
        "report.check_start('read the raster')\n"
        "UltimateApi(ARGS.host, ARGS.password, 5.0).machine.readmem(0xD012, 1)\n"
        "report.check_ok()\n")
    with DeviceDouble() as double, tempfile.TemporaryDirectory() as workspace:
        made = scripted_run(double, [Stub("held", body=reads_memory)],
                            workspace=workspace)
        path = made.path("127.0.0.1", "interactions.jsonl")
        if not os.path.exists(path):
            raise Failure(f"no interaction log at {path}")
        with open(path, encoding="utf-8") as handle:
            found = [json.loads(line) for line in handle if line.strip()]
    if not found:
        raise Failure("the interaction log is empty")
    for record in found:
        expect("every record is an interaction", record["kind"], "interaction")
        for field in ("time", "transport", "op", "suite"):
            if field not in record:
                raise Failure(f"a record carries no {field}: {record}")
    # The runner's own device calls: it sweeps the listeners outside any suite.
    transports = {record["transport"] for record in found}
    if "socket" not in transports:
        raise Failure(f"the runner's own listener probes are missing: "
                      f"{transports}")
    # And the suite's, which reached the log without a line of its own.
    writers = {record["suite"] for record in found}
    if "held" not in writers:
        raise Failure(f"the suite's own device call is missing: {writers}")
    return f"{len(found)} interactions from {sorted(writers)}"


@case(3, "OBS-7.4", "OBS-7.9", "OBS-7.3", "OBS-7.10", "OBS-15.10")
def a_run_checks_the_syslog_setting_at_both_ends() -> str:
    """Read at both ends, corrected at neither, and recorded where it went."""

    # The stub sends a datagram to the collector while the run is happening,
    # which is the only way to find out where the collector actually writes:
    # the `log` record's path is relative to the run's root and reads the same
    # whether or not the slug was composed into it twice. It takes the port
    # from the run rather than being told one this test chose, because two
    # runs choosing at the same moment are handed the same port.
    logger = ("import socket\n"
              "sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)\n"
              "port = int(os.environ['E2E_SYSLOG_PORT'])\n"
              "sock.sendto(b'a line from the device', ('127.0.0.1', port))\n"
              "sock.close()\n"
              "report.check_start('sent a log line')\n"
              "report.check_ok()\n")
    with DeviceDouble() as double, tempfile.TemporaryDirectory() as workspace:
        double.configs["Network Settings"]["Log to Syslog Server"] = ""
        made = scripted_run(double, [Stub("held", body=logger)],
                            workspace=workspace,
                            arguments=("--syslog", "--syslog-port", "0"))
        warnings = [r for r in made.records("127.0.0.1", "run.jsonl")
                    if r["kind"] == "warning"]
        both = [w for w in warnings if "not configured to send its log" in
                w["message"]]
        expect("warned at both ends", len(both), 2)
        logs = [r for r in made.records("127.0.0.1", "run.jsonl")
                if r["kind"] == "log"]
        # One when collection starts, saying where the log is going, and one
        # when it ends, saying which addresses it actually came from.
        expect("a log record at each end", len(logs), 2)
        expect("where it goes", logs[0]["path"], "127.0.0.1/syslog.txt")
        expect("where it was expected from", logs[0]["addresses"], ["127.0.0.1"])
        expect("and where it came from", logs[-1]["senders"], {"127.0.0.1": 1})
        # The path in the record is relative to the run's root, so it has to
        # be the file the collector actually opens. A collector handed one
        # target's own directory composes the slug into it a second time and
        # writes to DIR/<slug>/<slug>/syslog.txt, while the record still reads
        # the same either way.
        wanted = made.path("127.0.0.1", "syslog.txt")
        if not os.path.exists(wanted):
            found = [os.path.join(root, name)
                     for root, _dirs, names in os.walk(made.directory)
                     for name in names if name.startswith("syslog")]
            raise Failure(f"the collector did not write {wanted}: {found}")
        # Read, never written: Syslog::init runs once at boot, so writing this
        # during a run does nothing, and a suite that changed it may have been
        # testing exactly that.
        writes = [r for r in made.records("127.0.0.1", "run.jsonl")
                  if r["kind"] == "action" and r["method"] != "GET"
                  and "Syslog" in str(r.get("path"))]
        if writes:
            raise Failure(f"the run wrote the syslog setting: {writes}")
    return "warned twice, wrote nothing"


@case(3, "OBS-3.4", "OBS-8.22")
def the_screen_spool_is_not_a_suite() -> str:
    """One file every suite appends to is not one suite's records.

    It shares the `.jsonl` suffix with the per-suite files and sits in the
    same directory, so a walk that goes by suffix alone invents a suite run
    named after whichever suite wrote to it last, with no closing record and
    therefore no verdict.
    """

    generator = load_report_tool()
    with DeviceDouble() as double, tempfile.TemporaryDirectory() as workspace:
        made = scripted_run(double, [Stub("held")], workspace=workspace)
        # Written here rather than left to the stub suite: what is under test
        # is how the generator treats this file, and a run whose suites happen
        # to read no screen would leave nothing to treat.
        with open(made.path("127.0.0.1", "screens.jsonl"), "a",
                  encoding="utf-8") as handle:
            handle.write(json.dumps({
                "kind": "menu", "suite": "browse", "time": 1.0, "cols": 40,
                "rows": 1, "text": ["MENU"]}) + "\n")
        generator.write_report(made.directory)
        with open(made.path(generator.INDEX_NAME), encoding="utf-8") as handle:
            document = handle.read()
    if "/screens/" in document or "/browse/" in document:
        raise Failure("the spool is rendered as a suite run of its own")
    expect("the real suite is there", "/overlay/held/1" in document, True)
    return "one suite, not two"


@case(3, "OBS-2.2", "OBS-2.3")
def records_carry_the_target_and_the_attempt() -> str:
    """Every record joins to a target and to an attempt, from the environment."""
    records = records_from_a_stub_suite(
        {"E2E_SUITE": "prg-context-menu", "E2E_TARGET": "u2@c64u",
         "E2E_ATTEMPT": "2"})
    expect("one record", len(records), 1)
    expect("suite", records[0]["suite"], "prg-context-menu")
    expect("target", records[0]["target"], "u2@c64u")
    expect("attempt", records[0]["attempt"], 2)
    return "target and attempt"


@case(3, "OBS-2.3")
def a_suite_run_by_hand_guesses_nothing() -> str:
    """With neither variable set, neither field is recorded.

    A run's target is what a harness aimed it at. A suite started by hand has
    no way to know it, and a guessed value would be indistinguishable from a
    recorded one to every consumer.
    """
    records = records_from_a_stub_suite({})
    for absent in ("target", "attempt"):
        if absent in records[0]:
            raise Failure(f"{absent} was recorded as {records[0][absent]!r}")
    return "neither field"


@case(3, "OBS-2.8", "OBS-2.3")
def a_retried_suite_repeats_its_check_index() -> str:
    """Two records with one index are told apart by the attempt and nothing else."""
    first = records_from_a_stub_suite({"E2E_ATTEMPT": "1"})
    second = records_from_a_stub_suite({"E2E_ATTEMPT": "2"})
    expect("same index", first[0]["index"], second[0]["index"])
    expect("first attempt", first[0]["attempt"], 1)
    expect("second attempt", second[0]["attempt"], 2)
    return "index 1, attempts 1 and 2"


@case(3, "OBS-2.13")
def a_suite_console_reaches_the_log_and_the_terminal() -> str:
    """Every line a suite printed is in its log, in order, with no escapes."""

    body = ("report.check_start('coloured'); report.check_ok('20 rows')\n"
            "print('to stderr', file=sys.stderr)\n"
            "sys.stdout.write('no trailing newline')\n"
            "sys.stdout.flush()\n")
    with DeviceDouble() as double, tempfile.TemporaryDirectory() as workspace:
        made = scripted_run(double, [Stub("held", body=body)],
                            arguments=("--color", "always"), workspace=workspace)
        with open(made.path("127.0.0.1", "overlay-held.log"), "rb") as handle:
            saved = handle.read()
        expect("no escape bytes", b"\x1b" in saved, False)
        # The check's duration is whatever the machine was doing at the time,
        # so it is blanked rather than asserted. Comparing it byte for byte
        # failed a whole run against a `0.002s` where a quiet machine had
        # produced `0.000s`, which says nothing about what this case is for:
        # that every line a suite printed reached its log, in order, with no
        # escape bytes.
        timed = re.sub(rb"\d+\.\d+s", b"Ns", saved)
        expect("in order", timed,
               b"[01] coloured ... OK (20 rows, Ns)\n"
               b"to stderr\nno trailing newline\n")
        for wanted in ("coloured", "to stderr", "no trailing newline"):
            if wanted not in made.stdout:
                raise Failure(f"{wanted!r} did not reach the console")
        if "\x1b" not in made.stdout:
            raise Failure("the console lost its colour")
    return "3 lines, escapes on the console only"


@case(3, "OBS-2.13", "OBS-15.12")
def the_runner_console_is_captured_beside_the_suites() -> str:
    """run.log holds the runner's own output and not a second copy of a suite's."""

    body = "report.check_start('a suite line'); report.check_ok()\n"
    with DeviceDouble() as double, tempfile.TemporaryDirectory() as workspace:
        made = scripted_run(double, [Stub("held", body=body)],
                            workspace=workspace)
        with open(made.path("127.0.0.1", "run.log"), encoding="utf-8") as handle:
            saved = handle.read()
        if "Ultimate hardware test run" not in saved:
            raise Failure("the runner's own banner is missing from run.log")
        if "health:" not in saved:
            raise Failure("the runner's health line is missing from run.log")
        if "a suite line" in saved:
            raise Failure("a suite's line was written into the runner's log too")
        expect("no escape bytes", "\x1b" in saved, False)
    return f"{len(saved.splitlines())} lines"


@case(3, "OBS-2.13", "OBS-2.8")
def a_second_attempt_writes_beside_the_first() -> str:
    """Attempt 1 writes where it always did; attempt 2 gets a directory.

    The console log carries no attempt field, so two attempts sharing one file
    leave a reader unable to say which failure they are looking at. The
    per-suite JSONL is the opposite case and stays one file: every record in
    it already says which attempt wrote it, and the report joins on that.
    """

    with DeviceDouble() as double, tempfile.TemporaryDirectory() as workspace:
        flag = os.path.join(workspace, "unhealthy")
        double.withhold_ftp_banner_while(flag)
        made = scripted_run(
            double, [Stub("flaky", body=RETRY_BODY)],
            arguments=("--recover-command", f"rm -f {flag}"),
            workspace=workspace, extra_environment={"OBS_FLAG": flag})
        tree = made.tree()
        first = made.path("127.0.0.1", "overlay-flaky.log")
        second = made.path("127.0.0.1", "attempt-2", "overlay-flaky.log")
        for wanted in (first, second):
            if not os.path.exists(wanted):
                raise Failure(f"{os.path.basename(wanted)} is missing "
                              f"from {tree}")
        with open(first, encoding="utf-8") as handle:
            one = handle.read()
        with open(second, encoding="utf-8") as handle:
            two = handle.read()
        expect("the first attempt alone", one.count("the device is well"), 1)
        expect("the second attempt alone", two.count("the device is well"), 1)
        if "FAIL" not in one:
            raise Failure(f"the first attempt did not fail: {one!r}")
        if "OK" not in two:
            raise Failure(f"the second attempt did not pass: {two!r}")
        # And the records stay in one file, keyed by attempt.
        checks = [r for r in made.records("127.0.0.1", "overlay-flaky.jsonl")
                  if r["kind"] == "check"]
        expect("one record per attempt", [c["attempt"] for c in checks], [1, 2])
        expect("one index", {c["index"] for c in checks}, {1})
        if any("attempt-1" in name for name in tree):
            raise Failure("a first attempt made a directory of its own")
    return "2 attempts, 2 logs, 1 jsonl"


@case(3, "OBS-2.14")
def the_run_records_what_it_intended_to_run() -> str:
    """The plan names every registered suite and why each absent one is absent."""

    stubs = [Stub("held"), Stub("not-in-this-run"),
             Stub("operator-only", manual=True),
             Stub("a-benchmark", category="perf", args="-H @HOST@ -p @PASS@")]
    with DeviceDouble() as double, tempfile.TemporaryDirectory() as workspace:
        made = scripted_run(double, stubs, arguments=("--mode", "overlay,telnet",
                                                      "-s", "held"),
                            workspace=workspace)
        plans = [r for r in made.records("127.0.0.1", "run.jsonl")
                 if r["kind"] == "plan"]
        expect("one plan", len(plans), 1)
        plan = plans[0]
        expect("every registered suite", len(plan["suites"]), len(stubs))
        by_name = {entry["name"]: entry for entry in plan["suites"]}
        expect("held runs", by_name["held"]["run"], True)
        expect("the others do not", by_name["not-in-this-run"]["reason"],
               "not-selected")
        expect("path", by_name["held"]["path"].endswith("held.py"), True)
        # Two modes over the one selected suite.
        expect("sequence", [entry["label"] for entry in plan["sequence"]],
               ["overlay", "telnet"])
    return f"{len(plan['suites'])} suites, {len(plan['sequence'])} runs planned"


@case(3, "OBS-2.14")
def the_plan_says_why_a_manual_suite_did_not_run() -> str:
    """A run that named no suite excludes the manual ones, and says so."""

    stubs = [Stub("held"), Stub("operator-only", manual=True),
             Stub("a-benchmark", category="perf", args="-H @HOST@ -p @PASS@")]
    with DeviceDouble() as double, tempfile.TemporaryDirectory() as workspace:
        made = scripted_run(double, stubs, workspace=workspace)
        plan = next(r for r in made.records("127.0.0.1", "run.jsonl")
                if r["kind"] == "plan")
        by_name = {entry["name"]: entry for entry in plan["suites"]}
        expect("manual", by_name["operator-only"]["reason"], "manual")
        expect("category", by_name["a-benchmark"]["reason"], "category")
        expect("held runs", by_name["held"]["run"], True)
    return "manual and category"


@case(3, "OBS-2.15")
def the_run_records_what_it_assumed() -> str:
    """A run says which firmware fixes it treated as present."""

    with DeviceDouble() as double, tempfile.TemporaryDirectory() as workspace:
        plain = scripted_run(double, [Stub("held")], workspace=workspace)
        run = next(r for r in plain.records("127.0.0.1", "run.jsonl")
               if r["kind"] == "run")
        expect("none in force", run["assumptions"], [])
    with DeviceDouble() as double, tempfile.TemporaryDirectory() as workspace:
        assumed = scripted_run(
            double, [Stub("held")],
            arguments=("--assume-fix", "monitor-d-key-reserved"),
            workspace=workspace)
        run = next(r for r in assumed.records("127.0.0.1", "run.jsonl")
               if r["kind"] == "run")
        expect("in force", run["assumptions"], ["monitor-d-key-reserved"])
    return "one fix assumed"


@case(3, "OBS-2.16")
def the_action_log_says_what_the_harness_did() -> str:
    """Every mutation is recorded, and a plain successful GET is not."""

    body = ("from api import UltimateApi\n"
            "device = UltimateApi(ARGS.host, ARGS.password)\n"
            "report.check_start('press a key')\n"
            "device.machine.press('a')\n"
            "device.version()\n"
            "report.check_ok()\n")
    with DeviceDouble() as double, tempfile.TemporaryDirectory() as workspace:
        made = scripted_run(double, [Stub("held", body=body)],
                            workspace=workspace)
        actions = [r for r in made.records("127.0.0.1", "overlay-held.jsonl")
                   if r["kind"] == "action"]
        pressed = [r for r in actions if r["path"] == "/v1/machine:input"]
        expect("the keystroke", len(pressed), 1)
        expect("method", pressed[0]["method"], "POST")
        expect("inside check 1", pressed[0]["check"], 1)
        if "ms" not in pressed[0]:
            raise Failure(f"no duration on {pressed[0]}")
        quiet = [r for r in actions
                 if r["path"] == "/v1/version" and r.get("status") == 200]
        if quiet:
            raise Failure(f"a GET that answered first time was recorded: {quiet}")
    return f"{len(actions)} actions"


@case(3, "OBS-2.16", "OBS-1.8")
def the_action_log_keeps_what_the_device_said_when_it_refused() -> str:
    """A request that did not answer 200 carries the device's own words."""

    body = ("from api import UltimateApi\n"
            "device = UltimateApi(ARGS.host, ARGS.password)\n"
            "report.check_start('read the menu')\n"
            "device.machine.menu_screen()\n"
            "report.check_ok()\n")
    with DeviceDouble() as double, tempfile.TemporaryDirectory() as workspace:
        made = scripted_run(double, [Stub("held", body=body)],
                            workspace=workspace)
        refused = [r for r in made.records("127.0.0.1", "overlay-held.jsonl")
                   if r["kind"] == "action" and r.get("status") == 404
                   and r["path"] == "/v1/machine:menu_screen"]
        if not refused:
            raise Failure("a 404 answer was not recorded")
        if "Menu screen unavailable" not in refused[0].get("error", ""):
            raise Failure(f"the device's answer was lost: {refused[0]}")
    return refused[0]["error"]


@case(3, "OBS-2.16")
def every_transport_records_what_it_did() -> str:
    """A request that built its own URL is on the timeline too.

    tests/e2e/lib/ui_backend.py reaches the device through retrying_urlopen
    rather than through RestClient, and it is what sends every keystroke in
    every UI suite. A hook on one of the three entry points would leave the
    largest source of harness actions off the record entirely.
    """

    body = ("import urllib.request\n"
            "import rest as rest_lib\n"
            "port = os.environ['U64_REST_PORT']\n"
            "url = 'http://%s:%s/v1/machine:menu_screen' % (ARGS.host, port)\n"
            "report.check_start('read the menu the long way')\n"
            "try:\n"
            "    rest_lib.retrying_urlopen(\n"
            "        urllib.request.Request(url, method='GET'), 5.0).close()\n"
            "except Exception:\n"
            "    pass\n"
            "report.check_ok()\n")
    with DeviceDouble() as double, tempfile.TemporaryDirectory() as workspace:
        made = scripted_run(double, [Stub("held", body=body)],
                            workspace=workspace)
        actions = [r for r in made.records("127.0.0.1", "overlay-held.jsonl")
                   if r["kind"] == "action"
                   and r["path"] == "/v1/machine:menu_screen"]
        if not actions:
            raise Failure("a request made through retrying_urlopen was not recorded")
        expect("status", actions[0]["status"], 404)
        expect("inside check 1", actions[0]["check"], 1)
    return "retrying_urlopen is on the timeline"


@case(3, "OBS-2.16")
def the_action_log_carries_what_a_request_sent() -> str:
    """A keystroke is a JSON payload, and the record names the keys."""

    body = ("from api import UltimateApi\n"
            "device = UltimateApi(ARGS.host, ARGS.password)\n"
            "report.check_start('press a key')\n"
            "device.machine.press('run_stop')\n"
            "report.check_ok()\n")
    with DeviceDouble() as double, tempfile.TemporaryDirectory() as workspace:
        made = scripted_run(double, [Stub("held", body=body)],
                            workspace=workspace)
        pressed = [r for r in made.records("127.0.0.1", "overlay-held.jsonl")
                   if r["kind"] == "action" and r["path"] == "/v1/machine:input"]
        expect("one keystroke", len(pressed), 1)
        if "run_stop" not in pressed[0].get("params", ""):
            raise Failure(f"the key that was pressed is not in {pressed[0]}")
    return "the key names survive"


@case(3, "OBS-1.8")
def no_artefact_carries_the_password() -> str:
    """A password in a query, in argv or in a log is masked before it is written."""

    body = ("from api import UltimateApi\n"
            "device = UltimateApi(ARGS.host, ARGS.password)\n"
            "report.check_start('send it somewhere it should not go')\n"
            "device.rest.request('PUT', '/v1/machine:nowhere',\n"
            "                    params={'token': ARGS.password})\n"
            "report.check_ok(ARGS.password)\n")
    with DeviceDouble() as double, tempfile.TemporaryDirectory() as workspace:
        made = scripted_run(double, [Stub("held", body=body)],
                            arguments=("--password", "hunter2"),
                            workspace=workspace)
        for name in ("run.jsonl", "overlay-held.jsonl", "run.log",
                     "overlay-held.log"):
            with open(made.path("127.0.0.1", name), encoding="utf-8") as handle:
                text = handle.read()
            if "hunter2" in text:
                raise Failure(f"the password reached {name}")
        actions = [r for r in made.records("127.0.0.1", "overlay-held.jsonl")
                   if r["kind"] == "action" and "token" in str(r.get("params"))]
        if not actions or "***" not in str(actions[0]["params"]):
            raise Failure(f"the query was not masked: {actions}")
    return "4 artefacts clean"


@case(3, "OBS-2.16")
def the_runner_own_actions_name_no_check() -> str:
    """A request the runner made outside any check carries no check index.

    run-tests reports its own gates with unnumbered steps, so every one of them
    would otherwise claim check 0 and several would share it.
    """

    with DeviceDouble() as double, tempfile.TemporaryDirectory() as workspace:
        made = scripted_run(double, [Stub("held")], workspace=workspace)
        actions = [r for r in made.records("127.0.0.1", "run.jsonl")
                   if r["kind"] == "action"]
        if not actions:
            raise Failure("the runner made no recorded request")
        claimed = [r for r in actions if "check" in r]
        if claimed:
            raise Failure(f"a runner request claimed a check index: {claimed[0]}")
    return f"{len(actions)} runner actions"


@case(3, "OBS-2.14", "OBS-2.15")
def several_targets_record_the_plan_and_the_assumptions() -> str:
    """The parent plans for the run, and every process records what it assumed.

    A child is told the assumptions through the environment rather than on its
    command line, so a record derived from this process's own flags would say
    nothing for every process but the first.
    """

    with DeviceDouble() as double, tempfile.TemporaryDirectory() as workspace:
        made = scripted_run(double, [Stub("held")],
                            tokens=("127.0.0.1", "127.0.0.1@localhost"),
                            arguments=("--assume-fix", "monitor-d-key-reserved"),
                            workspace=workspace)
        parent = [r for r in made.records("run.jsonl") if r["kind"] == "plan"]
        expect("the parent planned", len(parent), 1)
        expect("for both targets", parent[0]["targets"],
               ["127.0.0.1", "127.0.0.1@localhost"])
        for name in ("run.jsonl", "127.0.0.1/run.jsonl",
                     "127.0.0.1-at-localhost/run.jsonl"):
            records = made.records(*name.split("/"))
            runs = [r for r in records if r["kind"] == "run"]
            expect(f"{name} assumptions", runs[0]["assumptions"],
                   ["monitor-d-key-reserved"])
    return "3 processes, one assumption"


@case(3, "OBS-2.1", "OBS-2.10")
def one_target_writes_its_own_slug_directory() -> str:
    """A single-target run produces DIR/<slug>/, not DIR/ and not DIR/<slug>/<slug>/."""

    with DeviceDouble() as double, tempfile.TemporaryDirectory() as workspace:
        made = scripted_run(double, [Stub("held")], workspace=workspace)
        tree = made.tree()
        expect("exit", made.status, 0)
        for wanted in ("127.0.0.1/run.jsonl", "127.0.0.1/overlay-held.jsonl"):
            if wanted not in tree:
                raise Failure(f"{wanted} is missing from {tree}")
        doubled = [name for name in tree if "127.0.0.1/127.0.0.1" in name]
        if doubled:
            raise Failure(f"the slug was appended twice: {doubled}")
    return f"{len(tree)} files"


@case(3, "OBS-2.1", "OBS-2.12", "OBS-15.12")
def several_targets_share_one_tree_shape() -> str:
    """Each target gets one slug directory and the parent records the run."""

    with DeviceDouble() as double, tempfile.TemporaryDirectory() as workspace:
        made = scripted_run(double, [Stub("held")],
                            tokens=("127.0.0.1", "127.0.0.1@localhost"),
                            workspace=workspace)
        tree = made.tree()
        expect("exit", made.status, 0)
        for wanted in ("run.jsonl", "127.0.0.1/run.jsonl",
                       "127.0.0.1-at-localhost/run.jsonl"):
            if wanted not in tree:
                raise Failure(f"{wanted} is missing from {tree}")
        doubled = [name for name in tree if name.count("127.0.0.1/") > 1]
        if doubled:
            raise Failure(f"the slug was appended twice: {doubled}")
        parent = [r for r in made.records("run.jsonl") if r["kind"] == "run"]
        expect("one parent record", len(parent), 1)
        expect("targets", parent[0]["targets"],
               ["127.0.0.1", "127.0.0.1@localhost"])
        expect("exit_code", parent[0]["exit_code"], 0)
        if "suites" in parent[0]:
            raise Failure("the parent recorded counts its children own")
    return "2 targets, 1 parent record"


@case(3, "OBS-2.11", "OBS-1.8")
def the_run_record_says_what_it_is_a_run_of() -> str:
    """A downloaded artifact names its commit, branch, host and command line."""

    with DeviceDouble() as double, tempfile.TemporaryDirectory() as workspace:
        made = scripted_run(double, [Stub("held")],
                            arguments=("--password", "hunter2"),
                            workspace=workspace)
        run = [r for r in made.records("127.0.0.1", "run.jsonl")
               if r["kind"] == "run"]
        expect("one record", len(run), 1)
        for field in ("host", "python", "argv", "started", "commit", "branch",
                      "worktree_dirty"):
            if field not in run[0]:
                raise Failure(f"{field} is missing from {sorted(run[0])}")
        if "hunter2" in " ".join(run[0]["argv"]):
            raise Failure(f"the password reached the record: {run[0]['argv']}")
        if "***" not in " ".join(run[0]["argv"]):
            raise Failure(f"the password was not masked: {run[0]['argv']}")
    return "7 identity fields, password masked"


@case(3, "OBS-2.2", "OBS-2.3")
def every_record_in_the_tree_names_its_target() -> str:
    """The runner's own records and the suites' records agree about the target."""

    with DeviceDouble() as double, tempfile.TemporaryDirectory() as workspace:
        made = scripted_run(double, [Stub("held")],
                            tokens=("127.0.0.1@localhost",), workspace=workspace)
        slug = "127.0.0.1-at-localhost"
        seen = 0
        for name in ("run.jsonl", "overlay-held.jsonl"):
            for record in made.records(slug, name):
                expect(f"{name} {record['kind']}", record.get("target"),
                       "127.0.0.1@localhost")
                seen += 1
        if not seen:
            raise Failure("no records were written")
    return f"{seen} records"


@case(3, "OBS-5.1", "OBS-5.3", "OBS-5.4", "OBS-5.5")
def a_failing_suite_leaves_a_capture() -> str:
    """Three reads, three artefacts, named from the suite run's own key."""

    body = "report.check_start('it holds'); report.check_fail(); sys.exit(1)\n"
    with DeviceDouble() as double, tempfile.TemporaryDirectory() as workspace:
        double.mounted_image = "/Usb0/game.d64"
        made = scripted_run(double, [Stub("held"), Stub("broken", body=body)],
                            workspace=workspace)
        capture = ("127.0.0.1", "capture")
        names = sorted(os.listdir(made.path(*capture)))
        expect("only the failure", names, [
            "overlay-broken-1-screen.bin", "overlay-broken-1-screen.txt",
            "overlay-broken-1-state.json"])
        with open(made.path(*capture, "overlay-broken-1-screen.txt"),
                  encoding="utf-8") as handle:
            screen = handle.read().splitlines()
        expect("25 rows", len(screen), 25)
        expect("40 columns", len(screen[0]), 40)
        with open(made.path(*capture, "overlay-broken-1-state.json"),
                  encoding="utf-8") as handle:
            state = json.load(handle)
        # No menu is open between suites, so the capture reads the C64's own
        # screen and says which of the two it took.
        expect("source", state["source"], "readmem")
        expect("mode", state["mode"], "overlay")
        expect("heap", state["heap"]["free"], double.heap_free)
        expect("drives", state["drives"]["a"]["image_file"], "/Usb0/game.d64")
        expect("no errors", state["errors"], [])
    return "3 artefacts"


@case(3, "OBS-5.3", "OBS-5.5")
def a_capture_reads_the_menu_when_one_is_open() -> str:
    """An open menu is the other of the two screens, and it says which."""

    body = ("import pathlib\n"
            "pathlib.Path(os.environ['OBS_MENU']).touch()\n"
            "report.check_start('it holds'); report.check_fail()\n"
            "sys.exit(1)\n")
    with DeviceDouble() as double, tempfile.TemporaryDirectory() as workspace:
        menu = os.path.join(workspace, "menu-open")
        double.menu_open_flag = menu
        made = scripted_run(double, [Stub("broken", body=body)],
                            workspace=workspace,
                            extra_environment={"OBS_MENU": menu})
        with open(made.path("127.0.0.1", "capture",
                            "overlay-broken-1-state.json"),
                  encoding="utf-8") as handle:
            state = json.load(handle)
        expect("source", state["source"], "menu_screen")
        with open(made.path("127.0.0.1", "capture",
                            "overlay-broken-1-screen.txt"),
                  encoding="utf-8") as handle:
            screen = handle.read()
        if "Ultimate 64 menu" not in screen:
            raise Failure(f"the menu did not decode: {screen[:80]!r}")
        with open(made.path("127.0.0.1", "capture",
                            "overlay-broken-1-screen.bin"), "rb") as handle:
            expect("the device's own bytes", len(handle.read()), 2000)
    return "menu_screen, 2000 bytes"


@case(3, "OBS-5.7", "OBS-1.1")
def a_capture_that_cannot_read_the_device_changes_nothing() -> str:
    """A device that has gone leaves a recorded failure and the same verdict."""

    body = ("import pathlib\n"
            "pathlib.Path(os.environ['OBS_GONE']).touch()\n"
            "report.check_start('it holds'); report.check_fail()\n"
            "sys.exit(1)\n")
    with DeviceDouble() as double, tempfile.TemporaryDirectory() as workspace:
        gone = os.path.join(workspace, "gone")
        double.offline_flag = gone
        made = scripted_run(double, [Stub("broken", body=body)],
                            workspace=workspace,
                            arguments=("--no-health-check", "--no-retry"),
                            extra_environment={"OBS_GONE": gone})
        # The device double has gone, so the health check after the last
        # attempt reports an abandoned run rather than a failed suite, which
        # is the distinction that check exists to make. What this case is
        # about is that the capture changed neither the suite's verdict nor
        # the status the run reaches without it.
        expect("the run still failed for the suite's reason", made.status, 4)
        with open(made.path("127.0.0.1", "capture",
                            "overlay-broken-1-state.json"),
                  encoding="utf-8") as handle:
            state = json.load(handle)
        if not state["errors"]:
            raise Failure("a capture against a gone device recorded no failure")
        suites = [r for r in made.records("127.0.0.1", "run.jsonl")
                  if r["kind"] == "suite"]
        expect("the suite's verdict", suites[-1]["verdict"], "FAIL")
    return f"{len(state['errors'])} recorded failures"


@case(3, "OBS-8.22", "OBS-8.21")
def the_suites_spool_every_screen_they_read() -> str:
    """A screen the harness read is in the spool, as text and as raw bytes."""

    body = ("import ui_backend\n"
            "backend = ui_backend.make_backend('overlay', ARGS.host,\n"
            "                                  ARGS.password, 5.0)\n"
            "report.check_start('drive the menu')\n"
            "backend.send_key('DOWN')\n"
            "report.check_ok()\n")
    with DeviceDouble() as double, tempfile.TemporaryDirectory() as workspace:
        menu = os.path.join(workspace, "menu-open")
        open(menu, "w").close()
        double.menu_open_flag = menu
        made = scripted_run(double, [Stub("held", body=body)],
                            workspace=workspace)
        spooled = made.records("127.0.0.1", "screens.jsonl")
        if not spooled:
            raise Failure("nothing was spooled")
        first = spooled[0]
        expect("kind", first["kind"], "menu")
        expect("cols", first["cols"], 40)
        expect("rows", first["rows"], 25)
        expect("suite", first["suite"], "held")
        expect("target", first["target"], "127.0.0.1")
        if "Ultimate 64 menu" not in first["text"][0]:
            raise Failure(f"the screen did not decode: {first['text'][0]!r}")
        expect("the device's own bytes", len(bytes.fromhex(first["raw"])), 2000)
        # A screen read inside a check joins to it; the ones the backend read
        # while it was starting up belong to no check and say so.
        inside = [r for r in spooled if r.get("check") == 1]
        if not inside:
            raise Failure("no screen was joined to the check that read it")
        # The settle loops read one screen many times per keystroke, and only a
        # change is written, which is both the volume control and the one
        # record per redraw the recorder's harness pane wants.
        if len(spooled) > 4:
            raise Failure(f"{len(spooled)} records for one unchanging screen")
    return f"{len(spooled)} screens"


@case(3, "OBS-8.22")
def a_selection_moving_is_a_screen_the_spool_keeps() -> str:
    """Bit 7 marks the selected row and does not survive into the text.

    A cursor moving one row is the navigation step a reader is looking for,
    and deduplicating on the rendered text would be the one thing that threw
    it away.
    """

    body = ("import ui_backend\n"
            "backend = ui_backend.make_backend('overlay', ARGS.host,\n"
            "                                  ARGS.password, 5.0)\n"
            "report.check_start('the selection moves')\n"
            "backend.send_key('DOWN')\n"
            "report.check_ok()\n")
    with DeviceDouble() as double, tempfile.TemporaryDirectory() as workspace:
        menu = os.path.join(workspace, "menu-open")
        open(menu, "w").close()
        double.menu_open_flag = menu
        # The same text with a different row marked, which is what moving a
        # cursor produces and what a text comparison cannot see.
        double.move_selection_on_input = True
        made = scripted_run(double, [Stub("held", body=body)],
                            workspace=workspace)
        spooled = made.records("127.0.0.1", "screens.jsonl")
        if len(spooled) < 2:
            raise Failure(f"the selection move was not spooled: {len(spooled)} "
                          "record(s)")
        rendered = [tuple(record["text"]) for record in spooled]
        if len(set(rendered)) == len(rendered):
            raise Failure("the double did not produce two screens with one text")
        payloads = {record["raw"] for record in spooled}
        expect("each payload once", len(payloads), len(spooled))
    return f"{len(spooled)} screens, {len(set(rendered))} distinct texts"


@case(3, "OBS-1.8", "OBS-8.22")
def the_capture_and_the_spool_carry_no_password() -> str:
    """A password on a screen is masked on the way into every artefact."""

    body = ("report.check_start('it holds')\n"
            "report.check_fail('the screen showed " + "hunter2" + "')\n"
            "sys.exit(1)\n")
    with DeviceDouble() as double, tempfile.TemporaryDirectory() as workspace:
        menu = os.path.join(workspace, "menu-open")
        open(menu, "w").close()
        double.menu_open_flag = menu
        double.menu_rows[2] = "password hunter2".ljust(40)
        made = scripted_run(double, [Stub("broken", body=body)],
                            arguments=("--password", "hunter2"),
                            workspace=workspace)
        with open(made.path("127.0.0.1", "capture",
                            "overlay-broken-1-screen.txt"),
                  encoding="utf-8") as handle:
            screen = handle.read()
        if "hunter2" in screen:
            raise Failure("the password reached the captured screen")
        if "***" not in screen:
            raise Failure(f"nothing was masked: {screen[:120]!r}")
        with open(made.path("127.0.0.1", "screens.jsonl"),
                  encoding="utf-8") as handle:
            if "hunter2" in handle.read():
                raise Failure("the password reached the screen spool")
    return "screen and spool clean"


@case(3, "OBS-8.22")
def the_spool_is_off_when_a_run_says_so() -> str:
    """--no-screens turns it off, and the run is otherwise unchanged."""

    with DeviceDouble() as double, tempfile.TemporaryDirectory() as workspace:
        made = scripted_run(double, [Stub("held")],
                            arguments=("--no-screens",), workspace=workspace)
        if "127.0.0.1/screens.jsonl" in made.tree():
            raise Failure("a spool was written with --no-screens")
        expect("the run is unchanged", made.status, 0)
    return "no spool"


@case(3, "OBS-3.1")
def the_documented_command_line_writes_the_document() -> str:
    """The generator is run the way its documentation says to run it.

    Every other case here imports the module, and an import defines every
    function in the file whatever order they are written in. Running it as a
    program does not: a name defined below the `__main__` guard does not exist
    by the time the guard calls into it, and only a subprocess sees that.
    """

    require_fixture()
    with tempfile.TemporaryDirectory() as workspace:
        tree = os.path.join(workspace, "run")
        shutil.copytree(fixture_tree(), tree)
        finished = subprocess.run(
            [sys.executable, REPORT_TOOL, tree],
            capture_output=True, text=True, timeout=120, check=False)
    if finished.returncode != 0:
        raise Failure(f"exit {finished.returncode}: "
                      f"{finished.stderr.strip().splitlines()[-1:]}")
    expect("the path it printed", finished.stdout.strip(),
           os.path.join(tree, "index.md"))
    return "written by the command in the documentation"


@case(3, "OBS-16.3", "OBS-3.1")
def a_report_is_generated_from_a_run_the_runner_just_wrote() -> str:
    """The generator reads a tree the current runner produced, not a fixture.

    This is the tier the others cannot replace. A generator can be perfect
    against a checked-in fixture that the runner no longer writes, and nothing
    else here would notice until somebody opened an artifact from a real run.
    """

    generator = load_report_tool()
    body = ("report.check_start('it holds')\n"
            "report.check_fail('0 rows, expected 20')\n"
            "sys.exit(1)\n")
    with DeviceDouble() as double, tempfile.TemporaryDirectory() as workspace:
        made = scripted_run(double, [Stub("held"), Stub("broken", body=body)],
                            workspace=workspace)
        generator.write_report(made.directory)
        with open(made.path(generator.INDEX_NAME), encoding="utf-8") as handle:
            document = handle.read()
    lines = [line for line in document.splitlines() if line.strip()]
    expect("verdict", lines[0], "# E2E gate run: FAIL")
    status = lines[1]
    for wanted in ("RESULT: FAIL", "targets=1", "suites=2", "ok=1", "fail=1",
                   "exit=3"):
        if wanted not in status:
            raise Failure(f"{wanted!r} is not in {status!r}")
    if "127.0.0.1/overlay/broken/1/1" not in document:
        raise Failure("the failing check is not named by its identity key")
    return status


@case(3, "OBS-8.1", "OBS-8.23")
def a_recorder_flag_without_record_is_refused() -> str:
    """Refused before the run starts, not silently ignored.

    A run invoked with a quality setting and no --record would otherwise
    produce no recording and no complaint, which is the shape of mistake that
    costs a whole gate run to discover.
    """

    with tempfile.TemporaryDirectory() as directory:
        for arguments, wanted in (
                (["--record-quality", "20"], "needs --record"),
                (["--record", "--record-scale", "0"], "integer factor"),
                (["--record", "--no-record-video", "--no-record-audio",
                  "--no-record-menu"], "records nothing"),
                (["--record"], "needs -o DIR")):
            completed = subprocess.run(
                [sys.executable, RUNNER_PATH, *arguments, "127.0.0.1"],
                capture_output=True, text=True, cwd=directory, timeout=60, check=False)
            # 64 is EX_USAGE. A usage error is not an outcome of a run, so it
            # is off the severity scale the other statuses form; on that scale
            # 2 now means "every suite passed, but a device needed
            # recovering", which is what a malformed command line used to
            # report.
            expect(f"{arguments} exits 64", completed.returncode, 64)
            if wanted not in completed.stderr:
                raise Failure(f"{arguments}: {completed.stderr.strip()[:120]}")
    # And the status argparse chooses for itself, which is the path that
    # breaks: `ArgumentParser.error` calls `sys.exit(2)` before any code in
    # the runner runs, so an option the parser rejects never reaches the
    # runner's own raise.
    with tempfile.TemporaryDirectory() as directory:
        completed = subprocess.run(
            [sys.executable, RUNNER_PATH, "--no-such-option"],
            capture_output=True, text=True, cwd=directory, timeout=60, check=False)
        expect("an option argparse rejects", completed.returncode, 64)
        completed = subprocess.run(
            [sys.executable, RUNNER_PATH, "--help"],
            capture_output=True, text=True, cwd=directory, timeout=60, check=False)
        expect("--help", completed.returncode, 0)
    return "6 usage errors"


@case(3, "OBS-8.1", "OBS-1.3")
def a_run_without_record_records_nothing() -> str:
    """With the flag absent nothing is recorded and nothing else changes."""

    with DeviceDouble() as double, tempfile.TemporaryDirectory() as workspace:
        made = scripted_run(double, [Stub("held")], workspace=workspace)
        video = [name for name in made.tree() if name.endswith((".mp4", ".srt"))]
        if video:
            raise Failure(f"a run with no --record wrote {video}")
        captures = [r for r in made.records("127.0.0.1", "run.jsonl")
                    if r["kind"] == "capture"]
        expect("no capture record", captures, [])
        expect("no stream was asked for", double.streams_started, [])
    return "nothing recorded"


@case(3, "OBS-3.28")
def a_run_is_compared_with_the_one_before_it() -> str:
    """Two trees, one section, joined on the identity key."""

    generator = load_report_tool()
    passing = "report.check_start('it holds'); report.check_ok()\n"
    failing = ("report.check_start('it holds')\n"
               "report.check_fail('0 rows, expected 20')\n"
               "sys.exit(1)\n")
    with DeviceDouble() as double, tempfile.TemporaryDirectory() as first, \
            tempfile.TemporaryDirectory() as second:
        before = scripted_run(double, [Stub("held", body=passing)],
                              workspace=first)
        after = scripted_run(double, [Stub("held", body=failing)],
                             workspace=second)
        generator.write_report(after.directory, compare=before.directory)
        with open(after.path(generator.INDEX_NAME), encoding="utf-8") as handle:
            document = handle.read()
    if "## Changes since" not in document:
        raise Failure("no comparison section was written")
    if "### Newly failing" not in document:
        raise Failure("the check that started failing is not named")
    if "127.0.0.1/overlay/held/1" not in document:
        raise Failure("the identity key is not in the comparison")
    return "newly failing"


@case(3, "OBS-2.4")
def the_run_record_names_the_build_that_produced_it() -> str:
    """The CI identifiers come from the environment and from nowhere else."""

    with DeviceDouble() as double, tempfile.TemporaryDirectory() as workspace:
        made = scripted_run(double, [Stub("held")], workspace=workspace,
                            extra_environment={"GITHUB_RUN_ID": "1234567",
                                               "GITHUB_RUN_ATTEMPT": "2"})
        run = next(r for r in made.records("127.0.0.1", "run.jsonl")
               if r["kind"] == "run")
        expect("run id", run["ci_run_id"], "1234567")
        expect("attempt", run["ci_run_attempt"], "2")
    with DeviceDouble() as double, tempfile.TemporaryDirectory() as workspace:
        made = scripted_run(double, [Stub("held")], workspace=workspace)
        run = next(r for r in made.records("127.0.0.1", "run.jsonl")
               if r["kind"] == "run")
        if "ci_run_id" in run:
            raise Failure(f"an identifier was invented: {run['ci_run_id']!r}")
    return "1234567 attempt 2, absent outside CI"
