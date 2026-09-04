#!/usr/bin/env python3
# Gate check: a stale machine.FIXES entry is reported, without a device.

"""Verify tools/stale_gates.py against synthetic runs, not a real one.

machine.FIXES entries are how a check for a since-fixed firmware gap stays
disabled: `skip_without_fix` reports the check as SKIP rather than running
it, and `--assume-fix` runs it anyway. When the assumption turns out to be
right -- the check passes -- nothing before this suite's branch said so; the
entry just kept skipping, and a stale gate reads exactly like a real one.

`report.note_assumed_fix` tags the check `skip_without_fix` lets through with
the entry and the machine it stood in for, and `tools/stale_gates.py` reads
that tag back out of a run's JSONL. This drives both ends directly -- a real
`Machine`, the real `skip_without_fix`, a real (temporary) JSONL file -- so
what is proved is the mechanism a live `--assume-fix=all` run depends on,
not a description of it. No device: everything here is a machine kind and a
check body chosen by the case.

Needs no device, so it runs in the device-free group beside lint_test.py and
registry_test.py.
"""

import contextlib
import importlib.machinery
import importlib.util
import io
import os
import sys
import tempfile
from pathlib import Path

# The one stanza that puts the shared library on sys.path; see tests/lib/bootstrap.py.
sys.path.insert(0, str(next(p for p in Path(__file__).resolve().parents
                            if (p / "tests" / "lib").is_dir()) / "tests" / "lib"))
import bootstrap  # noqa: E402,F401
import cli  # noqa: E402
import machine  # noqa: E402
from selftest import expect  # noqa: E402
from report import Failure, check, suite_fail, suite_ok  # noqa: E402

NAME = "stale_gates_test"
# tools/ is not tests/lib or tests/e2e/lib, so this reaches it by path rather
# than by another sys.path insert; see observability/support.py's
# load_report_tool for the same reason against tools/e2e_report.py.
STALE_GATES_PATH = os.path.join(os.path.dirname(bootstrap.TESTS), "tools", "stale_gates.py")


def load_stale_gates():
    loader = importlib.machinery.SourceFileLoader("stale_gates", STALE_GATES_PATH)
    spec = importlib.util.spec_from_loader("stale_gates", loader)
    module = importlib.util.module_from_spec(spec)
    # Registered before it runs, because the Stale dataclass declared in it
    # looks its own module up by name while the class body is being
    # processed; see observability/support.py's load_report_tool.
    sys.modules["stale_gates"] = module
    loader.exec_module(module)
    return module


stale_gates = load_stale_gates()

# A real entry this branch added (see machine.py and PR #845), used rather
# than an invented name so this exercises the table this run will actually
# read, not a fixture that happens to look like it.
FIX = machine.INFO_REPORTS_INTERFACES
FIX_MACHINE = machine.C64U
LABEL = "GET /v1/info reports each interface's MAC address"


@contextlib.contextmanager
def synthetic_run(path: str):
    """A block whose reporting goes only to `path`, on a Reporter of its own.

    Swapped rather than redirected by path alone, the way
    lib/observability/pure_test.py's thread-ownership case isolates itself:
    a Reporter is one object, so the whole numbering/console state goes with
    it and this suite's own check lines are untouched by what runs inside.
    """
    report_module = sys.modules["report"]
    outer = report_module._default
    report_module._default = report_module.Reporter(jsonl_path=path)
    try:
        with contextlib.redirect_stdout(io.StringIO()):
            yield
    finally:
        report_module._default = outer


def gated_check(fix: str, kind: str, label: str, *, passes: bool | None) -> bool:
    """Drive skip_without_fix and, when it lets the check through, run one.

    Returns whether the check ran (False means skip_without_fix skipped it
    for real). `passes` is ignored when the check does not run.
    """
    m = machine.Machine(kind=kind, product=kind, firmware="")
    if m.skip_without_fix(fix, label):
        return False
    if passes:
        with check(label):
            pass
    else:
        try:
            with check(label):
                raise Failure("still broken")
        except Failure:
            pass
    return True


def main() -> int:
    cli.device_free_arguments(__doc__)

    with check("a check that passes under an assumed fix is reported stale"):
        machine.assume(FIX)
        try:
            with tempfile.NamedTemporaryFile(suffix=".jsonl") as f:
                with synthetic_run(f.name):
                    ran = gated_check(FIX, FIX_MACHINE, LABEL, passes=True)
                expect("the check ran rather than skipped", ran, True)
                stale = stale_gates.find_stale(stale_gates.load_records(f.name))
                expect("one entry reported stale", len(stale), 1)
                entry = stale[0]
                expect("names the entry", entry.fix, FIX)
                expect("names the machine", entry.machine, FIX_MACHINE)
                expect("names the check", entry.checks, (LABEL,))
                line, = stale_gates.render(stale)
                for needle in (FIX, FIX_MACHINE, LABEL):
                    if needle not in line:
                        raise Failure(f"{needle!r} missing from rendered line {line!r}")
        finally:
            machine.forget_assumptions()

    with check("a tag in a per-suite file is found; run.jsonl alone would miss it"):
        # The layout a real run-tests output directory actually has: this
        # process's own run.jsonl sits beside one <mode>-<suite>.jsonl per
        # suite per mode, and a tagged check lands in whichever file the
        # suite that ran it was writing to -- never in run.jsonl. Found
        # running uci_targets_test.py for real: report_stale_gates() read
        # only report.JSONL_PATH (run.jsonl) and reported "none" every time,
        # even with a confirmed tag on disk in the suite's own file.
        machine.assume(FIX)
        try:
            with tempfile.TemporaryDirectory() as d:
                with synthetic_run(os.path.join(d, "run.jsonl")):
                    with check("an orchestrator-level check, unrelated to any fix"):
                        pass
                with synthetic_run(os.path.join(d, "overlay-some-suite.jsonl")):
                    ran = gated_check(FIX, FIX_MACHINE, LABEL, passes=True)
                expect("the check ran rather than skipped", ran, True)
                stale = stale_gates.find_stale(stale_gates.load_run_directory(d))
                expect("one entry reported stale", len(stale), 1)
                expect("names the entry", stale[0].fix, FIX)
        finally:
            machine.forget_assumptions()

    with check("a check that still fails under the assumption is not stale"):
        machine.assume(FIX)
        try:
            with tempfile.NamedTemporaryFile(suffix=".jsonl") as f:
                with synthetic_run(f.name):
                    ran = gated_check(FIX, FIX_MACHINE, LABEL, passes=False)
                expect("the check ran rather than skipped", ran, True)
                stale = stale_gates.find_stale(stale_gates.load_records(f.name))
                expect("nothing reported stale", stale, [])
        finally:
            machine.forget_assumptions()

    with check("one failure among repeats keeps the entry off the stale list"):
        # A retried suite runs the same gated check more than once, appending
        # to the one JSONL file each attempt. If even one attempt still fails,
        # the gap is real: the fix cannot be intermittently present.
        machine.assume(FIX)
        try:
            with tempfile.NamedTemporaryFile(suffix=".jsonl") as f:
                with synthetic_run(f.name):
                    gated_check(FIX, FIX_MACHINE, LABEL, passes=True)
                    gated_check(FIX, FIX_MACHINE, LABEL, passes=False)
                    gated_check(FIX, FIX_MACHINE, LABEL, passes=True)
                stale = stale_gates.find_stale(stale_gates.load_records(f.name))
                expect("nothing reported stale", stale, [])
        finally:
            machine.forget_assumptions()

    with check("with no assumption, the gate skips for real and tags nothing"):
        with tempfile.NamedTemporaryFile(suffix=".jsonl") as f:
            with synthetic_run(f.name):
                ran = gated_check(FIX, FIX_MACHINE, LABEL, passes=True)
            expect("the check did not run", ran, False)
            stale = stale_gates.find_stale(stale_gates.load_records(f.name))
            expect("nothing reported stale", stale, [])

    with check("assuming every fix does not tag a machine that never needed one"):
        # ASSUME_ALL, and a machine FIX's own `lacking` tuple does not name:
        # the entry exists and the assumption is in force, but this machine
        # was never skipping on it, so nothing about it is being assumed.
        machine.assume(machine.ASSUME_ALL)
        try:
            with tempfile.NamedTemporaryFile(suffix=".jsonl") as f:
                with synthetic_run(f.name):
                    ran = gated_check(FIX, machine.U64, LABEL, passes=True)
                expect("the check ran (U64 never lacked this fix)", ran, True)
                stale = stale_gates.find_stale(stale_gates.load_records(f.name))
                expect("nothing reported stale", stale, [])
        finally:
            machine.forget_assumptions()

    with check("a run with nothing tagged reports no stale gates at all"):
        with tempfile.NamedTemporaryFile(suffix=".jsonl") as f:
            with synthetic_run(f.name):
                with check("an ordinary check, nothing to do with any fix"):
                    pass
            stale = stale_gates.find_stale(stale_gates.load_records(f.name))
            expect("nothing reported stale", stale, [])
            expect("no output at all", stale_gates.render(stale), [])

    suite_ok(NAME)
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Failure as exc:
        suite_fail(NAME, str(exc))
        raise SystemExit(1)
