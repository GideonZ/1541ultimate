#!/usr/bin/env python3
"""Maps every requirement of the Software IEC specification to the tests that name it.

Each test names the requirement it holds the drive to: a Suite11 case is called after it,
the parser cases carry it in a comment, and a hardware check puts it in the label a run
prints. This walks the test sources, attributes every reference to the case it sits in,
and prints one row per requirement, so the specification's traceability table is generated
from the tests rather than maintained by hand.

    python3 tools/softiec_traceability.py [repository root]
"""
import os, re, sys

# Each source, and the name of the suite it belongs to in a run.
SOURCES = {
    'software/io/iec/cbmdos_parser_test.cc': 'parse',
    'software/test/iecdrive/testdrive.cc': 'iecdrive',
    'tests/e2e/io/iec/dos_command_test.py': 'iec-dos-commands',
    'tests/e2e/io/iec/rel_copy_test.py': 'rel-copy',
    'tests/soak/io/iec/softiec_soak_test.py': 'softiec-soak',
    'tests/e2e/filemanager/prg_context_menu_test.py': 'prg-context-menu',
    'tests/e2e/api/prg_load_path_trim_test.py': 'prg-load-path-trim',
}

# A Python test can be a method of a class, so its def may be indented.
FUNCTION = re.compile(r'^(?:static\s+)?(?:void|int|bool)\s+(\w+)\s*\(|^\s*def\s+(\w+)\s*\(')
CASE_NAME = re.compile(r'testname\s*=\s*"([^"]+)"|"(Suite\d+[A-Za-z0-9_-]+)"')


def scan(root):
    """{requirement: {what checks it}}, where "what" is a case name or a suite name.

    A reference is attributed to the test it sits in, which is the function it is inside
    together with the comment above that function, because a case's reason is written
    there rather than in its body.
    """
    found = {}
    for path, suite in SOURCES.items():
        full = os.path.join(root, path)
        if not os.path.exists(full):
            continue
        lines = open(full, errors='replace').read().split('\n')
        starts = [i for i, l in enumerate(lines) if FUNCTION.match(l)]
        blocks = []
        for n, start in enumerate(starts):
            head = start
            while head > 0 and lines[head - 1].lstrip().startswith(('//', '#')):
                head -= 1
            end = starts[n + 1] if n + 1 < len(starts) else len(lines)
            # The comment of the next function belongs to it, not to this one.
            tail = end
            while tail > start and lines[tail - 1].lstrip().startswith(('//', '#')):
                tail -= 1
            blocks.append((head, tail))
        for head, tail in blocks:
            body = '\n'.join(lines[head:tail])
            reqs = set(re.findall(r'SI-(\d+[a-z]?)', body))
            if not reqs:
                continue
            m = CASE_NAME.search(body)
            name = (m.group(1) or m.group(2)) if m else None
            for req in reqs:
                found.setdefault('SI-' + req, set()).add(
                    name if name and name.startswith('Suite') else suite)
    return found


def main(root='.'):
    spec = open(os.path.join(root, 'doc/softiec_compatibility_spec.md')).read()
    found = scan(root)
    for req in re.findall(r"\*\*(SI-\d+[a-z]?)(?: \([^)]+\))?\.", spec):
        print(f"{req}\t{', '.join(sorted(found.get(req, []))) or '-'}")


if __name__ == '__main__':
    main(sys.argv[1] if len(sys.argv) > 1 else '.')
