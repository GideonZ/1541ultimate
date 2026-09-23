#!/usr/bin/env python3
"""Report identical blocks of code, so a change can be checked for copies of itself.

A block is a run of `--min` significant lines: comments, blank lines and lines that
are only punctuation are ignored when blocks are compared, and leading whitespace is
dropped, so a copy that was reindented is still reported. With `--since <ref>` only
blocks that at least one commit after <ref> introduced are listed, which is what makes
this useful on a branch: the report is what this branch duplicated, not what the tree
has always held.

    python3 tools/find_duplicate_blocks.py --since master software tests tools
"""
from __future__ import annotations

import argparse
import hashlib
import os
import re
import subprocess
import sys

SUFFIXES = (".c", ".cc", ".cpp", ".h", ".hpp", ".py", ".s", ".asm", ".vhd")
COMMENT = re.compile(r"^\s*(//|#|;|--|\*|/\*)")
PUNCT_ONLY = re.compile(r"^[\s{}()\[\];:,]*$")


def significant(path: str) -> list[tuple[int, str]]:
    """The lines that carry meaning, with their 1-based numbers."""
    out = []
    try:
        with open(path, errors="replace") as handle:
            for number, raw in enumerate(handle, 1):
                line = raw.strip()
                if not line or COMMENT.match(line) or PUNCT_ONLY.match(line):
                    continue
                out.append((number, " ".join(line.split())))
    except OSError:
        pass
    return out


def files_under(roots: list[str]) -> list[str]:
    found = []
    for root in roots:
        if os.path.isfile(root):
            found.append(root)
            continue
        for base, dirs, names in os.walk(root):
            dirs[:] = [d for d in dirs
                       if d not in (".git", "output", "result", "__pycache__", "lwip", "FreeRTOS")]
            found += [os.path.join(base, n) for n in names if n.endswith(SUFFIXES)]
    return found


def added_lines(ref: str, path: str) -> set[int]:
    """The lines of `path` that are new since `ref`, by number in the working tree."""
    diff = subprocess.run(["git", "diff", "-U0", f"{ref}...HEAD", "--", path],
                          capture_output=True, text=True).stdout
    new: set[int] = set()
    for hunk in re.finditer(r"^@@ -\S+ \+(\d+)(?:,(\d+))? @@", diff, re.M):
        start = int(hunk.group(1))
        count = int(hunk.group(2) or 1)
        new.update(range(start, start + count))
    return new


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("roots", nargs="+")
    parser.add_argument("--min", type=int, default=6, help="significant lines in a block")
    parser.add_argument("--since", help="only blocks introduced after this ref")
    args = parser.parse_args()

    blocks: dict[str, list[tuple[str, int, int]]] = {}
    for path in files_under(args.roots):
        lines = significant(path)
        for i in range(len(lines) - args.min + 1):
            window = lines[i:i + args.min]
            digest = hashlib.sha1("\n".join(text for _, text in window).encode()).hexdigest()
            blocks.setdefault(digest, []).append((path, window[0][0], window[-1][0]))

    groups = [places for places in blocks.values() if len(places) > 1]
    # Keep the longest run of each repeat: a duplicated block of 20 lines would otherwise
    # be reported once for every window inside it.
    groups.sort(key=lambda places: (places[0][0], places[0][1]))
    kept: list[list[tuple[str, int, int]]] = []
    for places in groups:
        if kept and len(kept[-1]) == len(places) and all(
                a[0] == b[0] and b[1] <= a[2] + 1 for a, b in zip(kept[-1], places)):
            kept[-1] = [(p, s, max(e, n)) for (p, s, e), (_, _, n) in zip(kept[-1], places)]
        else:
            kept.append(list(places))

    new_by_path: dict[str, set[int]] = {}
    reported = 0
    for places in kept:
        if args.since:
            fresh = False
            for path, start, end in places:
                if path not in new_by_path:
                    new_by_path[path] = added_lines(args.since, path)
                if new_by_path[path] & set(range(start, end + 1)):
                    fresh = True
            if not fresh:
                continue
        reported += 1
        print(f"{len(places)} copies, {places[0][2] - places[0][1] + 1} lines:")
        for path, start, end in places:
            print(f"    {path}:{start}-{end}")
    print(f"{reported} duplicated block(s)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
