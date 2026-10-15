import hashlib
import os
import platform
import socket
import subprocess
from collections.abc import Sequence

import machine as machine_lib
from runtests.constants import ROOT


def git_answer(*arguments: str) -> str | None:
    """One git query about this checkout, or None when git does not answer.

    Run with the repository root as its working directory rather than with
    whatever the operator started the run in: this checkout is frequently a
    git worktree, so the process's own directory says nothing useful.

    None and "" are different answers: `git status --porcelain` is empty for a
    clean tree, and absent for a directory git will not talk about.
    """
    try:
        completed = subprocess.run(("git", *arguments), cwd=ROOT,
                                   capture_output=True, text=True, timeout=10, check=False)
    except (OSError, subprocess.TimeoutExpired):
        return None
    if completed.returncode != 0:
        return None
    return completed.stdout.strip()


def run_identity(argv: Sequence[str], started: float) -> dict[str, object]:
    """What this run is a run of, for the `run` record.

    A downloaded artifact has to say what produced it without a second file.
    Three git calls, all cheap, all on the host, none reaching the device; a
    checkout where git does not answer loses those fields rather than the run.
    """
    fields: dict[str, object] = {
        "host": socket.gethostname(),
        "python": platform.python_version(),
        # report.mask_secret has been told the password, so the record writer
        # takes it out of this and out of every other field it writes.
        "argv": list(argv),
        "started": started,
        # What this run assumed rather than proved. tests/lib/machine.py
        # reports a check tagged with a fix its machine lacks as SKIP, and
        # --assume-fix runs it anyway, so a reader who cannot see the
        # assumptions cannot tell a suite that passed from one that skipped
        # the part that would have failed.
        #
        # Read from the environment rather than from this process's flags,
        # because that is what is in force: --assume-fix reaches a child run
        # and every suite through ASSUME_ENV, so a child, and a run started
        # with the variable already exported, both have assumptions and
        # neither has the flag.
        "assumptions": sorted(machine_lib.parse_assumptions(
            os.environ.get(machine_lib.ASSUME_ENV, ""))),
    }
    commit = os.environ.get("GITHUB_SHA") or git_answer("rev-parse", "HEAD")
    if commit:
        fields["commit"] = commit
    branch = os.environ.get("GITHUB_REF_NAME") or \
        git_answer("rev-parse", "--abbrev-ref", "HEAD")
    if branch:
        fields["branch"] = branch
    # The only join from a downloaded artifact back to the build page that
    # produced it, and it comes from the environment alone: an identifier the
    # runner generated for itself could not be traced back to anything, which
    # is worse than none.
    for name, variable in (("ci_run_id", "GITHUB_RUN_ID"),
                           ("ci_run_attempt", "GITHUB_RUN_ATTEMPT")):
        value = os.environ.get(variable)
        if value:
            fields[name] = value
    status = git_answer("status", "--porcelain")
    if status is not None:
        # Not `dirty`: report.run_result already spends that name on the count
        # of suites that left the UI outside its documented state, and naming
        # one field twice with two meanings is worse than a longer name.
        fields["worktree_dirty"] = bool(status)
    tree = harness_hash()
    if tree:
        fields["harness"] = tree
    return fields


# What a run reads its own behaviour out of. Every suite is a separate process
# started from the working tree, so these files are read while the run is in
# progress rather than copied at the start.
HARNESS_PATHS = ("run-tests", "tests", "tools")


def harness_hash() -> str:
    """One hash over the harness files this run reads, as they are on disk.

    A suite is started from the working tree, so editing one of these files
    while a run is in progress means the later suites ran different code from
    the earlier ones. Nothing in the artefacts says so, and a reader cannot
    tell such a run from a run of one revision. Measured here rather than
    trusted: the hash is taken when the run starts and again when it ends, and
    a run whose harness moved says so on its own record.

    `git status --porcelain` cannot answer this. It lists which files are
    modified, not what is in them, so editing a file that was already modified
    leaves its output identical.

    Tracked files only, because untracked ones are scratch: a run writing its
    own output under the tree would otherwise report its harness as changing
    every time it wrote a line.
    """
    listing = git_answer("ls-files", "-z", *HARNESS_PATHS)
    if not listing:
        return ""
    digest = hashlib.sha256()
    for name in sorted(listing.split("\0")):
        if not name:
            continue
        digest.update(name.encode("utf-8", "replace"))
        try:
            with open(os.path.join(ROOT, name), "rb") as handle:
                digest.update(handle.read())
        except OSError:
            # A tracked file that is not on disk is itself a difference, and
            # naming it in the hash is what makes it one.
            digest.update(b"\0missing")
    return digest.hexdigest()[:16]
