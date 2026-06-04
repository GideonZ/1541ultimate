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
import re
import sys
import time
import urllib.request
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

FORBIDDEN_DEBUG_TEXT = (
    "UNSAFE TARGET",
    "DEBUG TIMEOUT",
    "TIMEOUT",
    "PATCH FAILED",
    "DEBUG NOT SUPPORTED",
    "NO CONTEXT",
    "ASSERT",
    "ERROR",
)

READY_SCREEN_TOKEN = bytes([0x12, 0x05, 0x01, 0x04, 0x19])
SAFE_CPU_PORT_VALUE = 0x37
SAFE_CPU_PORT_LOW_BITS = 0x07

# Optional suite chunking for diagnostics. The default is a true single-process full
# pass; use --chunk-size explicitly when investigating service-path slowdowns.
SUITE_CHUNK_SIZE = 0
SUITE_CHUNK_COOLDOWN_SECONDS = 6.0

# Post-group hygiene recovery. The per-group cleanup resets the C64 and then probes
# the jiffy clock ($00A2) to prove the KERNAL IRQ is actually running. Under sustained
# full-suite load the debug-exit resume / reset can be sluggish, so that probe
# occasionally runs before the freshly-reset KERNAL IRQ has restarted ($A2 frozen for
# the whole probe window, then it recovers). Rather than aborting the entire suite on
# that transient, we cool down, hard-reset again, and retry the hygiene assertion up
# to this many times. A genuinely wedged machine stays frozen across every recovery
# and still raises.
HYGIENE_RECOVERY_ATTEMPTS = 3


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


def _assert_no_debug_modal_snapshot(snap: mt.Snapshot, context: str) -> None:
    text = snap.text()
    for token in FORBIDDEN_DEBUG_TEXT:
        if token in text:
            raise mt.Failure(f"{context}: unexpected {token!r} after {snap.last_command}\n{text}")


def _assert_no_debug_modal(session: "mt.MonitorSession", context: str) -> None:
    _assert_no_debug_modal_snapshot(session.capture(), context)


def _assert_snapshot_contains(snap: mt.Snapshot, token: str, context: str) -> None:
    if token not in snap.text():
        raise mt.Failure(f"{context}: missing {token!r} after {snap.last_command}\n{snap.text()}")


def _assert_status_tokens(snap: mt.Snapshot, context: str, *tokens: str) -> None:
    line_index = snap.find_status_line()
    line = snap.line(line_index)
    for token in tokens:
        if token not in line:
            raise mt.Failure(f"{context}: status line missing {token!r}: {line!r}\n{snap.text()}")


def _disassembly_row(snap: mt.Snapshot, address: int) -> str:
    wanted = f"|{address:04X} "
    for y in range(mt.HEIGHT):
        line = snap.line(y)
        if line.startswith(wanted):
            return line
    raise mt.Failure(f"Disassembly row ${address:04X} not visible after {snap.last_command}\n{snap.text()}")


def _memory_row(snap: mt.Snapshot, address: int) -> str:
    wanted = f"|{address:04X} "
    for y in range(mt.HEIGHT):
        line = snap.line(y)
        if line.startswith(wanted):
            return line
    raise mt.Failure(f"Memory row ${address:04X} not visible after {snap.last_command}\n{snap.text()}")


def _instruction_length_from_row(row: str) -> int:
    fields = row[1:].split()
    if not fields:
        raise mt.Failure(f"Cannot parse disassembly row: {row!r}")
    length = 0
    for token in fields[1:4]:
        if len(token) == 2 and all(ch in "0123456789ABCDEFabcdef" for ch in token):
            length += 1
            continue
        break
    if length <= 0:
        raise mt.Failure(f"Cannot determine instruction length from row: {row!r}")
    return length


def _jsr_target_from_row(row: str) -> int:
    match = re.search(r"\bJSR \$([0-9A-Fa-f]{4})", row)
    if not match:
        raise mt.Failure(f"Expected JSR row, got: {row!r}")
    return int(match.group(1), 16)


def _find_visible_jsr_row(snap: mt.Snapshot, marker: str) -> tuple[int, str]:
    for y in range(mt.HEIGHT):
        line = snap.line(y)
        if f"[{marker}]" not in line or "JSR $" not in line:
            continue
        if len(line) >= 6 and line[0] == "|" and line[5] == " ":
            addr_text = line[1:5]
            if all(ch in "0123456789ABCDEFabcdef" for ch in addr_text):
                return int(addr_text, 16), line
    raise mt.Failure(f"No visible [{marker}] JSR row found after {snap.last_command}\n{snap.text()}")


def _dump_debug_scratch(rest_host: str, context: str) -> None:
    """Print the RAM-resident BRK debug scratch + vectors for [43] diagnosis.

    All addresses are CPU-visible RAM read via REST DMA. The cassette buffer
    ($0363-$03FB) holds the debug HANDLER/TRAMPOLINE/NMI_TRAMPOLINE/HARD_BRK_STUB
    and register stores. Reset heals the FPGA ROM image but NOT this RAM, so a
    stale launch-trampoline JMP target or stale vector here is the prime suspect
    for the second-pass "$0002" capture.
    """
    try:
        regions = [
            ("zp0000", 0x0000, 8),
            ("softvec_0314", 0x0314, 6),     # IRQ/BRK/NMI soft vectors
            ("handler_0363", 0x0363, 0x27),  # HANDLER (ends spin JMP @ $0387)
            ("tramp_038A", 0x038A, 0x26),    # TRAMPOLINE
            ("nmitramp_03B0", 0x03B0, 0x18), # NMI_TRAMPOLINE
            ("hardstub_03C8", 0x03C8, 0x28), # HARD_BRK_STUB + orig vector + stores
            ("hardnmi_FFFA", 0xFFFA, 2),     # CPU-visible hard NMI vector
        ]
        print(f"    ---- DEBUG SCRATCH DUMP ({context}) ----", flush=True)
        for name, addr, length in regions:
            data = mt.read_rest_memory(rest_host, addr, length)
            hexs = " ".join(f"{b:02X}" for b in data)
            print(f"      {name} ${addr:04X}: {hexs}", flush=True)
        print("    ---- END DEBUG SCRATCH DUMP ----", flush=True)
    except Exception as exc:  # noqa: BLE001
        print(f"    [scratch dump failed: {exc!r}]", flush=True)


def _enter_rom_debug_at(session: "mt.MonitorSession", address: int, marker: str,
                        context: str, *status_tokens: str) -> mt.Snapshot:
    _reopen_monitor(session)
    mt.ensure_status(session, "CPU7 $A:BAS $D:I/O $E:KRN VIC")
    _ensure_no_debug(session)
    session.goto(f"{address:04X}")
    session.send_char("A")
    header = _header_line(session)
    if "Dbg" not in header:
        session.send_char("D")
    snap = session.capture()
    _assert_no_debug_modal_snapshot(snap, context)
    _assert_snapshot_contains(snap, f"MONITOR ASM ${address:04X}", context)
    _assert_snapshot_contains(snap, f"[{marker}]", context)
    _assert_status_tokens(snap, context, "CPU7", *status_tokens)
    row = _disassembly_row(snap, address)
    if f"[{marker}]" not in row:
        raise mt.Failure(f"{context}: row ${address:04X} missing [{marker}]: {row!r}\n{snap.text()}")
    return snap


def _assert_debug_pc(session: "mt.MonitorSession", expected_pc: int, context: str) -> dict:
    expected = f"{expected_pc:04X}"
    parsed = _wait_for_pc(session, expected)
    header = _header_line(session)
    if f"MONITOR ASM ${expected}" not in header:
        raise mt.Failure(f"{context}: header did not follow PC ${expected}: {header!r}")
    snap = session.capture()
    _disassembly_row(snap, expected_pc)
    return parsed


def _step_and_assert_pc(session: "mt.MonitorSession", key: str,
                        expected_pc: int, context: str) -> dict:
    snap = session.send_char(key)
    _assert_no_debug_modal_snapshot(snap, context)
    return _assert_debug_pc(session, expected_pc, context)


def _ensure_breakpoint_at(session: "mt.MonitorSession", address: int,
                          context: str) -> None:
    row = _disassembly_row(session.capture(), address)
    if "[BRK" in row:
        return
    session.send_char("R")
    _assert_no_debug_modal(session, context)
    row = _disassembly_row(session.capture(), address)
    if "[BRK" not in row:
        raise mt.Failure(f"{context}: breakpoint was not set at ${address:04X}: {row!r}")


def _clear_breakpoint_at(session: "mt.MonitorSession", address: int,
                         context: str) -> None:
    session.goto(f"{address:04X}")
    row = _disassembly_row(session.capture(), address)
    if "[BRK" not in row:
        return
    session.send_char("R")
    _assert_no_debug_modal(session, context)
    row = _disassembly_row(session.capture(), address)
    if "[BRK" in row:
        raise mt.Failure(f"{context}: breakpoint was not cleared at ${address:04X}: {row!r}")


def _clear_all_breakpoints(session: "mt.MonitorSession", context: str) -> None:
    session.last_command = "CTRL_R_CLEAR_ALL"
    session.sock.sendall(b"\x12")
    snap = session.capture()
    if "BREAKPOINTS" not in snap.text():
        raise mt.Failure(f"{context}: breakpoint popup did not open:\n{snap.text()}")

    session.send_key_repeat("UP", 9)
    for slot in range(10):
        snap = session.capture()
        line = next((snap.line(y) for y in range(mt.HEIGHT)
                     if re.match(rf"^\|{slot} ", snap.line(y))), "")
        if not line:
            raise mt.Failure(f"{context}: slot {slot} missing from breakpoint popup:\n{snap.text()}")
        if "EMPTY" not in line:
            snap = session.send_key("DEL")
            if "BREAKPOINTS" not in snap.text():
                raise mt.Failure(f"{context}: DEL left the breakpoint popup unexpectedly:\n{snap.text()}")
        if slot < 9:
            session.send_key("DOWN")

    session.send_key("ESC")


def _leave_debug_and_reset(rest_host: str, session: "mt.MonitorSession") -> None:
    _ensure_no_debug(session)
    _reset_c64_core(rest_host)
    _reopen_monitor(session)


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


def _reset_c64_core(rest_host: str, timeout: float = 8.0) -> None:
    url = f"http://{rest_host}/v1/machine:reset"
    request = urllib.request.Request(url, data=b"", method="PUT")
    try:
        with urllib.request.urlopen(request, timeout=max(5.0, timeout)):
            pass
    except (OSError, TimeoutError, urllib.error.URLError) as exc:
        # The firmware may reset the C64 and briefly starve the REST response path
        # before the HTTP request is completed. Treat the response timeout as
        # recoverable only if the deterministic READY proof below succeeds.
        print(f"[info] reset response timed out, verifying READY anyway: {exc}", flush=True)
    _wait_for_c64_ready(rest_host, timeout)
    time.sleep(3.0)


def _chunk_boundary_recovery(rest_host: str, context: str, cooldown: float,
                             timeout: float = 8.0) -> None:
    """Drain accumulated REST/USB service load at a suite chunk boundary.

    Called between blocks of SUITE_CHUNK_SIZE groups. A plain per-group soft reset
    does not clear the gradual device slowdown that builds up over a long pass, so
    here we additionally idle for `cooldown` seconds (letting the firmware service
    path settle) and then issue one extra soft reset before the next block starts.
    Deliberately avoids any full device reboot, which would drop the JTAG-loaded
    firmware image. See WORKLOG.md Phase 3.
    """
    if cooldown <= 0:
        return
    print(f"[info] chunk boundary ({context}): cooling down {cooldown:.0f}s + "
          f"extra reset to drain accumulated load", flush=True)
    time.sleep(cooldown)
    _reset_c64_core(rest_host, timeout)


def _wait_for_c64_ready(rest_host: str, timeout: float = 8.0) -> None:
    deadline = time.time() + timeout
    stable_since = 0.0
    while time.time() < deadline:
        screen = mt.read_rest_memory(rest_host, 0x0400, 1000)
        if READY_SCREEN_TOKEN in screen:
            now = time.time()
            if stable_since == 0.0:
                stable_since = now
            elif now - stable_since >= 0.75:
                return
        else:
            stable_since = 0.0
        time.sleep(0.1)
    raise mt.Failure("C64 core reset did not reach READY prompt")


def _live_cpu_bank_from_status(status: str) -> int:
    match = re.search(r"\bCPU([0-7])\b|\bC([0-7])O[0-7]\b", status)
    if match is None:
        raise mt.Failure(f"Could not read live CPU bank from status line: {status!r}")
    value = match.group(1) if match.group(1) is not None else match.group(2)
    return int(value)


def _assert_safe_banking_display_hygiene(rest_host: str, session: "mt.MonitorSession",
                                         context: str) -> None:
    screen = mt.read_rest_memory(rest_host, 0x0400, 1000)
    if READY_SCREEN_TOKEN not in screen:
        raise mt.Failure(f"{context}: C64 READY screen is not readable/deterministic")

    _reopen_monitor(session)
    snap = session.capture()
    line = snap.line(snap.find_status_line())
    live_bank = _live_cpu_bank_from_status(line)
    if live_bank != SAFE_CPU_PORT_LOW_BITS:
        raise mt.Failure(
            f"{context}: live $0001 low bits are CPU{live_bank}, "
            f"expected CPU{SAFE_CPU_PORT_LOW_BITS}: {line!r}\n{snap.text()}")
    if "$D:I/O" not in line:
        raise mt.Failure(f"{context}: I/O is not visible in status line: {line!r}\n{snap.text()}")
    _assert_rest_byte_changes(rest_host, 0x00A2, f"{context}: jiffy clock responsiveness",
                              minimum_values=2)


def _force_safe_cpu_port(rest_host: str, context: str) -> None:
    # Some destructive banking regressions deliberately run with $0001 low bits
    # clear, which can keep KERNAL/I/O hidden across a raw C64 reset. Restore only
    # the 6510 port latch through the same REST memory aperture used by the tests,
    # then prove the low bits before issuing reset.
    mt.write_rest_memory(rest_host, 0x0001, bytes([SAFE_CPU_PORT_VALUE]))
    readback = mt.read_rest_memory(rest_host, 0x0001, 1)[0]
    if (readback & 0x07) != SAFE_CPU_PORT_LOW_BITS:
        raise mt.Failure(
            f"{context}: failed to restore $0001 low bits, read ${readback:02X}")


def _restore_safe_banking_display_hygiene(rest_host: str, session: "mt.MonitorSession",
                                          context: str) -> None:
    # Reset + verify, but tolerate a transient load-degraded slow resume: if the
    # hygiene assertion (notably the jiffy-clock responsiveness probe) misses, cool
    # down, hard-reset again, and retry. Only a persistently wedged machine fails.
    last_exc: "mt.Failure | None" = None
    for attempt in range(HYGIENE_RECOVERY_ATTEMPTS):
        _force_safe_cpu_port(rest_host, context)
        _reset_c64_core(rest_host)
        try:
            _assert_safe_banking_display_hygiene(rest_host, session, context)
            return
        except mt.Failure as exc:
            last_exc = exc
            if attempt + 1 >= HYGIENE_RECOVERY_ATTEMPTS:
                break
            print(f"[info] {context}: hygiene miss ({exc}); load-degradation "
                  f"recovery {attempt + 1}/{HYGIENE_RECOVERY_ATTEMPTS - 1} "
                  f"(cool down {SUITE_CHUNK_COOLDOWN_SECONDS:.0f}s + reset)",
                  flush=True)
            time.sleep(SUITE_CHUNK_COOLDOWN_SECONDS)
    assert last_exc is not None
    raise last_exc


def _reset_monitor_and_c64(rest_host: str, session: "mt.MonitorSession",
                           timeout: float = 8.0) -> None:
    _reopen_monitor(session)
    _send_ctrl_x(session)
    _wait_for_c64_ready(rest_host, timeout)
    # C=+X tears down the live Debug session, but the REST reset path is the
    # deterministic core reset used by standalone hardware runs.
    _reset_c64_core(rest_host, timeout)
    try:
        _wait_for_blank_debug_context(session, 2.0)
    except mt.Failure:
        pass
    _reopen_monitor(session)


def run_debug_tests(rest_host: str, session: "mt.MonitorSession") -> None:
    # Park the cursor at a known address so the screen is deterministic.
    c050_breakpoint_slot = 0

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

    with mt.check("Debug: clear stale breakpoint slots before exercising popup flows"):
        _clear_all_breakpoints(session, "suite setup breakpoint cleanup")

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
                      "C=+B List", "D Step Over", "T Step Into", "U Step Out",
                      "C=+R", "C=+D", "C=+X", "RSTOP"):
            if token not in joined:
                raise mt.Failure(f"Debug help missing {token!r}:\n{joined}")
        if "ESC" in joined:
            raise mt.Failure(f"Debug help must show RSTOP, not ESC:\n{joined}")
        session.send_key("ESC")

    with mt.check("Debug: R toggles a breakpoint (set + clear)"):
        before = session.capture().text()
        session.send_char("R")
        after_set = session.capture().text()
        if "BRK" in after_set and "SET" in after_set and after_set != before:
            raise mt.Failure("R must not show a redundant BRK SET popup")
        session.send_char("R")
        after_clear = session.capture().text()
        if "BRK" in after_clear and "CLR" in after_clear and after_clear != after_set:
            raise mt.Failure("Second R must not show a redundant BRK CLR popup")

    with mt.check("Debug: C=+R opens the breakpoint list popup"):
        session.last_command = "CTRL_R"
        session.sock.sendall(b"\x12")
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
        _clear_breakpoint_at(session, 0xC050, "$C050 stale breakpoint clear")
        session.goto("C050")
        session.send_char("R")
        lines = _capture_lines(session)
        row = next((line for line in lines if line.startswith("|C050 ")), "")
        if not re.search(r"\[BRK\d\]\[RAM\]", row):
            raise mt.Failure(f"Breakpoint line must show [BRKx][RAM], got: {row!r}")

    with mt.check("Debug: C=+R shows the live breakpoint list"):
        session.last_command = "CTRL_R_WITH_BREAKPOINT"
        session.sock.sendall(b"\x12")
        snap = session.capture()
        joined = "\n".join(snap.line(y) for y in range(mt.HEIGHT))
        if "BREAKPOINTS" not in joined:
            raise mt.Failure(f"C=+R did not open breakpoint list:\n{joined}")
        if "$C050" not in joined or "SET" not in joined:
            raise mt.Failure(f"Breakpoint list did not show the live breakpoint:\n{joined}")
        if "0-9/RET:Jmp S:Set L:Lbl E:Enbl DEL:Res" not in joined:
            raise mt.Failure(f"Breakpoint list help row did not list jump/store/label/enable/reset:\n{joined}")
        slot_match = re.search(r"^\|?(\d) SET \$C050\b", joined, re.MULTILINE)
        if not slot_match:
            raise mt.Failure(f"Breakpoint list did not expose the $C050 slot number:\n{joined}")
        c050_breakpoint_slot = int(slot_match.group(1))
        session.send_key_repeat("UP", 9)
        if c050_breakpoint_slot > 0:
            session.send_key_repeat("DOWN", c050_breakpoint_slot)
        session.send_char("L")
        snap = session.send_text("READ\r", "BP label READ")
        text = snap.text()
        if f"{c050_breakpoint_slot} SET READ" not in text or "$C050" not in text:
            raise mt.Failure(f"Breakpoint label edit did not update the live slot:\n{text}")
        session.send_key("ESC")

    with mt.check("Debug: breakpoint label replaces [BRKx] on the ASM page"):
        lines = _capture_lines(session)
        row = next((line for line in lines if line.startswith("|C050 ")), "")
        if "[READ][RAM]" not in row:
            raise mt.Failure(f"Labelled breakpoint line must show [READ][RAM], got: {row!r}")
        if re.search(r"\[BRK\d\]", row):
            raise mt.Failure(f"Labelled breakpoint line must not also show [BRKx], got: {row!r}")
        session.send_char("R")

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
        mt.write_rest_memory(rest_host, 0xC040, bytes([0xA9, 0x66, 0xEA, 0x4C, 0x42, 0xC0]))
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

    with mt.check("Debug: C=+D from Debug+Edit clears both and allows re-debug"):
        session.send_char("D")
        session.send_char("E")
        header = _header_line(session)
        if "Dbg" not in header or "Edit" not in header:
            raise mt.Failure(f"Header must show both Dbg and Edit: {header!r}")
        # C=+D leaves both Debug and Edit so the next keystroke is a monitor
        # command again. This mirrors the authoritative host regression
        # test_ctrl_d_from_edit_clears_edit_for_redebug: the user can leave a
        # debug+edit session with one key and immediately navigate/re-debug.
        _send_ctrl_d(session)
        header = _header_line(session)
        if "Dbg" in header or "Edit" in header:
            raise mt.Failure(f"C=+D in Debug+Edit must clear both Dbg and Edit: {header!r}")
        # Prove Edit really cleared: J is consumed as a monitor jump command,
        # not as edit-mode text input.
        session.goto("C040")
        session.send_char("A")
        header = _header_line(session)
        if "MONITOR ASM $C040" not in header or "Edit" in header:
            raise mt.Failure(f"After C=+D, J must act as a monitor jump command: {header!r}")
        # Re-enter Debug from the post-C=+D cursor and confirm a step runs.
        session.send_char("D")
        session.send_char("D")
        parsed = _wait_for_pc(session, "C042")
        if parsed["ac"] != "66":
            raise mt.Failure(f"Re-entered Debug must step from the cursor, got {parsed!r}")
        _ensure_no_debug(session)

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
        mt.write_rest_memory(rest_host, 0xC040, bytes([0xA9, 0x66, 0xEA, 0x4C, 0x42, 0xC0]))
        session.goto("C040")
        session.send_char("A")
        session.send_char("D")  # Over from the restored Debug cursor, not live snapshot state
        parsed = _wait_for_pc(session, "C042")
        if parsed["ac"] != "66":
            raise mt.Failure(f"No-context Over should execute LDA #$66, got {parsed!r}")
        _ensure_no_debug(session)


# ---------------------------------------------------------------------------
# Helpers for the BRK orchestrator tests below.

def _ensure_no_debug(session: "mt.MonitorSession") -> None:
    """Leave Debug mode if currently active."""
    deadline = time.time() + 3.0
    sent = False
    while time.time() < deadline:
        header = _header_line(session)
        if "Dbg" not in header:
            if sent:
                time.sleep(0.2)
            return
        if not sent:
            _send_ctrl_d(session)
        session.capture()
        sent = True
        time.sleep(0.1)
    raise mt.Failure("Could not leave Debug mode")


def _reopen_monitor(session: "mt.MonitorSession") -> None:
    try:
        _ensure_no_debug(session)
    except mt.Failure:
        # The previous command may already have left the monitor. Continue
        # with the same CTRL+O entry path used by the normal telnet harness.
        pass
    last_error = None
    for _ in range(6):
        try:
            snap = session.send_key("CTRL_O")
            snap.find_status_line()
            return
        except mt.Failure as exc:
            last_error = exc
    raise mt.Failure(f"Could not re-enter monitor: {last_error}")


def run_ram_edit_regression_tests(rest_host: str, session: "mt.MonitorSession") -> None:
    """Prove ASM Edit at $2000 writes real RAM and refreshes ASM/Hex readback."""

    base = 0x2000
    seed = bytes([0xFF] * 16)
    program = bytes([
        0xA9, 0x01,             # LDA #$01
        0x8D, 0x20, 0xD0,       # STA $D020
        0xEE, 0x20, 0xD0,       # INC $D020
        0x4C, 0x05, 0x20,       # JMP $2005
    ])

    with mt.check("Debug: ASM Edit at $2000 writes RAM and refreshes ASM/Hex"):
        _reopen_monitor(session)
        mt.write_rest_memory(rest_host, base, seed)
        seeded = mt.read_rest_memory(rest_host, base, len(seed))
        if seeded != seed:
            raise mt.Failure(f"$2000 seed did not round-trip before edit: {seeded.hex().upper()}")

        snap = session.goto(f"{base:04X}")
        snap = session.send_char("A")
        _assert_no_debug_modal_snapshot(snap, "$2000 RAM edit setup")
        row = _disassembly_row(snap, base)
        if "[RAM]" not in row or "[KRN]" in row or "[BAS]" in row or "[I/O]" in row:
            raise mt.Failure(f"$2000 must be ordinary RAM before edit, got: {row!r}\n{snap.text()}")

        snap = session.send_char("E")
        if "Edit" not in _header_line(session):
            raise mt.Failure(f"$2000 ASM edit did not enter Edit mode:\n{snap.text()}")

        commands = (
            ("LDA#$01", "LDA #$01"),
            ("STA$D020", "STA $D020"),
            ("INC$D020", "INC $D020"),
            ("JMP$2005", "JMP $2005"),
        )
        for typed, label in commands:
            for ch in typed:
                snap = session.send_char(ch)
            snap = session.send_key("ENTER")
            _assert_no_debug_modal_snapshot(snap, f"$2000 ASM edit {label}")

        snap = session.send_key("ESC")
        if "Edit" in _header_line(session):
            raise mt.Failure(f"$2000 ASM edit did not leave Edit mode:\n{snap.text()}")

        readback = mt.read_rest_memory(rest_host, base, len(program))
        if readback != program:
            raise mt.Failure(
                "$2000 ASM Edit bytes are not in RAM:\n"
                f"  expected: {program.hex().upper()}\n"
                f"  actual:   {readback.hex().upper()}\n"
                f"{snap.text()}")

        snap = session.goto(f"{base:04X}")
        snap = session.send_char("A")
        _assert_no_debug_modal_snapshot(snap, "$2000 ASM edit ASM readback")
        expected_rows = (
            (0x2000, "A9 01", "LDA #$01"),
            (0x2002, "8D 20 D0", "STA $D020"),
            (0x2005, "EE 20 D0", "INC $D020"),
            (0x2008, "4C 05 20", "JMP $2005"),
        )
        for address, raw, text in expected_rows:
            row = _disassembly_row(snap, address)
            if raw not in row or text not in row or "[RAM]" not in row:
                raise mt.Failure(
                    f"$2000 ASM readback row ${address:04X} mismatch: {row!r}\n{snap.text()}")
            if "FF" in row[:15]:
                raise mt.Failure(f"$2000 ASM readback still shows FF in row: {row!r}\n{snap.text()}")

        snap = mt.ensure_hex_width(session, 8)
        row0 = _memory_row(snap, base)
        row8 = _memory_row(snap, base + 8)
        monitor_bytes = mt.parse_memory_row(snap, base) + mt.parse_memory_row(snap, base + 8)
        if monitor_bytes[:len(program)] != program:
            raise mt.Failure(
                "$2000 Hex view does not show the edited RAM bytes:\n"
                f"  row0:     {row0!r}\n"
                f"  row8:     {row8!r}\n"
                f"  expected: {program.hex().upper()}\n"
                f"  actual:   {monitor_bytes[:len(program)].hex().upper()}\n"
                f"{snap.text()}")
        edited_tokens = (row0.split()[1:9] + row8.split()[1:4])
        if "FF" in edited_tokens:
            raise mt.Failure(
                f"$2000 Hex view still shows FF in edited bytes: {row0!r} / {row8!r}\n{snap.text()}")


def _parse_footer_values(values_line: str):
    """Extract PC / AC / XR / YR / SP / SR-binary from the footer value row."""
    # Strip the window border.
    body = values_line.lstrip("|+").rstrip("|+").rstrip()
    # Positions inside the body track the firmware footer layout exactly.
    fields = {
        "pc": body[0:4],
        "ac": body[5:7],
        "xr": body[8:10],
        "yr": body[11:13],
        "sp": body[14:16],
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
        snap = session.capture()
        _assert_no_debug_modal_snapshot(snap, f"Waiting for PC {expected_pc}")
        values = _footer_value_line(session)
        parsed = _parse_footer_values(values)
        last = parsed
        if parsed["pc"].upper() == expected_pc.upper():
            return parsed
        _time.sleep(0.1)
    raise mt.Failure(f"PC did not reach {expected_pc}; last footer={last!r} ({values!r})")


def _wait_for_pc_register(session: "mt.MonitorSession", expected_pc: str,
                          register: str, expected_value: str,
                          timeout: float = 6.0) -> dict:
    """Poll until the footer reaches PC with a register value from the new stop."""
    import time as _time
    deadline = _time.time() + timeout
    last = None
    register = register.lower()
    while _time.time() < deadline:
        snap = session.capture()
        _assert_no_debug_modal_snapshot(
            snap, f"Waiting for PC {expected_pc} {register.upper()}={expected_value}")
        values = _footer_value_line(session)
        parsed = _parse_footer_values(values)
        last = parsed
        if (parsed["pc"].upper() == expected_pc.upper() and
                parsed.get(register, "").upper() == expected_value.upper()):
            return parsed
        _time.sleep(0.1)
    raise mt.Failure(
        f"PC/register did not reach {expected_pc} {register.upper()}={expected_value}; "
        f"last footer={last!r} ({values!r})")


def _wait_for_pc_not(session: "mt.MonitorSession", previous_pc: str,
                     timeout: float = 6.0) -> dict:
    import time as _time
    deadline = _time.time() + timeout
    last = None
    while _time.time() < deadline:
        snap = session.capture()
        _assert_no_debug_modal_snapshot(snap, f"Waiting for PC to leave {previous_pc}")
        values = _footer_value_line(session)
        parsed = _parse_footer_values(values)
        last = parsed
        if parsed["pc"] and parsed["pc"].upper() != previous_pc.upper():
            return parsed
        _time.sleep(0.1)
    raise mt.Failure(f"PC did not advance from {previous_pc}; last footer={last!r}")


def _address_row_context(session: "mt.MonitorSession", pc: int, context: str) -> None:
    snap = session.capture()
    address_rows: list[tuple[int, str]] = []
    wanted = f"|{pc:04X} "
    wanted_index = -1
    for y in range(mt.HEIGHT):
        line = snap.line(y)
        if (len(line) >= 6 and line[0] == "|" and "[" in line and
                all(c in "0123456789ABCDEFabcdef" for c in line[1:5]) and
                line[5] == " "):
            if line.startswith(wanted):
                wanted_index = len(address_rows)
            address_rows.append((y, line))
    if wanted_index < 0:
        raise mt.Failure(f"{context}: current PC row {pc:04X} not visible:\n{snap.text()}")


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
    # $C215: 4C 14 C2    JMP $C214     ; park live CPU after leaving Debug
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
        0x4C, 0x14, 0xC2,
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
    rts_non_jsr_stack = bytes([
        0xA2, 0xFC,       # $C2B0: LDX #$FC
        0x9A,             # $C2B2: TXS
        0xA9, 0xFF,       # $C2B3: LDA #$FF
        0x8D, 0xFD, 0x01, # $C2B5: STA $01FD
        0xA9, 0x21,       # $C2B8: LDA #$21
        0x8D, 0xFE, 0x01, # $C2BA: STA $01FE
        0x60,             # $C2BD: RTS
        0xEA,             # $C2BE: NOP
    ])

    _reopen_monitor(session)

    with mt.check("Debug: unsafe-target/refusal fixtures load at $C240/$C260/$C280"):
        mt.write_rest_memory(rest_host, 0xC240, unsafe_program)
        mt.write_rest_memory(rest_host, 0xC25F, bytes([0xEA]))
        mt.write_rest_memory(rest_host, 0xC260, rts_caller)
        mt.write_rest_memory(rest_host, 0xC270, rts_subroutine)
        mt.write_rest_memory(rest_host, 0xC280, rti_setup)
        mt.write_rest_memory(rest_host, 0xC290, rti_target)
        mt.write_rest_memory(rest_host, 0xC2B0, rts_non_jsr_stack)
        mt.write_rest_memory(rest_host, 0x21FD, bytes([0xEA]))

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

    with mt.check("Debug: O outside a traced subroutine says NOT IN SUBROUTINE"):
        mt.write_rest_memory(rest_host, 0xC2A0, bytes([0xEE, 0x20, 0xD0, 0x4C, 0xA0, 0xC2]))
        session.goto("C2A0")
        session.send_char("A")
        session.send_char("D")
        session.send_char("U")
        snap = _wait_for_screen_text(session, "NOT IN SUBROUTINE")
        text = snap.text()
        if "UNSAFE TARGET" in text:
            raise mt.Failure(f"Invalid Step Out must not say UNSAFE TARGET:\n{text}")
        session.send_key("ENTER")
        _wait_for_blank_debug_context(session)
        _ensure_no_debug(session)

    with mt.check("Debug: Over on RTS without active JSR frame says NOT IN SUBROUTINE (not PATCH FAILED)"):
        _reopen_monitor(session)
        session.goto("C2B0")
        session.send_char("A")
        session.send_char("D")
        _wait_for_blank_debug_context(session)
        for expected_pc in ("C2B2", "C2B3", "C2B5", "C2B8", "C2BA", "C2BD"):
            session.send_char("D")
            _wait_for_pc(session, expected_pc)
        session.send_char("D")
        snap = _wait_for_screen_text(session, "NOT IN SUBROUTINE")
        text = snap.text()
        if "PATCH FAILED" in text:
            raise mt.Failure(f"RTS Over without active JSR must not say PATCH FAILED:\n{text}")
        session.send_key("ENTER")
        _ensure_no_debug(session)

    with mt.check("Debug: undocumented NOP is decoded by Undc but not debug-stepped"):
        session.goto("C243")
        session.send_char("A")
        # The Undc display toggle is U OUTSIDE Debug only; inside Debug U is Step
        # Out. Enable Undc here (before entering Debug) and verify $1A decodes as
        # NOP, then enter Debug and confirm the undocumented opcode refuses to
        # step regardless of the display flag.
        if "Undc" not in _header_line(session):
            session.send_char("U")
        header = _header_line(session)
        if "Undc" not in header:
            raise mt.Failure(f"Undc flag must appear after U: {header!r}")
        row = _disassembly_row(session.capture(), 0xC243)
        if "NOP" not in row:
            raise mt.Failure(f"Undc flag must decode $1A as NOP: {row!r}")
        session.send_char("D")
        session.send_char("D")
        _wait_for_screen_text(session, "UNSUPPORTED OPCODE")
        session.send_key("ENTER")
        _wait_for_blank_debug_context(session)
        _ensure_no_debug(session)
        # Restore the Undc display toggle off (outside Debug).
        if "Undc" in _header_line(session):
            session.send_char("U")

    with mt.check("Debug: traced RTS lands on the caller continuation address"):
        _reopen_monitor(session)
        session.goto("C25F")
        session.send_char("A")
        session.send_char("D")
        session.send_char("D")
        _wait_for_pc(session, "C260")
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
        0x4C, 0x02, 0xC2, # $C203: JMP $C202 (stable cleanup loop)
    ])
    branch_fallthrough = bytes([
        0xA9, 0x01,       # $C2FC: LDA #$01
        0xF0, 0x02,       # $C2FE: BEQ $C302 (not taken across page)
        0xA9, 0x44,       # $C300: LDA #$44 (must execute)
        0xEA,             # $C302: NOP
        0x4C, 0x02, 0xC3, # $C303: JMP $C302 (stable cleanup loop)
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
        session.send_char("U")
        parsed = _wait_for_pc(session, "C389")
        if mt.read_rest_memory(rest_host, 0xC193, 1)[0] != 0x33:
            raise mt.Failure(f"Inner Out did not execute the inner side effect: {parsed!r}")
        if mt.read_rest_memory(rest_host, 0xC191, 1)[0] != 0x00:
            raise mt.Failure("Inner Out must not run the caller-side store yet")
        session.send_char("U")
        parsed = _wait_for_pc(session, "C365")
        session.send_char("D")
        parsed = _wait_for_pc(session, "C368")
        if mt.read_rest_memory(rest_host, 0xC191, 1)[0] != 0x11:
            raise mt.Failure(f"Outer Out did not restore A for the caller-side store: {parsed!r}")
        _ensure_no_debug(session)


def prove_step_out_breaks_after_active_jsr(rest_host: str, session: "mt.MonitorSession",
                                           context: str) -> None:
    """Hostile Step Out proof: decoy RTS opcodes must not influence the target."""

    caller = bytes([
        0x20, 0xE0, 0xC6,       # $C6A0: JSR $C6E0
        0x8D, 0xF0, 0xC6,       # $C6A3: STA $C6F0
        0x4C, 0xA6, 0xC6,       # $C6A6: JMP $C6A6
    ])
    lower_helper = bytes([
        0xEE, 0xF1, 0xC6,       # $C6D0: INC $C6F1
        0xA9, 0x44,             # $C6D3: LDA #$44
        0x60,                   # $C6D5: RTS (the actual return)
        0x60,                   # $C6D6: decoy RTS
        0xEA,                   # $C6D7: NOP padding
    ])
    entry = bytes([
        0x4C, 0xD0, 0xC6,       # $C6E0: JMP $C6D0; current PC becomes < JSR target
        0x60,                   # $C6E3: decoy RTS
        0xEA,
        0x60,                   # $C6E5: decoy RTS
    ])

    _reopen_monitor(session)

    mt.write_rest_memory(rest_host, 0xC6A0, caller)
    mt.write_rest_memory(rest_host, 0xC6D0, lower_helper)
    mt.write_rest_memory(rest_host, 0xC6E0, entry)
    mt.write_rest_memory(rest_host, 0xC6F0, bytes([0x00, 0x00]))

    if mt.read_rest_memory(rest_host, 0xC6A0, len(caller)) != caller:
        raise mt.Failure(f"{context}: caller fixture did not round-trip")
    if mt.read_rest_memory(rest_host, 0xC6D0, len(lower_helper)) != lower_helper:
        raise mt.Failure(f"{context}: lower-helper fixture did not round-trip")
    if mt.read_rest_memory(rest_host, 0xC6E0, len(entry)) != entry:
        raise mt.Failure(f"{context}: entry fixture did not round-trip")

    session.goto("C6A0")
    session.send_char("A")
    session.send_char("D")

    session.send_char("T")
    _wait_for_pc(session, "C6E0")
    session.send_char("D")
    _wait_for_pc(session, "C6D0")

    parsed = _step_and_assert_pc(session, "U", 0xC6A3, context)
    side = mt.read_rest_memory(rest_host, 0xC6F0, 2)
    if side != bytes([0x00, 0x01]):
        raise mt.Failure(
            f"{context}: Step Out must execute the actual subroutine body but "
            f"stop before the caller-side STA; side={side.hex()} footer={parsed!r}")
    if parsed["ac"] != "44":
        raise mt.Failure(f"{context}: Step Out did not execute through the real RTS path: {parsed!r}")

    session.send_char("D")
    _wait_for_pc(session, "C6A6")
    if mt.read_rest_memory(rest_host, 0xC6F0, 1)[0] != 0x44:
        raise mt.Failure(f"{context}: caller-side opcode after JSR did not execute after one Over")
    _ensure_no_debug(session)


def prove_stop_debug_exit_resumes_current_context(rest_host: str,
                                                  session: "mt.MonitorSession",
                                                  context: str) -> None:
    """Step the mandatory $2000 loop, stop Debug, exit, and prove it resumes."""

    program = bytes.fromhex("EE21D04C0020")

    _reset_c64_core(rest_host)
    _reopen_monitor(session)
    mt.write_rest_memory(rest_host, 0x2000, program)

    session.goto("2000")
    session.send_char("A")
    session.send_char("D")
    _step_and_assert_pc(session, "D", 0x2003, context)

    before_exit = mt.read_rest_memory(rest_host, 0xD021, 1)[0]
    snap = session.send_key("ESC")
    _assert_no_debug_modal_snapshot(snap, context)
    if "Dbg" in _header_line(session):
        raise mt.Failure(f"{context}: ESC did not leave Debug before monitor exit")
    session.send_key("CTRL_O")

    after_exit = before_exit
    deadline = time.time() + 2.0
    while time.time() < deadline:
        time.sleep(0.1)
        after_exit = mt.read_rest_memory(rest_host, 0xD021, 1)[0]
        if after_exit != before_exit:
            break
    if after_exit == before_exit:
        raise mt.Failure(
            f"{context}: $D021 did not change after Stop Debugging + Exit; "
            f"target did not resume from the current debug context")
    if mt.read_rest_memory(rest_host, 0x2000, len(program)) != program:
        raise mt.Failure(f"{context}: $2000 loop bytes changed after Stop Debugging")

    _reopen_monitor(session)
    mt.write_rest_memory(rest_host, 0xC760, bytes([0xEA, 0xEA]))
    session.goto("C760")
    session.send_char("A")
    session.send_char("D")
    _step_and_assert_pc(session, "D", 0xC761, f"{context}: re-entry step")
    _ensure_no_debug(session)
    _reset_c64_core(rest_host)
    _reopen_monitor(session)


def run_step_out_target_proof_tests(rest_host: str, session: "mt.MonitorSession") -> None:
    """Dedicated E2E proof for stack-frame Step Out target selection."""

    with mt.check("Debug: Step Out breaks after active JSR, not nearby RTS"):
        prove_step_out_breaks_after_active_jsr(
            rest_host, session,
            "Step Out active-JSR target proof")


def run_cleanup_exit_tests(rest_host: str, session: "mt.MonitorSession") -> None:
    """Hardware proof that Stop Debugging exits through the current CPU state."""

    with mt.check("Debug: Stop Debugging + Exit resumes current $2000 context"):
        prove_stop_debug_exit_resumes_current_context(
            rest_host, session,
            "Stop Debugging + Exit $2000 loop")


def run_breakpoint_reentry_tests(rest_host: str, session: "mt.MonitorSession") -> None:
    """Ensure repeated Go from the current breakpoint re-arms cleanly."""

    # $C300: EE 90 C1    INC $C190
    # $C303: AE 90 C1    LDX $C190
    # $C306: 4C 00 C3    JMP $C300
    program = bytes([
        0xEE, 0x90, 0xC1,
        0xAE, 0x90, 0xC1,
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
        _clear_all_breakpoints(session, "breakpoint re-entry setup cleanup")
        session.goto("C300")
        _ensure_breakpoint_at(session, 0xC300, "$C300 re-entry breakpoint set")

        session.send_char("G")
        _wait_for_pc(session, "C300")
        if mt.read_rest_memory(rest_host, 0xC190, 1)[0] != 0x00:
            raise mt.Failure("Initial breakpoint hit must stop before INC $C190 executes")

        session.send_char("G")
        _wait_for_pc_register(session, "C300", "xr", "01")
        if mt.read_rest_memory(rest_host, 0xC190, 1)[0] != 0x01:
            raise mt.Failure("Second G from the active breakpoint must execute INC exactly once")

        _ensure_no_debug(session)
        if mt.read_rest_memory(rest_host, 0xC300, 1)[0] != 0xEE:
            raise mt.Failure("Breakpoint cleanup did not restore INC opcode at $C300")


def run_rom_single_step_tests(rest_host: str, session: "mt.MonitorSession") -> None:
    """Exercise bare CPU-visible BASIC/KERNAL ROM stepping over Telnet."""

    with mt.check("Debug: KERNAL ROM Step Into from $E000", u2=False,
                  u2_reason="U64 CPU-visible KERNAL ROM stepping is required"):
        snap = _enter_rom_debug_at(session, 0xE000, "KRN",
                                   "KERNAL Step Into $E000", "$E:KRN")
        row = _disassembly_row(snap, 0xE000)
        if "85 56" in row:
            if "STA $56" not in row:
                raise mt.Failure(f"Canonical $E000 bytes did not decode as STA $56: {row!r}")
            expected_pc = 0xE002
        else:
            expected_pc = 0xE000 + _instruction_length_from_row(row)
            print(f"[info] non-canonical KERNAL $E000 row selected: {row}", flush=True)
        try:
            _step_and_assert_pc(session, "T", expected_pc, "KERNAL Step Into $E000")
        except mt.Failure:
            _dump_debug_scratch(rest_host, "KERNAL Step Into $E000 FAIL")
            raise
        _leave_debug_and_reset(rest_host, session)

    with mt.check("Debug: KERNAL ROM Step Over on visible JSR", u2=False,
                  u2_reason="U64 CPU-visible KERNAL ROM stepping is required"):
        snap = _enter_rom_debug_at(session, 0xE002, "KRN",
                                   "KERNAL Step Over JSR", "$E:KRN")
        row = _disassembly_row(snap, 0xE002)
        jsr_addr = 0xE002
        if "20 0F BC" in row:
            if "JSR $BC0F" not in row:
                raise mt.Failure(f"Canonical $E002 bytes did not decode as JSR $BC0F: {row!r}")
        else:
            jsr_addr, row = _find_visible_jsr_row(snap, "KRN")
            session.goto(f"{jsr_addr:04X}")
            print(f"[info] non-canonical KERNAL JSR row selected: {row}", flush=True)
        expected_pc = jsr_addr + _instruction_length_from_row(row)
        _step_and_assert_pc(session, "D", expected_pc, f"KERNAL Step Over ${jsr_addr:04X}")
        _leave_debug_and_reset(rest_host, session)

    with mt.check("Debug: KERNAL ROM Step Into on visible JSR", u2=False,
                  u2_reason="U64 CPU-visible KERNAL ROM stepping is required"):
        snap = _enter_rom_debug_at(session, 0xE002, "KRN",
                                   "KERNAL Step Into JSR", "$E:KRN")
        row = _disassembly_row(snap, 0xE002)
        jsr_addr = 0xE002
        if "20 0F BC" in row:
            if "JSR $BC0F" not in row:
                raise mt.Failure(f"Canonical $E002 bytes did not decode as JSR $BC0F: {row!r}")
        else:
            jsr_addr, row = _find_visible_jsr_row(snap, "KRN")
            session.goto(f"{jsr_addr:04X}")
            print(f"[info] non-canonical KERNAL JSR row selected: {row}", flush=True)
        target_pc = _jsr_target_from_row(row)
        _step_and_assert_pc(session, "T", target_pc, f"KERNAL Step Into ${jsr_addr:04X}")
        after = session.capture()
        _assert_no_debug_modal_snapshot(after, "KERNAL Step Into JSR target")
        if target_pc == 0xBC0F:
            _assert_snapshot_contains(after, "[BAS]", "KERNAL Step Into JSR target")
        _leave_debug_and_reset(rest_host, session)

    with mt.check("Debug: BASIC ROM Step Over on visible JSR", u2=False,
                  u2_reason="U64 CPU-visible BASIC ROM stepping is required"):
        snap = _enter_rom_debug_at(session, 0xA800, "BAS",
                                   "BASIC Step Over visible JSR", "$A:BAS")
        jsr_addr, row = _find_visible_jsr_row(snap, "BAS")
        if jsr_addr != 0xA800:
            session.goto(f"{jsr_addr:04X}")
        expected_pc = jsr_addr + _instruction_length_from_row(row)
        _step_and_assert_pc(session, "D", expected_pc, f"BASIC Step Over ${jsr_addr:04X}")
        _leave_debug_and_reset(rest_host, session)


def run_rom_breakpoint_tests(rest_host: str, session: "mt.MonitorSession") -> None:
    """Prove BASIC/KERNAL ROM breakpoints patch, hit, clear, step, and restore."""

    bootstrap_addr = 0xC540
    # Use actual executable ROM instructions with stable one-instruction
    # stepping behavior. $A831 is BASIC END's CLC path, and $E018 is a KERNAL
    # SEC instruction with enough surrounding instructions for viewport checks.
    cases = (
        ("BASIC", 0xA831, "BAS"),
        ("KERNAL", 0xE018, "KRN"),
    )

    for name, target, marker in cases:
        with mt.check(f"Debug: {name} ROM breakpoint set/hit/remove/step", u2=False,
                      u2_reason="U64 volatile ROM-image patching is required"):
            _reopen_monitor(session)
            mt.ensure_status(session, "CPU7 $A:BAS $D:I/O $E:KRN VIC")
            original = mt.read_rest_memory(rest_host, target, 1)[0]
            if original == 0x00:
                raise mt.Failure(f"{name} test address ${target:04X} already contains BRK")
            bootstrap = bytes([
                0x4C, target & 0xFF, target >> 8,   # jump into the ROM target
            ])
            mt.write_rest_memory(rest_host, bootstrap_addr, bootstrap)

            session.goto(f"{target:04X}")
            session.send_char("A")
            session.send_char("D")
            lines = _capture_lines(session)
            row = next((line for line in lines if line.startswith(f"|{target:04X} ")), "")
            if f"[{marker}]" not in row:
                raise mt.Failure(f"{name} row did not show ROM source marker [{marker}]: {row!r}")

            _clear_breakpoint_at(session, target, f"{name} stale ROM breakpoint clear")
            _ensure_breakpoint_at(session, target, f"{name} ROM breakpoint set")
            armed_row = _disassembly_row(session.capture(), target)
            if "[BRK" not in armed_row:
                raise mt.Failure(f"{name} ROM breakpoint row was not armed: {armed_row!r}")
            session.goto(f"{bootstrap_addr:04X}")
            session.send_char("G")
            parsed = _wait_for_pc(session, f"{target:04X}")
            _assert_no_debug_modal(session, f"{name} ROM breakpoint hit")
            if parsed["pc"].upper() != f"{target:04X}":
                raise mt.Failure(f"{name} ROM breakpoint did not hit target: {parsed!r}")
            _address_row_context(session, target, f"{name} ROM breakpoint hit")

            session.send_char("R")
            _assert_no_debug_modal(session, f"{name} ROM breakpoint clear")
            session.last_command = f"CTRL_R_CLEAR_{name}"
            session.sock.sendall(b"\x12")
            text = session.capture().text()
            if f"SET ${target:04X}" in text:
                raise mt.Failure(f"{name} breakpoint remained in list after R clear:\n{text}")
            session.send_key("ESC")

            session.send_char("D")
            _assert_no_debug_modal(session, f"{name} ROM step after breakpoint")
            stepped = _wait_for_pc_not(session, f"{target:04X}", timeout=20.0)
            stepped_pc = int(stepped["pc"], 16)
            if name == "BASIC" and not (0xA000 <= stepped_pc <= 0xBFFF):
                raise mt.Failure(f"BASIC ROM step left BASIC unexpectedly: {stepped!r}")
            if name == "KERNAL" and not (0xE000 <= stepped_pc <= 0xFFFF):
                raise mt.Failure(f"KERNAL ROM step left KERNAL unexpectedly: {stepped!r}")
            _address_row_context(session, stepped_pc, f"{name} ROM step")

            _ensure_no_debug(session)
            restored = mt.read_rest_memory(rest_host, target, 1)[0]
            if restored != original:
                raise mt.Failure(
                    f"{name} ROM byte was not restored at ${target:04X}: "
                    f"expected ${original:02X}, got ${restored:02X}")
            _restore_safe_banking_display_hygiene(
                rest_host, session, f"{name} ROM breakpoint cleanup")


def run_kernal_basic_breakpoint_regression(rest_host: str, session: "mt.MonitorSession") -> None:
    """Reproduce a KERNAL-to-BASIC ROM breakpoint continuation path."""

    with mt.check("Debug: KERNAL $E002 G continues safely to BASIC $BC0F", u2=False,
                  u2_reason="U64 BASIC/KERNAL ROM breakpoints are required"):
        _reset_c64_core(rest_host)
        _reopen_monitor(session)
        _ensure_no_debug(session)
        mt.ensure_status(session, "CPU7 $A:BAS $D:I/O $E:KRN VIC")

        session.goto("BC0F")
        session.send_char("A")
        session.send_char("D")
        for stale in (0xBC0F, 0xE005, 0xBC9B, 0xBCF2):
            _clear_breakpoint_at(session, stale, f"${stale:04X} stale breakpoint clear")
        session.goto("BC0F")
        row = _disassembly_row(session.capture(), 0xBC0F)
        if "[BAS]" not in row:
            raise mt.Failure(f"$BC0F must be visible as BASIC ROM before breakpointing: {row!r}")
        _ensure_breakpoint_at(session, 0xBC0F, "$BC0F breakpoint set")

        _enter_rom_debug_at(session, 0xE002, "KRN", "$E002 Go to $BC0F setup", "$E:KRN")
        session.send_char("G")
        parsed = _wait_for_pc(session, "BC0F")
        _assert_no_debug_modal(session, "$E002 Go to $BC0F")
        if parsed["pc"].upper() != "BC0F":
            raise mt.Failure(f"$E002 Go did not stop at $BC0F: {parsed!r}")

        session.goto("BCF2")
        row = _disassembly_row(session.capture(), 0xBCF2)
        if "[BAS]" not in row:
            raise mt.Failure(f"$BCF2 must be visible as BASIC ROM before breakpointing: {row!r}")
        _ensure_breakpoint_at(session, 0xBCF2, "$BCF2 breakpoint set")

        session.send_char("G")
        parsed = _wait_for_pc_not(session, "BC0F")
        _assert_no_debug_modal(session, "$BC0F Go with $BCF2 armed")
        if parsed["pc"].upper() == "BC0F":
            raise mt.Failure(f"Continue from $BC0F did not advance: {parsed!r}")

        _clear_breakpoint_at(session, 0xBC0F, "$BC0F breakpoint clear")
        _clear_breakpoint_at(session, 0xBCF2, "$BCF2 breakpoint clear")
        _ensure_no_debug(session)
        _reset_c64_core(rest_host)
        _reopen_monitor(session)

    with mt.check("Debug: BASIC $BCF2 manual ROM breakpoint is reachable", u2=False,
                  u2_reason="U64 BASIC ROM breakpoints are required"):
        _ensure_no_debug(session)
        mt.write_rest_memory(rest_host, 0xC540, bytes([0x4C, 0xF2, 0xBC]))
        session.goto("BCF2")
        session.send_char("A")
        session.send_char("D")
        _clear_breakpoint_at(session, 0xBCF2, "$BCF2 stale breakpoint clear")
        session.goto("BCF2")
        row = _disassembly_row(session.capture(), 0xBCF2)
        if "[BAS]" not in row:
            raise mt.Failure(f"$BCF2 must be visible as BASIC ROM before breakpointing: {row!r}")
        _ensure_breakpoint_at(session, 0xBCF2, "$BCF2 breakpoint set")
        session.goto("C540")
        session.send_char("G")
        parsed = _wait_for_pc(session, "BCF2")
        _assert_no_debug_modal(session, "$BCF2 manual breakpoint")
        if parsed["pc"].upper() != "BCF2":
            raise mt.Failure(f"Manual $BCF2 breakpoint did not hit: {parsed!r}")
        _clear_breakpoint_at(session, 0xBCF2, "$BCF2 manual breakpoint clear")
        _ensure_no_debug(session)
        _reset_c64_core(rest_host)
        _reopen_monitor(session)


def run_deep_kernal_basic_trace_tests(rest_host: str, session: "mt.MonitorSession") -> None:
    """Trace a 4-deep RAM -> RAM -> KERNAL -> BASIC call chain over Telnet."""

    main_program = bytes([
        0xA9, 0x5A,             # $C700: LDA #$5A
        0x20, 0x30, 0xC7,       # $C702: JSR $C730
        0x8D, 0xF0, 0xC7,       # $C705: STA $C7F0
        0x4C, 0x08, 0xC7,       # $C708: JMP $C708
    ])
    outer_subroutine = bytes([
        0x20, 0x50, 0xC7,       # $C730: JSR $C750
        0x60,                   # $C733: RTS
    ])
    inner_subroutine = bytes([
        0x48,                   # $C750: PHA
        0x20, 0x02, 0xE0,       # $C751: JSR $E002
        0x68,                   # $C754: PLA
        0x60,                   # $C755: RTS
    ])

    with mt.check("Debug: deep KERNAL/BASIC trace fixture loads at $C700", u2=False,
                  u2_reason="U64 BASIC/KERNAL ROM breakpoints are required"):
        _reset_c64_core(rest_host)
        _reopen_monitor(session)
        mt.ensure_status(session, "CPU7 $A:BAS $D:I/O $E:KRN VIC")
        mt.write_rest_memory(rest_host, 0xC700, main_program)
        mt.write_rest_memory(rest_host, 0xC730, outer_subroutine)
        mt.write_rest_memory(rest_host, 0xC750, inner_subroutine)
        mt.write_rest_memory(rest_host, 0xC7F0, bytes([0x00]))
        if mt.read_rest_memory(rest_host, 0xC700, len(main_program)) != main_program:
            raise mt.Failure("Deep trace main fixture did not round-trip")
        if mt.read_rest_memory(rest_host, 0xC730, len(outer_subroutine)) != outer_subroutine:
            raise mt.Failure("Deep trace outer fixture did not round-trip")
        if mt.read_rest_memory(rest_host, 0xC750, len(inner_subroutine)) != inner_subroutine:
            raise mt.Failure("Deep trace inner fixture did not round-trip")

    with mt.check("Debug: deep trace uses D/T/G/O across RAM, KERNAL, and BASIC", u2=False,
                  u2_reason="U64 BASIC/KERNAL ROM breakpoints are required"):
        session.goto("BC0F")
        session.send_char("A")
        session.send_char("D")
        _clear_all_breakpoints(session, "deep trace setup breakpoint cleanup")
        session.goto("BC0F")
        row = _disassembly_row(session.capture(), 0xBC0F)
        if "[BAS]" not in row:
            raise mt.Failure(f"$BC0F must be visible as BASIC ROM before breakpointing: {row!r}")
        _ensure_breakpoint_at(session, 0xBC0F, "$BC0F BASIC breakpoint set")

        session.goto("C700")
        session.send_char("D")
        parsed = _wait_for_pc(session, "C702")
        if parsed["ac"] != "5A":
            raise mt.Failure(f"Deep trace LDA #$5A did not set AC: {parsed!r}")
        session.send_char("T")
        _wait_for_pc(session, "C730")
        session.send_char("T")
        _wait_for_pc(session, "C750")
        session.send_char("D")
        _wait_for_pc(session, "C751")
        session.send_char("T")
        _wait_for_pc(session, "E002")
        row = _disassembly_row(session.capture(), 0xE002)
        if "20 0F BC" not in row or "JSR $BC0F" not in row or "[KRN]" not in row:
            raise mt.Failure(f"$E002 must be the canonical KERNAL JSR into BASIC: {row!r}")

        session.send_char("G")
        parsed = _wait_for_pc(session, "BC0F")
        _assert_no_debug_modal(session, "deep trace Go to BASIC breakpoint")
        if parsed["pc"].upper() != "BC0F":
            raise mt.Failure(f"Deep trace did not stop at BASIC breakpoint: {parsed!r}")

        session.goto("E005")
        row = _disassembly_row(session.capture(), 0xE005)
        if "[KRN]" not in row:
            raise mt.Failure(f"$E005 must be visible as KERNAL ROM before breakpointing: {row!r}")
        _ensure_breakpoint_at(session, 0xE005, "$E005 KERNAL return breakpoint set")
        session.send_char("G")
        _wait_for_pc(session, "E005")
        _assert_no_debug_modal(session, "deep trace current BASIC breakpoint skip")

        session.send_char("U")
        _wait_for_pc(session, "C754")
        session.send_char("D")
        parsed = _wait_for_pc(session, "C755")
        if parsed["ac"] != "5A":
            raise mt.Failure(f"PLA after KERNAL/BASIC return did not restore AC: {parsed!r}")
        session.send_char("U")
        _wait_for_pc(session, "C733")
        session.send_char("U")
        _wait_for_pc(session, "C705")
        session.send_char("D")
        _wait_for_pc(session, "C708")
        if mt.read_rest_memory(rest_host, 0xC7F0, 1)[0] != 0x5A:
            raise mt.Failure("Deep trace caller-side store did not execute after full unwind")
        _clear_breakpoint_at(session, 0xBC0F, "$BC0F deep trace breakpoint clear")
        _clear_breakpoint_at(session, 0xE005, "$E005 deep trace breakpoint clear")
        _ensure_no_debug(session)
        _reset_c64_core(rest_host)
        _reopen_monitor(session)


def _banked_kernal_out_program(base: int, ready_addr: int) -> bytes:
    payload = bytes([
        0xEE, 0x20, 0xD0,       # INC $D020
        0x4C, 0x00, 0xE0,       # JMP $E000
    ])
    payload_addr = base + 43
    program = bytes([
        0x78,                   # SEI
        0xA9, 0x37,             # LDA #$37
        0x85, 0x00,             # STA $00
        0xA9, 0x35,             # LDA #$35
        0x85, 0x01,             # STA $01
        0xA2, 0x00,             # LDX #$00
        0xBD, payload_addr & 0xFF, payload_addr >> 8,
        0x9D, 0x00, 0xE0,       # STA $E000,X
        0xE8,                   # INX
        0xE0, len(payload),     # CPX #payload_end-payload
        0xD0, 0xF5,             # BNE copy
        0xA9, 0xA5,             # LDA #$A5
        0x8D, ready_addr & 0xFF, ready_addr >> 8,
        0xEA,                   # ready: NOP
        0xEA,                   # NOP
        0xA9, 0x37,             # LDA #$37
        0x85, 0x00,             # STA $00
        0xA9, 0x35,             # LDA #$35
        0x85, 0x01,             # STA $01
        0xEA,                   # NOP
        0xEA,                   # NOP
        0xEA,                   # NOP
        0x4C, 0x00, 0xE0,       # JMP $E000
    ])
    return program + payload


def _open_breakpoint_popup(session: "mt.MonitorSession", context: str) -> mt.Snapshot:
    session.last_command = f"CTRL_R_{context}"
    session.sock.sendall(b"\x12")
    snap = session.capture()
    if "BREAKPOINTS" not in snap.text():
        raise mt.Failure(f"{context}: breakpoint popup did not open:\n{snap.text()}")
    return snap


def _assert_breakpoint_popup_contains(session: "mt.MonitorSession",
                                      context: str, *patterns: str) -> mt.Snapshot:
    snap = _open_breakpoint_popup(session, context)
    text = snap.text()
    for pattern in patterns:
        if re.search(pattern, text, re.MULTILINE) is None:
            raise mt.Failure(f"{context}: missing popup pattern {pattern!r}\n{text}")
    return snap


def _select_monitor_view(session: "mt.MonitorSession", bank: int,
                         context: str) -> mt.Snapshot:
    bank &= 0x07
    for _ in range(8):
        snap = session.capture()
        status = snap.line(snap.find_status_line())
        if f"CPU{bank}" in status or f"O{bank}" in status:
            return snap
        session.send_char("o")
    snap = session.capture()
    raise mt.Failure(f"{context}: could not select monitor view O{bank}\n{snap.text()}")


def _assert_rest_byte_changes(rest_host: str, address: int, context: str,
                              minimum_values: int = 3) -> None:
    seen = set()
    deadline = time.time() + 4.0
    while time.time() < deadline and len(seen) < minimum_values:
        seen.add(mt.read_rest_memory(rest_host, address, 1)[0])
        time.sleep(0.05)
    if len(seen) < minimum_values:
        raise mt.Failure(
            f"{context}: ${address:04X} did not keep changing; "
            f"observed values={sorted(seen)!r}")


def _assert_rest_region_keeps_changing(rest_host: str, address: int, length: int,
                                       context: str, minimum_cells: int = 2,
                                       timeout: float = 4.0) -> None:
    previous = mt.read_rest_memory(rest_host, address, length)
    changed_cells: set[int] = set()
    deadline = time.time() + timeout
    while time.time() < deadline and len(changed_cells) < minimum_cells:
        time.sleep(0.08)
        current = mt.read_rest_memory(rest_host, address, length)
        for offset, (before, after) in enumerate(zip(previous, current)):
            if before != after:
                changed_cells.add(offset)
        previous = current
    if len(changed_cells) < minimum_cells:
        raise mt.Failure(
            f"{context}: ${address:04X}-${address + length - 1:04X} did not keep "
            f"changing in at least {minimum_cells} cells; changed={sorted(changed_cells)!r}")


def _assert_no_forced_cpu7_status(session: "mt.MonitorSession", context: str) -> mt.Snapshot:
    snap = session.capture()
    status = snap.line(snap.find_status_line())
    if "CPU7" in status or status.startswith("|C7") or " C7" in status:
        raise mt.Failure(f"{context}: live CPU bank was forced to 7: {status!r}\n{snap.text()}")
    if "CPU5" not in status and "C5O" not in status:
        raise mt.Failure(f"{context}: expected live CPU5 status, got: {status!r}\n{snap.text()}")
    return snap


def run_banked_breakpoint_tests(rest_host: str, session: "mt.MonitorSession") -> None:
    """Prove monitor-view breakpoints and live $0001 execution stay separate."""

    bootstrap_addr = 0xC880
    ready_addr = 0xC8F0
    capture_addr = bootstrap_addr + 27
    program = _banked_kernal_out_program(bootstrap_addr, ready_addr)

    with mt.check("Debug: KERNAL-out bank separation fixture reaches live CPU5", u2=False,
                  u2_reason="U64 CPU banking and volatile ROM-image breakpoints are required"):
        _reset_c64_core(rest_host)
        _reopen_monitor(session)
        session.send_char("A")
        session.send_char("D")
        _clear_all_breakpoints(session, "bank separation setup cleanup")
        _ensure_no_debug(session)
        _select_monitor_view(session, 7, "select KERNAL monitor view before bank capture")
        mt.write_rest_memory(rest_host, ready_addr, bytes([0x00]))
        mt.write_rest_memory(rest_host, bootstrap_addr, program)
        session.goto(f"{capture_addr:04X}")
        session.send_char("A")
        session.send_char("D")
        _clear_breakpoint_at(session, capture_addr, f"${capture_addr:04X} stale live-port breakpoint clear")
        _ensure_breakpoint_at(session, capture_addr, f"${capture_addr:04X} live-port capture breakpoint set")
        session.goto(f"{bootstrap_addr:04X}")
        session.send_char("G")
        _wait_for_pc(session, f"{capture_addr:04X}")
        mt.wait_for_rest_byte(rest_host, ready_addr, 0xA5, timeout=4.0)
        _clear_breakpoint_at(session, capture_addr, f"${capture_addr:04X} live-port capture breakpoint clear")
        snap = mt.ensure_status(session, "C5O7 $A:BAS $D:I/O $E:KRN VIC")
        status = snap.line(snap.find_status_line())
        if "C5O7" not in status or "$E:KRN" not in status:
            raise mt.Failure(f"Footer must show live CPU5, monitor O7 KERNAL labels: {status!r}")

    with mt.check("Debug: KERNAL and RAM $E000 breakpoints coexist with target tags", u2=False,
                  u2_reason="U64 visible-ROM and RAM-under-KERNAL breakpoints are required"):
        snap = session.goto("E000")
        row = _disassembly_row(snap, 0xE000)
        if "[KRN]" not in row or "STA $56" not in row:
            raise mt.Failure(f"Monitor O7 view must still browse KERNAL at $E000: {row!r}")
        header = _header_line(session)
        if "Dbg" not in header:
            session.send_char("D")
        session.send_char("R")
        _wait_for_screen_text(session, "BRK KRN, CPU RAM; not mapped now")
        session.send_key("ENTER")

        _ensure_no_debug(session)
        _select_monitor_view(session, 5, "select RAM-under-KERNAL monitor view")
        mt.ensure_status(session, "CPU5 $A:RAM $D:I/O $E:RAM VIC")
        session.goto("E000")
        session.send_char("A")
        row = _disassembly_row(session.capture(), 0xE000)
        if "[RAM]" not in row or "INC $D020" not in row:
            raise mt.Failure(f"Monitor O5 view must browse hidden RAM payload at $E000: {row!r}")
        session.send_char("D")
        session.send_char("R")
        _assert_no_debug_modal(session, "$E000 RAM breakpoint set")

        snap = _assert_breakpoint_popup_contains(
            session,
            "mixed $E000 KRN/RAM breakpoint list",
            r"^\|?\d SET\s+\$E000 KRN\b",
            r"^\|?\d SET\s+\$E000 RAM\b",
        )
        session.send_key_repeat("UP", 9)
        session.send_char("E")
        snap = session.capture()
        if re.search(r"^\|?0 OFF\s+\$E000 KRN\b", snap.text(), re.MULTILINE) is None:
            raise mt.Failure(f"Disabling KRN slot must retain its target tag:\n{snap.text()}")
        session.send_char("E")
        session.send_key("ESC")

    with mt.check("Debug: RAM-under-KERNAL breakpoint hits with KERNAL banked out", u2=False,
                  u2_reason="U64 live $0001 CPU5 breakpoint trapping is required"):
        mt.ensure_status(session, "CPU5 $A:RAM $D:I/O $E:RAM VIC")
        session.goto("E000")
        if "Dbg" not in _header_line(session):
            session.send_char("D")
        session.send_char("G")
        parsed = _wait_for_pc(session, "E000")
        _assert_no_debug_modal(session, "$E000 RAM-under-KERNAL breakpoint hit")
        if parsed["pc"].upper() != "E000":
            raise mt.Failure(f"RAM-under-KERNAL breakpoint did not hit $E000: {parsed!r}")
        row = _disassembly_row(session.capture(), 0xE000)
        if "[RAM]" not in row or "INC $D020" not in row:
            raise mt.Failure(f"Debug PC row must follow live RAM mapping: {row!r}")

        session.send_char("D")
        parsed = _wait_for_pc(session, "E003")
        _assert_no_debug_modal(session, "$E000 RAM-under-KERNAL step")
        row = _disassembly_row(session.capture(), 0xE003)
        if "[RAM]" not in row or "JMP $E000" not in row:
            raise mt.Failure(f"Step from RAM-under-KERNAL must decode live bytes: {row!r}")

    with mt.check("Debug: mixed $E000 breakpoint cleanup restores both stores", u2=False,
                  u2_reason="U64 visible-ROM and hidden-RAM patch restoration are required"):
        _clear_all_breakpoints(session, "bank separation final cleanup")
        _ensure_no_debug(session)
        mt.ensure_status(session, "CPU5 $A:RAM $D:I/O $E:RAM VIC")
        session.goto("E000")
        session.send_char("A")
        row = _disassembly_row(session.capture(), 0xE000)
        if "[BRK" in row or "INC $D020" not in row:
            raise mt.Failure(f"Hidden RAM breakpoint byte was not restored: {row!r}")

        _ensure_no_debug(session)
        _select_monitor_view(session, 7, "select KERNAL monitor view")
        mt.ensure_status(session, "C5O7 $A:BAS $D:I/O $E:KRN VIC")
        session.goto("E000")
        session.send_char("A")
        row = _disassembly_row(session.capture(), 0xE000)
        if "[BRK" in row or "[KRN]" not in row or "STA $56" not in row:
            raise mt.Failure(f"KERNAL ROM breakpoint byte was not restored: {row!r}")
        _reset_c64_core(rest_host)
        _reopen_monitor(session)


def _load_repeat_redebug_fixtures(rest_host: str) -> None:
    hidden_bootstrap = bytes([
        0x78,                   # SEI
        0xA2, 0x00,             # LDX #$00
        0xA9, 0x37,             # LDA #$37
        0x85, 0x00,             # STA $00
        0xA9, 0x35,             # LDA #$35
        0x85, 0x01,             # STA $01
        0x4C, 0x00, 0xE0,       # JMP $E000
    ])
    hidden_payload = bytes([
        0xFE, 0x00, 0x04,       # INC $0400,X
        0xFE, 0x00, 0x05,       # INC $0500,X
        0xE8,                   # INX
        0x8E, 0x21, 0xD0,       # STX $D021
        0x4C, 0x00, 0xE0,       # JMP $E000
    ])
    ordinary_loop = bytes([
        0xA2, 0x00,             # LDX #$00
        0xFE, 0x00, 0x06,       # INC $0600,X
        0xFE, 0xE8, 0x06,       # INC $06E8,X
        0xE8,                   # INX
        0x8E, 0x20, 0xD0,       # STX $D020
        0x4C, 0x02, 0xC3,       # JMP $C302
    ])
    mt.write_rest_memory(rest_host, 0x0400, bytes([0x00]) * 0x3E8)
    mt.write_rest_memory(rest_host, 0xC000, hidden_bootstrap)
    mt.write_rest_memory(rest_host, 0xE000, hidden_payload)
    mt.write_rest_memory(rest_host, 0xC300, ordinary_loop)


def _arm_loop_breakpoint_and_hit(session: "mt.MonitorSession", loop_addr: int,
                                 start_addr: int, monitor_bank: int,
                                 mapped_status: str, context: str) -> None:
    _select_monitor_view(session, monitor_bank, f"{context}: select loop view")
    session.goto(f"{loop_addr:04X}")
    session.send_char("A")
    header = _header_line(session)
    if "Dbg" not in header:
        session.send_char("D")
    row = _disassembly_row(session.capture(), loop_addr)
    if "[BRK" not in row:
        session.send_char("R")
        snap = session.capture()
        if "not mapped now" in snap.text():
            session.send_key("ENTER")
        _assert_no_debug_modal(session, f"{context}: loop breakpoint set")
    session.goto(f"{start_addr:04X}")
    session.send_char("G")
    _wait_for_pc(session, f"{loop_addr:04X}")
    mt.ensure_status(session, mapped_status)
    _clear_breakpoint_at(session, loop_addr, f"{context}: clear initial loop breakpoint")


def _repeat_cancel_redebug_cycles(rest_host: str, session: "mt.MonitorSession",
                                  label: str, loop_addr: int, monitor_bank: int,
                                  mapped_status: str, evidence_addr: int,
                                  evidence_len: int, first_pc: int,
                                  second_pc: int, row_tokens: tuple[str, ...],
                                  cycles: int = 3) -> None:
    for cycle in range(1, cycles + 1):
        _send_ctrl_d(session)
        snap = session.capture()
        if "Dbg" in _header_line(session):
            raise mt.Failure(f"{label} cycle {cycle}: Ctrl-D did not leave Debug\n{snap.text()}")
        _assert_rest_region_keeps_changing(
            rest_host, evidence_addr, evidence_len,
            f"{label} cycle {cycle} after cancelling Debug")
        session.goto(f"{loop_addr:04X}")
        session.send_char("A")
        _select_monitor_view(session, monitor_bank,
                             f"{label} cycle {cycle}: restore loop monitor view")
        session.send_char("D")
        _assert_no_debug_modal(session, f"{label} cycle {cycle}: re-enter Debug")
        if "CPU5" in mapped_status or mapped_status.startswith("C5"):
            _assert_no_forced_cpu7_status(session, f"{label} cycle {cycle}: re-enter Debug")
        else:
            mt.ensure_status(session, mapped_status)

        session.send_char("D")
        _wait_for_pc(session, f"{first_pc:04X}")
        session.send_char("D")
        _wait_for_pc(session, f"{second_pc:04X}")
        row = _disassembly_row(session.capture(), second_pc)
        for token in row_tokens:
            if token not in row:
                raise mt.Failure(
                    f"{label} cycle {cycle}: row ${second_pc:04X} missing {token!r}: "
                    f"{row!r}")


def _cancel_repeat_debug_and_reset(rest_host: str, session: "mt.MonitorSession",
                                   label: str, evidence_addr: int,
                                   evidence_len: int) -> None:
    _send_ctrl_d(session)
    snap = session.capture()
    if "Dbg" in _header_line(session):
        raise mt.Failure(f"{label}: Ctrl-D did not leave Debug\n{snap.text()}")
    _assert_rest_region_keeps_changing(
        rest_host, evidence_addr, evidence_len,
        f"{label} after final Debug cancel")
    _reset_monitor_and_c64(rest_host, session)


# Keep the former quarantine switch explicit and disabled so the repeated
# RAM-under-KERNAL redebug check remains part of every full-suite pass.
QUARANTINE_RAM_UNDER_KERNAL_REDEBUG_FLAKE = False


def run_repeat_redebug_tests(rest_host: str, session: "mt.MonitorSession") -> None:
    """Repeat cancel/re-enter/step after Debug releases live looping code."""

    with mt.check("Debug: repeated cancel/redebug ordinary RAM loop keeps running",
                  u2=False,
                  u2_reason="U64 repeated live Debug re-entry is required"):
        _ensure_no_debug(session)
        _reset_c64_core(rest_host)
        _reopen_monitor(session)
        session.send_char("A")
        session.send_char("D")
        _clear_all_breakpoints(session, "repeat ordinary setup cleanup")
        _ensure_no_debug(session)
        _load_repeat_redebug_fixtures(rest_host)
        _arm_loop_breakpoint_and_hit(
            session, 0xC302, 0xC300, 7,
            "CPU7 $A:BAS $D:I/O $E:KRN VIC",
            "repeat ordinary")
        _repeat_cancel_redebug_cycles(
            rest_host, session, "ordinary RAM repeat redebug",
            0xC302, 7, "CPU7 $A:BAS $D:I/O $E:KRN VIC",
            0x0600, 0x1E8, 0xC305, 0xC308, ("[RAM]", "INX"))
        _cancel_repeat_debug_and_reset(
            rest_host, session, "ordinary RAM repeat redebug final cleanup",
            0x0600, 0x1E8)

    with mt.check("Debug: repeated cancel/redebug RAM-under-KERNAL loop keeps CPU5",
                  u2=False,
                  u2_reason="U64 RAM-under-KERNAL repeated live Debug re-entry is required"):
        if QUARANTINE_RAM_UNDER_KERNAL_REDEBUG_FLAKE:
            raise mt.SkipCheck(
                "quarantined RAM-under-KERNAL repeated redebug check")
        _ensure_no_debug(session)
        _reset_c64_core(rest_host)
        _reopen_monitor(session)
        session.send_char("A")
        session.send_char("D")
        _clear_all_breakpoints(session, "repeat RAM-under-KERNAL setup cleanup")
        _ensure_no_debug(session)
        _load_repeat_redebug_fixtures(rest_host)
        _arm_loop_breakpoint_and_hit(
            session, 0xE000, 0xC000, 5,
            "CPU5 $A:RAM $D:I/O $E:RAM VIC",
            "repeat RAM-under-KERNAL")
        _repeat_cancel_redebug_cycles(
            rest_host, session, "RAM-under-KERNAL repeat redebug",
            0xE000, 5, "CPU5 $A:RAM $D:I/O $E:RAM VIC",
            0x0400, 0x0200, 0xE003, 0xE006, ("[RAM]", "INX"))
        _cancel_repeat_debug_and_reset(
            rest_host, session, "RAM-under-KERNAL repeat redebug final cleanup",
            0x0400, 0x0200)


def run_banked_continue_no_breakpoints_tests(rest_host: str,
                                             session: "mt.MonitorSession") -> None:
    """Prove G/continue with no breakpoints keeps a KERNAL-out program running.

    Mandatory $01=$00 repro from the handover:
        $C000: SEI; LDA #$00; STA $01; JMP $E000   (banks KERNAL out, bank 0)
        $E000: INC $D021; JMP $E000                (RAM-under-KERNAL loop)
    With $01=$00 the $D021 cell is plain RAM, so the live INC is observable as a
    changing value through the raw REST memory path. Pressing G with no
    persistent breakpoints must release the CPU back into this loop via the
    register-restore stub (which preserves $0001 and does not depend on the
    KERNAL NMI path), not via the KERNAL-dependent NMI trampoline that hangs
    when KERNAL is banked out.
    """

    bootstrap = 0xC000
    payload = 0xE000
    boot = bytes([0x78, 0xA9, 0x00, 0x85, 0x01, 0x4C, 0x00, 0xE0])
    pay = bytes([0xEE, 0x21, 0xD0, 0x4C, 0x00, 0xE0])

    with mt.check("Debug: $01=$00 continue repro reaches live CPU0 RAM-under-KERNAL",
                  u2=False,
                  u2_reason="U64 live $0001 CPU banking and RAM-under-KERNAL are required"):
        _ensure_no_debug(session)
        _reset_c64_core(rest_host)
        _reopen_monitor(session)
        session.send_char("A")
        session.send_char("D")
        _clear_all_breakpoints(session, "continue-no-bp setup cleanup")
        _ensure_no_debug(session)
        # Write payload directly to RAM-under-KERNAL and the bootstrap (raw REST).
        mt.write_rest_memory(rest_host, payload, pay)
        mt.write_rest_memory(rest_host, bootstrap, boot)
        # Monitor view 5 maps $E000 -> RAM, so the breakpoint targets RAM. The
        # live execution bank will be 0, so the footer must show a C0O5 mismatch.
        _select_monitor_view(session, 5, "select RAM view for $E000 breakpoint")
        session.goto("E000")
        session.send_char("A")
        session.send_char("D")
        # Setting a RAM-target breakpoint while the live CPU still maps $E000 as
        # KERNAL (bank 7, before the program runs) raises the mismatch notice.
        # This is expected; dismiss it. The breakpoint is still armed in RAM.
        row = _disassembly_row(session.capture(), payload)
        if "[BRK" not in row:
            snap = session.send_char("R")
            if "not mapped now" in snap.text():
                session.send_key("ENTER")
            else:
                _assert_no_debug_modal_snapshot(snap, "$01=$00 RAM breakpoint set")
            row = _disassembly_row(session.capture(), payload)
            if "[BRK" not in row:
                raise mt.Failure(f"$01=$00 RAM breakpoint was not armed: {row!r}")
        session.goto(f"{bootstrap:04X}")
        session.send_char("G")
        parsed = _wait_for_pc(session, "E000")
        _assert_no_debug_modal(session, "$01=$00 $E000 breakpoint hit")
        snap = session.capture()
        status = snap.line(snap.find_status_line())
        if "C0O5" not in status:
            raise mt.Failure(
                f"Footer must show live CPU0 vs monitor view 5 mismatch (C0O5): {status!r}")
        row = _disassembly_row(snap, payload)
        if "[RAM]" not in row or "INC $D021" not in row:
            raise mt.Failure(f"Debug PC row must follow live RAM mapping: {row!r}")

    with mt.check("Debug: G with no breakpoints keeps $01=$00 loop running",
                  u2=False,
                  u2_reason="U64 register-restore continue under non-standard banking is required"):
        # Clear the breakpoint so G releases the CPU into free execution.
        _clear_all_breakpoints(session, "continue-no-bp release cleanup")
        # Seed $D021 (plain RAM at $01=$00) to a known low value, then continue.
        mt.write_rest_memory(rest_host, 0xD021, bytes([0x00]))
        session.send_char("G")
        _assert_no_debug_modal(session, "$01=$00 continue G")
        # The loop must increment $D021. Observe several raw reads change over a
        # short window. A hang/forced-CPU7/lost-Telnet would leave it static.
        _assert_rest_byte_changes(
            rest_host, 0xD021,
            "$01=$00 continue G (machine appears hung or forced back to CPU7)")
        # $01=$00 deliberately hides I/O for this regression. Restore and prove
        # safe display-compatible banking immediately after the destructive case.
        _restore_safe_banking_display_hygiene(rest_host, session, "$01=$00 cleanup")

    with mt.check("Debug: $01=$35 continue with no breakpoints keeps I/O loop running",
                  u2=False,
                  u2_reason="U64 register-restore continue under KERNAL-out I/O banking is required"):
        bootstrap_addr = 0xC880
        ready_addr = 0xC8F0
        capture_addr = bootstrap_addr + 27
        program = _banked_kernal_out_program(bootstrap_addr, ready_addr)
        _ensure_no_debug(session)
        _reset_c64_core(rest_host)
        _reopen_monitor(session)
        session.send_char("A")
        session.send_char("D")
        _clear_all_breakpoints(session, "$01=$35 continue setup cleanup")
        _ensure_no_debug(session)
        _select_monitor_view(session, 5, "select RAM-under-KERNAL view for $01=$35 continue")
        mt.write_rest_memory(rest_host, ready_addr, bytes([0x00]))
        mt.write_rest_memory(rest_host, bootstrap_addr, program)
        session.goto(f"{capture_addr:04X}")
        session.send_char("A")
        session.send_char("D")
        _ensure_breakpoint_at(session, capture_addr,
                              f"${capture_addr:04X} $01=$35 payload-copy breakpoint set")
        session.goto(f"{bootstrap_addr:04X}")
        session.send_char("G")
        _wait_for_pc(session, f"{capture_addr:04X}")
        mt.wait_for_rest_byte(rest_host, ready_addr, 0xA5, timeout=4.0)
        mt.ensure_status(session, "CPU5 $A:RAM $D:I/O $E:RAM VIC")
        _clear_breakpoint_at(session, capture_addr,
                             f"${capture_addr:04X} $01=$35 payload-copy breakpoint clear")
        session.goto("E000")
        row = _disassembly_row(session.capture(), payload)
        if "[RAM]" not in row or "INC $D020" not in row:
            raise mt.Failure(f"$01=$35 payload copy did not install RAM loop: {row!r}")
        _ensure_breakpoint_at(session, payload, "$01=$35 RAM-under-KERNAL breakpoint set")
        session.send_char("G")
        _wait_for_pc(session, "E000")
        mt.ensure_status(session, "CPU5 $A:RAM $D:I/O $E:RAM VIC")
        row = _disassembly_row(session.capture(), payload)
        if "[RAM]" not in row or "INC $D020" not in row:
            raise mt.Failure(f"$01=$35 Debug PC row must follow live RAM mapping: {row!r}")
        _clear_all_breakpoints(session, "$01=$35 continue release cleanup")
        mt.write_rest_memory(rest_host, 0xD020, bytes([0x00]))
        session.send_char("G")
        _assert_no_debug_modal(session, "$01=$35 continue G")
        _assert_rest_byte_changes(
            rest_host, 0xD020,
            "$01=$35 continue G (machine appears hung or forced back to CPU7)")
        _reset_c64_core(rest_host)
        _reopen_monitor(session)

    with mt.check("Debug: $01=$37 continue with no breakpoints keeps baseline I/O loop running",
                  u2=False,
                  u2_reason="U64 register-restore continue under baseline banking is required"):
        base = 0xC020
        ready = 0xC08F
        loop = base + 14
        program = bytes([
            0x78,                   # SEI
            0xA9, 0x37,             # LDA #$37
            0x85, 0x00,             # STA $00
            0xA9, 0x37,             # LDA #$37
            0x85, 0x01,             # STA $01
            0xA9, 0xA5,             # LDA #$A5
            0x8D, ready & 0xFF, ready >> 8,
            0xEE, 0x20, 0xD0,       # INC $D020
            0x4C, loop & 0xFF, loop >> 8,
        ])
        _ensure_no_debug(session)
        _reset_c64_core(rest_host)
        _reopen_monitor(session)
        session.send_char("A")
        session.send_char("D")
        _clear_all_breakpoints(session, "$01=$37 continue setup cleanup")
        _ensure_no_debug(session)
        mt.write_rest_memory(rest_host, ready, bytes([0x00]))
        mt.write_rest_memory(rest_host, base, program)
        _select_monitor_view(session, 7, "select baseline view for $01=$37 continue")
        session.goto(f"{loop:04X}")
        session.send_char("A")
        session.send_char("D")
        _ensure_breakpoint_at(session, loop, f"${loop:04X} baseline loop breakpoint set")
        session.goto(f"{base:04X}")
        session.send_char("G")
        _wait_for_pc(session, f"{loop:04X}")
        mt.wait_for_rest_byte(rest_host, ready, 0xA5, timeout=4.0)
        mt.ensure_status(session, "CPU7 $A:BAS $D:I/O $E:KRN VIC")
        _clear_all_breakpoints(session, "$01=$37 continue release cleanup")
        mt.write_rest_memory(rest_host, 0xD020, bytes([0x00]))
        session.send_char("G")
        _assert_no_debug_modal(session, "$01=$37 continue G")
        _assert_rest_byte_changes(
            rest_host, 0xD020,
            "$01=$37 continue G (machine appears hung or forced out of CPU7)")
        _reset_c64_core(rest_host)
        _reopen_monitor(session)

    with mt.check("Debug: continue with breakpoints preserves live CPU5 backing store",
                  u2=False,
                  u2_reason="U64 concrete backing-store breakpoints under KERNAL-out banking are required"):
        bootstrap_addr = 0xC880
        boot = bytes([
            0x78,                   # SEI
            0xA9, 0x37,             # LDA #$37
            0x85, 0x00,             # STA $00
            0xA9, 0x35,             # LDA #$35
            0x85, 0x01,             # STA $01
            0x4C, 0x00, 0xE0,       # JMP $E000
        ])
        pay = bytes([0xEE, 0x20, 0xD0, 0x4C, 0x00, 0xE0])
        _ensure_no_debug(session)
        _reset_c64_core(rest_host)
        _reopen_monitor(session)
        session.send_char("A")
        session.send_char("D")
        _clear_all_breakpoints(session, "continue-with-bp setup cleanup")
        _ensure_no_debug(session)
        _select_monitor_view(session, 5, "select RAM view for continue-with-bp entry")
        mt.write_rest_memory(rest_host, payload, pay)
        mt.write_rest_memory(rest_host, bootstrap_addr, boot)
        session.goto("E000")
        row = _disassembly_row(session.capture(), payload)
        if "[RAM]" not in row or "INC $D020" not in row:
            raise mt.Failure(f"Continue-with-bp RAM payload was not visible: {row!r}")
        _select_monitor_view(session, 5, "select RAM view for $E003 RAM breakpoint")
        session.goto("E003")
        session.send_char("A")
        session.send_char("D")
        session.send_char("R")
        _wait_for_screen_text(session, "BRK RAM, CPU KRN; not mapped now")
        session.send_key("ENTER")
        row = _disassembly_row(session.capture(), 0xE003)
        if "[BRK" not in row:
            raise mt.Failure(f"$E003 RAM breakpoint was not set: {row!r}")
        session.goto(f"{bootstrap_addr:04X}")
        session.send_char("G")
        _wait_for_pc(session, "E003")
        mt.ensure_status(session, "CPU5 $A:RAM $D:I/O $E:RAM VIC")
        row = _disassembly_row(session.capture(), 0xE003)
        if "[RAM]" not in row or "JMP $E000" not in row:
            raise mt.Failure(
                f"Continue-with-breakpoints must hit RAM $E003, not KERNAL: {row!r}")
        _clear_all_breakpoints(session, "continue-with-bp final cleanup")
        _reset_monitor_and_c64(rest_host, session)


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
        _ensure_breakpoint_at(session, 0xC006, "$C006 orchestrator breakpoint set")
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
        session.send_char("U")
        parsed = _wait_for_pc(session, "C00A")
        if parsed["ac"] != "11":
            raise mt.Failure(f"Subroutine should have set AC to $11, got {parsed!r}")

    with mt.check("Debug: cleanup restores user bytes at the breakpoint"):
        _clear_breakpoint_at(session, 0xC006, "$C006 orchestrator breakpoint clear")
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

    # Stack side effects of step mode must match a real run: the NMI used to
    # launch the first step must not leak its 3-byte frame onto the stack. Trace
    # into the JSR (the first, NMI-launched step), confirm the real return
    # address is on the stack at the SP-relative slot RTS will read, and confirm
    # the JSR push / RTS pop balance the stack pointer exactly (+2 / -2). The
    # precise 3-byte NMI-frame balance is pinned deterministically by the host
    # unit test test_nmi_launch_trampoline_balances_stack.
    with mt.check("Debug: step-mode JSR/RTS keep the stack balanced and return address correct"):
        _reopen_monitor(session)
        session.goto("C105")
        session.send_char("A")
        session.send_char("D")
        session.send_char("T")               # first step: NMI launch, Trace into JSR
        into = _wait_for_pc(session, "C130")
        sp_into = int(into["sp"], 16)
        # JSR pushed the return address ($C105 + 2 = $C107): low byte at
        # $0100+SP+1, high byte at $0100+SP+2 - exactly what RTS will pull.
        stack = mt.read_rest_memory(rest_host, 0x0100, 256)
        ret = stack[(sp_into + 1) & 0xFF] | (stack[(sp_into + 2) & 0xFF] << 8)
        if ret != 0xC107:
            raise mt.Failure(
                f"JSR return address not on the stack at the SP slot RTS reads: "
                f"got ${ret:04X}, SP=${sp_into:02X}")
        session.send_char("D")               # Over INC $C181
        _wait_for_pc(session, "C133")
        out = _step_and_assert_pc(session, "D", 0xC108,
                                  "step-mode JSR/RTS stack balance")  # Over RTS
        sp_back = int(out["sp"], 16)
        if ((sp_back - sp_into) & 0xFF) != 2:
            raise mt.Failure(
                f"JSR/RTS did not balance the stack: SP ${sp_into:02X} -> ${sp_back:02X} "
                f"(expected +2)")
        _ensure_no_debug(session)


TEST_GROUPS = (
    ("ram-edit", run_ram_edit_regression_tests),
    ("debug", run_debug_tests),
    ("flags", run_flag_control_flow_tests),
    ("refusal", run_refusal_and_return_edge_tests),
    ("page-cross", run_page_cross_and_indirect_jump_tests),
    ("nested-out", run_nested_out_tests),
    ("step-out-target", run_step_out_target_proof_tests),
    ("rom-single-step", run_rom_single_step_tests),
    ("rom-breakpoints", run_rom_breakpoint_tests),
    ("kernal-basic-breakpoint", run_kernal_basic_breakpoint_regression),
    ("deep-trace", run_deep_kernal_basic_trace_tests),
    ("banked-breakpoints", run_banked_breakpoint_tests),
    ("repeat-redebug", run_repeat_redebug_tests),
    ("banked-continue-no-breakpoints", run_banked_continue_no_breakpoints_tests),
    ("brk-orchestrator", run_brk_orchestrator_tests),
    ("side-effect-step", run_side_effect_step_tests),
    ("cleanup-exit", run_cleanup_exit_tests),
    ("breakpoint-reentry", run_breakpoint_reentry_tests),
)


def _parse_selected_tests(parser: argparse.ArgumentParser,
                          selected: list[str] | None) -> list[str] | None:
    if not selected:
        return None
    names = []
    for raw in selected:
        names.extend(name.strip() for name in raw.split(",") if name.strip())
    valid = {name for name, _runner in TEST_GROUPS}
    unknown = sorted(set(names) - valid)
    if unknown:
        parser.error(
            "unknown --test value(s): "
            f"{', '.join(unknown)}; choose from {', '.join(sorted(valid))}")
    return names


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Validate Machine Code Monitor Debug mode over the standard telnet service")
    parser.add_argument("--host", default=os.environ.get("U64_MONITOR_HOST", "u64"))
    parser.add_argument("--port", type=int, default=int(os.environ.get("U64_MONITOR_PORT", "23")))
    parser.add_argument("--rest-host", default=os.environ.get("U64_MONITOR_REST_HOST"))
    parser.add_argument("--password", default=os.environ.get("U64_MONITOR_PASSWORD"))
    parser.add_argument("--timeout", type=float, default=float(os.environ.get("U64_MONITOR_TIMEOUT", "5.0")))
    parser.add_argument("--target", choices=("u64", "u2"),
                        default=os.environ.get("U64_MONITOR_TARGET", "u64"),
                        help="Target hardware. 'u2' skips U64-only features (REST API, "
                             "BASIC/KERNAL ROM stepping, CPU bank cycling, VIC bank).")
    parser.add_argument("--keep-going", action="store_true",
                        default=os.environ.get("U64_MONITOR_KEEP_GOING", "").lower() in ("1", "true", "yes"),
                        help="Continue after a check fails, logging verbose context per failure.")
    parser.add_argument("-t", "--test", action="append", metavar="NAME[,NAME...]",
                        help="Run only the named test group(s). Repeat or comma-separate. "
                             f"Choices: {', '.join(name for name, _runner in TEST_GROUPS)}.")
    parser.add_argument("--list-tests", action="store_true",
                        help="List available --test names and exit.")
    parser.add_argument("--chunk-size", type=int, default=SUITE_CHUNK_SIZE,
                        help="Run the suite in chunks of this many groups, with a "
                             "cool-down + extra reset at each chunk boundary. "
                             "0 disables chunking.")
    parser.add_argument("--chunk-cooldown", type=float,
                        default=SUITE_CHUNK_COOLDOWN_SECONDS,
                        help="Seconds to idle at each chunk boundary before the extra "
                             "reset.")
    args = parser.parse_args()
    selected_tests = _parse_selected_tests(parser, args.test)
    if args.list_tests:
        for name, _runner in TEST_GROUPS:
            print(name)
        return 0

    mt.TestConfig.target = args.target
    # In U2 mode the REST API is typically unreachable; default to keep-going
    # so all failures land in the same console capture rather than stopping
    # at the first connection error.
    mt.TestConfig.keep_going = args.keep_going or args.target == "u2"
    mt.TestConfig.failures = []
    mt.TestConfig.skipped = []

    rest_host = args.rest_host or args.host
    if args.target == "u2":
        if not mt.rest_available(rest_host):
            print(f"[info] target=u2 and REST unreachable on {rest_host}; "
                  f"REST-dependent checks will SKIP", flush=True)
        else:
            print(f"[info] target=u2 with REST reachable on {rest_host}", flush=True)
    session = None
    aborted = False
    try:
        runners = dict(TEST_GROUPS)
        ordered_tests = selected_tests if selected_tests is not None else [
            name for name, _runner in TEST_GROUPS
        ]
        for idx, name in enumerate(ordered_tests):
            if (args.chunk_size and idx > 0
                    and idx % args.chunk_size == 0):
                _chunk_boundary_recovery(
                    rest_host, f"before {name}", args.chunk_cooldown, args.timeout)
            if session is not None:
                session.close()
                session = None
                mt.TestConfig.session = None
            if args.target == "u64":
                _force_safe_cpu_port(rest_host, f"pre-{name} reset")
            _reset_c64_core(rest_host)
            mt.wait_for_monitor_ready(args.host, args.port, args.password, args.timeout)
            session = mt.MonitorSession(args.host, args.port, args.password, args.timeout)
            mt.TestConfig.session = session
            try:
                runners[name](rest_host, session)
            finally:
                if args.target == "u64":
                    _restore_safe_banking_display_hygiene(
                        rest_host, session, f"post-{name} cleanup")
        if session is not None:
            with mt.check("Debug: post-suite banking/display hygiene leaves $0001 safe and C64 readable",
                          u2=False,
                          u2_reason="U64 CPU banking/readability hygiene is required"):
                _restore_safe_banking_display_hygiene(rest_host, session, "post-suite hygiene")
    except mt.Failure as exc:
        aborted = True
        print(exc, file=sys.stderr)
        if session is not None:
            print("\nFinal screen:", file=sys.stderr)
            print(session.capture().text(), file=sys.stderr)
        # Fall through to summary so any prior keep-going state is still printed.
    except Exception as exc:  # noqa: BLE001
        aborted = True
        print(f"E2E debug test failed: {exc}", file=sys.stderr)
        # Fall through to summary so prior keep-going state is still printed.
    finally:
        if session is not None:
            session.close()
        mt.TestConfig.session = None

    _print_debug_summary()
    # An abnormal abort (a Failure raised outside a check body, e.g. inter-group
    # setup or post-group hygiene) must NOT be reported as green: it is not recorded
    # in TestConfig.failures, so without this guard the run would print OK and exit 0
    # despite having stopped early. Treat any abort as a non-zero result.
    if mt.TestConfig.failures or aborted:
        if aborted and not mt.TestConfig.failures:
            print("debug_e2e_test: ABORTED before completion (no check failed, but "
                  "the suite stopped early; see stderr above)", file=sys.stderr)
        return 1
    print(f"debug_e2e_test: OK ({mt.CHECK_COUNT} checks, "
          f"{len(mt.TestConfig.skipped)} skipped)")
    return 0


def _print_debug_summary() -> None:
    passed = mt.CHECK_COUNT - len(mt.TestConfig.failures) - len(mt.TestConfig.skipped)
    print("")
    print("==== debug_e2e_test summary ====")
    print(f"  target  : {mt.TestConfig.target}")
    print(f"  passed  : {passed}")
    print(f"  skipped : {len(mt.TestConfig.skipped)}")
    print(f"  failed  : {len(mt.TestConfig.failures)}")
    if mt.TestConfig.skipped:
        print("")
        print("Skipped checks:")
        for idx, label, reason in mt.TestConfig.skipped:
            print(f"  [{idx:02d}] {label}  ({reason})")
    if mt.TestConfig.failures:
        print("")
        print("Failed checks:")
        for idx, label, _diag in mt.TestConfig.failures:
            print(f"  [{idx:02d}] {label}")


if __name__ == "__main__":
    sys.exit(main())
