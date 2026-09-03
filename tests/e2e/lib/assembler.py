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
from typing import Mapping, Optional, Union

from report import Failure

REPO_ROOT = os.path.abspath(os.path.join(
    os.path.dirname(os.path.abspath(__file__)), "..", "..", ".."))
ASSEMBLER = os.path.join(REPO_ROOT, "tools", "64tass", "64tass")
ASSEMBLER_SOURCE_DIR = os.path.dirname(ASSEMBLER)
ASSEMBLE_TIMEOUT_SECONDS = 120.0
BUILD_TIMEOUT_SECONDS = 600.0


def ensure_assembler() -> None:
    """Build `tools/64tass/64tass` from the source beside it when it is absent.

    The same make that a firmware build runs, so the binary is the one the
    repository would have produced anyway, and a fresh checkout does not fail
    every assembling suite for want of a build step nobody was told about.
    """
    if os.path.exists(ASSEMBLER):
        return
    # A run naming several targets starts one process per target, and on a
    # fresh checkout two of them can reach an assembling suite together. The
    # lock makes the second wait for the first's make rather than run its own
    # over the same object files; it then finds the binary and returns.
    with open(os.path.join(ASSEMBLER_SOURCE_DIR, ".build-lock"), "w") as lock:
        fcntl.flock(lock, fcntl.LOCK_EX)
        if os.path.exists(ASSEMBLER):
            return
        try:
            result = subprocess.run(
                ["make", "-C", ASSEMBLER_SOURCE_DIR],
                capture_output=True, text=True, timeout=BUILD_TIMEOUT_SECONDS)
        except (OSError, subprocess.SubprocessError) as exc:
            raise Failure(f"could not build {ASSEMBLER}: {exc}") from exc
        if result.returncode or not os.path.exists(ASSEMBLER):
            raise Failure(f"{ASSEMBLER} is missing and `make -C tools/64tass` did "
                          f"not produce it: "
                          f"{(result.stderr or result.stdout).strip()[:400]}")


def assemble(source: Union[str, "os.PathLike[str]"],
             defines: Optional[Mapping[str, object]] = None) -> bytes:
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
                command + ["-o", output, source],
                capture_output=True, text=True, timeout=ASSEMBLE_TIMEOUT_SECONDS)
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
