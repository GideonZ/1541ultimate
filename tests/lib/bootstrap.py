#!/usr/bin/env python3
# Puts the test tree's shared directories on sys.path, in one order.

"""One import that makes the shared test library importable.

`tests/lib` and `tests/e2e/lib` are not packages, so before this module existed
every entry point pushed one or two directories onto `sys.path` itself. There
were 133 of those across 69 files in about twenty spellings, differing by author
and by how deep in the tree the file sat: `parents[1]`, `parents[2]`,
`parents[3]`, `SCRIPT_DIR.parent`, `os.path.join(HERE, "..", "lib")`. Moving a
suite one directory changed which index was right, and a wrong one failed at
import with a `ModuleNotFoundError` naming a module that plainly exists.

Every entry point now carries the same four lines, whatever its depth:

    sys.path[:0] = [str(p / "tests" / "lib")
                    for p in Path(__file__).resolve().parents
                    if (p / "tests" / "lib").is_dir()][:1]
    import bootstrap  # noqa: E402,F401

The search walks up from the file rather than counting directories, so a suite
that moves needs no edit. Importing this module then adds `tests/e2e/lib`.

Order matters and is fixed here: `tests/lib` comes first, so a future name that
exists in both directories resolves to the shared library rather than to
whichever insert happened to run last. Before this, `run-tests` inserted both at
index 0 and `tests/e2e/lib` therefore won.

A suite that imports another suite adds that directory itself:

    sys.path.insert(0, bootstrap.directory("e2e", "io", "c64"))
"""

import os
import sys

# .../tests, from this file's own location rather than from a caller's guess.
TESTS = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
LIB = os.path.join(TESTS, "lib")
E2E_LIB = os.path.join(TESTS, "e2e", "lib")


def directory(*parts: str) -> str:
    """Where one directory of the test tree is, for a suite that imports another.

    `parts` are relative to `tests/`, so `directory("e2e", "io", "c64")` names
    `tests/e2e/io/c64`. This only answers the question; the caller does the
    inserting, because `sys.path.insert(...)` is what says "a path adjustment,
    not ordinary code" to a reader and to the lint:

        sys.path.insert(0, bootstrap.directory("e2e", "api"))
    """
    return os.path.join(TESTS, *parts)


def _establish() -> None:
    """tests/e2e/lib after tests/lib, so tests/lib is searched first."""
    for directory in (E2E_LIB, LIB):
        if directory not in sys.path:
            sys.path.insert(0, directory)


_establish()
