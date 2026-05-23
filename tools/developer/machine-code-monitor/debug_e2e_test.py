#!/usr/bin/env python3
"""End-to-end test for Machine Code Monitor Debug mode (DBG-* feature IDs).

Uses the same telnet entry point and CLI options as ``monitor_test.py`` so the
two suites are interchangeable. The current U64 firmware refuses Over/Trace/
Out execution commands by design (DBG-SAFE-001) while the BRK trampoline
orchestrator is being landed; this script exercises everything that does not
require live stepping: header/footer composition, key routing, breakpoint
toggle and list popup, RETURN navigation preservation, and Edit+Debug
composition.

CLI options mirror monitor_test.py exactly so the same automation hooks work.
"""

from __future__ import annotations

import argparse
import os
import sys
from pathlib import Path

# Reuse the existing telnet session helpers so both suites stay in lockstep.
sys.path.insert(0, str(Path(__file__).parent))
import monitor_test as mt  # type: ignore  # noqa: E402


def _capture_lines(session: "mt.MonitorSession") -> list[str]:
    snap = session.capture()
    return [snap.line(y).rstrip() for y in range(mt.HEIGHT)]


def _find(lines: list[str], needle: str) -> bool:
    return any(needle in line for line in lines)


def _header_line(session: "mt.MonitorSession") -> str:
    snap = session.capture()
    for y in range(mt.HEIGHT):
        line = snap.line(y)
        if "MONITOR" in line:
            return line
    raise mt.Failure("monitor header line not found")


def _footer_value_line(session: "mt.MonitorSession") -> str:
    snap = session.capture()
    # Footer in Debug mode is the bottom two lines: header labels then values.
    return snap.line(mt.HEIGHT - 2)


def _footer_header_line(session: "mt.MonitorSession") -> str:
    snap = session.capture()
    return snap.line(mt.HEIGHT - 3)


def run_debug_tests(session: "mt.MonitorSession") -> None:
    # Park the cursor at a known address so the screen is deterministic.
    with mt.check("Debug: setup at $C000"):
        session.goto("C000")
        header = _header_line(session)
        if "$C000" not in header:
            raise mt.Failure(f"goto $C000 did not update header: {header!r}")

    with mt.check("Debug: A switches to Assembly view"):
        session.send_char("A")
        header = _header_line(session)
        if "ASM" not in header:
            raise mt.Failure(f"A did not switch to ASM view: {header!r}")
        if "Dbg" in header:
            raise mt.Failure(f"Dbg flag must be absent before pressing D: {header!r}")

    with mt.check("Debug: D enters Debug mode without executing"):
        session.send_char("D")
        header = _header_line(session)
        if "MONITOR ASM" not in header:
            raise mt.Failure(f"Header must remain MONITOR ASM in Debug mode: {header!r}")
        if "Dbg" not in header:
            raise mt.Failure(f"Dbg flag must appear after D: {header!r}")

    with mt.check("Debug: footer rows show CPU labels"):
        labels = _footer_header_line(session)
        for token in ("PC", "SP", "AC", "XR", "YR", "NV-BDIZC", "IRQ", "NMI"):
            if token not in labels:
                raise mt.Failure(f"Footer label {token!r} missing in row: {labels!r}")

    with mt.check("Debug: unknown context renders blanks"):
        values = _footer_value_line(session)
        # The footer body is 35 columns; with no context the entire field
        # area is blank. We tolerate trailing spaces past 35.
        body = values[:35]
        if any(ch not in (" ", " ") for ch in body):
            raise mt.Failure(f"Unknown footer must be blank, got: {values!r}")

    with mt.check("Debug: R toggles a breakpoint (set + clear)"):
        # First R should pop up a SET status, second R a CLR status. Both
        # popups dismiss themselves on ENTER.
        session.send_char("R")
        snap = session.capture()
        if not any("SET" in snap.line(y) and "BRK" in snap.line(y) for y in range(mt.HEIGHT)):
            raise mt.Failure("R did not surface a BRK SET status")
        session.send_key("ENTER")
        session.send_char("R")
        snap = session.capture()
        if not any("CLR" in snap.line(y) and "BRK" in snap.line(y) for y in range(mt.HEIGHT)):
            raise mt.Failure("Second R did not surface a BRK CLR status")
        session.send_key("ENTER")

    with mt.check("Debug: C=+R opens the breakpoint list popup"):
        # C=+R is encoded by the firmware keymap; we send the byte the
        # firmware emits for that combo (KEY_CTRL_R = 0xBA).
        session.last_command = "CBM_R"
        session.sock.sendall(b"\xba")
        snap = session.capture()
        if not any("BREAKPOINTS" in snap.line(y) for y in range(mt.HEIGHT)):
            raise mt.Failure("C=+R did not open the breakpoint list popup")
        session.send_key("ESC")

    with mt.check("Debug: RETURN preserves Assembly navigation"):
        # RETURN must follow a jumpable instruction without executing. We
        # simply verify the monitor stays in Debug mode and ASM view; the
        # follow/return semantics are already exercised in monitor_test.py.
        session.send_key("ENTER")
        header = _header_line(session)
        if "MONITOR ASM" not in header or "Dbg" not in header:
            raise mt.Failure(f"RETURN must keep ASM + Dbg state: {header!r}")

    with mt.check("Debug: E composes with D (Dbg + Edit both visible)"):
        session.send_char("E")
        header = _header_line(session)
        if "Dbg" not in header or "Edit" not in header:
            raise mt.Failure(f"Header must show both Dbg and Edit: {header!r}")
        session.send_key("ESC")  # leaves Edit
        header = _header_line(session)
        if "Edit" in header:
            raise mt.Failure(f"ESC must clear Edit: {header!r}")
        if "Dbg" not in header:
            raise mt.Failure(f"ESC inside Edit+Debug must leave Edit, keep Dbg: {header!r}")

    with mt.check("Debug: ESC leaves Debug mode"):
        session.send_key("ESC")
        header = _header_line(session)
        if "Dbg" in header:
            raise mt.Failure(f"ESC must clear Dbg: {header!r}")

    with mt.check("Debug: B retains Binary view (not stolen by breakpoint)"):
        session.send_char("B")
        header = _header_line(session)
        if "BIN" not in header:
            raise mt.Failure(f"B must switch to Binary view: {header!r}")
        # Restore to ASM for any subsequent tests
        session.send_char("A")

    with mt.check("Debug: Over/Trace/Out refuse cleanly without a real session"):
        # The U64 backend currently refuses these with a popup. Verify D
        # then T surfaces an explicit refusal message. The popup is
        # dismissed with ENTER.
        session.send_char("D")  # enter Debug
        session.send_char("T")  # request trace
        snap = session.capture()
        joined = "\n".join(snap.line(y) for y in range(mt.HEIGHT))
        if "NOT SUPPORTED" not in joined and "REFUS" not in joined.upper():
            raise mt.Failure(f"Trace without session must show refusal: {joined!r}")
        session.send_key("ENTER")
        session.send_key("ESC")  # leave Debug


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Validate Machine Code Monitor Debug mode over the standard U64 telnet service")
    parser.add_argument("--host", default=os.environ.get("U64_MONITOR_HOST", "u64"))
    parser.add_argument("--port", type=int, default=int(os.environ.get("U64_MONITOR_PORT", "23")))
    parser.add_argument("--rest-host", default=os.environ.get("U64_MONITOR_REST_HOST"))
    parser.add_argument("--password", default=os.environ.get("U64_MONITOR_PASSWORD"))
    parser.add_argument("--timeout", type=float, default=float(os.environ.get("U64_MONITOR_TIMEOUT", "5.0")))
    args = parser.parse_args()

    session = None
    try:
        session = mt.MonitorSession(args.host, args.port, args.password, args.timeout)
        run_debug_tests(session)
    except mt.Failure as exc:
        print(exc, file=sys.stderr)
        if session is not None:
            print("\nFinal screen:", file=sys.stderr)
            print(session.capture().text(), file=sys.stderr)
        return 1
    except Exception as exc:  # noqa: BLE001
        print(f"E2E debug test failed: {exc}", file=sys.stderr)
        return 1
    finally:
        if session is not None:
            session.close()

    print(f"debug_e2e_test: OK ({mt.CHECK_COUNT} checks)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
