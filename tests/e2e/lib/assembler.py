#!/usr/bin/env python3
"""Assemble a 6502 source into a .prg, using the assembler in this repository.

Several suites run small C64 programs as their stimulus. They build them here
rather than committing the assembled binary, so the source beside the test is
the thing that runs and the two cannot drift apart.

`tools/64tass/64tass` is committed and is also built by `make -C tools`, which
every firmware build runs, so a checkout that can build firmware can run these
suites. There is no fallback to another assembler on purpose: a different 64tass
would be a different fixture, and a suite that quietly assembled with one would
be reporting on something other than what the repository contains.

tests/e2e/io/printer takes the other approach and commits its .prg with a
Makefile to regenerate it, so that suite needs no assembler at all. Both are
reasonable; this is for the ones that would rather not commit a binary.
"""

import os
import subprocess
import tempfile
from typing import Union

from report import Failure

REPO_ROOT = os.path.abspath(os.path.join(
    os.path.dirname(os.path.abspath(__file__)), "..", "..", ".."))
ASSEMBLER = os.path.join(REPO_ROOT, "tools", "64tass", "64tass")
ASSEMBLE_TIMEOUT_SECONDS = 120.0


def assemble(source: Union[str, "os.PathLike[str]"]) -> bytes:
    """Return the assembled program, with its two-byte load address in front."""
    source = os.path.abspath(os.fspath(source))
    if not os.path.exists(source):
        raise Failure(f"no such 6502 source: {source}")
    if not os.path.exists(ASSEMBLER):
        raise Failure(f"{ASSEMBLER} is missing; run `make -C tools` to build it")
    name = os.path.basename(source)
    with tempfile.TemporaryDirectory(prefix="c64-asm-") as directory:
        output = os.path.join(directory, "fixture.prg")
        try:
            result = subprocess.run(
                [ASSEMBLER, "-q", "--cbm-prg", "-o", output, source],
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
