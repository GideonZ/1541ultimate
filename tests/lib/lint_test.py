#!/usr/bin/env python3
# Gate check: the tests tree passes the lint configuration it ships with.

"""Run ruff over tests/ and run-tests with the committed configuration.

The tree had no lint configuration, so 2,804 findings had accumulated: 799
`typing.List` annotations, 372 `Optional[X]`, 140 semicolon-joined statements
in one soak suite, unused imports, dead assignments, and an `except Failure:`
that named an exception the module never imported and so raised `NameError`
instead of handling it.

Those are fixed. This is what keeps them fixed. `tests/ruff.toml` records
which rule families are enforced and, for each one that is not, either the
finding in doc/research/tests-review/tests-review.md that removes it or the
reason it is deliberately off.

Needs no device, so it runs in the device-free group and costs a second.
"""

import os
import shutil
import subprocess
import sys
from pathlib import Path

# The one stanza that puts the shared library on sys.path; see tests/lib/bootstrap.py.
sys.path.insert(0, str(next(p for p in Path(__file__).resolve().parents
                            if (p / "tests" / "lib").is_dir()) / "tests" / "lib"))
import bootstrap  # noqa: E402,F401
import cli  # noqa: E402
from report import detail, suite_fail, suite_ok, suite_skip  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
CONFIG = os.path.join(ROOT, "tests", "ruff.toml")
TARGETS = ("tests", "run-tests")

NAME = "lint_test"


def ruff_command():
    """The ruff to run, or None when the host has none.

    Preferred as an installed executable; `python3 -m ruff` covers a host that
    has it as a library only.
    """
    found = shutil.which("ruff")
    if found:
        return [found]
    probe = subprocess.run([sys.executable, "-m", "ruff", "--version"],
                           capture_output=True, text=True, check=False)
    if probe.returncode == 0:
        return [sys.executable, "-m", "ruff"]
    return None


def main():
    cli.device_free_arguments(__doc__)

    command = ruff_command()
    if command is None:
        # Skipped rather than failed: a bench without ruff can still run every
        # suite that drives a device, and the build workflow installs it.
        suite_skip(NAME, "ruff is not installed (pip install ruff)")
        return 0

    completed = subprocess.run(
        [*command, "check", "--config", CONFIG, "--output-format", "concise",
         *TARGETS],
        cwd=ROOT, capture_output=True, text=True, check=False)
    lines = [line for line in completed.stdout.splitlines() if line.strip()]

    if completed.returncode == 0:
        version = subprocess.run([*command, "--version"], capture_output=True,
                                 text=True, check=False).stdout.strip()
        suite_ok(NAME, f"{version or 'ruff'} over {', '.join(TARGETS)}")
        return 0

    # A crash (bad configuration, unreadable file) is not a lint finding, and
    # reporting it as one would send a reader looking for code to change.
    findings = [line for line in lines if ": " in line]
    if not findings:
        suite_fail(NAME, f"ruff exited {completed.returncode} without findings")
        for line in lines[:20] or completed.stderr.splitlines()[:20]:
            detail(line)
        detail(f"config: {os.path.relpath(CONFIG, ROOT)}")
        return 1

    suite_fail(NAME, f"{len(findings)} lint finding(s)")
    for line in findings[:40]:
        detail(line)
    if len(findings) > 40:
        detail(f"... and {len(findings) - 40} more")
    detail(f"Run: ruff check --config {os.path.relpath(CONFIG, ROOT)} "
           f"{' '.join(TARGETS)}")
    detail("Add a rule to the ignore list in that file only with the reason, "
           "as the entries there do.")
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
