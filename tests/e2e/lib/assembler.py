#!/usr/bin/env python3
"""Assemble a 6502 source into a .prg, using the assembler in this repository.

Several suites run small C64 programs as their stimulus. They build them here
rather than committing the assembled binary, so the source beside the test is
the thing that runs and the two cannot drift apart.

The assembler's source is in `tools/64tass`, and `make -C tools`, which every
firmware build runs, builds it. The binary itself is not committed, so a
checkout that has not built firmware has no `tools/64tass/64tass`; `assemble`
builds it from that source the first time it is needed, which takes about ten
seconds once. There is no fallback to another assembler on purpose: a different
64tass would be a different fixture, and a suite that quietly assembled with a
`64tass` from the PATH would be reporting on something other than what the
repository contains.

tests/e2e/io/printer takes the other approach and commits its .prg with a
Makefile to regenerate it, so that suite needs no assembler at all. Both are
reasonable; this is for the ones that would rather not commit a binary.
"""

import fcntl
import os
import subprocess
import tempfile
from typing import Union
from collections.abc import Mapping

from report import Failure

REPO_ROOT = os.path.abspath(os.path.join(
    os.path.dirname(os.path.abspath(__file__)), "..", "..", ".."))
ASSEMBLER = os.path.join(REPO_ROOT, "tools", "64tass", "64tass")
ASSEMBLER_SOURCE_DIR = os.path.dirname(ASSEMBLER)
ASSEMBLE_TIMEOUT_SECONDS = 120.0
BUILD_TIMEOUT_SECONDS = 600.0
VERSION_TIMEOUT_SECONDS = 30.0


def assembler_runs() -> bool:
    """Whether the binary at ASSEMBLER is one this machine can actually run.

    Its presence is not enough. A firmware build under docker on a bind-mounted
    tree leaves a Linux binary at that path, which the host then cannot execute,
    and the failure surfaces from assemble() as `Exec format error` rather than
    as anything about the build.
    """
    try:
        return subprocess.run(
            [ASSEMBLER, "--version"], capture_output=True,
            timeout=VERSION_TIMEOUT_SECONDS, check=False).returncode == 0
    except (OSError, subprocess.SubprocessError):
        return False


def ensure_assembler() -> None:
    """Build `tools/64tass/64tass` when it is absent, or cannot run here.

    The same make that a firmware build runs, so the binary is the one the
    repository would have produced anyway, and a fresh checkout does not fail
    every assembling suite for want of a build step nobody was told about.
    """
    if assembler_runs():
        return
    # A run naming several targets starts one process per target, and on a
    # fresh checkout two of them can reach an assembling suite together. The
    # lock makes the second wait for the first's make rather than run its own
    # over the same object files; it then finds the binary and returns.
    with open(os.path.join(ASSEMBLER_SOURCE_DIR, ".build-lock"), "w") as lock:
        fcntl.flock(lock, fcntl.LOCK_EX)
        if assembler_runs():
            return
        # Anything already here was built for another platform, so it can be
        # neither run nor linked against. `make` alone would keep the objects
        # and relink them, failing with "unknown file type", so clear both
        # first. On a fresh checkout there is nothing to clear and this costs
        # one no-op make.
        subprocess.run(["make", "-C", ASSEMBLER_SOURCE_DIR, "clean"],
                       capture_output=True, timeout=BUILD_TIMEOUT_SECONDS, check=False)
        try:
            os.remove(ASSEMBLER)
        except OSError:
            pass
        try:
            result = subprocess.run(
                ["make", "-C", ASSEMBLER_SOURCE_DIR],
                capture_output=True, text=True, timeout=BUILD_TIMEOUT_SECONDS, check=False)
        except (OSError, subprocess.SubprocessError) as exc:
            raise Failure(f"could not build {ASSEMBLER}: {exc}") from exc
        if result.returncode or not assembler_runs():
            raise Failure(f"{ASSEMBLER} is missing, or not runnable here, and "
                          f"`make -C tools/64tass` did not produce one: "
                          f"{(result.stderr or result.stdout).strip()[:400]}")


def assemble(source: Union[str, "os.PathLike[str]"],
             defines: Mapping[str, object] | None = None) -> bytes:
    """Return the assembled program, with its two-byte load address in front.

    `defines` becomes 64tass -D arguments. A source shared with a build that is
    not this suite's uses them to guard what only a run on hardware needs, so
    there is one program rather than a copy per caller.
    """
    source = os.path.abspath(os.fspath(source))
    if not os.path.exists(source):
        raise Failure(f"no such 6502 source: {source}")
    ensure_assembler()
    name = os.path.basename(source)
    with tempfile.TemporaryDirectory(prefix="c64-asm-") as directory:
        output = os.path.join(directory, "fixture.prg")
        try:
            command = [ASSEMBLER, "-q", "--cbm-prg"]
            for name, value in (defines or {}).items():
                command += ["-D", f"{name}={value}"]
            result = subprocess.run(
                [*command, "-o", output, source],
                capture_output=True, text=True, timeout=ASSEMBLE_TIMEOUT_SECONDS, check=False)
        except (OSError, subprocess.SubprocessError) as exc:
            raise Failure(f"could not run {ASSEMBLER}: {exc}") from exc
        if result.returncode:
            raise Failure(f"64tass failed for {name}: "
                          f"{(result.stderr or result.stdout).strip()[:400]}")
        with open(output, "rb") as handle:
            program = handle.read()
    if len(program) < 3:
        raise Failure(f"64tass produced {len(program)} bytes for {name}, which is not a program")
    return program
