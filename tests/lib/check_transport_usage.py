#!/usr/bin/env python3
# Gate check: every suite reaches the device through tests/lib/rest.py.

"""Fail if a suite talks HTTP to the device without the shared retry policy.

Seven suites had each grown their own HTTP client, and every one of them got the
retry rule wrong in a different way: some never retried a POST at all, some
retried nothing, and one decided by whether the request carried a body rather
than by whether it had been sent. Each was found the same way, by a single
transient timeout failing an entire run, and each was fixed separately.

This makes that class of defect visible at the gate instead. A call into
`urllib.request.urlopen` or `http.client.HTTPConnection` from anywhere but the
library is a new copy of a rule that has one correct version, in
`rest.may_retry`.

Needs no device, so it runs first and costs nothing.
"""

import ast
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from report import detail, suite_fail, suite_ok  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
TESTS = os.path.join(ROOT, "tests")
LIBRARY = os.path.join(TESTS, "lib", "rest.py")

# Suites whose subject *is* the transport, so they must reach it directly. Each
# needs a reason, because "it was easier" is how the copies got here.
EXEMPT = {
    # Measures how the device behaves when connections are abandoned or half
    # open, so retrying would paper over exactly what it is checking.
    os.path.join(TESTS, "e2e", "network", "telnet_stale_session_test.py"):
        "measures abandoned connections; a retry would hide the subject",
    os.path.join(TESTS, "soak", "network", "listener_soak_test.py"):
        "counts transport failures as its result",
    os.path.join(TESTS, "soak", "network", "http_probe.py"):
        "drives HTTP itself to measure how the server behaves under stress",
    # Keeps its own attempt loop so it can pace itself between attempts: it is
    # the suite that saturates the connection pool, and a fixed pause would add
    # churn to an already saturated one. It still takes the decision from
    # rest.may_retry, which the check below verifies.
    os.path.join(TESTS, "e2e", "filesystem", "ftp_client_test.py"):
        "paces its own retries around the connection pool; uses rest.may_retry",
    # The one place the policy lives.
    LIBRARY: "defines the policy",
}

BANNED = {
    ("urllib", "request", "urlopen"): "urllib.request.urlopen",
    ("http", "client", "HTTPConnection"): "http.client.HTTPConnection",
}


def attribute_path(node):
    """Dotted name for an attribute chain, or None if it is not a plain one."""
    parts = []
    while isinstance(node, ast.Attribute):
        parts.append(node.attr)
        node = node.value
    if not isinstance(node, ast.Name):
        return None
    parts.append(node.id)
    return tuple(reversed(parts))


def offenders(path):
    try:
        tree = ast.parse(open(path, encoding="utf-8").read(), filename=path)
    except SyntaxError as exc:
        return [(getattr(exc, "lineno", 0), f"could not be parsed: {exc}")]
    found = []
    for node in ast.walk(tree):
        if not isinstance(node, ast.Call):
            continue
        name = attribute_path(node.func)
        if name and name in BANNED:
            found.append((node.lineno, BANNED[name]))
    return found


# An exempt file that runs its own loop must still take the decision from the
# library, or the rule has been copied after all.
MUST_USE_POLICY = {
    os.path.join(TESTS, "e2e", "filesystem", "ftp_client_test.py"),
}


def main():
    problems = []
    scanned = 0
    for path in sorted(MUST_USE_POLICY):
        if "may_retry" not in open(path, encoding="utf-8").read():
            problems.append((os.path.relpath(path, ROOT), 0,
                             "its own retry loop without rest.may_retry"))
    for base, _dirs, files in os.walk(TESTS):
        if "__pycache__" in base:
            continue
        for name in sorted(files):
            if not name.endswith(".py"):
                continue
            path = os.path.join(base, name)
            if path in EXEMPT:
                continue
            scanned += 1
            for line, what in offenders(path):
                problems.append((os.path.relpath(path, ROOT), line, what))

    if problems:
        suite_fail("check_transport_usage",
                   f"{len(problems)} direct HTTP call(s) bypass tests/lib/rest.py")
        for path, line, what in problems:
            detail(f"{path}:{line} calls {what}")
        detail("Use rest.RestClient, rest.retrying_urlopen or "
               "rest.retrying_http_request; they share rest.may_retry.")
        return 1

    suite_ok("check_transport_usage", f"{scanned} files")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
