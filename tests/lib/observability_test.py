#!/usr/bin/env python3
# Gate check: the code that watches a gate run is itself correct.

"""Verify the observability harness, without a device.

Everything the observability design adds is code the gate's own verdicts
cannot exercise: a component there may not change a suite verdict, a health
verdict or the process exit status, so nothing notices when one of them
breaks. A defect here is not found by a failing gate. It is found weeks later
by somebody who opened an artifact to diagnose a firmware defect and found an
empty report, a black recording or a log with half its lines, by which time
the run is gone.

Four tiers, and every piece of logic belongs to exactly one:

    1 pure       functions over bytes and records: rendering, interval and
                 timecode arithmetic, ANSI stripping, datagram attribution
    2 component  one component against the device double over real sockets
    3 pipeline   a whole scripted run with no real device, producing a -j tree
                 with the current runner, then the report generated from it
    4 golden     the report generated from the checked-in fixture, compared
                 byte for byte with the checked-in expected document

Tier 3 is the one the others cannot replace: the report generator can be
perfect against a fixture the runner no longer writes. Tier 4 is the one that
makes a rendering change visible in review, because a diff of the expected
document is exactly the diff a reader of a real report would see.

Runs three ways, one implementation: `make observability_test`, the
`observability` entry in the `SUITES` registry in `run-tests`, and a step in
.github/workflows/build.yml. It reports through tests/lib/report.py like every
other registered suite rather than through unittest, because a registered
suite that printed unittest output would break the one rule
tests/lib/README.md says is not negotiable.
"""

from __future__ import annotations

import argparse
import os
import sys
from contextlib import contextmanager
from dataclasses import dataclass
from typing import Callable, List, Optional, Sequence, Tuple

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import health  # noqa: E402
import report  # noqa: E402
import targets  # noqa: E402
from api import UltimateApi  # noqa: E402
from device_double import DeviceDouble  # noqa: E402
from report import Failure  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
RUNNER_PATH = os.path.join(ROOT, "run-tests")

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
    requirements: Tuple[str, ...]
    run: Callable[[], object]


CASES: List[Case] = []


def case(tier: int, *requirements: str, label: str = ""):
    """Register one case, naming the requirements it holds.

    The requirement numbers are on the case rather than only in a docstring so
    the registry check of OBS-16.7 can read them, and so a reviewer can read a
    list of requirement numbers with a test each instead of reading the
    specification against the code.
    """

    def register(function: Callable[[], object]) -> Callable[[], object]:
        CASES.append(Case(tier, label or function.__name__.replace("_", " "),
                          tuple(requirements), function))
        return function

    return register


def expect(what: str, actual, wanted) -> None:
    if actual != wanted:
        raise Failure(f"{what}: got {actual!r}, expected {wanted!r}")


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


# ---------------------------------------------------------------------------
# Tier 1: pure functions
# ---------------------------------------------------------------------------


@case(1, "OBS-15.13")
def target_carries_every_port() -> str:
    """A parsed target answers where each of the device's surfaces is."""
    with without_port_overrides():
        target = targets.parse("u64")
    expect("rest", target.rest_port, 80)
    expect("ftp", target.ftp_port, 21)
    expect("telnet", target.telnet_port, 23)
    expect("dma", target.dma_port, 64)
    return "4 ports"


@case(1, "OBS-15.13", "OBS-15.14")
def rest_port_environment_override() -> str:
    """U64_REST_PORT moves the REST port the way U64_TELNET_PORT moves Telnet."""
    with without_port_overrides():
        os.environ[targets.REST_PORT_ENV] = "8080"
        expect("overridden", targets.parse("u64").rest_port, 8080)
        os.environ[targets.REST_PORT_ENV] = "not a port"
        expect("malformed value ignored", targets.parse("u64").rest_port, 80)
        os.environ.pop(targets.REST_PORT_ENV)
    return "8080"


@case(1, "OBS-15.14")
def rest_url_names_the_port_only_when_it_moved() -> str:
    """A URL against a real device reads exactly as it does with no port field."""
    import rest as rest_lib

    with without_port_overrides():
        plain = rest_lib.RestClient("u64")
    expect("default", plain.url("/v1/version"), "http://u64/v1/version")
    moved = rest_lib.RestClient(
        targets.Target(token="u64", device="u64", computer="u64", rest_port=8080))
    expect("moved", moved.url("/v1/version"), "http://u64:8080/v1/version")
    return "80 stays implicit"


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


# ---------------------------------------------------------------------------
# Tier 2: one component against the device double
# ---------------------------------------------------------------------------


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


# ---------------------------------------------------------------------------
# Tier 3: a whole scripted run, with the real runner and no real device
# ---------------------------------------------------------------------------
#
# The report generator can be perfect against a fixture the runner no longer
# writes, and nothing else here would notice. So these run the actual
# `run-tests`, against the device double, over stub suites scripted to fail, to
# be retried, to be killed mid-line and to skip, and read the tree it wrote.
#
# Two things about the real runner are answered rather than reproduced. It
# starts a child `run-tests` per target, so the wrapper below replaces the
# script that command names with itself, and it drives the on-device UI object
# stack through a gate the double does not fake, so the wrapper answers that
# gate. Everything else - run_one_attempt, the retry loop, the health sweeps,
# the records, the console capture - is the runner's own code.

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
    runner.SUITES = tuple(runner.Suite(**entry) for entry in json.load(handle))

# The double serves REST, FTP, Telnet and the DMA control port. It does not
# fake the on-device UI object stack, which is what this gate drives.
runner.ui_state_gate = lambda action, options, label="", quiet=False: True

_child_command = runner.child_command


def child_command(args, target, jsonl_dir):
    """The real command, with this wrapper in place of the runner's own path."""
    command = _child_command(args, target, jsonl_dir)
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

sys.path.insert(0, {library!r})
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

    def records(self, *parts: str) -> List[dict]:
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

    def tree(self) -> List[str]:
        """Every file under the -j directory, by relative path, sorted."""
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
                 extra_environment: Optional[dict] = None) -> ScriptedRun:
    """Drive the real runner over `stubs` against `double`, and return the tree."""
    import json
    import subprocess

    # The wrapper answers the UI-state gate as satisfied, so the double agrees
    # with it: between suites no menu is open, and 404 on this endpoint is
    # what that state looks like.
    double.faults.menu_screen_404 = True

    library = os.path.dirname(os.path.abspath(__file__))
    suites = os.path.join(workspace, "suites")
    os.makedirs(suites, exist_ok=True)
    registry = []
    for stub in stubs:
        path = os.path.join(suites, f"{stub.name.replace('-', '_')}.py")
        if not stub.missing:
            with open(path, "w", encoding="utf-8") as handle:
                handle.write(STUB_PREAMBLE.format(library=library) + stub.body)
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
    environment.update(extra_environment or {})
    environment.pop("FORCE_COLOR", None)
    completed = subprocess.run(
        [sys.executable, wrapper, "-j", output, *arguments, *tokens],
        env=environment, capture_output=True, text=True)
    return ScriptedRun(output, completed.returncode,
                       completed.stdout + completed.stderr)


def records_from_a_stub_suite(environment: dict, body: str = "") -> List[dict]:
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
            f"sys.path.insert(0, {os.path.dirname(os.path.abspath(__file__))!r})\n"
            "import report\n"
            "report.check_start('a check'); report.check_ok('20 rows')\n"
            + body)
        child = dict(os.environ, E2E_JSONL=path)
        child.pop("E2E_TARGET", None)
        child.pop("E2E_ATTEMPT", None)
        child.pop("E2E_SUITE", None)
        child.update(environment)
        completed = subprocess.run([sys.executable, "-c", script], env=child,
                                   capture_output=True, text=True)
        if completed.returncode != 0:
            raise Failure(f"the stub suite exited {completed.returncode}: "
                          f"{completed.stderr.strip()[:200]}")
        with open(path, encoding="utf-8") as handle:
            return [json.loads(line) for line in handle if line.strip()]


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


@case(3, "OBS-2.13")
def a_suite_console_reaches_the_log_and_the_terminal() -> str:
    """Every line a suite printed is in its log, in order, with no escapes."""
    import tempfile

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
        expect("in order", saved,
               b"[01] coloured ... OK (20 rows, 0.000s)\n"
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
    import tempfile

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
def a_second_attempt_appends_to_the_log() -> str:
    """The first attempt truncates and every one after it appends."""
    import tempfile

    with DeviceDouble() as double, tempfile.TemporaryDirectory() as workspace:
        flag = os.path.join(workspace, "unhealthy")
        double.withhold_ftp_banner_while(flag)
        made = scripted_run(
            double, [Stub("flaky", body=RETRY_BODY)],
            arguments=("--recover-command", f"rm -f {flag}"),
            workspace=workspace, extra_environment={"OBS_FLAG": flag})
        with open(made.path("127.0.0.1", "overlay-flaky.log"),
                  encoding="utf-8") as handle:
            saved = handle.read()
        expect("both attempts", saved.count("the device is well"), 2)
        if "FAIL" not in saved or "OK" not in saved:
            raise Failure(f"one attempt is missing from the log: {saved!r}")
        checks = [r for r in made.records("127.0.0.1", "overlay-flaky.jsonl")
                  if r["kind"] == "check"]
        expect("one record per attempt", [c["attempt"] for c in checks], [1, 2])
        expect("one index", {c["index"] for c in checks}, {1})
    return "2 attempts in one log"


@case(3, "OBS-2.14")
def the_run_records_what_it_intended_to_run() -> str:
    """The plan names every registered suite and why each absent one is absent."""
    import tempfile

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
    import tempfile

    stubs = [Stub("held"), Stub("operator-only", manual=True),
             Stub("a-benchmark", category="perf", args="-H @HOST@ -p @PASS@")]
    with DeviceDouble() as double, tempfile.TemporaryDirectory() as workspace:
        made = scripted_run(double, stubs, workspace=workspace)
        plan = [r for r in made.records("127.0.0.1", "run.jsonl")
                if r["kind"] == "plan"][0]
        by_name = {entry["name"]: entry for entry in plan["suites"]}
        expect("manual", by_name["operator-only"]["reason"], "manual")
        expect("category", by_name["a-benchmark"]["reason"], "category")
        expect("held runs", by_name["held"]["run"], True)
    return "manual and category"


@case(3, "OBS-2.15")
def the_run_records_what_it_assumed() -> str:
    """A run says which firmware fixes it treated as present."""
    import tempfile

    with DeviceDouble() as double, tempfile.TemporaryDirectory() as workspace:
        plain = scripted_run(double, [Stub("held")], workspace=workspace)
        run = [r for r in plain.records("127.0.0.1", "run.jsonl")
               if r["kind"] == "run"][0]
        expect("none in force", run["assumptions"], [])
    with DeviceDouble() as double, tempfile.TemporaryDirectory() as workspace:
        assumed = scripted_run(
            double, [Stub("held")],
            arguments=("--assume-fix", "ftp-listing-full-length"),
            workspace=workspace)
        run = [r for r in assumed.records("127.0.0.1", "run.jsonl")
               if r["kind"] == "run"][0]
        expect("in force", run["assumptions"], ["ftp-listing-full-length"])
    return "one fix assumed"


@case(3, "OBS-2.16")
def the_action_log_says_what_the_harness_did() -> str:
    """Every mutation is recorded, and a plain successful GET is not."""
    import tempfile

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
    import tempfile

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
    import tempfile

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
    import tempfile

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
    import tempfile

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
    import tempfile

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
    import tempfile

    with DeviceDouble() as double, tempfile.TemporaryDirectory() as workspace:
        made = scripted_run(double, [Stub("held")],
                            tokens=("127.0.0.1", "127.0.0.1@localhost"),
                            arguments=("--assume-fix", "ftp-listing-full-length"),
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
                   ["ftp-listing-full-length"])
    return "3 processes, one assumption"


@case(3, "OBS-2.1", "OBS-2.10")
def one_target_writes_its_own_slug_directory() -> str:
    """A single-target run produces DIR/<slug>/, not DIR/ and not DIR/<slug>/<slug>/."""
    import tempfile

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
    import tempfile

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
    import tempfile

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
    import tempfile

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


# ---------------------------------------------------------------------------
# Running them
# ---------------------------------------------------------------------------


def selected(tiers: Sequence[int], only: Sequence[str]) -> List[Case]:
    chosen = [c for c in CASES if not tiers or c.tier in tiers]
    if only:
        chosen = [c for c in chosen
                  if any(name in c.label or name in c.requirements for name in only)]
    return chosen


def run_case(entry: Case) -> str:
    """One case, reported as one check. Never lets an exception end the suite."""
    report.check_start(entry.label)
    try:
        extra = entry.run() or ""
    except Skipped as exc:
        report.check_skip(str(exc))
        return report.SKIP
    except Failure as exc:
        report.check_fail(str(exc))
        return report.FAIL
    except Exception as exc:  # noqa: BLE001 - a case must not end the suite
        report.check_fail(f"{type(exc).__name__}: {report.format_exception(exc)}")
        return report.FAIL
    report.check_ok(str(extra))
    return report.OK


def main(argv: List[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--tier", action="append", type=int, default=[],
                        help="Run only this tier. Repeatable.")
    parser.add_argument("-k", "--only", action="append", default=[],
                        help="Run only cases whose label or requirement matches. "
                             "Repeatable.")
    args = parser.parse_args(argv)

    verdicts: List[str] = []
    for tier, title in TIERS:
        cases = [c for c in selected(args.tier, args.only) if c.tier == tier]
        if not cases:
            continue
        report.section(f"tier {tier}: {title}")
        verdicts += [run_case(entry) for entry in cases]

    failed = verdicts.count(report.FAIL)
    skipped = verdicts.count(report.SKIP)
    note = f"{len(verdicts)} cases" + (f", {skipped} skipped" if skipped else "")
    if failed:
        report.suite_fail("observability", f"{failed} of {note} failed")
        return 1
    report.suite_ok("observability", note)
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
