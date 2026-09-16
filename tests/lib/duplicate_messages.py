#!/usr/bin/env python3
"""Report diagnostic messages that appear in more than one file under tests/.

Not a suite and not wired into anything. A duplicated diagnostic is the one
kind of duplication guaranteed to be read by a person under time pressure, and
two copies drift: the wedge message in `Uci.wait_for_reply` claimed that only a
firmware restart or power cycle releases the command interface, in both copies,
after that had been measured to be false.

There is deliberately no guard test behind this. It found 40 texts on
2026-09-04, so a guard would need an allowlist of forty entries, and an
allowlist grown under deadline pressure ratchets in the wrong direction. A scan
somebody chooses to run is honest about being a prompt rather than a gate.

What it found on 2026-09-04, both structural rather than copied strings:

  - two `Uci` classes, `tests/e2e/lib/uci.py` and the private one in
    `tests/e2e/io/command_interface/uci_targets_test.py`, one protocol over
    two register accessors
  - `tests/e2e/u64ctrl/power_cycle_test.py` and `wake_on_wifi_test.py`,
    86 shared lines of 225 and 345

The thresholds are tuned against this corpus and re-deriving them is most of
the work, so they are written down rather than left to a future reader:
MIN_CHARACTERS and MIN_WORDS below keep out labels, format fragments and short
argparse help, which are repeated legitimately and in bulk.

What it deliberately does not do: it does not look inside .md or .sh files, it
does not compare texts that differ by a word, and it makes no judgement about
whether a duplicate is worth removing. Two copies of a message can be right
when the code around them is genuinely separate.

    python3 tests/lib/duplicate_messages.py [--min-words N] [--root DIR]
"""

from __future__ import annotations

import argparse
import ast
import collections
import pathlib
import sys

MIN_CHARACTERS = 40
MIN_WORDS = 7


def literal_text(node: ast.AST) -> str | None:
    """The text of a plain string, or the literal parts of an f-string.

    The interpolations are dropped rather than rendered, so an f-string is
    matched on its fixed words. Two messages that differ only in a value are
    the same message for this purpose.
    """
    if isinstance(node, ast.Constant) and isinstance(node.value, str):
        return node.value
    if isinstance(node, ast.JoinedStr):
        return "".join(part.value for part in node.values
                       if isinstance(part, ast.Constant)
                       and isinstance(part.value, str))
    return None


def scan(root: pathlib.Path, min_words: int) -> dict[str, set[str]]:
    found: dict[str, set[str]] = collections.defaultdict(set)
    for path in sorted(root.rglob("*.py")):
        try:
            tree = ast.parse(path.read_text(encoding="utf-8"))
        except (SyntaxError, UnicodeDecodeError):
            continue
        for node in ast.walk(tree):
            text = literal_text(node)
            if not text:
                continue
            collapsed = " ".join(text.split())
            if (len(collapsed) >= MIN_CHARACTERS
                    and len(collapsed.split()) >= min_words):
                found[collapsed].add(str(path.relative_to(root)))
    return {text: files for text, files in found.items() if len(files) > 1}


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--root", type=pathlib.Path,
                        default=pathlib.Path(__file__).resolve().parents[1],
                        help="Directory to scan (default: tests/).")
    parser.add_argument("--min-words", type=int, default=MIN_WORDS,
                        help=f"Words a text needs to count (default: {MIN_WORDS}).")
    args = parser.parse_args(argv)

    duplicates = scan(args.root, args.min_words)
    print(f"{len(duplicates)} message texts appear in more than one file "
          f"under {args.root}")
    for text, files in sorted(duplicates.items(), key=lambda item: -len(item[0])):
        print()
        print(f"  {', '.join(sorted(files))}")
        print(f"    {text[:160]}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
