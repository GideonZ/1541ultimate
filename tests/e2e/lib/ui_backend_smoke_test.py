#!/usr/bin/env python3
# E2E: Verifies the shared ui_backend.py facade itself against real hardware,
# independently of any suite built on top of it, via all three transports it
# supports: Telnet, REST/Freeze, and REST/Overlay.

"""tests/e2e/lib/ui_backend.py gives every suite one Backend interface over two
transports (Telnet and REST) and, for REST, two Interface Type UI modes
(Freeze and Overlay). A bug in that shared plumbing would surface as
confusing, unrelated-looking failures in every suite built on it, so it gets
its own direct check first: the same small scenario (root browser is visible,
a keypress moves the selection, typing a character quick-seeks, teardown
leaves the device clean) run once per transport/mode.

This is deliberately not a full UI regression suite (tests/e2e/api/input_test.py
and menu_screen_test.py already cover the REST input/menu_screen contract in
depth) -- it exists to prove the facade itself is wired correctly before any
other suite relies on it.
"""

import argparse
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__))))
from report import Failure, check, detail, format_exception, section, suite_fail, suite_ok
from ui_backend import Backend, RestBackend, TelnetBackend, Snapshot

MIN_PRINTABLE_CELLS = 20
MAX_DISTINCT_GLYPHS = 160


def device_unavailable(exc: BaseException) -> bool:
    text = format_exception(exc).lower()
    markers = (
        "no route to host", "network is unreachable", "connection refused",
        "timed out", "temporary failure in name resolution",
    )
    return any(marker in text for marker in markers)


def assert_looks_like_root_browser(snapshot: Snapshot) -> None:
    text = snapshot.text()
    printable = sum(1 for ch in text if ch not in (" ",))
    if printable < MIN_PRINTABLE_CELLS:
        raise Failure(f"screen looks blank after {snapshot.last_command}: only {printable} non-space cells\n{text}")
    distinct_glyphs = len(set(text))
    if distinct_glyphs > MAX_DISTINCT_GLYPHS:
        raise Failure(f"screen looks like garbage after {snapshot.last_command}: {distinct_glyphs} distinct glyphs\n{text}")
    snapshot.find_line_containing("Ultimate")
    snapshot.find_line_containing("/")


def path_row(snapshot: Snapshot) -> str:
    """The browser's own path indicator, wherever the row layout puts it."""
    for line in reversed(snapshot.lines):
        text = line.strip()
        if text.startswith("/"):
            return text.split()[0]
    raise Failure(f"no path row found after {snapshot.last_command}\n{snapshot.text()}")


def run_backend_smoke(backend: Backend) -> None:
    with check("root browser is visible on connect"):
        snapshot = backend.capture()
        assert_looks_like_root_browser(snapshot)

    with check("F5 opens the task menu and RUN/STOP restores the browser"):
        # The browser marks the selected row by colour, not by the character
        # matrix's reverse-video bit, and the two transports use different
        # colour encodings (a real C64 colour nibble over REST, an
        # ANSI-mapped approximation over Telnet's VT100 stream), so a cursor
        # key alone is not a transport-uniform text signal. Opening the task
        # menu replaces the visible text outright, which both transports
        # render identically in the character matrix.
        before = backend.capture().text()
        opened = backend.send_key("F5").text()
        if opened == before:
            raise Failure("F5 had no visible effect on the screen")
        closed = backend.send_key("RUNSTOP").text()
        if closed != before:
            raise Failure("RUN/STOP did not restore the original screen after F5")

    with check("typing a character quick-seeks to the matching entry"):
        # Proves send_char delivers the *correct* character, not merely *a*
        # character: quick-seeking on the wrong letter would step into the
        # wrong directory, which the resulting path would catch. "Temp"
        # already appears as a row label at the root, so this checks the
        # browser's own path indicator rather than screen content generally.
        backend.send_char("t")
        entered_path = path_row(backend.send_key("RIGHT"))
        if not entered_path.startswith("/Temp"):
            raise Failure(f"quick-seek on 't' + RIGHT did not enter /Temp: path was {entered_path!r}")
        left_path = path_row(backend.send_key("LEFT"))
        if left_path != "/":
            raise Failure(f"LEFT did not return to the root path: {left_path!r}")


def run_telnet_smoke(host: str, port: int, password: str, timeout: float) -> None:
    backend = TelnetBackend(host, port, password or None, timeout)
    try:
        run_backend_smoke(backend)
    finally:
        backend.close()


def run_rest_smoke(host: str, password: str, timeout: float, interface_type: str) -> None:
    backend = RestBackend(host, password or None, timeout, interface_type=interface_type)
    try:
        run_backend_smoke(backend)
    finally:
        backend.close()


def main() -> int:
    parser = argparse.ArgumentParser(description="Validate the shared ui_backend.py facade over Telnet, REST/Freeze and REST/Overlay")
    parser.add_argument("-H", "--host", default=os.environ.get("U64_HOST", "u64"))
    parser.add_argument("--telnet-port", type=int, default=int(os.environ.get("U64_TELNET_PORT", "23")))
    parser.add_argument("-p", "--password", default=os.environ.get("U64_PASS", ""))
    parser.add_argument("-t", "--timeout", type=float, default=float(os.environ.get("U64_TIMEOUT", "5.0")))
    args = parser.parse_args()

    try:
        section("Telnet backend")
        with check("Telnet: connect, navigate, teardown"):
            run_telnet_smoke(args.host, args.telnet_port, args.password, args.timeout)

        section("REST backend, Interface Type = Freeze")
        with check("REST/Freeze: connect, navigate, teardown"):
            run_rest_smoke(args.host, args.password, args.timeout, "Freeze")

        section("REST backend, Interface Type = Overlay on HDMI")
        with check("REST/Overlay: connect, navigate, teardown"):
            run_rest_smoke(args.host, args.password, args.timeout, "Overlay on HDMI")
    except Failure as exc:
        suite_fail("ui_backend_smoke_test", str(exc))
        return 1
    except Exception as exc:  # noqa: BLE001 - report any transport error through the shared library
        if device_unavailable(exc):
            suite_fail("ui_backend_smoke_test", f"device unavailable: {format_exception(exc)}")
        else:
            suite_fail("ui_backend_smoke_test", format_exception(exc))
        return 1

    suite_ok("ui_backend_smoke_test")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
