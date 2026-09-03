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
  registration cannot name `-p` for a suite that has no password argument.

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

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from report import Failure, check, detail, suite_fail, suite_ok  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
RUNNER_PATH = os.path.join(ROOT, "run-tests")
SEARCHED = ("tests/e2e", "tests/perf", "tests/soak")

NAME = "registry_test"

# What run-tests substitutes into a suite's argument template before starting
# it. A token outside this set would reach the suite as a literal.
TOKENS = {"@HOST@", "@PASS@", "@TIMEOUT@", "@MODE@", "@SOAKPROFILE@"}

# Suites the registry deliberately does not carry, each with the reason. A file
# ending in _test.py that is not a suite belongs here rather than in the
# registry, so that "not registered" always means "nobody can run it".
NOT_SUITES = {
    # Registered under tests/lib rather than under the directories searched
    # here; they check the tree or the build, not the device.
    "tests/lib/lint_test.py": "registered from tests/lib",
    "tests/lib/registry_test.py": "this file",
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
HELPER_SOURCES = ("tests/lib/report.py", "tests/e2e/lib/ui_backend.py",
                  "tests/lib/targets.py")


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
    return options


# Options a suite gets without registering them itself: argparse's own.
INHERITED = {"-h", "--help"}


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

    if failed:
        suite_fail(NAME, f"{failed} of 4 checks failed")
        return 1
    suite_ok(NAME, f"{len(registered)} suites")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
