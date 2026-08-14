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
REPORT_TOOL = os.path.join(ROOT, "tools", "e2e_report.py")

FIXTURES = os.path.join(os.path.dirname(os.path.abspath(__file__)), "fixtures")
# A reduced real run, written by the runner rather than by hand, and the
# document generated from it. A hand-written tree diverges from what the runner
# actually writes, and the first anyone finds out is when the report renders an
# empty section against a real run.
FIXTURE = os.path.join(FIXTURES, "e2e-run")
EXPECTED = os.path.join(FIXTURES, "e2e-run.expected.md")
# Re-recorded into a fixed directory rather than a temporary one, so the paths
# the runner writes into its own records are the same every time and a
# regeneration is a diff somebody can read.
FIXTURE_WORKSPACE = "/tmp/e2e-observability-fixture"

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
    e2e_library = os.path.join(ROOT, "tests", "e2e", "lib")
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


@case(3, "OBS-5.1", "OBS-5.3", "OBS-5.4", "OBS-5.5")
def a_failing_suite_leaves_a_capture() -> str:
    """Three reads, three artefacts, named from the suite run's own key."""
    import json
    import tempfile

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
    import json
    import tempfile

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
    import json
    import tempfile

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
        expect("the run still failed for the suite's reason", made.status, 1)
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


@case(3, "OBS-8.22")
def the_suites_spool_every_screen_they_read() -> str:
    """A screen the harness read is in the spool, as text and as raw bytes."""
    import tempfile

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
def the_spool_is_off_when_a_run_says_so() -> str:
    """--no-screens turns it off, and the run is otherwise unchanged."""
    import tempfile

    with DeviceDouble() as double, tempfile.TemporaryDirectory() as workspace:
        made = scripted_run(double, [Stub("held")],
                            arguments=("--no-screens",), workspace=workspace)
        if "127.0.0.1/screens.jsonl" in made.tree():
            raise Failure("a spool was written with --no-screens")
        expect("the run is unchanged", made.status, 0)
    return "no spool"


# ---------------------------------------------------------------------------
# Tier 4: the golden document, and the fixture behind it
# ---------------------------------------------------------------------------
#
# OBS-3.13 lists the shapes the fixture has to contain, and every one of them is
# produced by a stub below rather than written by hand: two targets, one of them
# a cartridge, a retried suite, a failing suite, a skipped suite, a suite with
# no closing record, a truncated final line, a health sweep with a failed check
# and one with a skipped check, and a console log holding a traceback.

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
    # Drives the menu through the shared UI backend, so the spool of every
    # screen the harness read is in the fixture too.
    Stub("browse", body=(
        "import pathlib\n"
        "import ui_backend\n"
        "pathlib.Path(os.environ['OBS_MENU']).touch()\n"
        "backend = ui_backend.make_backend('overlay', ARGS.host,\n"
        "                                  ARGS.password, 5.0)\n"
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
        "report.check_start('the first half')\n"
        "report.check_ok()\n"
        "if os.environ.get('E2E_TARGET') == '127.0.0.1@localhost':\n"
        "    with open(os.environ['E2E_JSONL'], 'a') as handle:\n"
        "        handle.write('{\"kind\": \"check\", \"index\": 2, \"lab')\n"
        "        handle.flush()\n"
        "    os.kill(os.getppid(), signal.SIGKILL)\n"
        "    os.kill(os.getpid(), signal.SIGKILL)\n"
        "report.check_start('the second half')\n"
        "report.check_ok()\n")),
)


def record_fixture() -> int:
    """Re-record the checked-in fixture and its expected document.

    A deliberate act, not a side effect: the expected document is what a
    reviewer reads to see a rendering change, so regenerating it has to be
    something somebody chose to do.
    """
    import shutil

    shutil.rmtree(FIXTURE_WORKSPACE, ignore_errors=True)
    os.makedirs(FIXTURE_WORKSPACE)
    unhealthy = os.path.join(FIXTURE_WORKSPACE, "unhealthy")
    menu = os.path.join(FIXTURE_WORKSPACE, "menu-open")
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
            arguments=("--e2e", "--perf",
                       "--recover-command", f"rm -f {unhealthy}"),
            workspace=FIXTURE_WORKSPACE,
            extra_environment={"OBS_FLAG": unhealthy, "OBS_MENU": menu})
    shutil.rmtree(FIXTURE, ignore_errors=True)
    shutil.copytree(made.directory, FIXTURE)
    with open(EXPECTED, "w", encoding="utf-8") as handle:
        handle.write(generated_document())
    report.detail(f"recorded {FIXTURE} and {EXPECTED} from a run that exited "
                  f"{made.status}")
    return 0


def generated_document() -> str:
    """The report the generator writes for the checked-in fixture.

    Generated in a copy so the checked-in tree is never written into: the
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
    import tempfile

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


def require_fixture() -> None:
    if not os.path.isdir(FIXTURE):
        raise Skipped(f"{os.path.relpath(FIXTURE, ROOT)} has not been recorded; "
                      "run this suite with --record-fixture")


@case(4, "OBS-3.13", "OBS-3.21")
def the_document_is_byte_identical_to_the_expected_one() -> str:
    """The report for the fixture is exactly the document checked in beside it.

    A diff of that document is exactly the diff a reader of a real report would
    see, which is what makes a rendering change visible in review rather than
    only in production.
    """
    require_fixture()
    if not os.path.exists(EXPECTED):
        raise Failure(f"{os.path.relpath(EXPECTED, ROOT)} is missing")
    with open(EXPECTED, encoding="utf-8") as handle:
        wanted = handle.read()
    made = generated_document()
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
    import re

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
    block: List[str] = []
    tables = 0
    for line in document.splitlines() + [""]:
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
    run = generator.load_tree(FIXTURE)
    document = generated_document()
    lines = [line for line in document.splitlines() if line.strip()]
    expect("the title first", lines[0], "# E2E gate run: FAIL")
    status = lines[1]
    if not status.startswith("RESULT: "):
        raise Failure(f"the status line is not second: {status!r}")
    fields = dict(part.split("=", 1) for part in status.split("  ")[1:])
    counts = run.counts()
    for name in ("targets", "suites", "ok", "fail", "warn", "skip", "recoveries"):
        expect(name, fields[name], str(counts[name]))
    expect("exit", fields["exit"], str(run.exit_code))
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


@case(4, "OBS-3.27")
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
    import re

    require_fixture()
    document = generated_document()
    section = document.split("## Files in this run", 1)[1].split("\n\n", 2)[2]
    named = re.findall(r"^\| `([^`]+)`", section, flags=re.M)
    if len(named) < 10:
        raise Failure(f"the file index is too short: {named}")
    for relative in named:
        if relative == "index.md":
            continue
        if not os.path.exists(os.path.join(FIXTURE, relative)):
            raise Failure(f"{relative} is named and is not there")
    return f"{len(named)} files"


@case(3, "OBS-16.3", "OBS-3.1")
def a_report_is_generated_from_a_run_the_runner_just_wrote() -> str:
    """The generator reads a tree the current runner produced, not a fixture.

    This is the tier the others cannot replace. A generator can be perfect
    against a checked-in fixture that the runner no longer writes, and nothing
    else here would notice until somebody opened an artifact from a real run.
    """
    import tempfile

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
                   "exit=1"):
        if wanted not in status:
            raise Failure(f"{wanted!r} is not in {status!r}")
    if "127.0.0.1/overlay/broken/1/1" not in document:
        raise Failure("the failing check is not named by its identity key")
    return status


# Requirements this suite deliberately does not name a test for, and why. A
# reason here is a decision somebody took; a requirement in neither this table
# nor a test is one nobody has decided about.
UNTESTED_REQUIREMENTS = {
    "OBS-1.2": "a startup message, which every component's own test asserts "
               "for itself rather than as a rule",
    "OBS-1.9": "a rule about which artefacts exist, held by the tests for each "
               "of them",
    "OBS-2.6": "the interval rule, exercised by every case that joins a record "
               "to a time",
    "OBS-2.7": "attribution of the gaps between checks, which only the syslog "
               "slices consume",
    "OBS-3.16": "one documented pandoc command, run by hand and never in CI",
    "OBS-9.1": "optional firmware work, proven red then green on hardware",
    "OBS-9.2": "optional firmware work, proven red then green on hardware",
    "OBS-9.3": "optional firmware work, proven red then green on hardware",
    "OBS-14.1": "a statement about which host platforms are supported",
    "OBS-14.4": "a host requirement, documented rather than executable",
    "OBS-16.1": "the shape of this suite, which running it demonstrates",
    "OBS-16.4": "the three ways this suite is invoked, each of which invokes it",
    "OBS-16.8": "the suite's own budget, measured by running it",
    "OBS-16.9": "the suite's own constraint, held by every case in it",
    "OBS-16.10": "where the device-free acceptance criteria live",
}


def specified_requirements() -> "dict":
    """Every requirement the specification defines, with its priority."""
    import re

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


@case(1, "OBS-16.7")
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


@case(4, "OBS-3.17")
def a_record_of_the_wrong_shape_costs_that_record_only() -> str:
    """Every one of these still produces a document and exits zero."""
    import json
    import shutil
    import tempfile

    require_fixture()
    generator = load_report_tool()
    for label, record, name in MALFORMED_RECORDS:
        with tempfile.TemporaryDirectory() as directory:
            tree = os.path.join(directory, "run")
            shutil.copytree(FIXTURE, tree)
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
    run = generator.load_tree(FIXTURE)
    for made in run.all_suites():
        target = next(t for t in run.targets if t.token == made.target)
        for relative in (f"{made.stem}.log",
                         f"capture/{made.stem}-{made.attempt}-screen.txt"):
            path = os.path.join(FIXTURE, target.slug, relative)
            if os.path.exists(path) and relative not in document:
                raise Failure(f"{relative} is in the tree and not in the report")
    return "log and capture names"


@case(4, "OBS-3.7")
def the_health_table_is_every_sweep_in_order() -> str:
    """One row per sweep, one column per check, in the console's own words."""
    require_fixture()
    generator = load_report_tool()
    run = generator.load_tree(FIXTURE)
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


@case(4, "OBS-3.26")
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
    import collections

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


@case(1, "OBS-14.5")
def the_generator_adds_no_dependency() -> str:
    """It imports the standard library and this repository's own modules only.

    The same rule check_transport_usage.py applies to the HTTP client, applied
    to imports: a new package here is a package the CI host has to grow.
    """
    import ast
    import sys as sys_lib

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


@case(4, "OBS-4.1", "OBS-1.7")
def the_job_summary_is_a_copy_of_part_of_the_report() -> str:
    """The summary is the report's own bytes, plus at most the two lines.

    Asserting this by construction is what keeps one authored report true:
    the summary is a copy, and there is no second generator to keep in step.
    """
    import tempfile

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


@case(1, "OBS-4.1", "OBS-4.3")
def the_job_summary_stays_inside_its_limits() -> str:
    """No marker copies the whole file; an oversized one truncates and says so."""
    import tempfile

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


@case(1, "OBS-4.1")
def the_job_summary_step_does_nothing_outside_ci() -> str:
    """With no GITHUB_STEP_SUMMARY the step exits zero and writes nothing."""
    import tempfile

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


@case(3, "OBS-3.28")
def a_run_is_compared_with_the_one_before_it() -> str:
    """Two trees, one section, joined on the identity key."""
    import tempfile

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
    import tempfile

    with DeviceDouble() as double, tempfile.TemporaryDirectory() as workspace:
        made = scripted_run(double, [Stub("held")], workspace=workspace,
                            extra_environment={"GITHUB_RUN_ID": "1234567",
                                               "GITHUB_RUN_ATTEMPT": "2"})
        run = [r for r in made.records("127.0.0.1", "run.jsonl")
               if r["kind"] == "run"][0]
        expect("run id", run["ci_run_id"], "1234567")
        expect("attempt", run["ci_run_attempt"], "2")
    with DeviceDouble() as double, tempfile.TemporaryDirectory() as workspace:
        made = scripted_run(double, [Stub("held")], workspace=workspace)
        run = [r for r in made.records("127.0.0.1", "run.jsonl")
               if r["kind"] == "run"][0]
        if "ci_run_id" in run:
            raise Failure(f"an identifier was invented: {run['ci_run_id']!r}")
    return "1234567 attempt 2, absent outside CI"


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
    parser.add_argument("--record-fixture", action="store_true",
                        help="Re-record tests/lib/fixtures/ from a scripted run "
                             "and regenerate the expected document.")
    parser.add_argument("-k", "--only", action="append", default=[],
                        help="Run only cases whose label or requirement matches. "
                             "Repeatable.")
    args = parser.parse_args(argv)
    if args.record_fixture:
        return record_fixture()

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
