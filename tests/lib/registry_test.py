#!/usr/bin/env python3
# Gate check: every executable suite in the tree is registered in run-tests.

"""Fail if a suite exists that no profile can select, or is registered wrongly.

`tests/e2e/network/telnet_sustained_input_test.py` sat in the tree unregistered.
Its docstring describes a measured regression, a held cursor key dropping a
Telnet session on a slow link, with the cause in the firmware named. No profile
selected it, `--all` did not run it, and no README mentioned it, so the guard it
implements had never run since the day it was written.
`tests/e2e/av/stream_test.py` was in the same position.

A written regression guard that never runs is the cheapest kind of defect to
prevent and the most expensive to notice, so it is checked here rather than
left to whoever next reads the registry. Three rules:

- every `*_test.py` under tests/e2e, tests/perf and tests/soak appears in the
  registry, and every registered path exists;
- a registered suite's argument template only spells tokens the runner
  substitutes;
- every argument in the template is one its own parser accepts, so a
  registration cannot name `-p` for a suite that has no password argument;
- every `sys.path` adjustment is the shared bootstrap, not a private one;
- `-H`, `-p` and `-t` come from `tests/lib/cli.py`, not from a fourth copy.

The third rule reads the suite's parser rather than running it, because
importing 60 suites would cost more than the check is worth and several of them
open sockets at import time.

Needs no device.
"""

import argparse
import ast
import importlib.machinery
import importlib.util
import os
import sys
from pathlib import Path

# tests/lib holds the shared library; importing bootstrap adds tests/e2e/lib.
# The search walks up rather than counting directories, so this is the same in
# every entry point and a suite that moves needs no edit. See tests/lib/bootstrap.py.
sys.path.insert(0, str(next(p for p in Path(__file__).resolve().parents
                            if (p / "tests" / "lib").is_dir()) / "tests" / "lib"))
import bootstrap  # noqa: E402,F401
from report import Failure, check, detail, suite_fail, suite_ok  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
RUNNER_PATH = os.path.join(ROOT, "run-tests")
SEARCHED = ("tests/lib", "tests/e2e", "tests/perf", "tests/soak")

NAME = "registry_test"

# What run-tests substitutes into a suite's argument template before starting
# it. A token outside this set would reach the suite as a literal.
TOKENS = {"@HOST@", "@PASS@", "@TIMEOUT@", "@MODE@", "@SOAKPROFILE@"}

# Suites the registry deliberately does not carry, each with the reason. A file
# ending in _test.py that is not a suite belongs here rather than in the
# registry, so that "not registered" always means "nobody can run it".
# A file ending in _test.py that no profile should select, each with its
# reason. Everything else under SEARCHED has to be in the registry.
NOT_SUITES: dict[str, str] = {
    # The four tiers of the observability suite. Importing one registers its
    # cases in support.CASES; tests/lib/observability_test.py imports all four
    # and is the registered suite. A tier cannot be selected on its own,
    # because run_cases takes them in order and the golden tier reads a
    # fixture the pipeline tier builds.
    "tests/lib/observability/pure_test.py": "tier 1 of the observability suite",
    "tests/lib/observability/component_test.py": "tier 2 of the observability suite",
    "tests/lib/observability/pipeline_test.py": "tier 3 of the observability suite",
    "tests/lib/observability/golden_test.py": "tier 4 of the observability suite",
}

# tests/lib/bootstrap.py is the one place that may compute a path into the
# tree. Everywhere else, a sys.path line has to be one of these two shapes.
BOOTSTRAP_OWNER = "tests/lib/bootstrap.py"
BOOTSTRAP_SHAPES = ("Path(__file__).resolve().parents", "bootstrap.directory(")

# Suites that still register their own -H, each with the reason. A suite whose
# defaults are the shared ones belongs in cli.add_device_arguments; these are
# the ones whose defaults are not, and adopting the helper would change what a
# run does rather than only how the file reads.
OWN_DEVICE_ARGUMENTS = {
    "tests/lib/cli.py": "defines them",
    "tests/e2e/api/openapi_contract_test.py": "-H is required, with no default",
    "tests/e2e/lib/ui_state.py": "-H is required, with no default",
    "tests/e2e/io/printer/verify_printer_output.py":
        "reads the printer's output over FTP, so it takes --ftp-password "
        "rather than a REST password, and no per-call budget",
    "tests/e2e/av/stream_test.py":
        "addresses the stream source, so U64_C64_HOST comes first",
    "tests/e2e/u64ctrl/power_cycle_test.py": "defaults to the computer, c64u",
    "tests/e2e/u64ctrl/wake_on_wifi_test.py": "defaults to the computer, c64u",
    "tests/soak/network/connection_test.py": "its own HOST constant",
    # A fixed timeout rather than one U64_TIMEOUT can move: adopting the helper
    # would make these honour that variable, which is a change to a run.
    "tests/e2e/filesystem/ftp_client_test.py": "fixed timeout",
    "tests/e2e/io/c64/assembly64_test.py": "fixed timeout",
    "tests/e2e/io/c64/doom_release_test.py": "fixed timeout",
    "tests/e2e/io/c64/reu_turbo_test.py": "fixed timeout",
    "tests/perf/rest_latency_perf_test.py": "fixed timeout, a measured budget",
    "tests/perf/telnet_key_latency_perf_test.py": "fixed timeout, a measured budget",
    "tests/soak/filemanager/menu_navigation_soak_test.py": "fixed timeout",
    "tests/soak/network/listener_soak_test.py": "timeout is REST_BUDGET_SECONDS",
    # -t is how long the page has to become ready, not a device call budget.
    "tests/e2e/web/index_test.py": "-t is READY_TIMEOUT, a page load",
    "tests/e2e/web/theme_test.py": "-t is READY_TIMEOUT, a page load",
}


def load_runner():
    """Import run-tests as a module. It has no .py suffix, so it needs a loader."""
    loader = importlib.machinery.SourceFileLoader("run_tests_registry", RUNNER_PATH)
    spec = importlib.util.spec_from_loader("run_tests_registry", loader)
    module = importlib.util.module_from_spec(spec)
    loader.exec_module(module)
    return module


def suites_on_disk():
    """Every executable suite under the searched directories, repo-relative."""
    found = set()
    for top in SEARCHED:
        base = os.path.join(ROOT, top)
        for directory, _dirs, files in os.walk(base):
            if "__pycache__" in directory:
                continue
            for name in files:
                if not name.endswith("_test.py"):
                    continue
                path = os.path.relpath(os.path.join(directory, name), ROOT)
                if path not in NOT_SUITES:
                    found.add(path)
    return found


# Modules holding the shared "add_<something>_argument(parser)" helpers. A suite
# that calls one of these registers the helper's flags without spelling them, so
# the options each helper adds are read from here and credited to its callers.
HELPER_SOURCES = ("tests/lib/cli.py", "tests/lib/report.py",
                  "tests/e2e/lib/ui_backend.py", "tests/lib/targets.py")


def parse_file(path):
    """The file's AST, or None when it is missing or does not parse.

    A registered path that is not in the tree is reported by its own check, so
    it is skipped here rather than raising out of this one.
    """
    try:
        with open(os.path.join(ROOT, path), encoding="utf-8") as handle:
            source = handle.read()
    except OSError:
        return None
    try:
        return ast.parse(source, filename=path)
    except SyntaxError:
        # The lint suite reports a syntax error; do not report it twice.
        return None


def option_strings(node):
    """The "-x"/"--xy" literals an add_argument call registers."""
    return {arg.value for arg in node.args
            if isinstance(arg, ast.Constant) and isinstance(arg.value, str)
            and arg.value.startswith("-")}


def argument_helpers():
    """Map each shared helper's name to the options it registers."""
    helpers = {}
    for source in HELPER_SOURCES:
        tree = parse_file(source)
        if tree is None:
            continue
        for node in ast.walk(tree):
            if not isinstance(node, ast.FunctionDef):
                continue
            if not (node.name.startswith("add_")
                    and node.name.endswith(("_argument", "_arguments"))):
                continue
            options = set()
            for inner in ast.walk(node):
                if isinstance(inner, ast.Call) and isinstance(inner.func, ast.Attribute) \
                        and inner.func.attr == "add_argument":
                    options |= option_strings(inner)
            if options:
                helpers[node.name] = options
    return helpers


def declared_options(path, helpers):
    """Every option the file's parser accepts, its shared helpers included.

    Read from the source rather than by importing it: several suites open a
    socket or read the environment at import time, and this check must not
    depend on a device or on the order it runs in.
    """
    tree = parse_file(path)
    if tree is None:
        return None
    options = set()
    for node in ast.walk(tree):
        if not isinstance(node, ast.Call):
            continue
        func = node.func
        if isinstance(func, ast.Attribute) and func.attr == "add_argument":
            options |= option_strings(node)
            continue
        # A call to one of the shared helpers, spelled bare or qualified.
        name = func.attr if isinstance(func, ast.Attribute) else \
            func.id if isinstance(func, ast.Name) else None
        if name in helpers:
            options |= helpers[name]
            options -= suppressed_by(node)
    return options


# Keywords that turn one of the helper's arguments off, and what they remove.
# Reading the helper's add_argument calls alone says a suite accepts every
# option the helper can register, which is how two suites came to be
# registered with a `-t` they refused: `add_device_arguments(timeout=None)`
# registers none, and the runner still passed one.
SUPPRESSING = {"timeout": {"-t", "--timeout"},
               "colour": {"--color", "--colour"}}


def suppressed_by(node):
    """Options a helper call switched off with a keyword."""
    off = set()
    for keyword in node.keywords:
        if keyword.arg not in SUPPRESSING:
            continue
        value = keyword.value
        if isinstance(value, ast.Constant) and value.value in (None, False):
            off |= SUPPRESSING[keyword.arg]
    return off


# Options a suite gets without registering them itself: argparse's own.
INHERITED = {"-h", "--help"}


def python_files():
    """Every host-side Python file under tests/, repo-relative."""
    for base, _dirs, files in os.walk(os.path.join(ROOT, "tests")):
        if "__pycache__" in base or os.sep + "pico" in base:
            continue
        for name in sorted(files):
            if name.endswith(".py"):
                yield os.path.relpath(os.path.join(base, name), ROOT)


def private_path_lines(relative):
    """Module-level sys.path lines in this file that are not the bootstrap.

    Module level only. A sys.path line inside a function is doing something
    else: observability_test.py reaches tools/api for the generator it checks,
    and openapi_contract_test.py puts a temporary workspace on the path.
    """
    if relative == BOOTSTRAP_OWNER:
        return []
    tree = parse_file(relative)
    if tree is None:
        return []
    found = []
    for node in tree.body:
        if not isinstance(node, (ast.Expr, ast.Assign, ast.AugAssign)):
            continue
        try:
            text = ast.unparse(node)
        except Exception:  # noqa: BLE001 - unparsable is not this check's business
            continue
        if not text.startswith("sys.path"):
            continue
        if any(shape in text for shape in BOOTSTRAP_SHAPES):
            continue
        found.append((node.lineno, text.splitlines()[0]))
    return found


def own_device_arguments(relative):
    """Device arguments this file registers instead of taking from cli.py."""
    tree = parse_file(relative)
    if tree is None:
        return []
    found = []
    for node in ast.walk(tree):
        if not (isinstance(node, ast.Call) and isinstance(node.func, ast.Attribute)
                and node.func.attr == "add_argument" and node.args):
            continue
        first = node.args[0]
        if isinstance(first, ast.Constant) and first.value in ("-H", "-p", "-t"):
            found.append((node.lineno, first.value))
    return found


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0]
                                     if __doc__ else "")
    report_module = sys.modules["report"]
    report_module.add_colour_argument(parser)
    report_module.apply_colour(parser.parse_args().color)

    runner = load_runner()
    registered = {suite.path: suite for suite in runner.SUITES}
    helpers = argument_helpers()
    on_disk = suites_on_disk()
    failed = 0

    def report_all(problems):
        """Print every problem, then raise so the check reads FAIL."""
        for one in problems:
            detail(one)
        raise Failure(f"{len(problems)} problem(s)")

    try:
        with check("every suite in the tree is registered in run-tests"):
            missing = sorted(on_disk - set(registered))
            if missing:
                report_all([f"{path} is not in run-tests SUITES, so no profile "
                            "and no --all can select it" for path in missing])
            detail(f"{len(on_disk)} suites under {', '.join(SEARCHED)}")
    except Failure:
        failed += 1

    try:
        with check("every registered path exists"):
            gone = sorted(path for path in registered
                          if not os.path.exists(os.path.join(ROOT, path)))
            if gone:
                report_all([f"{path} is registered but is not in the tree"
                            for path in gone])
            detail(f"{len(registered)} registered paths")
    except Failure:
        failed += 1

    try:
        with check("every argument template spells only tokens the runner knows"):
            unknown = [f"{suite.name} passes {word}, which run-tests does not "
                       "substitute"
                       for suite in runner.SUITES for word in suite.args.split()
                       if word.startswith("@") and word not in TOKENS]
            if unknown:
                report_all(unknown)
            detail(f"tokens: {', '.join(sorted(TOKENS))}")
    except Failure:
        failed += 1

    try:
        with check("every registered argument is one the suite's parser accepts"):
            rejected = []
            read = 0
            for suite in runner.SUITES:
                if not suite.args:
                    continue
                options = declared_options(suite.path, helpers)
                if options is None:
                    continue
                read += 1
                for word in suite.args.split():
                    if not word.startswith("-") or word in INHERITED:
                        continue
                    if word not in options:
                        rejected.append(
                            f"{suite.name} is registered with {word}, which "
                            f"{suite.path} does not accept")
            if rejected:
                report_all(rejected)
            detail(f"{read} templates, against {len(helpers)} shared argument "
                   "helpers")
    except Failure:
        failed += 1

    try:
        with check("every sys.path line is the shared bootstrap"):
            private = [f"{relative}:{line} {text}"
                       for relative in python_files()
                       for line, text in private_path_lines(relative)]
            if private:
                private.append("Use the four-line stanza in tests/lib/bootstrap.py, "
                               "and sys.path.insert(0, bootstrap.directory(...)) "
                               "for a suite that imports another suite.")
                report_all(private)
            detail("one bootstrap, computed by walking up rather than by counting")
    except Failure:
        failed += 1

    try:
        with check("-H, -p and -t come from tests/lib/cli.py"):
            private = []
            for relative in python_files():
                if relative in OWN_DEVICE_ARGUMENTS:
                    continue
                for line, flag in own_device_arguments(relative):
                    private.append(
                        f"{relative}:{line} registers {flag} itself; use "
                        "cli.add_device_arguments(parser, timeout=None), or name the file in "
                        "OWN_DEVICE_ARGUMENTS with the reason")
            if private:
                report_all(private)
            detail(f"{len(OWN_DEVICE_ARGUMENTS)} files keep their own, each with "
                   "a reason")
    except Failure:
        failed += 1

    if failed:
        suite_fail(NAME, f"{failed} of 6 checks failed")
        return 1
    suite_ok(NAME, f"{len(registered)} suites")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
