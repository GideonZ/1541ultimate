#!/usr/bin/env python3
"""End-to-end test for Machine Code Monitor Debug mode (DBG-* feature IDs).

Uses the same telnet entry point and CLI options as ``monitor_test.py`` so the
two suites are interchangeable. Verifies the full Debug mode stack: header/
footer composition, key routing, breakpoint toggle and list popup, RETURN
navigation preservation, Edit+Debug composition, the BRK trampoline
orchestrator (Go, Over, Trace, Out), and patch hygiene on cleanup.

CLI options mirror monitor_test.py exactly so the same automation hooks work.
"""

from __future__ import annotations

import argparse
import os
import sys
import time
from pathlib import Path

# Reuse the existing telnet session helpers so both suites stay in lockstep.
sys.path.insert(0, str(Path(__file__).parent))
import monitor_test as mt  # type: ignore  # noqa: E402

FLAG_BIT_INDEX = {
    "N": 0,
    "V": 1,
    "B": 3,
    "D": 4,
    "I": 5,
    "Z": 6,
    "C": 7,
}


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
    # Find the CPU label row by scanning, then the value row is the line
    # directly below it. This is more robust than fixed indices, which vary
    # depending on whether the screen renders the system status line at the
    # bottom.
    snap = session.capture()
    for y in range(mt.HEIGHT):
        line = snap.line(y)
        if "PC" in line and "SP" in line and "NV-BDIZC" in line:
            if y + 1 < mt.HEIGHT:
                return snap.line(y + 1)
    raise mt.Failure("CPU label row not found; cannot locate footer values")


def _footer_header_line(session: "mt.MonitorSession") -> str:
    snap = session.capture()
    for y in range(mt.HEIGHT):
        line = snap.line(y)
        if "PC" in line and "SP" in line and "NV-BDIZC" in line:
            return line
    raise mt.Failure("CPU label row not found")


def _debug_footer_row_indices(session: "mt.MonitorSession") -> tuple[int, int, int]:
    snap = session.capture()
    label_y = -1
    status_y = -1
    for y in range(mt.HEIGHT):
        line = snap.line(y)
        if "PC" in line and "SP" in line and "NV-BDIZC" in line:
            label_y = y
        if mt.STATUS_LINE_RE.search(line):
            status_y = y
    if label_y < 0:
        raise mt.Failure("Debug footer label row not found")
    if status_y < 0:
        raise mt.Failure(f"Normal CPU/VIC footer not visible in Debug:\n{snap.text()}")
    return label_y, label_y + 1, status_y


def _send_ctrl_d(session: "mt.MonitorSession") -> None:
    session.last_command = "CTRL_D"
    session.sock.sendall(b"\x04")


def _send_ctrl_x(session: "mt.MonitorSession") -> None:
    session.last_command = "CTRL_X"
    session.sock.sendall(b"\x18")


def _wait_for_screen_text(session: "mt.MonitorSession", needle: str,
                          timeout: float = 2.0) -> mt.Snapshot:
    deadline = time.time() + timeout
    last = None
    while time.time() < deadline:
        snap = session.capture()
        last = snap
        if needle in snap.text():
            return snap
        time.sleep(0.1)
    raise mt.Failure(
        f"Timed out waiting for {needle!r} after {session.last_command}\n"
        f"{last.text() if last is not None else '<no snapshot>'}")


def _wait_for_blank_debug_context(session: "mt.MonitorSession",
                                  timeout: float = 4.0) -> None:
    deadline = time.time() + timeout
    while time.time() < deadline:
        header = _header_line(session)
        if "Dbg" not in header:
            time.sleep(0.1)
            continue
        parsed = _parse_footer_values(_footer_value_line(session))
        if not any(parsed[field] for field in ("pc", "sp", "ac", "xr", "yr", "sr", "irq", "nmi")):
            return
        time.sleep(0.1)
    raise mt.Failure(f"Debug footer did not clear after reset:\n{session.capture().text()}")


def run_debug_tests(rest_host: str, session: "mt.MonitorSession") -> None:
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

    with mt.check("Debug: CPU table sits above normal CPU/VIC footer"):
        label_y, value_y, status_y = _debug_footer_row_indices(session)
        if status_y != value_y + 1:
            snap = session.capture()
            raise mt.Failure(
                f"Debug footer must occupy rows n-2/n-1 above normal footer n; "
                f"got label={label_y}, value={value_y}, status={status_y}\n{snap.text()}")

    with mt.check("Debug: unknown context renders blanks"):
        values = _footer_value_line(session)
        # Strip the window left/right border. With no captured context
        # every CPU footer column must be blank - never zero or question marks.
        inside = values.strip("| ")
        if inside:
            raise mt.Failure(f"Unknown footer must be blank, got: {values!r}")

    with mt.check("Debug: help keeps normal shortcuts and uses RSTOP labels"):
        session.send_key("F3")
        snap = session.capture()
        joined = "\n".join(snap.line(y) for y in range(mt.HEIGHT))
        for token in ("M Memory", "A Assembly", "L Load", "S Save",
                      "C=+B List", "D Debug/Over", "T Trace", "O Out",
                      "C=+R", "C=+D", "C=+X", "RSTOP"):
            if token not in joined:
                raise mt.Failure(f"Debug help missing {token!r}:\n{joined}")
        if "ESC" in joined:
            raise mt.Failure(f"Debug help must show RSTOP, not ESC:\n{joined}")
        session.send_key("ESC")

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
        # Over telnet, C=+R is sent as ESC+R (same pattern as ESC+B used
        # for C=+B). The firmware keyboard_vt100 layer recognises this and
        # emits KEY_CTRL_R into the monitor.
        session.last_command = "CBM_R"
        session.sock.sendall(b"\x1bR")
        snap = session.capture()
        if not any("BREAKPOINTS" in snap.line(y) for y in range(mt.HEIGHT)):
            raise mt.Failure("C=+R did not open the breakpoint list popup")
        for ch in "DTOGR":
            session.send_char(ch)
            snap = session.capture()
            if not any("BREAKPOINTS" in snap.line(y) for y in range(mt.HEIGHT)):
                raise mt.Failure(f"Breakpoint popup did not block Debug key {ch!r}")
        session.send_key("ESC")

    with mt.check("Debug: breakpoint opcode line shows [BRKx] before 3-char source"):
        mt.write_rest_memory(rest_host, 0xC050, bytes([0xEA, 0xEA]))
        session.goto("C050")
        session.send_char("A")
        if "Dbg" not in _header_line(session):
            session.send_char("D")
        session.send_char("R")
        session.send_key("ENTER")
        lines = _capture_lines(session)
        row = next((line for line in lines if line.startswith("|C050 ")), "")
        if "[BRK0][RAM]" not in row:
            raise mt.Failure(f"Breakpoint line must show [BRK0][RAM], got: {row!r}")

    with mt.check("Debug: C=+R shows the live breakpoint list"):
        session.last_command = "CBM_R_WITH_BREAKPOINT"
        session.sock.sendall(b"\x1bR")
        snap = session.capture()
        joined = "\n".join(snap.line(y) for y in range(mt.HEIGHT))
        if "BREAKPOINTS" not in joined:
            raise mt.Failure(f"C=+R did not open breakpoint list:\n{joined}")
        if "$C050" not in joined or "SET" not in joined:
            raise mt.Failure(f"Breakpoint list did not show the live breakpoint:\n{joined}")
        session.send_key("ESC")

    with mt.check("Debug: visible memory source indicators are 3 chars"):
        _ensure_no_debug(session)
        mt.ensure_status(session, "CPU7 $A:BAS $D:I/O $E:KRN VIC")
        session.send_char("D")
        expected = {
            "C000": "[RAM]",
            "A000": "[BAS]",
            "D000": "[I/O]",
            "E000": "[KRN]",
        }
        for address, marker in expected.items():
            session.goto(address)
            lines = _capture_lines(session)
            row = next((line for line in lines if line.startswith(f"|{address} ")), "")
            if marker not in row:
                raise mt.Failure(f"{address} row missing {marker}: {row!r}")
            for stale in ("[BASIC]", "[KERNAL]", "[CHAR]", "[IO]"):
                if stale in row:
                    raise mt.Failure(f"{address} row used stale source label: {row!r}")

    with mt.check("Debug: RETURN preserves Assembly navigation"):
        # RETURN must follow a jumpable instruction without executing. We
        # simply verify the monitor stays in Debug mode and ASM view; the
        # follow/return semantics are already exercised in monitor_test.py.
        session.send_key("ENTER")
        header = _header_line(session)
        if "MONITOR ASM" not in header or "Dbg" not in header:
            raise mt.Failure(f"RETURN must keep ASM + Dbg state: {header!r}")

    with mt.check("Debug: RETURN does not execute target code"):
        # A JSR target that would mutate $C030 if executed. RETURN may follow
        # the subroutine in the Assembly view, but must not run it.
        program = bytes([0x20, 0x24, 0xC0, 0xEA, 0xEE, 0x30, 0xC0, 0x60])
        mt.write_rest_memory(rest_host, 0xC020, program)
        mt.write_rest_memory(rest_host, 0xC030, bytes([0x44]))
        session.goto("C020")
        session.send_char("A")
        if "Dbg" not in _header_line(session):
            session.send_char("D")
        session.send_key("ENTER")
        after = mt.read_rest_memory(rest_host, 0xC030, 1)[0]
        if after != 0x44:
            raise mt.Failure(f"RETURN executed subroutine side effect; $C030=${after:02X}")
        session.send_key("ENTER")

    with mt.check("Debug: ESC/RUNSTOP leaves Edit before Dbg so debugging can continue"):
        _ensure_no_debug(session)
        mt.write_rest_memory(rest_host, 0xC040, bytes([0xA9, 0x66, 0xEA]))
        session.goto("C040")
        session.send_char("A")
        session.send_char("D")
        session.send_char("M")
        session.send_char("E")
        header = _header_line(session)
        if "Dbg" not in header or "Edit" not in header:
            raise mt.Failure(f"Header must show both Dbg and Edit: {header!r}")
        session.send_key("ESC")
        header = _header_line(session)
        if "Dbg" not in header or "Edit" in header:
            raise mt.Failure(f"ESC in Debug+Edit must keep Dbg and clear Edit: {header!r}")
        session.send_char("A")
        header = _header_line(session)
        if "MONITOR ASM" not in header or "Dbg" not in header or "Edit" in header:
            raise mt.Failure(f"Leaving Edit should keep Debug alive for ASM resume: {header!r}")
        session.send_char("D")
        parsed = _wait_for_pc(session, "C042")
        if parsed["ac"] != "66":
            raise mt.Failure(f"Debugging should continue after leaving Edit, got {parsed!r}")
        _ensure_no_debug(session)

    with mt.check("Debug: C=+D leaves Debug mode while keeping Edit active"):
        session.send_char("D")
        session.send_char("E")
        header = _header_line(session)
        if "Dbg" not in header or "Edit" not in header:
            raise mt.Failure(f"Header must show both Dbg and Edit: {header!r}")
        _send_ctrl_d(session)
        header = _header_line(session)
        if "Dbg" in header or "Edit" not in header:
            raise mt.Failure(f"C=+D in Debug+Edit must keep Edit and clear Dbg: {header!r}")
        session.send_key("ESC")
        header = _header_line(session)
        if "Edit" in header:
            raise mt.Failure(f"ESC after C=+D must clear the remaining Edit flag: {header!r}")
        session.send_char("D")

    with mt.check("Debug: returning to ASM after stepping elsewhere follows the current debug PC"):
        _reopen_monitor(session)
        mt.write_rest_memory(rest_host, 0xC060, bytes([0xA9, 0x22, 0xEA, 0xEA]))
        session.goto("C060")
        session.send_char("A")
        session.send_char("D")
        session.send_char("D")
        _wait_for_pc(session, "C062")
        session.send_char("M")
        session.goto("C180")
        session.send_char("D")
        _wait_for_pc(session, "C063")
        session.send_char("A")
        header = _header_line(session)
        if "MONITOR ASM $C063" not in header:
            raise mt.Failure(f"ASM view must snap back to the current debug PC: {header!r}")
        lines = _capture_lines(session)
        row = next((line for line in lines if line.startswith("|C063 ")), "")
        if not row:
            raise mt.Failure(f"Disassembly cursor did not return to $C063:\n{session.capture().text()}")
        _ensure_no_debug(session)

    with mt.check("Debug: C=+X resets the machine and keeps Debug open with blank context"):
        _reopen_monitor(session)
        mt.write_rest_memory(rest_host, 0xC070, bytes([0xA9, 0x11, 0xEA]))
        session.goto("C070")
        session.send_char("A")
        session.send_char("D")
        session.send_char("D")
        _wait_for_pc(session, "C072")
        _send_ctrl_x(session)
        _wait_for_blank_debug_context(session)

    with mt.check("Debug: C=+B opens bookmarks in Debug mode"):
        session.last_command = "CBM_B"
        session.sock.sendall(b"\x1bB")
        snap = session.capture()
        if not any("BOOKMARKS" in snap.line(y) for y in range(mt.HEIGHT)):
            raise mt.Failure("C=+B must open bookmark popup while Debug is active")
        session.send_key("ESC")
        header = _header_line(session)
        if "Dbg" not in header:
            raise mt.Failure(f"Closing C=+B popup must keep Debug active: {header!r}")

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

    with mt.check("Debug: C=+D leaves Debug mode"):
        session.send_char("D")
        _send_ctrl_d(session)
        header = _header_line(session)
        if "Dbg" in header:
            raise mt.Failure(f"C=+D must clear Dbg: {header!r}")

    with mt.check("Debug: D over without captured context executes from cursor"):
        _reopen_monitor(session)
        mt.write_rest_memory(rest_host, 0xC040, bytes([0xA9, 0x66, 0xEA]))
        session.goto("C040")
        session.send_char("A")
        session.send_char("D")  # enter Debug; footer is unknown
        session.send_char("D")  # Over from cursor, not NO CONTEXT
        parsed = _wait_for_pc(session, "C042")
        if parsed["ac"] != "66":
            raise mt.Failure(f"No-context Over should execute LDA #$66, got {parsed!r}")
        _ensure_no_debug(session)


# ---------------------------------------------------------------------------
# Helpers for the BRK orchestrator tests below.

def _ensure_no_debug(session: "mt.MonitorSession") -> None:
    """Leave Debug mode if currently active."""
    for _ in range(2):
        header = _header_line(session)
        if "Dbg" not in header:
            return
        session.send_key("ESC")
    raise mt.Failure("Could not leave Debug mode")


def _reopen_monitor(session: "mt.MonitorSession") -> None:
    _ensure_no_debug(session)
    session.send_key("CTRL_O")
    session.enter_monitor()


def _parse_footer_values(values_line: str):
    """Extract PC / SP / AC / XR / YR / SR-binary from the footer value row."""
    # Strip the window border.
    body = values_line.lstrip("|+").rstrip("|+").rstrip()
    # Positions inside the body track the firmware footer layout exactly.
    fields = {
        "pc": body[0:4],
        "sp": body[5:7],
        "ac": body[8:10],
        "xr": body[11:13],
        "yr": body[14:16],
        "sr": body[17:25],
        "irq": body[26:30],
        "nmi": body[31:35],
    }
    return {k: v.strip() for k, v in fields.items()}


def _wait_for_pc(session: "mt.MonitorSession", expected_pc: str,
                 timeout: float = 6.0) -> dict:
    """Poll the debug footer until PC matches `expected_pc`."""
    import time as _time
    deadline = _time.time() + timeout
    last = None
    while _time.time() < deadline:
        values = _footer_value_line(session)
        parsed = _parse_footer_values(values)
        last = parsed
        if parsed["pc"].upper() == expected_pc.upper():
            return parsed
        _time.sleep(0.1)
    raise mt.Failure(f"PC did not reach {expected_pc}; last footer={last!r} ({values!r})")


def _assert_flag_bits(label: str, parsed: dict, **expected: str) -> None:
    sr = parsed["sr"]
    if len(sr) != 8 or any(bit not in "01" for bit in sr):
        raise mt.Failure(f"{label}: SR must be 8 binary digits, got {parsed!r}")
    for flag, value in expected.items():
        actual = sr[FLAG_BIT_INDEX[flag]]
        if actual != value:
            raise mt.Failure(
                f"{label}: expected {flag}={value}, got SR={sr} footer={parsed!r}")


def run_flag_control_flow_tests(rest_host: str, session: "mt.MonitorSession") -> None:
    """Exercise flag capture and taken/not-taken control flow without duplication."""

    # $C200: 18          CLC
    # $C201: A9 00       LDA #$00
    # $C203: D0 02       BNE $C207     ; must fall through
    # $C205: A9 80       LDA #$80
    # $C207: 10 02       BPL $C20B     ; must fall through
    # $C209: 38          SEC
    # $C20A: 69 7F       ADC #$7F
    # $C20C: 30 02       BMI $C210     ; must fall through
    # $C20E: B0 04       BCS $C214     ; must branch
    # $C210: A9 EE       LDA #$EE      ; skipped when branch predictor is right
    # $C212: EA          NOP
    # $C213: EA          NOP
    # $C214: EA          NOP
    program = bytes([
        0x18,
        0xA9, 0x00,
        0xD0, 0x02,
        0xA9, 0x80,
        0x10, 0x02,
        0x38,
        0x69, 0x7F,
        0x30, 0x02,
        0xB0, 0x04,
        0xA9, 0xEE,
        0xEA,
        0xEA,
        0xEA,
    ])

    _reopen_monitor(session)

    with mt.check("Debug: flag/control-flow program loads at $C200"):
        mt.write_rest_memory(rest_host, 0xC200, program)
        readback = mt.read_rest_memory(rest_host, 0xC200, len(program))
        if readback != program:
            raise mt.Failure(f"Flag/control-flow program readback mismatch: {readback.hex()}")

    with mt.check("Debug: flag capture and branch fall-through stay truthful across steps"):
        session.goto("C200")
        session.send_char("A")
        session.send_char("D")

        session.send_char("D")
        parsed = _wait_for_pc(session, "C201")
        _assert_flag_bits("CLC", parsed, C="0")

        session.send_char("D")
        parsed = _wait_for_pc(session, "C203")
        _assert_flag_bits("LDA #$00", parsed, Z="1", N="0")

        session.send_char("D")
        _wait_for_pc(session, "C205")

        session.send_char("D")
        parsed = _wait_for_pc(session, "C207")
        _assert_flag_bits("LDA #$80", parsed, Z="0", N="1")

        session.send_char("D")
        _wait_for_pc(session, "C209")

        session.send_char("D")
        parsed = _wait_for_pc(session, "C20A")
        _assert_flag_bits("SEC", parsed, C="1")

        session.send_char("D")
        parsed = _wait_for_pc(session, "C20C")
        if parsed["ac"] != "00":
            raise mt.Failure(f"ADC #$7F should wrap AC to $00, got {parsed!r}")
        _assert_flag_bits("ADC #$7F", parsed, N="0", V="0", Z="1", C="1")

        session.send_char("D")
        _wait_for_pc(session, "C20E")

        session.send_char("D")
        parsed = _wait_for_pc(session, "C214")
        if parsed["ac"] != "00":
            raise mt.Failure(f"Taken BCS must skip LDA #$EE, got {parsed!r}")
        _ensure_no_debug(session)


def run_refusal_and_return_edge_tests(rest_host: str, session: "mt.MonitorSession") -> None:
    """Cover unsafe targets, undocumented-opcode gating, RTS, and RTI handoff."""

    unsafe_program = bytes([0x00, 0x60, 0x40, 0x1A, 0xEA])
    rts_caller = bytes([0x20, 0x70, 0xC2, 0xEA, 0x4C, 0x64, 0xC2])
    rts_subroutine = bytes([0x60])
    rti_setup = bytes([
        0xA9, 0xC2,
        0x48,
        0xA9, 0x90,
        0x48,
        0xA9, 0xA1,
        0x48,
        0x40,
    ])
    rti_target = bytes([0xEA, 0x4C, 0x90, 0xC2])

    _reopen_monitor(session)

    with mt.check("Debug: unsafe-target/refusal fixtures load at $C240/$C260/$C280"):
        mt.write_rest_memory(rest_host, 0xC240, unsafe_program)
        mt.write_rest_memory(rest_host, 0xC260, rts_caller)
        mt.write_rest_memory(rest_host, 0xC270, rts_subroutine)
        mt.write_rest_memory(rest_host, 0xC280, rti_setup)
        mt.write_rest_memory(rest_host, 0xC290, rti_target)

    with mt.check("Debug: BRK, RTS, and RTI refuse no-context Over without fabricating state"):
        session.goto("C240")
        session.send_char("A")
        session.send_char("D")
        for address in ("C240", "C241", "C242"):
            session.goto(address)
            session.send_char("D")
            _wait_for_screen_text(session, "UNSAFE TARGET")
            session.send_key("ENTER")
            _wait_for_blank_debug_context(session)
        _ensure_no_debug(session)

    with mt.check("Debug: undocumented NOP is gated by Undc and steps once enabled"):
        session.goto("C243")
        session.send_char("A")
        session.send_char("D")
        session.send_char("D")
        _wait_for_screen_text(session, "UNSAFE TARGET")
        session.send_key("ENTER")
        _wait_for_blank_debug_context(session)
        session.send_char("U")
        header = _header_line(session)
        if "Undc" not in header:
            raise mt.Failure(f"Undc flag must appear after U: {header!r}")
        session.send_char("D")
        _wait_for_pc(session, "C244")
        session.send_char("U")
        _ensure_no_debug(session)

    with mt.check("Debug: traced RTS lands on the caller continuation address"):
        _reopen_monitor(session)
        session.goto("C260")
        session.send_char("A")
        session.send_char("D")
        session.send_char("T")
        _wait_for_pc(session, "C270")
        session.send_char("D")
        _wait_for_pc(session, "C263")
        _ensure_no_debug(session)

    with mt.check("Debug: RTI restores the stacked target PC and flags truthfully"):
        _reopen_monitor(session)
        session.goto("C280")
        session.send_char("A")
        session.send_char("D")
        for expected_pc in ("C282", "C283", "C285", "C286", "C288", "C289"):
            session.send_char("D")
            _wait_for_pc(session, expected_pc)
        session.send_char("D")
        parsed = _wait_for_pc(session, "C290")
        _assert_flag_bits("RTI", parsed, N="1", Z="0", C="1")
        _ensure_no_debug(session)


def run_page_cross_and_indirect_jump_tests(rest_host: str, session: "mt.MonitorSession") -> None:
    """Cover page-crossing branches and the 6502 indirect-JMP wrap quirk."""

    branch_taken = bytes([
        0xA9, 0x00,       # $C1FC: LDA #$00
        0xF0, 0x02,       # $C1FE: BEQ $C202 (taken across page)
        0xA9, 0xEE,       # $C200: LDA #$EE (must be skipped)
        0xEA,             # $C202: NOP
    ])
    branch_fallthrough = bytes([
        0xA9, 0x01,       # $C2FC: LDA #$01
        0xF0, 0x02,       # $C2FE: BEQ $C302 (not taken across page)
        0xA9, 0x44,       # $C300: LDA #$44 (must execute)
        0xEA,             # $C302: NOP
    ])
    indirect_jmp = bytes([0x6C, 0xFF, 0xC4])  # JMP ($C4FF)

    _reopen_monitor(session)

    with mt.check("Debug: page-cross branch and indirect-JMP fixtures load"):
        mt.write_rest_memory(rest_host, 0xC1FC, branch_taken)
        mt.write_rest_memory(rest_host, 0xC2FC, branch_fallthrough)
        mt.write_rest_memory(rest_host, 0xC320, indirect_jmp)
        mt.write_rest_memory(rest_host, 0xC440, bytes([0x4C, 0x40, 0xC4]))  # JMP $C440
        mt.write_rest_memory(rest_host, 0xC4FF, bytes([0x40]))
        mt.write_rest_memory(rest_host, 0xC400, bytes([0xC4]))
        mt.write_rest_memory(rest_host, 0xC500, bytes([0xC5]))

    with mt.check("Debug: taken branch across a page stops at the real target"):
        session.goto("C1FC")
        session.send_char("A")
        session.send_char("D")
        session.send_char("D")
        _wait_for_pc(session, "C1FE")
        session.send_char("D")
        parsed = _wait_for_pc(session, "C202")
        if parsed["ac"] != "00":
            raise mt.Failure(f"Taken page-cross branch must skip LDA #$EE, got {parsed!r}")
        _ensure_no_debug(session)

    with mt.check("Debug: not-taken branch across a page falls through correctly"):
        _reopen_monitor(session)
        session.goto("C2FC")
        session.send_char("A")
        session.send_char("D")
        session.send_char("D")
        _wait_for_pc(session, "C2FE")
        session.send_char("D")
        parsed = _wait_for_pc(session, "C300")
        if parsed["ac"] != "01":
            raise mt.Failure(f"Not-taken page-cross branch must preserve A=$01, got {parsed!r}")
        session.send_char("D")
        parsed = _wait_for_pc(session, "C302")
        if parsed["ac"] != "44":
            raise mt.Failure(f"Fall-through LDA #$44 did not execute after page-cross branch: {parsed!r}")
        _ensure_no_debug(session)

    with mt.check("Debug: Over on JMP ($xxFF) follows the real 6502 page-wrap target"):
        _reopen_monitor(session)
        session.goto("C320")
        session.send_char("A")
        session.send_char("D")
        session.send_char("D")
        _wait_for_pc(session, "C440")
        _ensure_no_debug(session)


def run_nested_out_tests(rest_host: str, session: "mt.MonitorSession") -> None:
    """Prove Out unwinds the current stack frame, not just a trivial single-level call."""

    main_program = bytes([
        0xA9, 0x11,             # $C360: LDA #$11
        0x20, 0x80, 0xC3,       # $C362: JSR $C380
        0x8D, 0x91, 0xC1,       # $C365: STA $C191
        0x4C, 0x68, 0xC3,       # $C368: JMP $C368
    ])
    outer_subroutine = bytes([
        0x48,                   # $C380: PHA
        0xA9, 0x22,             # $C381: LDA #$22
        0x8D, 0x92, 0xC1,       # $C383: STA $C192
        0x20, 0xC0, 0xC3,       # $C386: JSR $C3C0
        0x68,                   # $C389: PLA
        0x60,                   # $C38A: RTS
    ])
    inner_subroutine = bytes([
        0xA9, 0x33,             # $C3C0: LDA #$33
        0x8D, 0x93, 0xC1,       # $C3C2: STA $C193
        0x60,                   # $C3C5: RTS
    ])

    _reopen_monitor(session)

    with mt.check("Debug: nested Out fixtures load with stack-changing outer frame"):
        mt.write_rest_memory(rest_host, 0xC360, main_program)
        mt.write_rest_memory(rest_host, 0xC380, outer_subroutine)
        mt.write_rest_memory(rest_host, 0xC3C0, inner_subroutine)
        mt.write_rest_memory(rest_host, 0xC191, bytes([0x00, 0x00, 0x00]))

    with mt.check("Debug: nested Out unwinds inner then outer caller frames truthfully"):
        session.goto("C360")
        session.send_char("A")
        session.send_char("D")
        session.send_char("D")
        _wait_for_pc(session, "C362")
        session.send_char("T")
        _wait_for_pc(session, "C380")
        session.send_char("D")
        _wait_for_pc(session, "C381")
        session.send_char("D")
        _wait_for_pc(session, "C383")
        session.send_char("D")
        _wait_for_pc(session, "C386")
        if mt.read_rest_memory(rest_host, 0xC192, 1)[0] != 0x22:
            raise mt.Failure("Outer-frame side effect did not execute before entering the inner call")
        session.send_char("T")
        _wait_for_pc(session, "C3C0")
        session.send_char("O")
        parsed = _wait_for_pc(session, "C389")
        if mt.read_rest_memory(rest_host, 0xC193, 1)[0] != 0x33:
            raise mt.Failure(f"Inner Out did not execute the inner side effect: {parsed!r}")
        if mt.read_rest_memory(rest_host, 0xC191, 1)[0] != 0x00:
            raise mt.Failure("Inner Out must not run the caller-side store yet")
        session.send_char("O")
        parsed = _wait_for_pc(session, "C365")
        session.send_char("D")
        parsed = _wait_for_pc(session, "C368")
        if mt.read_rest_memory(rest_host, 0xC191, 1)[0] != 0x11:
            raise mt.Failure(f"Outer Out did not restore A for the caller-side store: {parsed!r}")
        _ensure_no_debug(session)


def run_breakpoint_reentry_tests(rest_host: str, session: "mt.MonitorSession") -> None:
    """Ensure repeated Go from the current breakpoint re-arms cleanly."""

    # $C300: EE 90 C1    INC $C190
    # $C303: 4C 00 C3    JMP $C300
    program = bytes([
        0xEE, 0x90, 0xC1,
        0x4C, 0x00, 0xC3,
    ])

    _reopen_monitor(session)

    with mt.check("Debug: breakpoint re-entry loop loads at $C300"):
        mt.write_rest_memory(rest_host, 0xC300, program)
        mt.write_rest_memory(rest_host, 0xC190, bytes([0x00]))

    with mt.check("Debug: repeated G from the current breakpoint skips once and re-arms cleanly"):
        session.goto("C300")
        session.send_char("A")
        session.send_char("D")
        session.send_char("R")
        session.send_key("ENTER")

        session.send_char("G")
        _wait_for_pc(session, "C300")
        if mt.read_rest_memory(rest_host, 0xC190, 1)[0] != 0x00:
            raise mt.Failure("Initial breakpoint hit must stop before INC $C190 executes")

        session.send_char("G")
        _wait_for_pc(session, "C300")
        if mt.read_rest_memory(rest_host, 0xC190, 1)[0] != 0x01:
            raise mt.Failure("Second G from the active breakpoint must execute INC exactly once")

        _ensure_no_debug(session)
        if mt.read_rest_memory(rest_host, 0xC300, 1)[0] != 0xEE:
            raise mt.Failure("Breakpoint cleanup did not restore INC opcode at $C300")


def run_brk_orchestrator_tests(rest_host: str, session: "mt.MonitorSession") -> None:
    """Drive the live BRK trampoline through Over / Trace / Out / Go."""

    # Layout in $C000-$C00F: a deterministic program we can step through.
    #   $C000: A9 AA       LDA #$AA
    #   $C002: A2 BB       LDX #$BB
    #   $C004: A0 CC       LDY #$CC
    #   $C006: EA          NOP
    #   $C007: 20 0D C0    JSR $C00D
    #   $C00A: 4C 0A C0    JMP $C00A     ; infinite loop after RTS
    #   $C00D: A9 11       LDA #$11      ; called subroutine
    #   $C00F: 60          RTS
    program = bytes([
        0xA9, 0xAA,
        0xA2, 0xBB,
        0xA0, 0xCC,
        0xEA,
        0x20, 0x0D, 0xC0,
        0x4C, 0x0A, 0xC0,
        0xA9, 0x11,
        0x60,
    ])
    _reopen_monitor(session)

    with mt.check("Debug: load test program at $C000 (via REST)"):
        mt.write_rest_memory(rest_host, 0xC000, program)
        # Sanity-read it back; the monitor REST endpoint occasionally needs a
        # second tick to settle when poke / peek hit the same DMA aperture.
        readback = mt.read_rest_memory(rest_host, 0xC000, len(program))
        if readback != program:
            raise mt.Failure(
                f"Test program did not round-trip via REST:\n"
                f"  wrote: {program.hex()}\n"
                f"  read:  {readback.hex()}")

    with mt.check("Debug: G with BP at $C006 stops with A=$AA X=$BB Y=$CC"):
        # Park the cursor at $C000 (G uses cursor as start PC when no
        # captured context exists yet) and enter Debug mode.
        session.goto("C000")
        session.send_char("A")     # ensure ASM view
        session.send_char("D")     # enter Debug
        # Set a breakpoint at $C006 by parking the cursor there and pressing R.
        session.send_char("J")
        session.send_text("C006\r", "J C006")
        session.send_char("R")
        session.send_key("ENTER")  # dismiss SET popup
        # Bring cursor back to $C000 so G's start_pc fallback runs from there.
        session.send_char("J")
        session.send_text("C000\r", "J C000")
        # Go: NMI-jump to $C000, run, BRK at $C006, capture.
        session.send_char("G")
        parsed = _wait_for_pc(session, "C006")
        if parsed["ac"] != "AA":
            raise mt.Failure(f"AC expected AA, got {parsed!r}")
        if parsed["xr"] != "BB":
            raise mt.Failure(f"XR expected BB, got {parsed!r}")
        if parsed["yr"] != "CC":
            raise mt.Failure(f"YR expected CC, got {parsed!r}")
        # SR must be a binary string with the B (break) bit set after BRK.
        if len(parsed["sr"]) != 8 or any(c not in "01" for c in parsed["sr"]):
            raise mt.Failure(f"SR must be 8 binary digits, got {parsed!r}")

    with mt.check("Debug: Over from $C006 steps NOP and stops at JSR ($C007)"):
        # Cursor is at $C006 (the NOP). D = Over: execute the NOP, stop at C007 (JSR).
        session.send_char("D")
        parsed = _wait_for_pc(session, "C007")

    with mt.check("Debug: Trace into JSR enters subroutine at $C00D"):
        # Trace at JSR: should land at the JSR target ($C00D).
        session.send_char("T")
        parsed = _wait_for_pc(session, "C00D")

    with mt.check("Debug: Out from inside the subroutine returns to $C00A"):
        # We are inside the subroutine at $C00D. Out should run LDA #$11
        # then RTS and stop at the instruction after the JSR (i.e. $C00A).
        session.send_char("O")
        parsed = _wait_for_pc(session, "C00A")
        if parsed["ac"] != "11":
            raise mt.Failure(f"Subroutine should have set AC to $11, got {parsed!r}")

    with mt.check("Debug: cleanup restores user bytes at the breakpoint"):
        _ensure_no_debug(session)
        # The byte at $C006 must be the original NOP again.
        b = mt.read_rest_memory(rest_host, 0xC006, 1)[0]
        if b != 0xEA:
            raise mt.Failure(
                f"BRK was not restored at $C006: read ${b:02X}, expected $EA")
        # Vector $0316/$0317 must be the saved value, not pointing into our
        # cassette-buffer trampoline.
        vec = mt.read_rest_memory(rest_host, 0x0316, 2)
        vec_addr = vec[0] | (vec[1] << 8)
        if 0x033C <= vec_addr < 0x0400:
            raise mt.Failure(
                f"BRK vector still points into the debug trampoline area: ${vec_addr:04X}")


def run_side_effect_step_tests(rest_host: str, session: "mt.MonitorSession") -> None:
    """Prove stepping executes real instructions by checking RAM side effects."""

    # $C100: A9 10       LDA #$10
    # $C102: 8D 80 C1    STA $C180
    # $C105: 20 30 C1    JSR $C130
    # $C108: A9 00       LDA #$00
    # $C10A: F0 05       BEQ $C111
    # $C10C: A9 EE       LDA #$EE       ; skipped when branch is taken
    # $C10E: 8D 82 C1    STA $C182      ; skipped when branch is taken
    # $C111: 4C 18 C1    JMP $C118
    # $C114: EA          NOP            ; padding / should not execute
    # $C115: EA          NOP
    # $C116: EA          NOP
    # $C117: EA          NOP
    # $C118: A9 44       LDA #$44
    # $C11A: 8D 83 C1    STA $C183
    # $C11D: 4C 1D C1    JMP $C11D
    # $C130: EE 81 C1    INC $C181
    # $C133: 60          RTS
    program = bytes([
        0xA9, 0x10,
        0x8D, 0x80, 0xC1,
        0x20, 0x30, 0xC1,
        0xA9, 0x00,
        0xF0, 0x05,
        0xA9, 0xEE,
        0x8D, 0x82, 0xC1,
        0x4C, 0x18, 0xC1,
        0xEA, 0xEA, 0xEA, 0xEA,
        0xA9, 0x44,
        0x8D, 0x83, 0xC1,
        0x4C, 0x1D, 0xC1,
    ])
    subroutine = bytes([0xEE, 0x81, 0xC1, 0x60])

    _reopen_monitor(session)

    with mt.check("Debug: load side-effect stepping program"):
        mt.write_rest_memory(rest_host, 0xC100, program)
        mt.write_rest_memory(rest_host, 0xC130, subroutine)
        mt.write_rest_memory(rest_host, 0xC180, bytes([0x00, 0x00, 0x00, 0x00]))
        readback = mt.read_rest_memory(rest_host, 0xC100, len(program))
        if readback != program:
            raise mt.Failure(f"Side-effect program readback mismatch: {readback.hex()}")

    with mt.check("Debug: Over STA writes $C180"):
        session.goto("C100")
        session.send_char("A")
        session.send_char("D")
        session.send_char("D")  # LDA #$10
        _wait_for_pc(session, "C102")
        session.send_char("D")  # STA $C180
        _wait_for_pc(session, "C105")
        side = mt.read_rest_memory(rest_host, 0xC180, 4)
        if side != bytes([0x10, 0x00, 0x00, 0x00]):
            raise mt.Failure(f"STA side effect missing after Over: {side.hex()}")

    with mt.check("Debug: Over JSR runs subroutine side effect and stops after call"):
        session.send_char("D")
        parsed = _wait_for_pc(session, "C108")
        side = mt.read_rest_memory(rest_host, 0xC180, 4)
        if side != bytes([0x10, 0x01, 0x00, 0x00]):
            raise mt.Failure(f"JSR side effect missing after Over: footer={parsed!r} side={side.hex()}")

    with mt.check("Debug: Over taken branch skips skipped-store side effect"):
        session.send_char("D")  # LDA #$00, sets Z
        _wait_for_pc(session, "C10A")
        session.send_char("D")  # BEQ $C111, taken
        _wait_for_pc(session, "C111")
        side = mt.read_rest_memory(rest_host, 0xC180, 4)
        if side != bytes([0x10, 0x01, 0x00, 0x00]):
            raise mt.Failure(f"Taken branch executed skipped store: {side.hex()}")

    with mt.check("Debug: Over JMP reaches target and later store mutates $C183"):
        session.send_char("D")  # JMP $C118
        _wait_for_pc(session, "C118")
        session.send_char("D")  # LDA #$44
        _wait_for_pc(session, "C11A")
        session.send_char("D")  # STA $C183
        _wait_for_pc(session, "C11D")
        side = mt.read_rest_memory(rest_host, 0xC180, 4)
        if side != bytes([0x10, 0x01, 0x00, 0x44]):
            raise mt.Failure(f"JMP-target store side effect missing: {side.hex()}")
        _ensure_no_debug(session)

    with mt.check("Debug: Trace JSR enters before subroutine side effect, then Over executes it"):
        mt.write_rest_memory(rest_host, 0xC181, bytes([0x00]))
        _reopen_monitor(session)
        session.goto("C105")
        session.send_char("A")
        session.send_char("D")
        session.send_char("T")  # trace into JSR target, before INC executes
        _wait_for_pc(session, "C130")
        side = mt.read_rest_memory(rest_host, 0xC180, 4)
        if side[1] != 0x00:
            raise mt.Failure(f"Trace into JSR executed subroutine too early: {side.hex()}")
        session.send_char("D")  # Over INC $C181
        _wait_for_pc(session, "C133")
        side = mt.read_rest_memory(rest_host, 0xC180, 4)
        if side[1] != 0x01:
            raise mt.Failure(f"Over inside traced subroutine did not execute INC: {side.hex()}")
        _ensure_no_debug(session)


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Validate Machine Code Monitor Debug mode over the standard U64 telnet service")
    parser.add_argument("--host", default=os.environ.get("U64_MONITOR_HOST", "u64"))
    parser.add_argument("--port", type=int, default=int(os.environ.get("U64_MONITOR_PORT", "23")))
    parser.add_argument("--rest-host", default=os.environ.get("U64_MONITOR_REST_HOST"))
    parser.add_argument("--password", default=os.environ.get("U64_MONITOR_PASSWORD"))
    parser.add_argument("--timeout", type=float, default=float(os.environ.get("U64_MONITOR_TIMEOUT", "5.0")))
    args = parser.parse_args()

    rest_host = args.rest_host or args.host
    session = None
    try:
        session = mt.MonitorSession(args.host, args.port, args.password, args.timeout)
        run_debug_tests(rest_host, session)
        run_flag_control_flow_tests(rest_host, session)
        run_refusal_and_return_edge_tests(rest_host, session)
        run_page_cross_and_indirect_jump_tests(rest_host, session)
        run_nested_out_tests(rest_host, session)
        run_brk_orchestrator_tests(rest_host, session)
        run_side_effect_step_tests(rest_host, session)
        run_breakpoint_reentry_tests(rest_host, session)
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
