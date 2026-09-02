#!/usr/bin/env python3
# Regenerate the parts of the test documentation that a command already prints.

"""Splice `run-tests` output into tests/README.md, between stable markers.

The profile matrix in tests/README.md is the answer to "which suites does each
profile select", and `run-tests --list-profiles` already computes it from the
registry. A hand-maintained copy of that answer goes stale the first time a
suite is added or retagged, and nothing notices, so the copy is generated here
instead and the markers say which lines are generated.

Every block below names one command and one marker pair. Running this script
rewrites each block; `--check` rewrites nothing and exits non-zero when a block
is out of date, which is what a CI step would call. Both are idempotent: the
generated text is a function of the command's output alone, so a second run
after a first changes nothing.

    tools/docs/update_test_docs.py            # rewrite what has changed
    tools/docs/update_test_docs.py --check    # say so instead, and exit 1
"""

from __future__ import annotations

import argparse
import io
import os
import subprocess
import sys
from typing import List, NamedTuple, Tuple

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))


class Block(NamedTuple):
    """One generated region of one file."""

    path: str          # relative to the repository root
    marker: str        # the name inside the HTML comments
    command: List[str] # what produces the text, run from the repository root
                       # with this interpreter, so a checkout needs no PATH
    language: str      # the fenced code block's language tag, or ""


BLOCKS: Tuple[Block, ...] = (
    Block(path="tests/README.md",
          marker="profile-matrix",
          command=["run-tests", "--list-profiles"],
          language=""),
)


def begin(marker: str) -> str:
    return f"<!-- BEGIN: {marker} -->"


def end(marker: str) -> str:
    return f"<!-- END: {marker} -->"


def render(block: Block, output: str) -> str:
    """The whole region, markers included, for this command output."""
    body = output.replace("\r\n", "\n").rstrip("\n")
    return (f"{begin(block.marker)}\n"
            f"```{block.language}\n"
            f"{body}\n"
            "```\n"
            f"{end(block.marker)}")


def capture(block: Block) -> str:
    """What the command prints, or a message and a non-zero exit."""
    try:
        done = subprocess.run([sys.executable] + block.command,
                              cwd=ROOT, check=False,
                              stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    except OSError as exc:
        raise SystemExit(f"{' '.join(block.command)}: {exc}")
    if done.returncode != 0:
        sys.stderr.write(done.stderr.decode("utf-8", "replace"))
        raise SystemExit(f"{' '.join(block.command)} exited "
                         f"{done.returncode}")
    return done.stdout.decode("utf-8", "replace")


def splice(text: str, block: Block, region: str) -> str:
    """`text` with this block's region replaced. The markers must be there."""
    opening, closing = begin(block.marker), end(block.marker)
    start = text.find(opening)
    if start < 0:
        raise SystemExit(f"{block.path}: no {opening}")
    stop = text.find(closing, start)
    if stop < 0:
        raise SystemExit(f"{block.path}: no {closing} after {opening}")
    return text[:start] + region + text[stop + len(closing):]


def update(check_only: bool) -> int:
    """Rewrite every block, or report which are stale. The process status."""
    stale = 0
    for block in BLOCKS:
        path = os.path.join(ROOT, block.path)
        try:
            before = io.open(path, encoding="utf-8").read()
        except OSError as exc:
            raise SystemExit(f"{block.path}: {exc}")
        after = splice(before, block, render(block, capture(block)))
        what = f"{block.path}: {block.marker}, from {' '.join(block.command)}"
        if after == before:
            print(f"unchanged  {what}")
            continue
        stale += 1
        if check_only:
            print(f"OUT OF DATE  {what}")
            continue
        io.open(path, "w", encoding="utf-8").write(after)
        print(f"rewrote    {what}")
    if check_only and stale:
        print(f"\n{stale} block(s) out of date. Run "
              "tools/docs/update_test_docs.py to fix.")
        return 1
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Regenerate the generated blocks in the test documentation.",
        epilog="Needs no device: every command it runs reads the registry.")
    parser.add_argument("--check", action="store_true",
                        help="Change nothing. Exit 1 when a block is out of "
                             "date, so CI can call it.")
    return update(parser.parse_args().check)


if __name__ == "__main__":
    sys.exit(main())
