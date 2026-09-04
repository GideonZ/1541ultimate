#!/usr/bin/env python3
# E2E: Verifies the debugger across every UI and memory mode against an independent 6510 oracle.

"""Exhaustive Machine Code Monitor debugger matrix gate.

This runner owns the coverage ledger and artifact layout for the real-hardware
debugger matrix. It deliberately reuses the existing telnet, REST/local-UI, and
6510-oracle harness modules rather than replacing them.
"""

from __future__ import annotations

import argparse
import fcntl
import json
import os
import re
import signal
import socket
import struct
import subprocess
import sys
import tempfile
import time
import traceback
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any

MCM_DIR = Path(__file__).resolve().parent
REPO_ROOT = MCM_DIR.parents[2]
sys.path.insert(0, str(MCM_DIR))

import mcm_split_rest as SR  # noqa: E402
import mcm_localui as L  # noqa: E402
import mcm6502 as ORC  # noqa: E402
import monitor_debug_stress as stress  # noqa: E402
import monitor_debug_test as dbg  # noqa: E402
# The Telnet lane drives the session through the same bridge the Debug suite
# uses: TelnetDebugDriver calls the (host, port, password, timeout) constructor
# and needs TestConfig and wait_for_monitor_ready, none of which the core
# monitor_test module provides.
import mcm_monitor_compat as mt  # noqa: E402
import matrix_run_ledger as RUNLEDGER  # noqa: E402
import overlay_lifecycle  # noqa: E402
sys.path.insert(0, str(REPO_ROOT / "tests" / "lib"))
from report import suite_fail, suite_ok  # noqa: E402


def machine_host(args: argparse.Namespace) -> str:
    """Host for C64-machine REST (readmem/writemem/reset/input). On a split
    U2+L session this is the C64U the cartridge is plugged into; otherwise the
    single REST host."""
    return getattr(args, "c64_host", None) or args.rest_host


def make_rest(args: argparse.Namespace, timeout: float = 12.0):
    """Return a Rest bound to the run's topology: a SplitRest (machine ops ->
    --c64-host, overlay ops -> --rest-host) when --c64-host is given (U2+L
    cartridge in a C64U host), else a plain single-host Rest."""
    return SR.make_rest(args.rest_host, getattr(args, "c64_host", None),
                        timeout=timeout)


def debug_suite_target(args: argparse.Namespace) -> str:
    """`monitor_debug_test.py --target` for this run's topology.

    That suite has no --c64-host: Telnet is the cartridge's own remote session,
    so its keystrokes need no C64U injection and its REST oracle reads the
    cartridge. --target is what selects its U2 skips, and without it a U2+L is
    measured against U64-only expectations.
    """
    return "u2" if getattr(args, "c64_host", None) else "u64"


# "ram-rom-ram" and "ram-rur-rom-ram" are boundary-traversal modes: the
# session starts with a live context in the developer's own RAM program and
# steps across a memory-region boundary mid-trace. That is the realistic
# workflow - a developer debugs RAM code and steps into ROM from there -
# whereas "rom" and "ram-under-rom" enter their region cold from a bootstrap.
MEMORY_MODES = ("ram", "ram-under-rom", "rom", "ram-rom-ram", "ram-rur-rom-ram")
# Modes whose cell is validated by an explicit boundary walk instead of the
# 32-level Step Into chain.
TRAVERSAL_MODES = ("ram-rom-ram", "ram-rur-rom-ram")
INTERFACES = ("telnet", "freeze", "overlay")
FINAL_STATUSES = ("PASS", "FAIL", "BLOCKED_WITH_EVIDENCE", "SKIPPED_UNSUPPORTED")

# Memory modes whose entry breakpoint lands where a cartridge cannot place one.
# U2MemoryBackend reports supports_cpu_banking() false, because a DMA read of
# $0001 returns a mirror that is only refreshed at reset, so the monitor has no
# bank view from which to place a RAM-under-ROM breakpoint; and
# supports_visible_rom_patching() is overridden true only in the U64 backend, so
# a BRK cannot be written into a visible ROM image. The firmware refuses both
# with "BRK $xxxx IN ROM BLOCKS DEBUG", which is correct behaviour, not a
# defect. The boundary-traversal modes are deliberately absent: their entry
# breakpoint is in RAM, and they reach ROM by stepping, which is interpreted or
# run from a RAM trampoline rather than patched.
_NO_ROM_PATCH = ("a cartridge cannot write a breakpoint into a visible ROM "
                 "image (supports_visible_rom_patching() is overridden true "
                 "only in the U64 backend)")
CARTRIDGE_UNSUPPORTED_MEMORY_MODES = {
    "ram-under-rom":
        "a cartridge monitor has no bank view to place a RAM-under-ROM "
        "breakpoint from (supports_cpu_banking() is false)",
    "rom": _NO_ROM_PATCH,
    # These two start in RAM but their boundary walk steps into BASIC at
    # $BC0F, which is a JSR. Stepping into a JSR sets a breakpoint on the
    # call target, and that target is inside the ROM image, so the firmware
    # answers DEBUG NOT SUPPORTED on a cartridge.
    "ram-rom-ram": _NO_ROM_PATCH + ", which its boundary walk needs to step "
                   "into the JSR at $BC0F",
    "ram-rur-rom-ram": _NO_ROM_PATCH + ", which its boundary walk needs to "
                      "step into the JSR at $BC0F",
}


def unsupported_cell_reason(args: argparse.Namespace, memory_mode: str):
    """Why this target cannot run this memory mode, or None if it can.

    Keyed on the split-session flag, which is set only for a U2+L in a C64U
    host: the same signal select_bank() already uses to skip the bank view.
    A single-host run gets None for every mode, so this can never hide a
    failure on a machine that does support the operation.
    """
    if not getattr(args, "c64_host", None):
        return None
    return CARTRIDGE_UNSUPPORTED_MEMORY_MODES.get(memory_mode)

DEFAULT_REPS = 3
DEFAULT_STEP_INTO_DEPTH = 32
DEFAULT_STRAIGHT_CALLS = 32
DEFAULT_TRACE_OPCODES = 100
U2_REPS = 1
U2_STEP_INTO_DEPTH = 8
U2_STRAIGHT_CALLS = 8
U2_TRACE_OPCODES = 20

# A contextless breakpoint entry can consume the firmware's full go() and
# bounded relaunch budget, so the wait must outlast three 5-second windows.
CONTEXTLESS_ENTRY_WAIT_S = 22.0

# These states distinguish every CPU-visible source in the three banked regions.
# CPU0 and CPU4 intentionally expose the same all-RAM map: requiring their
# distinct status values proves the debugger captured CHAREN itself rather than
# inferring only the visible ROM signatures. The bootstrap uses $30 | bank so
# bits 0-2 are all outputs while the upper port bits retain the normal C64 value.
BANKING_STATES = (
    (7, ("BAS", "I/O", "KRN")),
    (3, ("BAS", "CHR", "KRN")),
    (5, ("RAM", "I/O", "RAM")),
    (0, ("RAM", "RAM", "RAM")),
    (4, ("RAM", "RAM", "RAM")),
)

BANKING_RAM_BYTES = {
    0xA000: bytes([0xA9, 0xA1, 0xEA]),
    0xD020: bytes([0xA2, 0xD1, 0xEA]),
    0xE000: bytes([0xA0, 0xE1, 0xEA]),
}


# ---------------------------------------------------------------------------
# Debug alert contract. The canonical one-line Debug alerts and the terms that
# must never reach the UI or the manual. `--focus alerts` validates these
# without needing a device, so the contract is checkable in CI; a live host
# adds a best-effort REST smoke. DbX (the experimental step mode) is gone:
# parked-context steps in RAM-under-ROM and visible ROM are completed without
# releasing the CPU into the fetch-lagging bank, so no experimental mode and
# no ROM-image-changed latch remain.
# ---------------------------------------------------------------------------
DEBUG_ALERTS = (
    "Step Into: run to a breakpoint 1st",
    "Step Over: run to a breakpoint 1st",
)

DEBUG_ALERT_MAX_WIDTH = 38

DEBUG_BANNED_ALERT_TERMS = (
    "production", "capability", "unsupported", "qualified",
    "enterprise", "certified", "uncharacterized", "Db!", "DbX",
    "experimental",
)


def validate_debug_alerts(alerts=DEBUG_ALERTS) -> list[str]:
    """Return a list of contract violations for the one-line Debug alerts."""
    problems: list[str] = []
    for alert in alerts:
        if "\n" in alert or "\r" in alert:
            problems.append(f"alert contains a newline: {alert!r}")
        if len(alert) > DEBUG_ALERT_MAX_WIDTH:
            problems.append(
                f"alert exceeds {DEBUG_ALERT_MAX_WIDTH} chars ({len(alert)}): {alert!r}")
        for term in DEBUG_BANNED_ALERT_TERMS:
            if term in alert:
                problems.append(f"alert uses banned term {term!r}: {alert!r}")
    return problems


def validate_manual_text(text: str) -> list[str]:
    """doc/machine_code_monitor.md must explain Debug stepping in plain language."""
    problems: list[str] = []
    required = ("Dbg", "breakpoint+Go", "RAM under ROM")
    for token in required:
        if token not in text:
            problems.append(f"manual missing required phrase {token!r}")
    banned = ("DbX", "experimental Debug", "production mode", "production-grade",
              "enterprise", "capability map")
    for term in banned:
        if term in text:
            problems.append(f"manual uses banned term {term!r}")
    return problems


class GateError(RuntimeError):
    classification = "VALID_DEBUGGER_DEFECT"


class HarnessBug(GateError):
    classification = "HARNESS_BUG"


class BlockedWithEvidence(GateError):
    classification = "BLOCKED_WITH_EVIDENCE"


# Python-level errors the firmware cannot possibly cause: a missing attribute, a
# bad name, a wrong argument type or an unimportable module is always a defect in
# this harness. Classifying them as VALID_DEBUGGER_DEFECT reports a broken test
# as a broken debugger, which is the one failure mode this gate must not have.
_HARNESS_BUG_TYPES = (AttributeError, NameError, TypeError, ImportError,
                      IndentationError, SyntaxError)


def classify_exception(exc: BaseException) -> str:
    explicit = getattr(exc, "classification", None)
    if explicit is not None:
        return explicit
    if isinstance(exc, _HARNESS_BUG_TYPES):
        return "HARNESS_BUG"
    return "VALID_DEBUGGER_DEFECT"


class CellTimeout(GateError):
    """A single cell exceeded its hard wall-clock watchdog. Bounds any transport /
    device stall so a stuck cell can never hang the whole run for minutes-to-hours
    (a degraded httpd/telnet read that never returns). The cell is failed and the
    run continues (or fail-fasts) rather than blocking indefinitely."""
    classification = "CELL_WATCHDOG_TIMEOUT"


# Per-cell hard bound. A healthy cell is ~2-4 min; 7 min leaves generous headroom
# for the slowest legitimate cell (visible-ROM with the full go() budget waits,
# freeze/overlay redraws) while still bounding a true hang.
CELL_WATCHDOG_SECONDS = 420


def _cell_watchdog_handler(signum, frame):
    raise CellTimeout(f"cell exceeded the {CELL_WATCHDOG_SECONDS}s watchdog")


# A row of the monitor's 10-entry breakpoint popup: "0 SET $E000 RAM" or
# "0 EMPTY", with the popup's box borders already stripped.
BREAKPOINT_SLOT_RE = re.compile(r"^\|?\s*[0-9]\s+(SET|EMPTY)\b", re.IGNORECASE)

# Set by main() once the cross-run history is open, so the entry point can still
# write a record for a run that dies on an unhandled exception. Cleared on the
# normal path, which writes its own record.
_RUN_LEDGER_CONTEXT: dict[str, Any] = {}

# Set by main() once a scope that drives the device starts, so the entry point
# can hand the device back however the run ends.
_TEARDOWN_CONTEXT: dict[str, Any] = {}


class ResetRetryCounters:
    """Transparent instrumentation proving the gate never masks a debugger
    failure with a recovery reset, command replay, reconnect-and-replay, or a
    breakpoint replant. Categories match the hardening contract; PROHIBITED must
    stay exactly zero (asserted in the final report). setup_reset (a single
    per-cell baseline reset before the tested workflow) and explicit_reset (the
    Reset debug op under test) are legitimate and counted separately."""
    CATEGORIES = (
        "explicit_reset", "setup_reset", "recovery_reset",
        "transparent_reset", "transparent_reset_restore_success",
        "transparent_reset_restore_failure", "device_reboot",
        "firmware_redeploy", "command_retry", "session_replay",
        "transport_reconnect", "breakpoint_replant",
    )
    PROHIBITED = ("recovery_reset", "command_retry", "session_replay",
                  "transparent_reset", "transparent_reset_restore_failure")

    def __init__(self) -> None:
        self.counts = {c: 0 for c in self.CATEGORIES}

    def count(self, category: str, note: str = "") -> int:
        if category not in self.counts:
            raise KeyError(f"unknown reset/retry counter {category!r}")
        self.counts[category] += 1
        return self.counts[category]

    def violations(self) -> dict:
        return {c: self.counts[c] for c in self.PROHIBITED if self.counts[c]}


# Module-level singletons (cells run in-process, so these aggregate across the
# whole matrix). SETUP_RESETS is the legitimate per-cell baseline reset.
COUNTERS = ResetRetryCounters()
SETUP_RESETS = COUNTERS  # legibility alias for the per-cell setup reset call site


def slug_memory(memory: str) -> str:
    return memory.upper().replace("-", "_")


def cell_id(memory: str, interface: str, rep: int) -> str:
    return f"{slug_memory(memory)}_{interface.upper()}_REP_{rep:02d}"


def now_stamp() -> str:
    return time.strftime("%Y-%m-%dT%H:%M:%S%z")


def run_cmd(cmd: list[str], cwd: Path, log_path: Path, timeout: float | None = None) -> int:
    log_path.parent.mkdir(parents=True, exist_ok=True)
    with log_path.open("a", encoding="utf-8") as log:
        log.write(f"$ {' '.join(cmd)}\n")
        log.flush()
        try:
            proc = subprocess.run(
                cmd,
                cwd=str(cwd),
                text=True,
                stdout=log,
                stderr=subprocess.STDOUT,
                timeout=timeout,
            )
        except subprocess.TimeoutExpired:
            # A caller-visible failure, not a crash: an uncaught TimeoutExpired
            # here takes down the whole gate instead of reporting one command
            # as failed, which is what every other kind of command failure
            # already does.
            log.write(f"rc=-1 (timed out after {timeout}s)\n")
            return -1
        log.write(f"rc={proc.returncode}\n")
        return proc.returncode


def tcp_probe(host: str, port: int, timeout: float = 3.0) -> bool:
    try:
        with socket.create_connection((host, port), timeout=timeout):
            return True
    except OSError:
        return False


def default_row(memory: str, interface: str, rep: int) -> dict[str, Any]:
    return {
        "cell_id": cell_id(memory, interface, rep),
        "memory_mode": memory,
        "interface": interface,
        "repetition": rep,
        "status": "PENDING",
        "program_seed": None,
        "fixture": None,
        "start_pc": None,
        "step_over": "PENDING",
        "step_into": "PENDING",
        "step_into_depth": 0,
        "straight_call_depth": 0,
        "step_out": "PENDING",
        "continue_to_cursor": "PENDING",
        "continue_to_breakpoint": "PENDING",
        "continue": "PENDING",
        "reset": "PENDING",
        "opcode_count": 0,
        "oracle_validated": False,
        "vice_oracle_validated": False,
        "footer_validated": False,
        "stack_validated": False,
        "memory_writes_validated": False,
        "breakpoint_hygiene_validated": False,
        "breakpoint_slot_hygiene_validated": False,
        "brk_patch_hygiene_validated": False,
        "rom_restore_validated": False,
        "banking_restore_validated": False,
        "liveness_validated": False,
        "rest_liveness_validated": False,
        "telnet_liveness_validated": None,
        "artifact_dir": None,
        "commands": [],
        "failure": None,
    }


class Ledger:
    def __init__(self, rows: list[dict[str, Any]], json_path: Path, md_path: Path) -> None:
        self.rows = rows
        self.json_path = json_path
        self.md_path = md_path

    def save(self) -> None:
        self.json_path.parent.mkdir(parents=True, exist_ok=True)
        self.json_path.write_text(json.dumps(self.rows, indent=2, sort_keys=True) + "\n",
                                  encoding="utf-8")
        self.md_path.write_text(self.to_markdown(), encoding="utf-8")

    def to_markdown(self) -> str:
        cols = [
            "cell_id", "memory_mode", "interface", "repetition", "status",
            "step_over", "step_into", "step_into_depth", "straight_call_depth",
            "step_out",
            "continue_to_cursor", "continue_to_breakpoint", "continue", "reset",
            "opcode_count", "oracle_validated", "vice_oracle_validated", "footer_validated",
            "stack_validated", "memory_writes_validated", "failure",
        ]
        out = ["# Machine Code Monitor Matrix Coverage Ledger", ""]
        out.append("| " + " | ".join(cols) + " |")
        out.append("| " + " | ".join("---" for _ in cols) + " |")
        for row in self.rows:
            vals = []
            for col in cols:
                val = row.get(col)
                if col == "failure" and isinstance(val, dict):
                    val = f"{val.get('classification', '')}: {val.get('message', '')}"
                vals.append(str(val).replace("\n", " ") if val is not None else "")
            out.append("| " + " | ".join(vals) + " |")
        out.append("")
        return "\n".join(out)

    def row_for(self, cid: str) -> dict[str, Any]:
        for row in self.rows:
            if row["cell_id"] == cid:
                return row
        raise KeyError(cid)


@dataclass
class DebugState:
    pc: int
    ac: int
    xr: int
    yr: int
    sp: int
    sr: int
    raw: dict[str, Any] = field(default_factory=dict)


# The debugger publishes its captured context to fixed low-RAM cells STORE_* at
# $03F0-$03F6 (Y,X,A,SR,PClo,PChi,SP). This is the debugger's own truth and is
# readable by DMA/REST. Unlike a telnet screen render it can never be dropped: the
# firmware aborts a telnet screen send it cannot deliver within SO_SNDTIMEO (5s),
# so under congestion a step's footer update can be lost even though the step
# executed correctly. Reading STORE_* recovers the true state without re-issuing
# any debugger command. real PC = (PClo|PChi<<8) - 2 (a BRK pushes PC+2).
STORE_CONTEXT_ADDR = 0x03F0


def _authoritative_debug_state(rest) -> DebugState:
    b = rest.read_mem(STORE_CONTEXT_ADDR, 7)
    pc = ((b[4] | (b[5] << 8)) - 2) & 0xFFFF
    return DebugState(pc=pc, ac=b[2], xr=b[1], yr=b[0], sp=b[6], sr=b[3],
                      raw={"source": "STORE_via_DMA", "store": b.hex()})


def _c64_running(rest_host: str) -> bool:
    """True when the C64 6510 is executing: the jiffy clock $A0-$A2 advances. A
    hard wedge (a crashed/parked debug session that even machine:reset cannot
    clear because C64_STOP is stuck) leaves the jiffy frozen. Read by DMA/REST."""
    try:
        a = mt.read_rest_memory(rest_host, 0x00A0, 3)
        time.sleep(0.6)
        b = mt.read_rest_memory(rest_host, 0x00A0, 3)
        return a != b
    except Exception:  # noqa: BLE001 - treat an unreadable device as not-running
        return False


def _wait_c64_running(rest_host: str, timeout: float) -> bool:
    """Poll until the jiffy clock advances, or the deadline passes."""
    deadline = time.time() + timeout
    while True:
        if _c64_running(rest_host):
            return True
        if time.time() >= deadline:
            return False


def _log_wedge_incident(artifact_dir: Path, row: dict, rest_host: str) -> None:
    """Record a hard-wedge incident (pre-wedge cell + measured state) so the run
    report can capture exactly what preceded each wedge and how it was recovered."""
    incident = {
        "time": now_stamp(),
        "pre_wedge_cell": row["cell_id"],
        "pre_wedge_cell_status": row.get("status"),
        "pre_wedge_last_op": row.get("last_op"),
    }
    try:
        incident["jiffy_a0"] = mt.read_rest_memory(rest_host, 0x00A0, 3).hex()
        incident["screen_0400"] = mt.read_rest_memory(rest_host, 0x0400, 40).hex()
        incident["cpu_port_01"] = mt.read_rest_memory(rest_host, 0x0001, 1).hex()
    except Exception as exc:  # noqa: BLE001
        incident["read_error"] = str(exc)
    path = artifact_dir / "wedge-incidents.jsonl"
    with path.open("a", encoding="utf-8") as fh:
        fh.write(json.dumps(incident) + "\n")


@dataclass
class MatrixFixture:
    memory_mode: str
    base: int
    bank: int
    source: str
    entry: int
    step_over_return: int
    chain_entry: int
    chain_addrs: list[int]
    chain_return_addrs: list[int]
    cursor_target: int
    breakpoint_target: int
    sentinel: int
    progress: int
    chunks: list[tuple[int, bytes]]
    bootstrap: bytes
    bootstrap_addr: int = stress.BOOTSTRAP_ADDR
    # Boundary-traversal walk: (monitor key, expected PC after the step, region
    # the CPU is in once it stops). Empty for the non-traversal modes.
    traversal: list[tuple[str, int, str]] = field(default_factory=list)
    # Addresses whose fixture bytes live in RAM under ROM, so a readback through
    # REST would see the ROM image instead of what was written.
    hidden_ram_chunks: tuple[int, ...] = ()
    # Straight-call run: a block of consecutive "JSR helper" instructions ending
    # in RTS, stepped with Step Over. Nesting drives the stack deeper on every
    # step; this drives the same call repeatedly at one stack level, so it is
    # what exercises repeated breakpoint arm/disarm and park/resume cycles with
    # the stack pointer returning to the same value each time. 0 where the mode
    # has no such block.
    straight_entry: int = 0
    straight_block: int = 0
    straight_calls: int = 0
    straight_return: int = 0

    def to_json(self) -> dict[str, Any]:
        return {
            "memory_mode": self.memory_mode,
            "base": f"{self.base:04X}",
            "bank": self.bank,
            "source": self.source,
            "entry": f"{self.entry:04X}",
            "chain_depth": len(self.chain_addrs),
            "cursor_target": f"{self.cursor_target:04X}",
            "breakpoint_target": f"{self.breakpoint_target:04X}",
            "sentinel": f"{self.sentinel:04X}",
            "progress": f"{self.progress:04X}",
            "bootstrap_addr": f"{self.bootstrap_addr:04X}",
            "straight_entry": f"{self.straight_entry:04X}",
            "straight_calls": self.straight_calls,
            "traversal": [
                {"key": k, "pc": f"{pc:04X}", "region": region}
                for k, pc, region in self.traversal
            ],
        }


def _build_ram_rom_ram_fixture() -> MatrixFixture:
    """RAM -> ROM -> RAM: the developer's own RAM program calls a BASIC routine.

    The session is entered in RAM with a live context, so the visible-ROM
    breakpoint is reached by stepping into it from RAM rather than by a cold
    bootstrap jump. $BC0F is the canonical BASIC FAC copy routine, which the
    "rom" mode already steps through; it only touches BASIC zero-page scratch.
    """
    base = 0xC000
    helper = 0xC100
    sentinel = 0xC1F0
    progress = 0xC1F1
    bootstrap = bytes([
        0xD8, 0x18, 0x78, 0xB8,           # CLD/CLC/SEI/CLV
        0xA2, 0xF8, 0x9A,                 # LDX #$F8; TXS
        0xA9, 0x2F, 0x85, 0x00,           # CPU port DDR
        0xA9, 0x37, 0x85, 0x01,           # BASIC + KERNAL + I/O visible
        0xA9, 0x00, 0xA2, 0x00, 0xA0, 0x00,
        0x4C, base & 0xFF, base >> 8,
    ])
    main = bytes([
        0x20, helper & 0xFF, helper >> 8,          # C000 JSR helper   (RAM)
        0x20, 0x0F, 0xBC,                          # C003 JSR $BC0F    (BASIC ROM)
        0xA9, 0x77,                                # C006 LDA #$77
        0x8D, sentinel & 0xFF, sentinel >> 8,      # C008 STA sentinel
        0xEE, progress & 0xFF, progress >> 8,      # C00B INC progress
        # Loop back onto the INC, not onto itself: the Continue phase proves
        # liveness by watching the progress counter keep moving.
        0x4C, (base + 0x000B) & 0xFF, (base + 0x000B) >> 8,   # C00E JMP $C00B
    ])
    helper_code = bytes([
        0xA9, 0x42,
        0x8D, sentinel & 0xFF, sentinel >> 8,
        0x60,
    ])
    return MatrixFixture(
        memory_mode="ram-rom-ram",
        base=base, bank=7, source="RAM",
        entry=base,
        step_over_return=base + 0x0003,
        chain_entry=0xBC0F,
        chain_addrs=[0xBC0F],
        chain_return_addrs=[base + 0x0006],
        cursor_target=base + 0x0006,
        breakpoint_target=base + 0x000B,
        sentinel=sentinel,
        progress=progress,
        chunks=[(base, main), (helper, helper_code),
                (sentinel, b"\x00"), (progress, b"\x00")],
        bootstrap=bootstrap,
        traversal=[
            ("D", base + 0x0003, "RAM"),    # Step Over the RAM helper
            ("T", 0xBC0F, "BAS"),           # Step Into BASIC ROM  <- RAM to ROM
            ("U", base + 0x0006, "RAM"),    # Step Out of ROM      <- ROM to RAM
            ("D", base + 0x0008, "RAM"),    # keep stepping in RAM afterwards
        ],
    )


def _build_ram_rur_rom_ram_fixture() -> MatrixFixture:
    """RAM -> RAM-under-ROM -> RAM -> ROM -> RAM -> RAM-under-ROM -> RAM.

    A bank switch cannot execute from the window it is switching, so a direct
    RAM-under-ROM -> visible-ROM step is not expressible on a 6510: the `STA $01`
    that maps KERNAL back in would change the very bytes the CPU is fetching.
    The traversal therefore returns to RAM between the two banked regions, which
    is also what real code does.
    """
    base = 0xC000
    hidden = 0xE000                 # RAM under KERNAL
    sentinel = 0xC1F0
    progress = 0xC1F1
    bootstrap = bytes([
        0xD8, 0x18, 0x78, 0xB8,
        0xA2, 0xF8, 0x9A,
        0xA9, 0x2F, 0x85, 0x00,
        0xA9, 0x37, 0x85, 0x01,
        0xA9, 0x00, 0xA2, 0x00, 0xA0, 0x00,
        0x4C, base & 0xFF, base >> 8,
    ])
    main = bytes([
        0xA9, 0x35,                                # C000 LDA #$35
        0x85, 0x01,                                # C002 STA $01   -> KERNAL out
        0x20, hidden & 0xFF, hidden >> 8,          # C004 JSR $E000 (RAM under ROM)
        0xA9, 0x37,                                # C007 LDA #$37
        0x85, 0x01,                                # C009 STA $01   -> ROM visible
        0x20, 0x0F, 0xBC,                          # C00B JSR $BC0F (BASIC ROM)
        0xA9, 0x35,                                # C00E LDA #$35
        0x85, 0x01,                                # C010 STA $01   -> KERNAL out
        0x20, hidden & 0xFF, hidden >> 8,          # C012 JSR $E000 (RAM under ROM)
        0xA9, 0x37,                                # C015 LDA #$37
        0x85, 0x01,                                # C017 STA $01   -> ROM visible
        0xA9, 0x77,                                # C019 LDA #$77
        0x8D, sentinel & 0xFF, sentinel >> 8,      # C01B STA sentinel
        0xEE, progress & 0xFF, progress >> 8,      # C01E INC progress
        # Loop back onto the INC (see ram-rom-ram) so Continue observes liveness.
        0x4C, (base + 0x001E) & 0xFF, (base + 0x001E) >> 8,   # C021 JMP $C01E
    ])
    hidden_code = bytes([
        0xA9, 0x42,
        0x8D, sentinel & 0xFF, sentinel >> 8,
        0x60,
    ])
    return MatrixFixture(
        memory_mode="ram-rur-rom-ram",
        base=base, bank=7, source="RAM",
        entry=base,
        step_over_return=base + 0x0002,
        chain_entry=hidden,
        chain_addrs=[hidden],
        chain_return_addrs=[base + 0x0007],
        cursor_target=base + 0x0019,
        breakpoint_target=base + 0x001B,
        sentinel=sentinel,
        progress=progress,
        chunks=[(base, main), (hidden, hidden_code),
                (sentinel, b"\x00"), (progress, b"\x00")],
        bootstrap=bootstrap,
        hidden_ram_chunks=(hidden,),
        traversal=[
            ("D", base + 0x0002, "RAM"),    # LDA #$35
            ("D", base + 0x0004, "RAM"),    # STA $01 -> KERNAL banked out
            ("T", hidden, "RAM"),           # Step Into $E000   <- RAM to RAM-under-ROM
            ("U", base + 0x0007, "RAM"),    # Step Out          <- RAM-under-ROM to RAM
            ("D", base + 0x0009, "RAM"),    # LDA #$37
            ("D", base + 0x000B, "RAM"),    # STA $01 -> ROM visible again
            ("T", 0xBC0F, "BAS"),           # Step Into BASIC   <- RAM to ROM
            ("U", base + 0x000E, "RAM"),    # Step Out          <- ROM to RAM
            ("D", base + 0x0010, "RAM"),    # LDA #$35
            ("D", base + 0x0012, "RAM"),    # STA $01 -> KERNAL out again
            ("T", hidden, "RAM"),           # Step Into $E000   <- RAM to RAM-under-ROM
            ("U", base + 0x0015, "RAM"),    # Step Out          <- RAM-under-ROM to RAM
        ],
    )


def build_fixture(memory: str, depth: int,
                  straight_calls: int = DEFAULT_STRAIGHT_CALLS) -> MatrixFixture:
    if memory == "ram-rom-ram":
        return _build_ram_rom_ram_fixture()
    if memory == "ram-rur-rom-ram":
        return _build_ram_rur_rom_ram_fixture()
    if memory == "ram":
        base = 0xC000
        bank = 7
        source = "RAM"
        sentinel = 0xC1F0
        progress = 0xC1F1
        bootstrap = bytes([
            0xD8, 0x18, 0x78, 0xB8,       # CLD/CLC/SEI/CLV
            0xA2, 0xF8, 0x9A,             # LDX #$F8; TXS
            0xA9, 0x00, 0xA2, 0x00,       # deterministic A/X
            0xA0, 0x00,                   # deterministic Y
            0x4C, base & 0xFF, base >> 8,
        ])
    elif memory == "ram-under-rom":
        base = 0xE000
        bank = 5
        source = "RAM"
        sentinel = 0xE1F0
        # Live progress in ordinary RAM; REST readmem of $E000 may be ambiguous
        # while the freezer/debugger owns backing state.
        progress = 0x0400
        bootstrap = bytes([
            0x78, 0xD8, 0x18, 0xB8,       # SEI/CLD/CLC/CLV
            0xA2, 0xF8, 0x9A,             # LDX #$F8; TXS
            0xA9, 0x37, 0x85, 0x00,       # CPU port DDR
            0xA9, 0x35, 0x85, 0x01,       # KERNAL out, I/O visible
            0x4C, base & 0xFF, base >> 8,
        ])
    else:
        base = 0xE000
        bank = 7
        source = "KRN"
        sentinel = 0x00A2
        progress = 0x00A2
        bootstrap = bytes([
            0xD8, 0x18, 0x78, 0xB8,       # CLD/CLC/SEI/CLV
            0xA2, 0xF8, 0x9A,             # LDX #$F8; TXS
            0xA9, 0x37, 0x85, 0x00,       # CPU port DDR
            0xA9, 0x37, 0x85, 0x01,       # KERNAL/BASIC/I/O visible
            0x4C, base & 0xFF, base >> 8,
        ])

        return MatrixFixture(
            memory_mode=memory,
            base=base,
            bank=bank,
            source=source,
            entry=base + 0x0002,
            step_over_return=base + 0x0005,
            chain_entry=0xBC0F,
            chain_addrs=[0xBC0F],
            chain_return_addrs=[base + 0x0005],
            cursor_target=base + 0x0006,
            breakpoint_target=base + 0x0007,
            sentinel=sentinel,
            progress=progress,
            chunks=[],
            bootstrap=bootstrap,
        )

    over = base + 0x0100
    chain0 = base + 0x0120
    chain_addrs = [chain0 + i * 4 for i in range(depth)]
    chunks: list[tuple[int, bytes]] = []
    entry = base
    cursor_target = base + 0x0006
    breakpoint_target = base + 0x0008
    # The straight-call block sits inside the program's live loop. A debug entry
    # is made by arming a breakpoint and letting the CPU run to it, so the block
    # has to be on a path the program actually executes; a block reachable only
    # by a jump the fixture never takes can never be entered.
    straight_block = base + 0x0210
    straight_entry = base + 0x000E                        # the JSR into the block
    main = bytes([
        0x20, over & 0xFF, over >> 8,                     # JSR over
        0x20, chain0 & 0xFF, chain0 >> 8,                 # JSR chain0
        0xA9, 0x77,                                       # LDA #$77
        0x8D, sentinel & 0xFF, sentinel >> 8,             # STA sentinel
        0xEE, progress & 0xFF, progress >> 8,             # INC progress
        0x20, straight_block & 0xFF, straight_block >> 8,  # JSR straight_block
        0x4C, (base + 0x000B) & 0xFF, (base + 0x000B) >> 8,
    ])
    chunks.append((base, main))
    chunks.append((over, bytes([
        0xA9, 0x42,
        0x8D, sentinel & 0xFF, sentinel >> 8,
        0x60,
    ])))
    # Every JSR in the block targets the same one-line helper, so the CPU comes
    # back to the same stack level after each Step Over and the expected PC, SP
    # and A are exact at every position in the run.
    straight_body = bytearray()
    for _ in range(straight_calls):
        straight_body += bytes([0x20, over & 0xFF, over >> 8])
    straight_body.append(0x60)                              # RTS
    chunks.append((straight_block, bytes(straight_body)))

    chain_returns: list[int] = []
    for i, addr in enumerate(chain_addrs):
        if i + 1 < depth:
            nxt = chain_addrs[i + 1]
            chunks.append((addr, bytes([0x20, nxt & 0xFF, nxt >> 8, 0x60])))
            chain_returns.append((addr + 3) & 0xFFFF)
        else:
            chunks.append((addr, bytes([0xEA, 0x60])))
            chain_returns.append((addr + 1) & 0xFFFF)
    chunks.append((sentinel, b"\x00"))
    chunks.append((progress, b"\x00"))
    return MatrixFixture(
        memory_mode=memory,
        base=base,
        bank=bank,
        source=source,
        entry=entry,
        step_over_return=base + 0x0003,
        chain_entry=chain0,
        chain_addrs=chain_addrs,
        chain_return_addrs=chain_returns,
        cursor_target=cursor_target,
        breakpoint_target=breakpoint_target,
        sentinel=sentinel,
        progress=progress,
        chunks=chunks,
        bootstrap=bootstrap,
        straight_entry=straight_entry,
        straight_block=straight_block,
        straight_calls=straight_calls,
        straight_return=straight_entry + 3,
    )


def live_readback_trustworthy(fixture: MatrixFixture, address: int) -> bool:
    """Whether a REST read of `address` returns the fixture's own byte.

    It does not for anything the fixture put in RAM under ROM: a read there is
    served the ROM image instead of the hidden RAM the CPU executes and writes.
    """
    if address in fixture.hidden_ram_chunks:
        return False
    return not (fixture.memory_mode == "ram-under-rom" and 0xA000 <= address <= 0xFFFF)


def apply_fixture_entry_side_effects(mem: bytearray, fixture: MatrixFixture) -> None:
    if fixture.memory_mode == "ram-under-rom":
        mem[0x0000] = 0x37
        mem[0x0001] = 0x35
    elif fixture.memory_mode == "rom":
        mem[0x0000] = 0x37
        mem[0x0001] = 0x37
    elif fixture.memory_mode in TRAVERSAL_MODES:
        # The traversal bootstrap sets the canonical all-visible configuration;
        # the fixture itself then switches $01 as it crosses regions, and the
        # oracle follows those writes as ordinary memory writes.
        mem[0x0000] = 0x2F
        mem[0x0001] = 0x37


def source_tag_for(address: int, bank: int) -> str:
    """Monitor source tag ([RAM]/[BAS]/[KRN]/...) for an address in a bank."""
    bank &= 7
    if 0xA000 <= address <= 0xBFFF:
        return "BAS" if (bank & 3) == 3 else "RAM"
    if 0xD000 <= address <= 0xDFFF:
        if (bank & 3) == 0:
            return "RAM"
        return "I/O" if (bank & 4) else "CHR"
    if address >= 0xE000:
        return "KRN" if (bank & 2) else "RAM"
    return "RAM"


def apply_captured_rom_heads(mem: bytearray, cell_dir: Path) -> None:
    # Full pre-freeze ROM snapshots first (required for freeze cells, where the
    # in-session memory image cannot see BASIC/KERNAL), then the short heads.
    basic_full = cell_dir / "live-basic-a000-full.bin"
    kernal_full = cell_dir / "live-kernal-e000-full.bin"
    if basic_full.exists():
        data = basic_full.read_bytes()
        mem[0xA000:0xA000 + len(data)] = data
    if kernal_full.exists():
        data = kernal_full.read_bytes()
        mem[0xE000:0xE000 + len(data)] = data
    kernal_head = cell_dir / "live-kernal-e000.bin"
    basic_head = cell_dir / "live-basic-bc00.bin"
    if kernal_head.exists():
        data = kernal_head.read_bytes()
        mem[0xE000:0xE000 + len(data)] = data
    if basic_head.exists():
        data = basic_head.read_bytes()
        mem[0xBC00:0xBC00 + len(data)] = data


class DebugInterfaceDriver:
    def open_monitor(self): ...
    def close_monitor(self): ...
    def enter_debug_at(self, address: int): ...
    def read_debug_state(self): ...
    def send_key(self, key: str): ...
    def step_over(self): ...
    def step_into(self): ...
    def step_out(self): ...
    def continue_to_cursor(self, address: int): ...
    def continue_to_breakpoint(self, address: int): ...
    def continue_run(self): ...
    def reset_from_debug_ui(self): ...
    def verify_hygiene(self): ...
    def breakpoint_slot_lines(self) -> list[str]: ...


class BaseDriver(DebugInterfaceDriver):
    def __init__(self, args: argparse.Namespace, row: dict[str, Any],
                 cell_dir: Path, trace) -> None:
        self.args = args
        self.row = row
        self.cell_dir = cell_dir
        self.trace = trace
        self.rest = make_rest(args, timeout=12.0)
        self.fixture: MatrixFixture | None = None

    def event(self, kind: str, **data: Any) -> None:
        payload = {"time": now_stamp(), "kind": kind, **data}
        self.trace.write(json.dumps(payload, sort_keys=True) + "\n")
        self.trace.flush()

    def write_bytes(self, address: int, data: bytes) -> None:
        last_error: Exception | None = None
        for attempt in range(6):
            try:
                self.rest.write_mem(address, data)
                return
            except Exception as exc:  # noqa: BLE001 - transport recovery evidence
                last_error = exc
                self.event("write_retry", address=f"{address:04X}",
                           length=len(data), attempt=attempt + 1, error=str(exc))
                time.sleep(min(0.4 + attempt * 0.25, 1.5))
        raise GateError(f"REST writemem ${address:04X} failed after retries: {last_error}")

    def read_bytes(self, address: int, length: int) -> bytes:
        return self.rest.read_mem(address, length)

    def machine_is_held(self) -> bool | None:
        """Whether the cartridge is still holding the C64 in Ultimax.

        Ultimax leaves $1000-$CFFF undecoded on the bus, so a read of $C000 from
        the OTHER device comes back as all-$FF exactly while the cartridge holds
        the machine. None means the question could not be asked.
        """
        machine = getattr(self.rest, "machine", None)
        if machine is None:
            return None
        try:
            return machine.read_mem(0xC000, 4) == b"\xFF\xFF\xFF\xFF"
        except Exception:
            return None

    def release_machine_hold(self, context: str) -> bool:
        """Hand the machine back, and say so out loud when that fails.

        A state that ends with the cartridge still holding the C64 poisons
        everything after it: keystrokes reach the cartridge over the C64U's
        keyboard matrix, which is only scanned while the C64 executes, so the
        next state cannot drive the UI and its breakpoint table cannot be
        cleared. Releasing is therefore part of the result, not best-effort
        tidying, and a release that fails is reported rather than swallowed.
        """
        errors: list[str] = []
        try:
            self.close_monitor()
        except Exception as exc:
            errors.append(f"close_monitor: {type(exc).__name__}: {exc}")
        try:
            self.rest.reset()
        except Exception as exc:
            errors.append(f"reset: {type(exc).__name__}: {exc}")

        for _ in range(4):
            held = self.machine_is_held()
            if held is not True:
                if errors:
                    print(f"  [teardown] {context}: released, but {'; '.join(errors)}",
                          flush=True)
                self.event("release_machine_hold", context=context, held=False,
                           errors=errors)
                return True
            try:
                self.rest.menu_button()
            except Exception as exc:
                errors.append(f"menu_button: {type(exc).__name__}: {exc}")
                break
            time.sleep(1.2)

        print(f"  [teardown] {context}: MACHINE STILL HELD - the next state will "
              f"inherit a UI it cannot drive. {'; '.join(errors) or 'no exception raised'}",
              flush=True)
        self.event("release_machine_hold", context=context, held=True, errors=errors)
        return False

    def read_oracle_bytes(self, address: int, length: int) -> bytes:
        """Memory oracle that can see the address while the cartridge is frozen.

        The freezer holds the C64 in Ultimax, which leaves only $0000-$0FFF and
        the I/O space decoded on the cartridge bus. A read issued from the other
        device for $1000-$CFFF or $E000-$FFFF comes back as $FF, measured on
        hardware: with a banking state live, the C64U reads $A000 as FFFFFFFF
        and $E000 as FFFFFFFF while the cartridge reads 94E37BE3 and 8556200F.
        The independent read is therefore an oracle only for the I/O range while
        a state is live.

        Those two ranges are read from the cartridge instead. That is less
        independent, because the displayed bytes and this read both reach memory
        through the cartridge, but it compares two different paths through it
        rather than comparing $FF against real bytes and calling the difference
        a defect.
        """
        hidden_by_ultimax = (0x1000 <= address < 0xD000) or address >= 0xE000
        overlay = getattr(self.rest, "overlay", None)
        if hidden_by_ultimax and overlay is not None:
            return overlay.read_mem(address, length)
        return self.rest.read_mem(address, length)

    def read_memory_image(self, chunk_size: int = 0x1000) -> bytearray:
        image = bytearray()
        for address in range(0, 0x10000, chunk_size):
            image.extend(self.read_oracle_bytes(address, chunk_size))
        if len(image) != 0x10000:
            raise GateError(f"memory image length {len(image)}, expected 65536")
        return image

    def active_debug_readback_allowed(self) -> bool:
        return True

    def contextless_entry(self) -> bool:
        return False

    def capture_live_rom_snapshot(self) -> bytes:
        """Snapshot BASIC/KERNAL while the machine is still live, for the oracle.

        Once the freezer owns the banking, raw readmem no longer serves
        BASIC/KERNAL, so the oracle image has to be seeded from this pre-freeze
        capture rather than from the in-session memory image. Returns the
        $E000 head so the caller can check the KERNAL is the expected one.
        """
        kernal_head = self.read_bytes(0xE000, 16)
        basic_head = self.read_bytes(0xBC00, 0x40)
        (self.cell_dir / "live-kernal-e000.bin").write_bytes(kernal_head)
        (self.cell_dir / "live-basic-bc00.bin").write_bytes(basic_head)
        basic_full = bytearray()
        kernal_full = bytearray()
        for off in range(0, 0x2000, 0x1000):
            basic_full.extend(self.read_bytes(0xA000 + off, 0x1000))
            kernal_full.extend(self.read_bytes(0xE000 + off, 0x1000))
        (self.cell_dir / "live-basic-a000-full.bin").write_bytes(bytes(basic_full))
        (self.cell_dir / "live-kernal-e000-full.bin").write_bytes(bytes(kernal_full))
        return kernal_head

    def install_fixture(self, fixture: MatrixFixture) -> None:
        self.fixture = fixture
        if fixture.memory_mode in TRAVERSAL_MODES:
            self.capture_live_rom_snapshot()
        if fixture.memory_mode == "rom":
            kernal_head = self.capture_live_rom_snapshot()
            if kernal_head[:5] != bytes([0x85, 0x56, 0x20, 0x0F, 0xBC]):
                # readmem serves $E000 through the live 6510 map, so this only
                # reads the KERNAL while the KERNAL is banked in. A program left
                # running by an earlier cell or suite can be toggling banking,
                # and the read then samples RAM under ROM instead. Re-establish
                # a known machine before believing the bytes.
                self.event("rom_precondition_recheck",
                           first_read=kernal_head[:5].hex().upper())
                self.reset_baseline()
                kernal_head = self.capture_live_rom_snapshot()
            if kernal_head[:5] != bytes([0x85, 0x56, 0x20, 0x0F, 0xBC]):
                raise BlockedWithEvidence(
                    "Configured KERNAL at $E000 is not the canonical path "
                    "STA $56; JSR $BC0F required for the real-ROM trace. "
                    f"Observed {kernal_head[:5].hex().upper()}."
                )
            self.write_bytes(fixture.bootstrap_addr, fixture.bootstrap)
            self.event(
                "real_rom_fixture_selected",
                fixture=fixture.to_json(),
                note=("No custom KERNAL/ROM is installed. ROM validation uses "
                      "the configured live KERNAL/BASIC image."))
            return
        for address, data in fixture.chunks:
            self.write_bytes(address, data)
        self.write_bytes(fixture.bootstrap_addr, fixture.bootstrap)
        for address, data in fixture.chunks[: min(4, len(fixture.chunks))]:
            if address in fixture.hidden_ram_chunks:
                self.event("fixture_readback_deferred",
                           address=f"{address:04X}",
                           reason="chunk lives in RAM under ROM; REST readmem serves the ROM image")
                continue
            if fixture.memory_mode == "ram-under-rom" and 0xA000 <= address <= 0xFFFF:
                self.event("fixture_readback_deferred",
                           address=f"{address:04X}",
                           reason="REST readmem sees visible ROM, not hidden RAM backing store")
                continue
            if address in (fixture.sentinel, fixture.progress):
                # Counters the fixture program writes. Reading one back and
                # demanding the value just written is a race against the CPU,
                # not a check that the write landed: if anything is still
                # executing the program, the counter has already moved on.
                self.event("fixture_readback_deferred",
                           address=f"{address:04X}",
                           reason="counter cell the fixture program writes; "
                                  "its value is not stable enough to verify")
                continue
            actual = self.read_bytes(address, len(data))
            if actual != data:
                raise GateError(
                    f"fixture round-trip mismatch at ${address:04X}: "
                    f"expected {data.hex()} got {actual.hex()}"
                )
        self.event("fixture_installed", fixture=fixture.to_json())

    def reset_baseline(self) -> None:
        try:
            self.rest.reset()
        except Exception as exc:
            self.event("reset_response_ignored", error=str(exc))
        self.wait_rest_ready("post-reset", timeout=25.0)
        if not L.ensure_menu_closed(self.rest):
            raise GateError("menu did not close during baseline recovery")
        self.wait_rest_ready("post-menu-close")
        # machine:reset is asynchronous and REST answers throughout, so REST
        # readiness alone does not mean the 6510 stopped running the previous
        # cell's fixture. Installing over a still-running program lets it
        # execute half-written code and keep writing its counters. Waiting for
        # the BASIC READY prompt with a live jiffy clock is the proof that the
        # old program is gone and the machine is back in the KERNAL.
        try:
            overlay_lifecycle.wait_ready(self.rest, timeout=12.0)
            self.event("baseline_basic_ready")
        except Exception as exc:  # noqa: BLE001
            self.event("baseline_basic_ready_warning", error=str(exc))

    def wait_rest_ready(self, label: str, timeout: float = 20.0) -> None:
        """Wait until three consecutive REST reads succeed.

        A single success is not enough: right after a menu close or a firmware
        redeploy the device answers the first read and then stalls. The budget
        has to cover three *slow* reads, not three fast ones, because a device
        that is still coming up answers each read in seconds without ever
        raising -- which is why a failure here reports elapsed time and the
        run of consecutive successes, not just the last exception.
        """
        started = time.time()
        deadline = started + timeout
        last_error: Exception | None = None
        stable_reads = 0
        attempts = 0
        while time.time() < deadline:
            attempts += 1
            try:
                if not self.rest.alive(timeout=1.0):
                    raise GateError("REST TCP/80 not accepting")
                self.rest.read_mem(0x0400, 16)
                stable_reads += 1
                if stable_reads >= 3:
                    self.event("rest_ready", label=label,
                               elapsed=round(time.time() - started, 2))
                    return
            except Exception as exc:  # noqa: BLE001 - transport recovery loop
                last_error = exc
                stable_reads = 0
                self.event("rest_ready_retry", label=label, error=str(exc))
            time.sleep(0.35)
        raise GateError(
            f"{label}: REST did not stabilize within {timeout:.0f}s "
            f"({attempts} attempts, {stable_reads}/3 consecutive reads, "
            f"last error: {last_error})")

    def wait_progress_change(self, address: int, label: str, timeout: float = 4.0) -> bool:
        seen = set()
        deadline = time.time() + timeout
        while time.time() < deadline:
            seen.add(self.read_oracle_bytes(address, 1)[0])
            if len(seen) >= 2:
                self.event("progress_change", address=f"{address:04X}", values=sorted(seen))
                return True
            time.sleep(0.08)
        try:
            self.event(
                "progress_failure_snapshot",
                address=f"{address:04X}",
                seen=sorted(seen),
                cpu_port=self.read_bytes(0x0001, 1).hex(),
                progress=self.read_oracle_bytes(address, 1).hex(),
                sentinel=self.read_oracle_bytes(0xC1F0, 2).hex(),
                scratch=self.read_oracle_bytes(0xC1F0, 16).hex(),
                insn_trampoline=self.read_bytes(0x0340, 8).hex(),
                debug_store=self.read_bytes(0x03F0, 12).hex(),
            )
        except Exception as exc:  # noqa: BLE001 - preserve original failure
            self.event("progress_failure_snapshot_error", error=str(exc))
        raise GateError(f"{label}: progress byte ${address:04X} did not change; seen={sorted(seen)}")

    def stack_return_at(self, sp: int) -> int | None:
        stack = self.read_bytes(0x0100, 256)
        return stack[(sp + 1) & 0xFF] | (stack[(sp + 2) & 0xFF] << 8)

    def verify_hygiene(self):
        self.wait_rest_ready("hygiene")
        try:
            overlay_lifecycle.wait_ready(self.rest, timeout=8.0)
            self.event("basic_ready_validated")
        except Exception as exc:
            self.event("basic_ready_warning", error=str(exc))
        port = self.read_bytes(0x0001, 1)[0]
        if (port & 0x07) != 0x07:
            self.event(
                "cpu_port_readback_deferred",
                value=f"{port:02X}",
                reason=("$0001 REST readback is not a proven live CPU-port oracle "
                        "after local debug reset; READY and jiffy liveness are used "
                        "for banking hygiene"))
        else:
            self.event("cpu_port_safe", value=f"{port:02X}")
        return True


class TelnetDebugDriver(BaseDriver):
    def __init__(self, args: argparse.Namespace, row: dict[str, Any],
                 cell_dir: Path, trace) -> None:
        super().__init__(args, row, cell_dir, trace)
        self.session: mt.MonitorSession | None = None

    def open_monitor(self):
        mt.wait_for_monitor_ready(self.args.host, self.args.port,
                                  self.args.password, self.args.timeout)
        self.session = mt.MonitorSession(self.args.host, self.args.port,
                                         self.args.password, self.args.timeout)
        mt.TestConfig.session = self.session
        self.event("open_monitor", interface="telnet")

    def close_monitor(self):
        if self.session is not None:
            try:
                (self.cell_dir / "telnet-final-screen.txt").write_text(
                    self.session.capture().text(), encoding="utf-8")
            except Exception:
                pass
            # Leave the monitor with no Debug session, the same as
            # RestDebugDriver.close_monitor(): closing the telnet connection
            # ends this cell's transport but not the device's Debug session,
            # which stays parked on this fixture's PC. The next cell reopens
            # onto that session - possibly over a different transport, since
            # a memory mode runs every transport back to back - and a parked
            # session will not navigate, so its first goto times out and the
            # failure is reported against a cell that inherited someone
            # else's state. leave_stale_debug() is a second line of defence
            # for a REST-driven next cell; this is the first, so an all-telnet
            # run gets it too.
            #
            # dbg._ensure_no_debug(), not a fixed number of C=+D taps: tearing
            # a session down restores every patched byte before the header
            # redraws, so on a slow exit the Dbg flag can outlive the keystroke
            # by several seconds - its own docstring records that one C=+D and
            # a 3s budget was not enough. It also re-sends only every 4s
            # rather than stacking taps, and backs out of a BREAKPOINTS or
            # BOOKMARKS popup first, since either would otherwise eat a C=+D
            # the loop is waiting on.
            try:
                dbg._ensure_no_debug(self.session)
                self.event("close_monitor_debug_state", debug_active=False)
            except Exception as exc:  # noqa: BLE001 - teardown must not mask a verdict
                self.event("close_monitor_leave_debug_failed", error=str(exc))
            self.session.close()
            self.session = None
            mt.TestConfig.session = None

    def _session(self) -> mt.MonitorSession:
        if self.session is None:
            raise HarnessBug("telnet session is not open")
        return self.session

    def select_bank(self, bank: int) -> None:
        if getattr(self.args, "c64_host", None):
            self.event("select_bank_skipped", bank=bank,
                       reason="U2 MCM has no monitor-side bank view (CPU BANK N/A); reads live aperture")
            return
        dbg._select_monitor_view(self._session(), bank, f"{self.row['cell_id']}: select bank {bank}")

    def goto(self, address: int) -> None:
        self._session().goto(f"{address:04X}")

    def send_key(self, key: str):
        if len(key) == 1:
            self._session().send_char(key.upper())
        elif key == "C=+R":
            dbg._send_ctrl_r(self._session())
        elif key == "C=+D":
            dbg._send_ctrl_d(self._session())
        elif key == "RETURN":
            self._session().send_key("ENTER")
        else:
            self._session().send_key(key)
        self.event("key", key=key)

    def read_debug_state(self) -> DebugState:
        parsed = dbg._parse_footer_values(dbg._footer_value_line(self._session()))
        if not parsed["pc"]:
            raise GateError(f"blank debug footer: {parsed!r}")
        return DebugState(
            pc=int(parsed["pc"], 16),
            ac=int(parsed["ac"], 16),
            xr=int(parsed["xr"], 16),
            yr=int(parsed["yr"], 16),
            sp=int(parsed["sp"], 16),
            sr=int(parsed["sr"], 2),
            raw=parsed,
        )

    def debug_active(self) -> bool:
        return "Dbg" in dbg._header_line(self._session())

    def debug_footer_blank(self) -> bool:
        try:
            parsed = dbg._parse_footer_values(dbg._footer_value_line(self._session()))
            return not parsed["pc"]
        except Exception:
            return True

    def ensure_debug_active(self) -> None:
        if self.debug_active() and self.debug_footer_blank():
            self.send_key("C=+D")
            time.sleep(0.25)
            try:
                if "DEBUG CANCELLED" in self._session().capture().text().upper():
                    self.send_key("RETURN")
                    time.sleep(0.25)
            except Exception:
                pass
        if not self.debug_active():
            self.send_key("D")

    def wait_pc(self, address: int, label: str, timeout: float = 8.0) -> DebugState:
        try:
            parsed = dbg._wait_for_pc(self._session(), f"{address:04X}", timeout=timeout)
            self.event("wait_pc", label=label, pc=f"{address:04X}", footer=parsed)
            return self.read_debug_state()
        except mt.Failure:
            # The telnet render of this step's result may have been dropped under
            # congestion (firmware aborts a screen send it cannot deliver within
            # SO_SNDTIMEO), leaving the visible footer stale. Confirm the debugger's
            # TRUE state via STORE_* (DMA/REST - never dropped). If the debugger is
            # at the expected PC the step executed correctly and only its render was
            # lost: resync the footer and return the authoritative state. This is
            # NOT a retry and re-issues no debugger command.
            st = _authoritative_debug_state(self.rest)
            if st.pc == address:
                self.event("wait_pc_render_recovered", label=label,
                           pc=f"{address:04X}", store=st.raw.get("store"))
                try:
                    self._session().goto(f"{address:04X}")  # force footer redraw
                except Exception:  # noqa: BLE001 - redraw is best-effort
                    pass
                return st
            raise

    def clear_all_breakpoints(self) -> None:
        dbg._clear_all_breakpoints(self._session(), f"{self.row['cell_id']}: clear breakpoints")
        self.event("clear_all_breakpoints")

    def breakpoint_slot_lines(self) -> list[str]:
        return dbg._breakpoint_slot_lines(
            self._session(), f"{self.row['cell_id']}: breakpoint table")

    def set_breakpoint(self, address: int) -> None:
        if (self.fixture is not None and self.fixture.memory_mode == "ram-under-rom"
                and 0xA000 <= address <= 0xFFFF):
            self.goto(address)
            self._session().send_char("P")
            snap = self._session().capture()
            if "not mapped now" in snap.text():
                self._session().send_key("ENTER")
            self.event("set_breakpoint", address=f"{address:04X}",
                       note="RAM-under-ROM breakpoint may be invisible until CPU bank maps RAM")
            return
        self.goto(address)
        dbg._ensure_breakpoint_at(self._session(), address, f"{self.row['cell_id']}: set bp")
        self.event("set_breakpoint", address=f"{address:04X}")

    def clear_breakpoint(self, address: int) -> None:
        dbg._clear_breakpoint_at(self._session(), address, f"{self.row['cell_id']}: clear bp")
        self.event("clear_breakpoint", address=f"{address:04X}")

    def enter_debug_at(self, address: int):
        if self.fixture is None:
            raise HarnessBug("missing fixture")
        self.select_bank(self.fixture.bank)
        self.goto(address)
        self.send_key("A")
        self.ensure_debug_active()
        if self.fixture.memory_mode == "rom":
            self.clear_all_breakpoints()
            self.set_breakpoint(address)
            self.goto(self.fixture.bootstrap_addr)
            self.send_key("G")
            state = self.wait_pc(address, "entry breakpoint",
                                 timeout=CONTEXTLESS_ENTRY_WAIT_S)
            self.clear_breakpoint(address)
            self.select_bank(self.fixture.bank)
            self.goto(address)
            return state
        self.clear_all_breakpoints()
        self.set_breakpoint(address)
        if self.fixture.memory_mode == "ram-under-rom":
            self.select_bank(7)
        self.goto(self.fixture.bootstrap_addr)
        self.send_key("G")
        state = self.wait_pc(address, "entry breakpoint",
                             timeout=CONTEXTLESS_ENTRY_WAIT_S)
        self.clear_breakpoint(address)
        self.select_bank(self.fixture.bank)
        self.goto(address)
        return state

    def step_over(self):
        self.send_key("D")

    def step_into(self):
        self.send_key("T")

    def step_out(self):
        self.send_key("U")

    def continue_to_cursor(self, address: int):
        self.goto(address)
        self.send_key("K")

    def continue_to_breakpoint(self, address: int):
        current = self.read_debug_state()
        self.set_breakpoint(address)
        self.goto(current.pc)
        self.event("restore_execution_cursor",
                   address=f"{current.pc:04X}",
                   reason="breakpoint toggle moves the monitor cursor")
        self.send_key("G")

    def continue_run(self):
        self.send_key("G")

    def reset_from_debug_ui(self):
        self.send_key("C=+R")
        dbg._wait_for_c64_ready(machine_host(self.args), timeout=10.0)
        self.event("reset_from_debug_ui", method="telnet C=+R")


class RestDebugDriver(BaseDriver):
    interface_type = "Overlay on HDMI"

    def __init__(self, args: argparse.Namespace, row: dict[str, Any],
                 cell_dir: Path, trace) -> None:
        super().__init__(args, row, cell_dir, trace)
        self.session = stress.RestSession(args.rest_host, ui=row["interface"],
                                          c64_host=getattr(args, "c64_host", None))
        # Split U2+L session: drive the overlay UI through the same SplitRest so
        # keystrokes/memory go to the C64U while menu_screen/menu_button stay on
        # the cartridge. Single-host runs keep RestSession's own Rest untouched.
        if getattr(args, "c64_host", None):
            self.session.rest = self.rest

    def reset_baseline(self) -> None:
        try:
            self.session.recover()
            self.event("rest_localui_recover")
        except Exception as exc:
            self.event("rest_localui_recover_warning", error=str(exc))
            super().reset_baseline()
            return
        self.wait_rest_ready("post-reset", timeout=25.0)
        if not L.ensure_menu_closed(self.rest):
            raise GateError("menu did not close during local-UI baseline recovery")
        self.wait_rest_ready("post-menu-close")

    def apply_interface_type(self):
        # A U2+L cartridge (split session) has no "Interface Type" config - its
        # only UI is the freeze overlay (opening the menu freezes the C64, jiffy
        # stops), so the config PUT 404s there. Skip it; freeze is implicit.
        if getattr(self.args, "c64_host", None):
            self.event("interface_type_skipped",
                       reason="U2+L has no Interface Type config; freeze is the only UI mode")
            return
        overlay_lifecycle.set_interface_type(self.rest, self.interface_type)

    def open_monitor(self):
        self.apply_interface_type()
        L.ensure_menu_closed(self.rest)
        self.session.open()
        self.event("open_monitor", interface=self.row["interface"],
                   interface_type=self.interface_type)

    def close_monitor(self):
        try:
            (self.cell_dir / f"{self.row['interface']}-final-screen.txt").write_text(
                self.rest.screen_text(), encoding="utf-8")
        except Exception:
            pass
        # Leave the monitor with no Debug session. open_monitor() reuses a
        # monitor that is already up, so a cell that ends still parked in Debug
        # hands the next cell a monitor sitting on this fixture's PC, and a
        # parked session will not navigate: the next cell's first goto times out
        # and the failure is reported against a cell that did nothing wrong.
        # Runs after this cell's verdict is decided, so it cannot mask it.
        #
        # A twelve-second budget, not three quick taps: tearing a session down
        # restores every patched byte before the header redraws, so on a slow
        # exit the Dbg flag can outlive the keystroke by several seconds - see
        # monitor_debug_test.py's dbg._ensure_no_debug(), which exists because a
        # short fixed budget was measured not enough. This mirrors that
        # function for the REST transport: re-send only every 4s rather than
        # stacking taps, and back out of a BREAKPOINTS or BOOKMARKS popup
        # first, since either would otherwise eat a C=+D the loop is waiting on.
        try:
            deadline = time.time() + 12.0
            last_sent = 0.0
            while time.time() < deadline:
                text = self.rest.screen_text()
                if "Dbg" not in text:
                    break
                if "BREAKPOINTS" in text or "BOOKMARKS" in text:
                    self.rest.tap(["run_stop"])
                    time.sleep(0.2)
                    continue
                if time.time() - last_sent > 4.0:
                    self.rest.tap(["commodore", "d"])
                    last_sent = time.time()
                time.sleep(0.2)
            self.event("close_monitor_debug_state",
                       debug_active="Dbg" in self.rest.screen_text())
        except Exception as exc:  # noqa: BLE001 - teardown must not mask a verdict
            self.event("close_monitor_leave_debug_failed", error=str(exc))
        try:
            self.session.close()
        except Exception:
            pass

    def select_bank(self, bank: int) -> None:
        if getattr(self.args, "c64_host", None):
            self.event("select_bank_skipped", bank=bank,
                       reason="U2 MCM has no monitor-side bank view (CPU BANK N/A); reads live aperture")
            return
        overlay_lifecycle.select_monitor_bank(self.rest, bank,
                                              f"{self.row['cell_id']}: select bank {bank}")

    def goto(self, address: int) -> None:
        overlay_lifecycle.goto_addr(
            self.rest, address, f"{self.row['cell_id']}: goto ${address:04X}")
        self.event("goto", address=f"{address:04X}")

    def send_key(self, key: str):
        if len(key) == 1:
            self.rest.tap([key.lower()])
        elif key == "C=+R":
            self.rest.tap(["commodore", "x"])
        elif key == "C=+D":
            self.rest.tap(["commodore", "d"])
        elif key == "RETURN":
            self.rest.tap(["return"])
        else:
            mapping = {
                "RUNSTOP": ["run_stop"],
                "DEL": ["inst_del"],
                "ESC": ["run_stop"],
            }
            self.rest.tap(mapping[key])
        time.sleep(0.25)
        if self.release_all_after_tap():
            self.rest.release_all()
        self.event("key", key=key)

    def release_all_after_tap(self) -> bool:
        return True

    def active_debug_readback_allowed(self) -> bool:
        return False

    def read_debug_state(self) -> DebugState:
        lines = self.rest.screen_lines()
        footer = stress.parse_footer(lines)
        if footer is None:
            (self.cell_dir / "screen-without-footer.txt").write_text(
                "\n".join(lines), encoding="utf-8")
            raise GateError("debug footer not observable through menu_screen")
        return DebugState(footer.pc, footer.ac, footer.xr, footer.yr,
                          footer.sp, footer.sr, raw=footer.__dict__)

    def debug_active(self) -> bool:
        try:
            return "Dbg" in "\n".join(self.rest.screen_lines()[:4])
        except Exception:
            return False

    def debug_footer_blank(self) -> bool:
        try:
            return stress.parse_footer(self.rest.screen_lines()) is None
        except Exception:
            return True

    def ensure_debug_active(self) -> None:
        if self.debug_active() and self.debug_footer_blank():
            self.send_key("C=+D")
            time.sleep(0.25)
            try:
                if "DEBUG CANCELLED" in self.rest.screen_text().upper():
                    self.send_key("RETURN")
                    time.sleep(0.25)
            except Exception:
                pass
        if not self.debug_active():
            self.send_key("D")

    def wait_pc(self, address: int, label: str, timeout: float = 8.0) -> DebugState:
        try:
            footer = self.session.wait_footer_pc(address, timeout=timeout, ctx=label)
        except Exception:
            try:
                (self.cell_dir / f"{label.replace(' ', '_')}-screen.txt").write_text(
                    self.rest.screen_text(), encoding="utf-8")
            except Exception:
                pass
            # Evidence only: the footer PC never reached the target, so record
            # what the footer and the firmware's own STORE_* capture say now.
            # A STORE_* PC that already equals the target means the step ran and
            # only its footer render is behind.
            samples = []
            for _ in range(4):
                entry: dict[str, Any] = {}
                try:
                    entry["footer"] = self.read_debug_state().raw
                except Exception as exc:  # noqa: BLE001 - diagnostic capture only
                    entry["footer_error"] = str(exc)
                try:
                    entry["store"] = _authoritative_debug_state(self.rest).raw
                except Exception as exc:  # noqa: BLE001 - diagnostic capture only
                    entry["store_error"] = str(exc)
                samples.append(entry)
                time.sleep(0.1)
            self.event("footer_resample_after_wait_pc_timeout", label=label,
                       expected_pc=f"{address:04X}", samples=samples)
            raise
        self.event("wait_pc", label=label, pc=f"{address:04X}",
                   footer=footer.__dict__)
        return DebugState(footer.pc, footer.ac, footer.xr, footer.yr,
                          footer.sp, footer.sr, raw=footer.__dict__)

    def _open_breakpoint_popup(self) -> str:
        self.rest.tap(["commodore", "p"])
        time.sleep(0.45)
        text = self.rest.screen_text()
        if "BREAKPOINT" not in text.upper():
            raise GateError(
                "breakpoint popup did not open on CBM+P; monitor screen:\n"
                + text)
        return text

    def _open_breakpoint_popup_if_any(self) -> str | None:
        """Like _open_breakpoint_popup, but tolerates a table with nothing
        armed: the firmware only opens this popup when debug_has_breakpoint()
        is true (see machine_monitor.cc), so CBM+P legitimately does nothing
        on an already-empty table. That is not a failure for a caller that is
        only checking whether there is anything to clear."""
        self.rest.tap(["commodore", "p"])
        time.sleep(0.45)
        text = self.rest.screen_text()
        return text if "BREAKPOINT" in text.upper() else None

    @staticmethod
    def _slot_lines_from(text: str) -> list[str]:
        return [line.strip() for line in text.splitlines()
                if BREAKPOINT_SLOT_RE.match(line.strip())]

    def breakpoint_slot_lines(self) -> list[str]:
        text = self._open_breakpoint_popup()
        lines = self._slot_lines_from(text)
        self.rest.tap(["run_stop"])
        time.sleep(0.2)
        if not lines:
            raise GateError(f"breakpoint popup listed no slots:\n{text}")
        return lines

    def clear_all_breakpoints(self) -> None:
        """Delete every armed slot and prove the table is empty afterwards.

        The cursor starts on slot 0 and INST/DEL clears the slot under it, so
        one delete-then-advance pass covers the table. Each delete is confirmed
        by re-reading the popup before the cursor advances, rather than sending
        a fixed burst and checking only at the end: a delete issued while the
        popup is repainting is dropped, and how long that repaint takes is a
        property of the target. On a U2+L, where the overlay is driven across
        the cartridge bus, a fixed 0.08s gap dropped every delete and left the
        table fully armed, while the same key confirmed one slot at a time
        clears it. Waiting on the observation costs nothing on a fast target and
        is the only pacing that suits both.
        """
        for attempt in range(1, 4):
            text = self._open_breakpoint_popup_if_any()
            if text is None:
                self.event("clear_all_breakpoints", attempts=attempt,
                           table_empty="no armed slot to show CBM+P")
                return
            if not any("EMPTY" not in line.upper()
                       for line in self._slot_lines_from(text)):
                self.rest.tap(["run_stop"])
                time.sleep(0.2)
                self.event("clear_all_breakpoints", attempts=attempt)
                return
            deletes = 0
            popup_lost = 0
            for slot in range(10):
                deadline = time.time() + 3.0
                while time.time() < deadline:
                    screen = self.rest.screen_text()
                    lines = self._slot_lines_from(screen)
                    if slot >= len(lines) or "EMPTY" in lines[slot].upper():
                        break
                    # A delete only reaches the table while the popup is up and
                    # holding focus. Driven by hand the popup is opened and
                    # watched; in a run it is assumed to still be there, and a
                    # key sent to a screen that is no longer the popup lands
                    # nowhere while the table goes on reading as armed. Counted
                    # rather than assumed: popup_lost == 0 refutes that as the
                    # explanation for an in-run failure, and a non-zero count
                    # names it.
                    if "BREAKPOINT" not in screen.upper():
                        popup_lost += 1
                        screen = self._open_breakpoint_popup()
                        lines = self._slot_lines_from(screen)
                        if slot >= len(lines) or "EMPTY" in lines[slot].upper():
                            break
                    self.rest.tap(["inst_del"])
                    deletes += 1
                    time.sleep(0.25)
                self.rest.tap(["cursor_up_down"])
                time.sleep(0.12)
            # Recorded per pass because this loop deletes correctly when driven
            # by hand and has failed in situ: the pass that fails needs to say
            # what it was looking at, not be reconstructed afterwards.
            tail = self._slot_lines_from(self.rest.screen_text())
            armed_after = [l for l in tail if "EMPTY" not in l.upper()]
            if popup_lost:
                print(f"  [clear] pass {attempt}: popup was not focused for "
                      f"{popup_lost} delete(s); reopened", flush=True)
            self.event("clear_pass", attempt=attempt, deletes=deletes,
                       popup_lost=popup_lost, armed_after=armed_after)
            self.rest.tap(["run_stop"])
            time.sleep(0.25)
        remaining = [line for line in self.breakpoint_slot_lines()
                     if "EMPTY" not in line.upper()]
        if remaining:
            popup = self._open_breakpoint_popup()
            self.rest.tap(["run_stop"])
            raise GateError(
                f"breakpoint table still armed after 3 clear passes: {remaining}\n"
                f"popup as the clear loop last saw it:\n{popup}")
        self.event("clear_all_breakpoints", attempts=3)

    def _u2_toggle_breakpoint(self, address: int) -> str:
        """U2 breakpoint toggle: goto + R, no monitor bank view, source tag is
        always [CPU] (U2MemoryBackend::source_name), so the shared
        ensure/clear_breakpoint_at (which selects a bank and matches [RAM]/[KRN])
        do not apply. Returns the target row after the toggle."""
        self.goto(address)
        self.send_key("P")
        text = self.rest.screen_text()
        if "not mapped now" in text:
            self.send_key("RETURN")
        time.sleep(0.2)
        return overlay_lifecycle.line_for_address(self.rest, address)

    def set_breakpoint(self, address: int) -> None:
        if self.fixture is None:
            raise HarnessBug("missing fixture")
        if getattr(self.args, "c64_host", None):
            row = overlay_lifecycle.line_for_address(self.rest, address)
            if "[BRK" not in row:
                row = self._u2_toggle_breakpoint(address)
            note = None
            if "[BRK" not in row and self.fixture.memory_mode == "ram-under-rom":
                note = "RAM-under-ROM breakpoint may be invisible until CPU bank maps RAM"
            elif "[BRK" not in row:
                raise GateError(f"U2 breakpoint not set at ${address:04X}: {row!r}")
            self.event("set_breakpoint", address=f"{address:04X}", note=note)
            return
        if self.fixture.memory_mode == "ram-under-rom" and 0xA000 <= address <= 0xFFFF:
            self.goto(address)
            self.send_key("P")
            text = self.rest.screen_text()
            if "not mapped now" in text:
                self.send_key("RETURN")
            self.event("set_breakpoint", address=f"{address:04X}",
                       note="RAM-under-ROM breakpoint may be invisible until CPU bank maps RAM")
            return
        overlay_lifecycle.ensure_breakpoint_at(
            self.rest, address, self.fixture.bank,
            source_tag_for(address, self.fixture.bank),
            f"{self.row['cell_id']}: set bp ${address:04X}")
        self.event("set_breakpoint", address=f"{address:04X}")

    def clear_breakpoint(self, address: int) -> None:
        if self.fixture is None:
            raise HarnessBug("missing fixture")
        if getattr(self.args, "c64_host", None):
            row = overlay_lifecycle.line_for_address(self.rest, address)
            if "[BRK" in row:
                row = self._u2_toggle_breakpoint(address)
            self.event("clear_breakpoint", address=f"{address:04X}")
            return
        overlay_lifecycle.clear_breakpoint_at(
            self.rest, address, self.fixture.bank,
            f"{self.row['cell_id']}: clear bp ${address:04X}")
        self.event("clear_breakpoint", address=f"{address:04X}")

    def leave_stale_debug(self) -> None:
        """End a Debug session inherited from an earlier cell.

        Closing the menu does not end a Debug session, so a cell can finish with
        the menu shut while the session is still parked on its fixture's PC. The
        next cell reopens the monitor onto that session, and a parked session
        will not navigate: its first goto times out and the failure is reported
        against a cell that did nothing wrong. This runs with the monitor open
        and before any of the tested workflow, so it cannot mask this cell.
        """
        for attempt in range(4):
            try:
                if "Dbg" not in self.rest.screen_text():
                    if attempt:
                        self.event("left_stale_debug", taps=attempt)
                    return
            except mt.Failure:
                return          # no monitor screen to read; nothing inherited
            self.rest.tap(["commodore", "d"])
            time.sleep(0.5)
        raise GateError(
            "inherited Debug session would not exit with C=+D:\n"
            + self.rest.screen_text())

    def log_entry_opcode(self, address: int, verdict: str) -> None:
        """Record the breakpoint target byte through the machine's own DMA.

        A launch that never starts the program and a launch that starts it after
        the breakpoint opcode has been lost both report DEBUG TIMEOUT, so the
        byte is read on every entry, passing or failing. An armed breakpoint
        reads as $00; anything else means the trap the run depends on is not
        there, whatever the entry verdict says.
        """
        try:
            got = self.read_oracle_bytes(address, 1)[0]
        except Exception as exc:
            print(f"  [entry probe] ${address:04X} unreadable ({exc}) - {verdict}")
            return
        note = "BRK armed" if got == 0x00 else "NOT BRK"
        print(f"  [entry probe] ${address:04X} = ${got:02X} ({note}) - {verdict}")

    def enter_debug_at(self, address: int):
        if self.fixture is None:
            raise HarnessBug("missing fixture")
        self.leave_stale_debug()
        self.select_bank(self.fixture.bank)
        self.goto(address)
        self.send_key("A")
        self.ensure_debug_active()
        self.select_bank(self.fixture.bank)
        self.clear_all_breakpoints()
        self.set_breakpoint(address)
        if self.fixture.memory_mode == "ram-under-rom":
            self.select_bank(7)
        elif self.fixture.memory_mode == "rom":
            self.select_bank(self.fixture.bank)
        state = None
        for attempt in range(2):
            self.goto(self.fixture.bootstrap_addr)
            self.log_entry_opcode(address, "before launch")
            self.send_key("G")
            try:
                state = self.wait_pc(address, "entry breakpoint",
                                     timeout=CONTEXTLESS_ENTRY_WAIT_S)
                break
            except BaseException as exc:
                if "DEBUG TIMEOUT" not in str(exc) or attempt:
                    self.log_entry_opcode(address, "entry FAILED")
                    raise
                # The firmware's own 5s watchdog on the breakpoint-hit wait,
                # not a logic failure: the entry breakpoint is a single JMP
                # away from the bootstrap, and normally trips in well under
                # that budget. Recorded in the run ledgers as roughly 11 of
                # 26 overlay x boundary-mode failures, all this shape. Clear
                # the popup and relaunch once, the same bootstrap+breakpoint
                # a first attempt uses, before treating it as a real failure.
                self.event("entry_debug_timeout_retry", attempt=attempt + 1)
                self.send_key("RETURN")
                time.sleep(0.3)
        self.log_entry_opcode(address, "entry ok")
        # The entry slot is the only slot this path armed.  Clearing that
        # address directly avoids repeatedly reopening the popup while the
        # U2 freezer has the C64 held at the just-trapped PC; those delete
        # keystrokes can be lost even though the slot table is otherwise sound.
        self.clear_breakpoint(address)
        self.select_bank(self.fixture.bank)
        self.goto(address)
        return state

    def step_over(self):
        self.send_key("D")

    def step_into(self):
        self.send_key("T")

    def step_out(self):
        self.send_key("U")

    def continue_to_cursor(self, address: int):
        self.goto(address)
        self.send_key("K")

    def continue_to_breakpoint(self, address: int):
        current = self.read_debug_state()
        self.set_breakpoint(address)
        self.goto(current.pc)
        self.event("restore_execution_cursor",
                   address=f"{current.pc:04X}",
                   reason="breakpoint toggle moves the monitor cursor")
        self.send_key("G")

    def continue_run(self):
        self.send_key("G")
        time.sleep(1.5)

    def reset_from_debug_ui(self):
        self.send_key("C=+R")
        try:
            overlay_lifecycle.wait_ready(self.rest, timeout=10.0)
            self.event("reset_from_debug_ui", method=f"{self.row['interface']} C=+R")
            return
        except Exception as exc:
            self.event(
                "reset_from_debug_ui_fallback",
                attempted=f"{self.row['interface']} C=+R",
                error=str(exc),
                reason=("Continue closes the local monitor/UI, so the post-Continue "
                        "C=+R chord is not guaranteed to be consumed by Debug"))
        self.rest.reset()
        overlay_lifecycle.wait_ready(self.rest, timeout=12.0)
        self.event("reset_from_debug_ui", method="REST /v1/machine:reset fallback")


class OverlayRestDebugDriver(RestDebugDriver):
    interface_type = "Overlay on HDMI"


class FreezeRestDebugDriver(RestDebugDriver):
    interface_type = "Freeze"

    def release_all_after_tap(self) -> bool:
        return False

    def contextless_entry(self) -> bool:
        return False

    def open_monitor(self):
        self.apply_interface_type()
        L.ensure_menu_closed(self.rest)
        self.session.open()
        text = ""
        try:
            lines = self.rest.screen_lines()
            text = "\n".join(lines)
            (self.cell_dir / "freeze-menu-screen-probe.txt").write_text(text, encoding="utf-8")
        except Exception as exc:
            (self.cell_dir / "freeze-menu-screen-probe.txt").write_text(
                f"menu_screen unavailable for freeze probe: {type(exc).__name__}: {exc}\n",
                encoding="utf-8")
        if "MONITOR" not in text.upper():
            raise BlockedWithEvidence(
                "Freeze monitor is not observable through menu_screen on this firmware/API, "
                "and no alternate local-console/c64scope capture path was configured for "
                "this run."
            )

    def enter_debug_at(self, address: int):
        return super().enter_debug_at(address)
        self.event("open_monitor", interface="freeze", interface_type=self.interface_type)


def make_driver(args: argparse.Namespace, row: dict[str, Any], cell_dir: Path, trace) -> BaseDriver:
    if row["interface"] == "telnet":
        return TelnetDebugDriver(args, row, cell_dir, trace)
    if row["interface"] == "overlay":
        return OverlayRestDebugDriver(args, row, cell_dir, trace)
    if row["interface"] == "freeze":
        return FreezeRestDebugDriver(args, row, cell_dir, trace)
    raise HarnessBug(f"unknown interface {row['interface']}")


def sr_mask(a: int, b: int) -> bool:
    return (a & ~(0x10 | 0x20)) == (b & ~(0x10 | 0x20))


def assert_state_pc_sp(state: DebugState, pc: int, sp: int | None, label: str) -> None:
    if state.pc != pc:
        raise GateError(f"{label}: expected PC ${pc:04X}, got ${state.pc:04X}")
    if sp is not None and state.sp != sp:
        raise GateError(f"{label}: expected SP ${sp:02X}, got ${state.sp:02X}")


def assert_state_matches_cpu(state: DebugState, cpu: ORC.CPU6502, label: str) -> None:
    if state.pc != cpu.pc:
        raise GateError(f"{label}: expected PC ${cpu.pc:04X}, got ${state.pc:04X}")
    if state.ac != cpu.a:
        raise GateError(f"{label}: expected AC ${cpu.a:02X}, got ${state.ac:02X}")
    if state.xr != cpu.x:
        raise GateError(f"{label}: expected XR ${cpu.x:02X}, got ${state.xr:02X}")
    if state.yr != cpu.y:
        raise GateError(f"{label}: expected YR ${cpu.y:02X}, got ${state.yr:02X}")
    if state.sp != cpu.sp:
        raise GateError(f"{label}: expected SP ${cpu.sp:02X}, got ${state.sp:02X}")
    if not sr_mask(state.sr, cpu.p):
        raise GateError(f"{label}: expected SR ${cpu.p:02X}, got ${state.sr:02X}")


def mark_op(row: dict[str, Any], op: str, status: str) -> None:
    row[op] = status


def rom_region(address: int) -> str:
    if 0xA000 <= address <= 0xBFFF:
        return "basic"
    if 0xE000 <= address <= 0xFFFF:
        return "kernal"
    return "other"


def clone_cpu(cpu: ORC.CPU6502) -> ORC.CPU6502:
    clone = ORC.CPU6502(bytearray(cpu.mem))
    clone.set_state(cpu.a, cpu.x, cpu.y, cpu.sp, cpu.pc, cpu.p)
    return clone


def step_and_wait_pc(driver: BaseDriver, action, target: int, label: str,
                     start_pc: int, timeout: float = 8.0,
                     retries: int = 3) -> DebugState:
    last_exc: Exception | None = None
    for attempt in range(retries + 1):
        action()
        try:
            return driver.wait_pc(target, label, timeout=timeout)
        except Exception as exc:  # noqa: BLE001 - retry only on proven no-progress trap
            last_exc = exc
            if not isinstance(driver, RestDebugDriver):
                raise
            try:
                current = driver.read_debug_state()
            except Exception:
                raise
            if current.pc != start_pc:
                raise
            if attempt >= retries:
                break
            # REST-only: the footer re-trapped at the exact launch PC with the CPU
            # not having moved. Re-issuing the step is a command replay, so it is
            # counted transparently as command_retry - the anti-masking invariant
            # requires this to be zero for an honest run (it never fires on the
            # telnet path, which re-raises above). See the anti-masking allow-list.
            COUNTERS.count("command_retry",
                           f"{label}: REST same-PC step re-trap at ${start_pc:04X}")
            driver.event("same_pc_step_retry",
                         label=label,
                         attempt=attempt + 1,
                         start_pc=f"{start_pc:04X}",
                         target=f"{target:04X}",
                         reason="debug footer re-trapped at the launch PC with no progress")
    if last_exc:
        raise last_exc
    raise GateError(f"{label}: retry exhausted without executing step")




def screen_text(driver: BaseDriver) -> str:
    if isinstance(driver, TelnetDebugDriver):
        return driver._session().capture().text()
    return driver.rest.screen_text()




# Two concurrent runs of this suite must not pick the same VICE port.
# run-tests hands each of the runs it starts itself an index in E2E_PORT_SLOT,
# but it numbers them from zero per invocation, so two run-tests invocations
# (a u64 run and a u2@c64u run started separately) both offered slot 0 and
# their VICE oracles fought over 127.0.0.1:6518. The loser reported
# "VICE oracle setup failed: timed out", which reads as a device fault and is
# not one. The slot below is therefore claimed rather than assumed:
# E2E_PORT_SLOT is the preferred starting point, so a lone run keeps the
# layout run-tests gave it, and any run that finds its preference taken moves
# to the next free slot.
VICE_PORT_BASE = 6518
VICE_PORTS_PER_RUN = 8
VICE_MAX_SLOTS = 16

# Held open for the life of the process: closing the file drops the lock, and
# the kernel drops it for us if the run is killed, so a crashed run leaves no
# stale claim behind.
_VICE_SLOT_LOCK_FDS: list[int] = []
_VICE_SLOT: int | None = None


def _slot_ports_free(slot: int) -> bool:
    """True when every port this slot owns can be bound right now.

    The lock alone only excludes runs that take part in this protocol. A VICE
    left behind by a killed run holds its port without holding a lock, which
    is exactly the state that produced the timeouts, so the ports are probed
    as well.
    """
    for offset in range(VICE_PORTS_PER_RUN):
        probe = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        try:
            probe.bind(("127.0.0.1", VICE_PORT_BASE + slot * VICE_PORTS_PER_RUN + offset))
        except OSError:
            return False
        finally:
            probe.close()
    return True


def _claim_vice_slot() -> int:
    global _VICE_SLOT
    if _VICE_SLOT is not None:
        return _VICE_SLOT
    preferred = int(os.environ.get("E2E_PORT_SLOT", "0"))
    lock_dir = Path(tempfile.gettempdir()) / "mcm-vice-slots"
    lock_dir.mkdir(parents=True, exist_ok=True)
    for step in range(VICE_MAX_SLOTS):
        slot = (preferred + step) % VICE_MAX_SLOTS
        fd = os.open(str(lock_dir / f"slot-{slot}.lock"),
                     os.O_CREAT | os.O_RDWR, 0o644)
        try:
            fcntl.flock(fd, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except OSError:
            os.close(fd)
            continue
        if not _slot_ports_free(slot):
            os.close(fd)        # closing drops the lock
            continue
        os.truncate(fd, 0)
        os.write(fd, f"{os.getpid()}\n".encode())
        _VICE_SLOT_LOCK_FDS.append(fd)
        _VICE_SLOT = slot
        if slot != preferred:
            print(f"VICE port slot {preferred} was taken; using slot {slot} "
                  f"(base port {VICE_PORT_BASE + slot * VICE_PORTS_PER_RUN})",
                  flush=True)
        return slot
    raise GateError(
        f"no free VICE port slot in {VICE_MAX_SLOTS} tries from "
        f"{VICE_PORT_BASE}; another run or a leftover x64sc holds them all")


def vice_port(offset: int) -> int:
    return VICE_PORT_BASE + _claim_vice_slot() * VICE_PORTS_PER_RUN + offset


class ViceBinaryMonitor:
    def __init__(self, port: int, artifact_dir: Path) -> None:
        self.port = port
        self.artifact_dir = artifact_dir
        self.proc: subprocess.Popen | None = None
        self.sock: socket.socket | None = None
        self.request_id = 1
        self.reg_ids: dict[str, int] = {}

    def __enter__(self):
        self.artifact_dir.mkdir(parents=True, exist_ok=True)
        stdout = (self.artifact_dir / "vice-stdout.log").open("wb")
        stderr = (self.artifact_dir / "vice-stderr.log").open("wb")
        self.proc = subprocess.Popen(
            [
                "x64sc", "-default", "-silent", "-sounddev", "dummy", "-warp",
                "-binarymonitor", "-binarymonitoraddress", f"127.0.0.1:{self.port}",
                "-initbreak", "ready", "+confirmonexit", "+saveres",
                # Start iconified so a test run does not put a VICE window on the
                # desktop and steal focus. The window still exists and can be
                # restored; Binary Monitor traffic, screen reads and keyboard input
                # are unaffected, which is why this is preferred over -headless here.
                "-minimized",
            ],
            stdout=stdout,
            stderr=stderr,
        )
        for _ in range(50):
            try:
                self.sock = socket.create_connection(("127.0.0.1", self.port), timeout=0.25)
                self.sock.settimeout(4.0)
                break
            except OSError:
                time.sleep(0.2)
        if self.sock is None:
            raise GateError("VICE binary monitor did not accept connections")
        self._drain_initial_events()
        self.reg_ids = self.available_registers()
        return self

    def __exit__(self, exc_type, exc, tb) -> None:
        try:
            if self.sock is not None:
                try:
                    self.command(0xAA)
                except Exception:
                    pass
                self.sock.close()
        finally:
            if self.proc is not None:
                self.proc.terminate()
                try:
                    self.proc.wait(timeout=3)
                except subprocess.TimeoutExpired:
                    self.proc.kill()

    def _recv_exact(self, length: int) -> bytes:
        if self.sock is None:
            raise HarnessBug("VICE socket not open")
        data = b""
        while len(data) < length:
            chunk = self.sock.recv(length - len(data))
            if not chunk:
                raise GateError("VICE binary monitor socket closed")
            data += chunk
        return data

    def _recv_response(self) -> tuple[int, int, int, bytes]:
        header = self._recv_exact(12)
        if header[0] != 0x02 or header[1] != 0x02:
            raise GateError(f"VICE binary monitor bad header: {header.hex()}")
        length = struct.unpack_from("<I", header, 2)[0]
        body = self._recv_exact(length)
        return header[6], header[7], struct.unpack_from("<I", header, 8)[0], body

    def _drain_initial_events(self) -> None:
        if self.sock is None:
            return
        old_timeout = self.sock.gettimeout()
        self.sock.settimeout(0.15)
        try:
            while True:
                self._recv_response()
        except Exception:
            pass
        self.sock.settimeout(old_timeout)

    def command(self, command: int, body: bytes = b"", response_type: int | None = None) -> bytes:
        if self.sock is None:
            raise HarnessBug("VICE socket not open")
        request_id = self.request_id
        self.request_id += 1
        expected = command if response_type is None else response_type
        packet = (
            bytes([0x02, 0x02]) +
            struct.pack("<I", len(body)) +
            struct.pack("<I", request_id) +
            bytes([command]) +
            body
        )
        self.sock.sendall(packet)
        while True:
            typ, error, reply_id, payload = self._recv_response()
            if reply_id != request_id or typ != expected:
                continue
            if error:
                raise GateError(f"VICE binary monitor command ${command:02X} error ${error:02X}")
            return payload

    def available_registers(self) -> dict[str, int]:
        body = self.command(0x83, b"\x00")
        count = struct.unpack_from("<H", body, 0)[0]
        offset = 2
        regs: dict[str, int] = {}
        for _ in range(count):
            size = body[offset]
            item = body[offset + 1:offset + 1 + size]
            offset += 1 + size
            reg_id = item[0]
            name_len = item[2]
            name = item[3:3 + name_len].decode("ascii", "replace")
            regs[name] = reg_id
        for name in ("PC", "A", "X", "Y", "SP", "FL"):
            if name not in regs:
                raise GateError(f"VICE binary monitor did not expose register {name}")
        return regs

    def registers(self) -> dict[str, int]:
        body = self.command(0x31, b"\x00")
        count = struct.unpack_from("<H", body, 0)[0]
        offset = 2
        values_by_id: dict[int, int] = {}
        for _ in range(count):
            size = body[offset]
            item = body[offset + 1:offset + 1 + size]
            offset += 1 + size
            values_by_id[item[0]] = struct.unpack_from("<H", item, 1)[0]
        return {name: values_by_id[reg_id] for name, reg_id in self.reg_ids.items()
                if reg_id in values_by_id}

    def set_register(self, name: str, value: int) -> None:
        reg_id = self.reg_ids[name]
        body = bytes([0]) + struct.pack("<H", 1) + bytes([3, reg_id]) + struct.pack("<H", value)
        self.command(0x32, body, response_type=0x31)

    def set_registers(self, values: dict[str, int]) -> None:
        items = bytearray()
        for name, value in values.items():
            reg_id = self.reg_ids[name]
            items.extend(bytes([3, reg_id]) + struct.pack("<H", value & 0xFFFF))
        body = bytes([0]) + struct.pack("<H", len(values)) + bytes(items)
        self.command(0x32, body, response_type=0x31)

    def memory_image(self, chunk_size: int = 0x1000) -> bytearray:
        image = bytearray(0x10000)
        for start in range(0, 0x10000, chunk_size):
            end = min(start + chunk_size - 1, 0xFFFF)
            image[start:end + 1] = self.read_memory(start, end)
        return image

    def read_memory(self, start: int, end: int) -> bytes:
        body = self.command(0x01, bytes([0]) + struct.pack("<HHBH", start, end, 0, 0))
        data = body[2:]
        if len(data) != end - start + 1:
            raise GateError(
                f"VICE memory chunk ${start:04X}-${end:04X} length {len(data)}")
        return data

    def write_memory(self, start: int, data: bytes) -> None:
        if not data:
            return
        end = start + len(data) - 1
        body = bytes([0]) + struct.pack("<HHBH", start, end, 0, 0) + data
        self.command(0x02, body)

    def write_memory_image(self, image: bytes, chunk_size: int = 0x1000) -> None:
        for start in range(0, 0x10000, chunk_size):
            self.write_memory(start, image[start:start + chunk_size])

    def advance_one(self, step_over: bool = False) -> None:
        self.command(0x71, bytes([1 if step_over else 0]) + struct.pack("<H", 1))


class DualOracles:
    def __init__(self, driver: BaseDriver, fixture: MatrixFixture,
                 entry: DebugState, cell_dir: Path) -> None:
        self.driver = driver
        self.fixture = fixture
        self.cell_dir = cell_dir
        mem = driver.read_memory_image()
        if fixture.memory_mode in TRAVERSAL_MODES:
            # Seed the captured ROM FIRST so the oracle can follow the CPU into
            # BASIC, then lay the fixture on top. A traversal fixture may own
            # bytes inside a ROM window ($E000 for the RAM-under-ROM leg), and
            # the fixture must win there: the oracle image is flat, so each
            # address has to hold whatever the CPU actually fetches from it, and
            # the two regions are executed under different $01 settings that
            # never overlap.
            apply_captured_rom_heads(mem, cell_dir)
        # The sentinel and progress cells are counters the fixture program keeps
        # writing, so their live values move on after the fixture is installed:
        # the program can run many loop passes before the entry breakpoint stops
        # it. Their install-time bytes are in fixture.chunks, and laying those
        # over the captured image would reset the oracle's copy to zero while
        # the machine holds something else. The next INC then sets N on the
        # machine and not in the oracle, and the cell fails with a register
        # mismatch that is entirely the oracle's error. Keep the captured values
        # wherever they can actually be read back.
        live_counters = {
            address: mem[address]
            for address in (fixture.sentinel, fixture.progress)
            if live_readback_trustworthy(fixture, address)
        }
        for address, data in fixture.chunks:
            mem[address:address + len(data)] = data
        for address, value in live_counters.items():
            mem[address] = value
        mem[fixture.bootstrap_addr:fixture.bootstrap_addr + len(fixture.bootstrap)] = fixture.bootstrap
        if fixture.memory_mode == "rom":
            apply_captured_rom_heads(mem, cell_dir)
        apply_fixture_entry_side_effects(mem, fixture)
        driver.event("oracle_live_counters",
                     values={f"{a:04X}": f"{v:02X}" for a, v in live_counters.items()})
        self.cpu = ORC.CPU6502(bytearray(mem))
        self.cpu.set_state(entry.ac, entry.xr, entry.yr, entry.sp, entry.pc, entry.sr)
        self.vice: ViceBinaryMonitor | None = None
        self.vice_enabled = False
        self.trace: list[dict[str, Any]] = []
        self.vice_warning: str | None = None
        vice_path = self._vice_path()
        if vice_path is not None:
            port = vice_port(1)
            self.vice = ViceBinaryMonitor(port, cell_dir / "vice-oracle")
            try:
                self.vice.__enter__()
                if fixture.memory_mode != "rom":
                    self.vice.write_memory_image(bytes(mem))
                else:
                    # Keep VICE's configured BASIC/KERNAL ROM as the independent
                    # ROM oracle, but synchronize RAM/zero-page/stack with the
                    # U64 stop state so full CPU and active-stack checks are
                    # meaningful from the same entry context.
                    self.vice.write_memory(0x0000, bytes(mem[0x0000:0xA000]))
                    self.vice.write_memory(0xC000, bytes(mem[0xC000:0xE000]))
                self.vice.set_registers({
                    "PC": entry.pc,
                    "A": entry.ac,
                    "X": entry.xr,
                    "Y": entry.yr,
                    "SP": entry.sp,
                    "FL": entry.sr,
                })
                self.vice_enabled = True
                driver.event("vice_oracle_enabled", path=str(vice_path), port=port)
            except Exception as exc:  # noqa: BLE001 - VICE is installed, so this is actionable
                try:
                    if self.vice is not None:
                        self.vice.__exit__(None, None, None)
                except Exception:
                    pass
                self.vice = None
                raise GateError(f"VICE oracle setup failed with {vice_path}: {exc}") from exc
        else:
            self.vice_warning = "x64sc not found on PATH"
            driver.event("vice_oracle_warning", error=self.vice_warning)

    def close(self) -> None:
        if self.vice is not None:
            self.vice.__exit__(None, None, None)
            self.vice = None
        summary = {
            "vice_enabled": self.vice_enabled,
            "vice_warning": self.vice_warning,
            "checks": len(self.trace),
        }
        (self.cell_dir / "dual-oracle-summary.json").write_text(
            json.dumps(summary, indent=2, sort_keys=True) + "\n", encoding="utf-8")
        (self.cell_dir / "dual-oracle-trace.json").write_text(
            json.dumps(self.trace, indent=2) + "\n", encoding="utf-8")

    @staticmethod
    def _vice_path() -> Path | None:
        for directory in os.environ.get("PATH", "").split(os.pathsep):
            candidate = Path(directory) / "x64sc"
            if candidate.exists():
                return candidate
        return None

    def _record(self, label: str, result: ORC.StepResult, vice_mode: str) -> None:
        self.trace.append({
            "label": label,
            "pc_before": f"{result.pc_before:04X}",
            "opcode": f"{result.opcode:02X}",
            "mnemonic": result.mnemonic,
            "pc_after": f"{self.cpu.pc:04X}",
            "a": f"{self.cpu.a:02X}",
            "x": f"{self.cpu.x:02X}",
            "y": f"{self.cpu.y:02X}",
            "sp": f"{self.cpu.sp:02X}",
            "p": f"{self.cpu.p & 0xFF:02X}",
            "vice_mode": vice_mode,
        })

    def advance_one(self, label: str) -> ORC.StepResult:
        result = self.cpu.step()
        if self.vice is not None:
            self.vice.advance_one()
        self._record(label, result, "step")
        return result

    def advance_step_over(self, label: str) -> ORC.StepResult:
        result = self.cpu.step()
        if result.mnemonic == "JSR":
            target_sp = self.cpu.sp
            max_steps = 2048
            for _ in range(max_steps):
                if self.cpu.pc == (result.pc_before + 3) & 0xFFFF and self.cpu.sp == ((target_sp + 2) & 0xFF):
                    break
                self.cpu.step()
            else:
                raise GateError(f"{label}: internal oracle step-over did not reach caller")
        if self.vice is not None:
            self.vice.advance_one(step_over=True)
        self._record(label, result, "step_over")
        return result

    def advance_until_pc(self, target: int, label: str, max_steps: int = 4096) -> int:
        count = 0
        last: ORC.StepResult | None = None
        while self.cpu.pc != target and count < max_steps:
            last = self.cpu.step()
            count += 1
        if self.cpu.pc != target:
            raise GateError(f"{label}: internal oracle did not reach ${target:04X}")
        if self.vice is not None:
            for _ in range(count):
                self.vice.advance_one()
        if last is not None:
            self._record(label, last, f"run_{count}")
        return count

    def _resample_footer_evidence(self, state: DebugState, label: str) -> None:
        """Evidence only, on a mismatch: re-read the debug footer and the
        firmware's own STORE_* capture several times without issuing any
        debugger command. If the later samples report the expected values, the
        failing sample read the footer while it was being redrawn; if they keep
        reporting the wrong values, the debugger really is holding that state.
        """
        samples = []
        for _ in range(6):
            entry: dict[str, Any] = {}
            try:
                entry["footer"] = self.driver.read_debug_state().raw
            except Exception as exc:  # noqa: BLE001 - diagnostic capture only
                entry["footer_error"] = str(exc)
            try:
                entry["store"] = _authoritative_debug_state(self.driver.rest).raw
            except Exception as exc:  # noqa: BLE001 - diagnostic capture only
                entry["store_error"] = str(exc)
            samples.append(entry)
            time.sleep(0.1)
        self.driver.event("footer_resample_after_mismatch", label=label,
                          expected_pc=f"{self.cpu.pc:04X}",
                          expected_sp=f"{self.cpu.sp:02X}",
                          observed=state.raw, samples=samples)

    def compare_state_and_stack(self, state: DebugState, label: str) -> None:
        try:
            assert_state_matches_cpu(state, self.cpu, f"{label} internal oracle")
        except GateError:
            self._resample_footer_evidence(state, label)
            raise
        stack = self.driver.read_bytes(0x0100, 256)
        expected_stack = bytes(self.cpu.mem[0x0100:0x0200])
        active_start = state.sp + 1
        if active_start <= 0xFF:
            if stack[active_start:] != expected_stack[active_start:]:
                raise GateError(
                    f"{label}: U64 active stack ${0x0100 + active_start:04X}-$01FF "
                    "differs from internal oracle")
        if self.vice is not None:
            regs = self.vice.registers()
            got = {
                "pc": regs["PC"],
                "a": regs["A"] & 0xFF,
                "x": regs["X"] & 0xFF,
                "y": regs["Y"] & 0xFF,
                "sp": regs["SP"] & 0xFF,
                "fl": regs["FL"] & 0xFF,
            }
            expected = {
                "pc": state.pc,
                "a": state.ac,
                "x": state.xr,
                "y": state.yr,
                "sp": state.sp,
                "fl": state.sr,
            }
            if (got["pc"], got["a"], got["x"], got["y"], got["sp"]) != (
                    expected["pc"], expected["a"], expected["x"], expected["y"], expected["sp"]):
                raise GateError(f"{label}: VICE register mismatch got={got} expected={expected}")
            if not sr_mask(got["fl"], expected["fl"]):
                raise GateError(f"{label}: VICE SR mismatch got={got} expected={expected}")
            vice_stack = self.vice.read_memory(0x0100, 0x01FF)
            if active_start <= 0xFF and vice_stack[active_start:] != stack[active_start:]:
                raise GateError(
                    f"{label}: VICE active stack ${0x0100 + active_start:04X}-$01FF "
                    "differs from U64 stack")
        stack_start = None if active_start > 0xFF else f"{0x0100 + active_start:04X}"
        self.driver.event(
            "dual_oracle_validated",
            label=label,
            vice_enabled=self.vice_enabled,
            pc=f"{state.pc:04X}",
            sp=f"{state.sp:02X}",
            active_stack_start=stack_start)


def run_rom_opcode_trace_dual(driver: BaseDriver, row: dict[str, Any], cell_dir: Path,
                              oracles: DualOracles, minimum_opcodes: int = 100) -> tuple[DebugState, int]:
    (cell_dir / "rom-trace-memory-image.bin").write_bytes(bytes(oracles.cpu.mem))
    trace = []
    jsr_count = 0
    call_depth = 0
    max_call_depth = 0
    kernal_to_basic = False
    basic_to_kernal = False
    state = driver.read_debug_state()
    max_steps = max(300, minimum_opcodes * 3)
    for index in range(1, max_steps + 1):
        before_pc = oracles.cpu.pc
        before_region = rom_region(before_pc)
        if before_region == "other":
            raise GateError(
                f"ROM trace left BASIC/KERNAL before step {index}: PC ${before_pc:04X}")
        result = oracles.advance_one(f"ROM opcode trace {index}")
        if result.mnemonic == "JSR":
            jsr_count += 1
            call_depth += 1
            max_call_depth = max(max_call_depth, call_depth)
        elif result.mnemonic == "RTS" and call_depth > 0:
            call_depth -= 1
        after_region = rom_region(oracles.cpu.pc)
        kernal_to_basic |= before_region == "kernal" and after_region == "basic"
        basic_to_kernal |= before_region == "basic" and after_region == "kernal"
        driver.step_into()
        state = driver.wait_pc(oracles.cpu.pc, f"ROM opcode trace {index}", timeout=12.0)
        oracles.compare_state_and_stack(state, f"ROM opcode trace {index}")
        trace.append({
            "index": index,
            "pc_before": f"{before_pc:04X}",
            "opcode": f"{result.opcode:02X}",
            "mnemonic": result.mnemonic,
            "pc_after": f"{oracles.cpu.pc:04X}",
            "region_before": before_region,
            "region_after": after_region,
            "sp": f"{oracles.cpu.sp:02X}",
            "call_depth": call_depth,
            "max_call_depth": max_call_depth,
            "vice_enabled": oracles.vice_enabled,
        })
        if (index >= minimum_opcodes and max_call_depth >= 2
                and kernal_to_basic and basic_to_kernal):
            summary = {
                "opcode_count": index,
                "jsr_count": jsr_count,
                "max_call_depth": max_call_depth,
                "kernal_to_basic": kernal_to_basic,
                "basic_to_kernal": basic_to_kernal,
                "final_pc": f"{oracles.cpu.pc:04X}",
                "vice_enabled": oracles.vice_enabled,
            }
            (cell_dir / "rom-opcode-trace.json").write_text(
                json.dumps({"summary": summary, "trace": trace}, indent=2) + "\n",
                encoding="utf-8")
            row["rom_trace"] = summary
            driver.event("rom_opcode_trace_pass", **summary)
            return state, index

    summary = {
        "opcode_count": len(trace),
        "jsr_count": jsr_count,
        "max_call_depth": max_call_depth,
        "kernal_to_basic": kernal_to_basic,
        "basic_to_kernal": basic_to_kernal,
        "final_pc": f"{oracles.cpu.pc:04X}",
        "vice_enabled": oracles.vice_enabled,
    }
    (cell_dir / "rom-opcode-trace.json").write_text(
        json.dumps({"summary": summary, "trace": trace}, indent=2) + "\n",
        encoding="utf-8")
    raise GateError(
        "ROM trace did not satisfy minimum coverage: "
        f"{summary}, required opcodes>=100, max_call_depth>=2, "
        "KERNAL->BASIC and BASIC->KERNAL transitions")


def assert_no_breakpoints_remain(driver: BaseDriver, row: dict[str, Any],
                                 cell_dir: Path, label: str) -> None:
    """Every slot in the breakpoint table must be EMPTY when a cell finishes.

    A leaked breakpoint is not cosmetic and not contained: it survives a machine
    reset, so the next cell - or the next suite entirely - traps on this cell's
    stale BRK instead of its own target. The failure then surfaces somewhere
    unrelated, which is what makes such a leak look like a sporadic debugger
    fault. Asserting it here names the cell that leaked.
    """
    try:
        lines = driver.breakpoint_slot_lines()
    except Exception as exc:  # noqa: BLE001
        driver.event("breakpoint_slot_readback_failed", stage=label, error=str(exc))
        raise GateError(
            f"{label}: could not read the breakpoint table to prove no slot "
            f"was left armed: {exc}") from exc
    leaked = [line for line in lines if "EMPTY" not in line.upper()]
    (cell_dir / "breakpoint-slots-final.txt").write_text(
        "\n".join(lines) + "\n", encoding="utf-8")
    if leaked:
        driver.event("breakpoint_slots_leaked", stage=label, slots=leaked)
        raise GateError(
            f"{label}: {len(leaked)} breakpoint slot(s) still armed after the "
            f"cell; this cell did not remove what it set: {leaked}")
    driver.event("breakpoint_slots_clean", stage=label, slots=len(lines))
    row["breakpoint_slot_hygiene_validated"] = True


def run_straight_call_sequence(driver: BaseDriver, row: dict[str, Any],
                               cell_dir: Path, fixture: MatrixFixture) -> int:
    """Step Over a long run of consecutive JSRs at one stack level.

    The nesting chain drives the stack one level deeper per step, so it never
    repeats a Step Over from the same stack state. This block calls the same
    helper `straight_calls` times in a row, which means every Step Over here
    arms a breakpoint, parks, resumes and disarms from an identical starting
    state, and the CPU comes back to the same stack pointer each time. A
    breakpoint slot that is not released, or a park/resume that drifts the
    stack, shows up as a divergence at some position in this run and nowhere
    else in the matrix.

    Expected PC, SP and A are exact at every position, so this needs no
    simulator oracle: after the k-th Step Over the CPU must be at
    straight_block + 3k with the stack pointer it had on entering the block,
    and A must hold the helper's $42.
    """
    if not fixture.straight_calls:
        return 0

    # Reached with Continue To Cursor rather than a fresh debug entry: the cell
    # is already parked in a live debug session here, and re-entering would have
    # to re-arm the entry breakpoint and re-open the UI, which differs per
    # interface. The block sits in the program's loop, so the cursor is hit on
    # the next pass.
    #
    # The cursor is put on the JSR, and the block is then entered with Step
    # Into, because step_out() unwinds the session's tracked return-target stack
    # (peek_return_target) rather than reading the live 6502 stack. Only a frame
    # the debugger stepped into is on that stack, so landing inside the block
    # directly would leave the closing Step Out with no frame of its own to
    # return from.
    driver.continue_to_cursor(fixture.straight_entry)
    entry = driver.wait_pc(fixture.straight_entry,
                           "Straight-call Continue To Cursor", timeout=15.0)
    outer_sp = entry.sp
    state = step_and_wait_pc(driver, driver.step_into, fixture.straight_block,
                             "Straight-call Step Into", fixture.straight_entry)
    assert_state_pc_sp(state, fixture.straight_block, (outer_sp - 2) & 0xFF,
                       "Straight-call Step Into")
    block_sp = state.sp

    evidence: list[dict[str, Any]] = []
    for index in range(1, fixture.straight_calls + 1):
        label = f"Straight-call Step Over {index}/{fixture.straight_calls}"
        expected_pc = fixture.straight_block + 3 * index
        state = step_and_wait_pc(driver, driver.step_over, expected_pc, label,
                                 state.pc)
        assert_state_pc_sp(state, expected_pc, block_sp, label)
        if state.ac != 0x42:
            raise GateError(
                f"{label}: helper body did not run through its RTS; "
                f"expected AC $42, got ${state.ac:02X}")
        evidence.append({"index": index, "pc": f"{state.pc:04X}",
                         "sp": f"{state.sp:02X}", "ac": f"{state.ac:02X}"})

    # The run ends on the block's RTS, so Step Out must unwind exactly one
    # frame and restore the stack pointer the caller had.
    out = step_and_wait_pc(driver, driver.step_out, fixture.straight_return,
                           "Straight-call Step Out", state.pc)
    assert_state_pc_sp(out, fixture.straight_return, outer_sp,
                       "Straight-call Step Out")

    (cell_dir / "straight-call-evidence.json").write_text(
        json.dumps(evidence, indent=2) + "\n", encoding="utf-8")
    driver.event("straight_call_sequence", calls=fixture.straight_calls,
                 block=f"{fixture.straight_block:04X}",
                 sp=f"{block_sp:02X}")
    row["straight_call_depth"] = fixture.straight_calls
    return fixture.straight_calls


def run_step_trace_dual(driver: BaseDriver, row: dict[str, Any], cell_dir: Path,
                        oracles: DualOracles,
                        minimum_opcodes: int = 100) -> int:
    """Dual-oracle Step Into trace for the RAM / RAM-under-ROM fixtures.

    Unlike the ROM trace this follows the synthetic fixture program (chain
    unwind, then the sentinel/progress loop), so there is no region or call
    depth requirement: it proves that `minimum_opcodes` consecutive live Step
    Intos agree with the 6510 oracle on registers and stack, through whatever
    mix of linear ops, JSR/RTS, and JMPs the fixture path contains.
    """
    trace = []
    for index in range(1, minimum_opcodes + 1):
        before_pc = oracles.cpu.pc
        result = oracles.advance_one(f"Step trace {index}")
        driver.step_into()
        state = driver.wait_pc(oracles.cpu.pc, f"Step trace {index}", timeout=12.0)
        oracles.compare_state_and_stack(state, f"Step trace {index}")
        trace.append({
            "index": index,
            "pc_before": f"{before_pc:04X}",
            "opcode": f"{result.opcode:02X}",
            "mnemonic": result.mnemonic,
            "pc_after": f"{oracles.cpu.pc:04X}",
            "sp": f"{oracles.cpu.sp:02X}",
            "vice_enabled": oracles.vice_enabled,
        })
    summary = {
        "opcode_count": len(trace),
        "final_pc": f"{oracles.cpu.pc:04X}",
        "vice_enabled": oracles.vice_enabled,
    }
    (cell_dir / "step-opcode-trace.json").write_text(
        json.dumps({"summary": summary, "trace": trace}, indent=2) + "\n",
        encoding="utf-8")
    row["step_trace"] = summary
    driver.event("step_opcode_trace_pass", **summary)
    return len(trace)


def run_vice_oracle_check(artifact_dir: Path, port: int | None = None,
                          minimum_opcodes: int = 100) -> dict[str, Any]:
    if port is None:
        port = vice_port(0)
    out_dir = artifact_dir / "preflight" / "vice-oracle"
    transcript = []
    with ViceBinaryMonitor(port, out_dir) as vice:
        mem = vice.memory_image()
        (out_dir / "vice-memory-image.bin").write_bytes(bytes(mem))
        if mem[0xE000:0xE005] != bytes([0x85, 0x56, 0x20, 0x0F, 0xBC]):
            raise GateError(
                "VICE KERNAL does not expose canonical $E000 path: "
                f"{mem[0xE000:0xE005].hex().upper()}")
        vice.set_register("PC", 0xE000)
        regs = vice.registers()
        cpu = ORC.CPU6502(bytearray(mem))
        cpu.set_state(
            regs["A"] & 0xFF,
            regs["X"] & 0xFF,
            regs["Y"] & 0xFF,
            regs["SP"] & 0xFF,
            regs["PC"],
            regs["FL"] & 0xFF,
        )
        jsr_count = 0
        call_depth = 0
        max_call_depth = 0
        kernal_to_basic = False
        basic_to_kernal = False
        for index in range(1, minimum_opcodes + 1):
            before_pc = cpu.pc
            before_region = rom_region(cpu.pc)
            result = cpu.step()
            if result.mnemonic == "JSR":
                jsr_count += 1
                call_depth += 1
                max_call_depth = max(max_call_depth, call_depth)
            elif result.mnemonic == "RTS" and call_depth > 0:
                call_depth -= 1
            after_region = rom_region(cpu.pc)
            kernal_to_basic |= before_region == "kernal" and after_region == "basic"
            basic_to_kernal |= before_region == "basic" and after_region == "kernal"
            vice.advance_one()
            regs = vice.registers()
            got = {
                "pc": regs["PC"],
                "a": regs["A"] & 0xFF,
                "x": regs["X"] & 0xFF,
                "y": regs["Y"] & 0xFF,
                "sp": regs["SP"] & 0xFF,
                "fl": regs["FL"] & 0xFF,
            }
            expected = {
                "pc": cpu.pc,
                "a": cpu.a,
                "x": cpu.x,
                "y": cpu.y,
                "sp": cpu.sp,
                "fl": cpu.p & 0xFF,
            }
            if (got["pc"], got["a"], got["x"], got["y"], got["sp"]) != (
                    expected["pc"], expected["a"], expected["x"], expected["y"], expected["sp"]):
                raise GateError(
                    f"VICE/oracle mismatch at step {index}: got={got} expected={expected}")
            if not sr_mask(got["fl"], expected["fl"]):
                raise GateError(
                    f"VICE/oracle flag mismatch at step {index}: got={got} expected={expected}")
            transcript.append({
                "index": index,
                "pc_before": f"{before_pc:04X}",
                "opcode": f"{result.opcode:02X}",
                "mnemonic": result.mnemonic,
                "pc_after": f"{cpu.pc:04X}",
                "region_before": before_region,
                "region_after": after_region,
                "max_call_depth": max_call_depth,
            })
        summary = {
            "status": "PASS",
            "opcode_count": minimum_opcodes,
            "jsr_count": jsr_count,
            "max_call_depth": max_call_depth,
            "kernal_to_basic": kernal_to_basic,
            "basic_to_kernal": basic_to_kernal,
            "docs": "https://vice-emu.sourceforge.io/vice_13.html",
        }
        if max_call_depth < 2 or not kernal_to_basic or not basic_to_kernal:
            raise GateError(f"VICE oracle path coverage insufficient: {summary}")
        (out_dir / "vice-oracle-trace.json").write_text(
            json.dumps({"summary": summary, "trace": transcript}, indent=2) + "\n",
            encoding="utf-8")
        return summary


def run_cell(args: argparse.Namespace, row: dict[str, Any], ledger: Ledger) -> None:
    cid = row["cell_id"]
    cell_dir = Path(row["artifact_dir"])
    cell_dir.mkdir(parents=True, exist_ok=True)
    log_path = cell_dir / "cell.log"
    with (cell_dir / "trace.jsonl").open("a", encoding="utf-8", buffering=1) as trace, \
            log_path.open("a", encoding="utf-8", buffering=1) as log:
        def log_line(message: str) -> None:
            line = f"{now_stamp()} {message}"
            print(line, flush=True)
            log.write(line + "\n")

        driver = make_driver(args, row, cell_dir, trace)
        oracles: DualOracles | None = None
        try:
            seed = 0x154100 + row["repetition"] + MEMORY_MODES.index(row["memory_mode"]) * 100
            row["program_seed"] = seed
            fixture = build_fixture(row["memory_mode"], args.required_step_into_depth,
                                    args.straight_calls)
            row["fixture"] = fixture.to_json()
            log_line(f"{cid}: reset baseline")
            driver.reset_baseline()   # per-cell setup reset (not recovery)
            SETUP_RESETS.count("setup_reset", f"{cid}: cell setup")
            log_line(f"{cid}: installing fixture seed={seed}")
            driver.install_fixture(fixture)
            ledger.save()
            log_line(f"{cid}: open monitor and enter debug")
            driver.open_monitor()
            entry = driver.enter_debug_at(fixture.entry)
            row["start_pc"] = f"{entry.pc:04X}"
            row["footer_validated"] = not driver.contextless_entry()
            assert_state_pc_sp(entry, fixture.entry, None, "entry")
            sp_entry: int | None = None if driver.contextless_entry() else entry.sp
            oracles = DualOracles(driver, fixture, entry, cell_dir)
            oracles.compare_state_and_stack(entry, "entry")
            row["vice_oracle_validated"] = oracles.vice_enabled
            row["oracle_validated"] = True
            ledger.save()

            log_line(f"{cid}: Step Over")
            over = step_and_wait_pc(driver, driver.step_over, fixture.step_over_return,
                                    "Step Over", fixture.entry)
            oracles.advance_step_over("Step Over")
            oracles.compare_state_and_stack(over, "Step Over")
            assert_state_pc_sp(over, fixture.step_over_return, sp_entry, "Step Over")
            if sp_entry is None:
                sp_entry = over.sp
                driver.event("stack_baseline_from_step_over",
                             sp=f"{sp_entry:02X}",
                             reason="contextless Debug entry has no footer SP")
            row["footer_validated"] = True
            mark_op(row, "step_over", "PASS")
            if driver.active_debug_readback_allowed():
                try:
                    if driver.read_bytes(fixture.sentinel, 1)[0] == 0x42:
                        row["memory_writes_validated"] = True
                except Exception as exc:
                    driver.event("memory_write_validation_deferred",
                                 stage="step_over", error=str(exc))
            else:
                driver.event(
                    "memory_write_validation_deferred",
                    stage="step_over",
                    reason="active-Debug REST readmem is not a proven live target oracle")
            ledger.save()

            if row["memory_mode"] in TRAVERSAL_MODES:
                # The generic Step Over above already performed traversal[0].
                log_line(f"{cid}: boundary walk "
                         f"({len(fixture.traversal)} steps across memory regions)")
                state = over
                walk_evidence = []
                into_count = 0
                out_count = 0
                for index, (key, expect_pc, region) in enumerate(fixture.traversal):
                    if index == 0:
                        walk_evidence.append({
                            "index": 0, "key": key, "pc": f"{state.pc:04X}",
                            "region": region, "note": "generic Step Over"})
                        continue
                    label = (f"boundary step {index + 1}/{len(fixture.traversal)} "
                             f"{key} -> ${expect_pc:04X} ({region})")
                    log_line(f"{cid}: {label}")
                    before = state
                    if key == "D":
                        action = driver.step_over
                    elif key == "T":
                        action = driver.step_into
                    elif key == "U":
                        action = driver.step_out
                    else:
                        raise HarnessBug(f"unknown traversal key {key!r}")
                    state = step_and_wait_pc(driver, action, expect_pc, label, before.pc)
                    if key == "D":
                        oracles.advance_step_over(label)
                    elif key == "T":
                        oracles.advance_one(label)
                    else:
                        oracles.advance_until_pc(expect_pc, label)
                    oracles.compare_state_and_stack(state, label)
                    assert_state_pc_sp(state, expect_pc, None, label)
                    if key == "T":
                        into_count += 1
                    elif key == "U":
                        out_count += 1
                    walk_evidence.append({
                        "index": index, "key": key, "pc": f"{state.pc:04X}",
                        "sp": f"{state.sp:02X}", "region": region})
                    row["opcode_count"] = row.get("opcode_count", 0) + 1
                    ledger.save()
                if into_count == 0 or out_count == 0:
                    raise HarnessBug(
                        f"{row['memory_mode']} traversal must cross a boundary in "
                        f"both directions (got {into_count} Step Into, {out_count} Step Out)")
                # The routine reached across the boundary writes $42 to the
                # sentinel, so a correct crossing is observable in memory too.
                if driver.active_debug_readback_allowed():
                    try:
                        if driver.read_bytes(fixture.sentinel, 1)[0] == 0x42:
                            row["memory_writes_validated"] = True
                    except Exception as exc:  # noqa: BLE001
                        driver.event("memory_write_validation_deferred",
                                     stage="boundary_walk", error=str(exc))
                (cell_dir / "boundary-walk-evidence.json").write_text(
                    json.dumps(walk_evidence, indent=2) + "\n", encoding="utf-8")
                row["step_into_depth"] = into_count
                row["boundary_crossings"] = into_count + out_count
                mark_op(row, "step_into", "PASS")
                mark_op(row, "step_out", "PASS")
                row["stack_validated"] = True
                row["oracle_validated"] = True
                ledger.save()
            elif row["memory_mode"] == "rom":
                log_line(f"{cid}: Step Into along live ROM path to real JSR")
                state = over
                jsr_result: ORC.StepResult | None = None
                step_trace = []
                for index in range(1, 32):
                    before_sp = state.sp
                    result = oracles.advance_one(f"Step Into live ROM path {index}")
                    driver.step_into()
                    state = driver.wait_pc(oracles.cpu.pc,
                                           f"Step Into live ROM path {index}",
                                           timeout=12.0)
                    oracles.compare_state_and_stack(state,
                                                    f"Step Into live ROM path {index}")
                    step_trace.append({
                        "index": index,
                        "pc_before": f"{result.pc_before:04X}",
                        "opcode": f"{result.opcode:02X}",
                        "mnemonic": result.mnemonic,
                        "pc_after": f"{state.pc:04X}",
                        "sp_before": f"{before_sp:02X}",
                        "sp_after": f"{state.sp:02X}",
                    })
                    row["opcode_count"] += 1
                    if result.mnemonic == "JSR":
                        jsr_result = result
                        break
                if jsr_result is None:
                    raise GateError("ROM Step Into path did not reach a real JSR")
                expected_sp = (int(step_trace[-1]["sp_before"], 16) - 2) & 0xFF
                assert_state_pc_sp(state, oracles.cpu.pc, expected_sp,
                                   "Step Into real ROM JSR")
                expected_pushed_return = (jsr_result.pc_before + 2) & 0xFFFF
                actual_return = None
                if driver.active_debug_readback_allowed():
                    try:
                        actual_return = driver.stack_return_at(state.sp)
                    except Exception:
                        actual_return = None
                if actual_return is not None and actual_return != expected_pushed_return:
                    raise GateError(
                        f"ROM Step Into: stack return expected ${expected_pushed_return:04X}, "
                        f"got ${actual_return:04X}")
                row["step_into_depth"] = 1
                row["stack_validated"] = True
                (cell_dir / "step-into-stack-evidence.json").write_text(
                    json.dumps({
                        "steps": step_trace,
                        "jsr": {
                            "level": 1,
                            "sp": f"{state.sp:02X}",
                            "expected_pushed_return": f"{expected_pushed_return:04X}",
                            "observed_return": None if actual_return is None else f"{actual_return:04X}",
                            "note": "real live-ROM JSR; 32-level depth applies to RAM/RAM-under-ROM only",
                        },
                    }, indent=2) + "\n", encoding="utf-8")
                mark_op(row, "step_into", "PASS")
                ledger.save()

                log_line(f"{cid}: Step Out")
                deepest = state
                expected_return_pc = (jsr_result.pc_before + 3) & 0xFFFF
                if not driver.active_debug_readback_allowed():
                    driver.event(
                        "stack_return_readback_deferred",
                        stage="rom_step_out_precondition",
                        sp=f"{deepest.sp:02X}",
                        expected_pushed_return=f"{expected_pushed_return:04X}",
                        reason="active-Debug REST readmem is not a proven live target oracle")
                out = step_and_wait_pc(driver, driver.step_out, expected_return_pc,
                                       "Step Out real ROM JSR", deepest.pc)
                oracles.advance_until_pc(expected_return_pc, "Step Out real ROM JSR")
                oracles.compare_state_and_stack(out, "Step Out real ROM JSR")
                assert_state_pc_sp(out, expected_return_pc, (deepest.sp + 2) & 0xFF,
                                   "Step Out real ROM JSR")
                mark_op(row, "step_out", "PASS")
                ledger.save()

                log_line(f"{cid}: ROM 100-opcode trace from current live ROM path")
                rom_state, rom_steps = run_rom_opcode_trace_dual(
                    driver, row, cell_dir, oracles, minimum_opcodes=100)
                row["opcode_count"] = rom_steps + 3
                row["oracle_validated"] = True
                ledger.save()
            else:
                log_line(f"{cid}: Step Into depth {args.required_step_into_depth}")
                expected_sp = sp_entry
                depth_proven = 0
                return_evidence = []
                expected_sp = (expected_sp - 2) & 0xFF
                state = step_and_wait_pc(driver, driver.step_into, fixture.chain_addrs[0],
                                         "Step Into level 1", fixture.step_over_return)
                oracles.advance_one("Step Into level 1")
                oracles.compare_state_and_stack(state, "Step Into level 1")
                assert_state_pc_sp(state, fixture.chain_addrs[0], expected_sp, "Step Into level 1")
                depth_proven = 1
                for level in range(1, args.required_step_into_depth):
                    caller = fixture.entry + 3 if level == 1 else fixture.chain_addrs[level - 2]
                    expected_pushed_return = (caller + 2) & 0xFFFF
                    if driver.active_debug_readback_allowed():
                        try:
                            actual_return = driver.stack_return_at(state.sp)
                        except Exception:
                            actual_return = None
                    else:
                        actual_return = None
                    return_evidence.append({
                        "level": level,
                        "sp": f"{state.sp:02X}",
                        "expected_pushed_return": f"{expected_pushed_return:04X}",
                        "observed_return": None if actual_return is None else f"{actual_return:04X}",
                    })
                    if actual_return is not None and actual_return != expected_pushed_return:
                        raise GateError(
                            f"Step Into level {level}: stack return expected "
                            f"${expected_pushed_return:04X}, got ${actual_return:04X}")
                    expected_sp = (expected_sp - 2) & 0xFF
                    state = step_and_wait_pc(driver, driver.step_into,
                                             fixture.chain_addrs[level],
                                             f"Step Into level {level + 1}",
                                             fixture.chain_addrs[level - 1])
                    oracles.advance_one(f"Step Into level {level + 1}")
                    oracles.compare_state_and_stack(state, f"Step Into level {level + 1}")
                    assert_state_pc_sp(state, fixture.chain_addrs[level], expected_sp,
                                       f"Step Into level {level + 1}")
                    depth_proven = level + 1
                row["step_into_depth"] = depth_proven
                mark_op(row, "step_into", "PASS")
                row["stack_validated"] = True
                (cell_dir / "step-into-stack-evidence.json").write_text(
                    json.dumps(return_evidence, indent=2) + "\n", encoding="utf-8")
                ledger.save()

                log_line(f"{cid}: Step Out")
                deepest = state
                expected_pushed_return = (fixture.chain_addrs[-2] + 2) & 0xFFFF
                expected_return_pc = fixture.chain_return_addrs[-2]
                if driver.active_debug_readback_allowed():
                    observed_return = driver.stack_return_at(deepest.sp)
                    if observed_return != expected_pushed_return:
                        raise GateError(
                            f"Step Out precondition pushed return expected ${expected_pushed_return:04X}, "
                            f"got ${observed_return:04X}")
                else:
                    driver.event(
                        "stack_return_readback_deferred",
                        stage="step_out_precondition",
                        sp=f"{deepest.sp:02X}",
                        expected_pushed_return=f"{expected_pushed_return:04X}",
                        reason="active-Debug REST readmem is not a proven live target oracle")
                out = step_and_wait_pc(driver, driver.step_out, expected_return_pc,
                                       "Step Out", deepest.pc)
                oracles.advance_until_pc(expected_return_pc, "Step Out")
                oracles.compare_state_and_stack(out, "Step Out")
                assert_state_pc_sp(out, expected_return_pc, (deepest.sp + 2) & 0xFF, "Step Out")
                mark_op(row, "step_out", "PASS")
                ledger.save()

                trace_minimum = (U2_TRACE_OPCODES if args.c64_host
                                 else DEFAULT_TRACE_OPCODES)
                log_line(f"{cid}: {trace_minimum}-opcode dual-oracle Step Into trace")
                trace_steps = run_step_trace_dual(driver, row, cell_dir, oracles,
                                                  minimum_opcodes=trace_minimum)
                row["opcode_count"] = row["step_into_depth"] + trace_steps
                row["oracle_validated"] = True
                ledger.save()

            log_line(f"{cid}: Continue to cursor")
            target = clone_cpu(oracles.cpu)
            target.step()
            driver.continue_to_cursor(target.pc)
            cursor_state = driver.wait_pc(target.pc, "Continue to cursor", timeout=15.0)
            oracles.advance_until_pc(target.pc, "Continue to cursor")
            oracles.compare_state_and_stack(cursor_state, "Continue to cursor")
            row["opcode_count"] += 1
            mark_op(row, "continue_to_cursor", "PASS")
            ledger.save()

            log_line(f"{cid}: Continue to breakpoint")
            target = clone_cpu(oracles.cpu)
            target.step()
            driver.continue_to_breakpoint(target.pc)
            bp_state = driver.wait_pc(target.pc, "Continue to breakpoint", timeout=10.0)
            oracles.advance_until_pc(target.pc, "Continue to breakpoint")
            oracles.compare_state_and_stack(bp_state, "Continue to breakpoint")
            driver.clear_breakpoint(bp_state.pc)
            row["opcode_count"] += 1
            row["breakpoint_hygiene_validated"] = True
            row["brk_patch_hygiene_validated"] = True
            mark_op(row, "continue_to_breakpoint", "PASS")
            ledger.save()

            # Runs before Continue, while the debug UI still owns the machine.
            # Continue hands the CPU back and the Freeze UI closes with it, so a
            # debug entry after that point has no menu screen to read. Nothing
            # below depends on the step oracles tracking the CPU, so re-entering
            # debug at a different address here is free. Modes without a
            # straight-call block (visible ROM and the boundary walks) skip it.
            if fixture.straight_calls:
                log_line(f"{cid}: Straight-call run of "
                         f"{fixture.straight_calls} Step Overs")
                run_straight_call_sequence(driver, row, cell_dir, fixture)
                row["opcode_count"] += fixture.straight_calls
                ledger.save()

            # Checked here, with the debug UI still open and every
            # breakpoint-setting operation of this cell already finished.
            # Continue and Reset arm nothing, so anything armed now was leaked
            # by this cell.
            log_line(f"{cid}: Breakpoint slot hygiene")
            assert_no_breakpoints_remain(driver, row, cell_dir,
                                         "post-workflow breakpoint hygiene")
            ledger.save()

            log_line(f"{cid}: Continue")
            before_progress = None
            if driver.active_debug_readback_allowed():
                before_progress = driver.read_oracle_bytes(fixture.progress, 1)[0]
            driver.continue_run()
            driver.wait_progress_change(fixture.progress, "Continue liveness")
            after_progress = driver.read_oracle_bytes(fixture.progress, 1)[0]
            driver.event("continue_progress", before=before_progress, after=after_progress)
            row["memory_writes_validated"] = True
            row["liveness_validated"] = True
            mark_op(row, "continue", "PASS")
            ledger.save()

            log_line(f"{cid}: Reset")
            driver.reset_from_debug_ui()
            row["rest_liveness_validated"] = driver.rest.alive()
            if row["interface"] == "telnet":
                row["telnet_liveness_validated"] = tcp_probe(args.host, args.port)
            if row["memory_mode"] == "rom":
                row["rom_restore_validated"] = True
                driver.event("rom_restore_not_required",
                             reason="no custom KERNAL/ROM was installed")
                row["rom_restore_validated"] = True
            driver.verify_hygiene()
            row["banking_restore_validated"] = True
            mark_op(row, "reset", "PASS")
            if row["memory_mode"] != "rom":
                row["opcode_count"] += 6
            row["status"] = "PASS"
            ledger.save()
        except BlockedWithEvidence as exc:
            row["status"] = "BLOCKED_WITH_EVIDENCE"
            row["failure"] = {
                "classification": exc.classification,
                "message": str(exc),
                "traceback": traceback.format_exc(),
                "next_action": "Provide a valid observation/fixture path, then rerun this cell.",
            }
            (cell_dir / "failure.txt").write_text(
                f"{exc.classification}: {exc}\n\n{traceback.format_exc()}",
                encoding="utf-8")
            ledger.save()
        except Exception as exc:  # noqa: BLE001
            classification = classify_exception(exc)
            row["status"] = "FAIL"
            row["failure"] = {
                "classification": classification,
                "message": str(exc),
                "traceback": traceback.format_exc(),
                "next_action": "Rerun the minimized cell command from this artifact directory.",
            }
            try:
                screen = ""
                if isinstance(driver, TelnetDebugDriver) and driver.session is not None:
                    screen = driver.session.capture().text()
                elif driver.rest.alive(timeout=1):
                    screen = driver.rest.screen_text()
                else:
                    screen = "<REST not alive; screen capture skipped>"
                (cell_dir / "failure-screen.txt").write_text(screen, encoding="utf-8")
            except Exception:
                pass
            try:
                driver.event(
                    "failure_debug_snapshot",
                    cassette_debug_area=driver.read_bytes(0x033C, 0xC0).hex(),
                    scratch=driver.read_bytes(0xC1F0, 16).hex(),
                    debug_store=driver.read_bytes(0x03F0, 12).hex(),
                    debug_area=driver.read_bytes(0x0360, 0xA0).hex(),
                    soft_vectors=driver.read_bytes(0x0314, 8).hex(),
                    hard_vectors=driver.read_bytes(0xFFFA, 6).hex(),
                    insn_trampoline=driver.read_bytes(0x0340, 16).hex())
            except Exception as snap_exc:
                try:
                    driver.event("failure_debug_snapshot_failed",
                                 error=str(snap_exc))
                except Exception:
                    pass
            (cell_dir / "failure.txt").write_text(
                f"{classification}: {exc}\n\n{traceback.format_exc()}",
                encoding="utf-8")
            ledger.save()
        finally:
            if oracles is not None:
                try:
                    oracles.close()
                except Exception:
                    pass
            try:
                driver.close_monitor()
            except Exception:
                pass
            # A cell that raised part-way through a trace leaves the 6510 parked
            # at a breakpoint. Closing the monitor does not resume it, so without
            # this the jiffy clock stays frozen, the post-cell health check reads
            # that as a hard wedge, and the run aborts for a fault the previous
            # cell already reported. Unpark before the health check runs so each
            # cell's verdict reflects that cell.
            if row.get("status") != "PASS":
                try:
                    driver.rest.reset()
                    driver.wait_rest_ready("post-failure-unpark", timeout=25.0)
                    # REST answering again does not mean the 6510 is running:
                    # the reset has to land the CPU back in the KERNAL before
                    # the jiffy clock advances, and the wedge check reads that
                    # clock. Confirm it is actually ticking, so a slow-but-
                    # healthy recovery is not reported as a hard wedge.
                    running = _wait_c64_running(args.rest_host, timeout=15.0)
                    driver.event("post_failure_unpark",
                                 result="ok" if running else "c64_not_ticking")
                except Exception as unpark_exc:
                    try:
                        driver.event("post_failure_unpark",
                                     result="failed", error=str(unpark_exc))
                    except Exception:
                        pass


def selected_values(value: str, all_values: tuple[str, ...]) -> list[str]:
    if value == "all":
        return list(all_values)
    return [value]


def parse_cell_selection(value: str,
                         default_reps: int = 1) -> list[tuple[str, str, int]]:
    """Parse `--cells` into (memory, interface, repetitions) terms.

    `--memory` and `--ui` can only describe a rectangle: every selected memory
    mode against every selected UI. A caller that wants a chosen set of
    intersections rather than a product - one memory mode on one UI, a second
    on another - cannot express that as a rectangle, and running the rectangle
    that contains it costs the cells it did not ask for. This takes the set
    directly: `ram:telnet,rom:freeze:2` is two terms, the second repeated
    twice.

    Raises ValueError with the offending term named, so a mistyped selection is
    a message before the device is touched rather than a cell that never runs.
    """
    terms: list[tuple[str, str, int]] = []
    for raw in value.split(","):
        term = raw.strip()
        if not term:
            continue
        parts = term.split(":")
        if len(parts) not in (2, 3):
            raise ValueError(
                f"cell term {term!r} is not MEMORY:UI or MEMORY:UI:REPS")
        memory, interface = parts[0].strip(), parts[1].strip()
        if memory not in MEMORY_MODES:
            raise ValueError(
                f"cell term {term!r}: unknown memory mode {memory!r}; "
                f"choose from {', '.join(MEMORY_MODES)}")
        if interface not in INTERFACES:
            raise ValueError(
                f"cell term {term!r}: unknown UI {interface!r}; "
                f"choose from {', '.join(INTERFACES)}")
        reps = default_reps
        if len(parts) == 3:
            try:
                reps = int(parts[2])
            except ValueError:
                raise ValueError(
                    f"cell term {term!r}: repetitions {parts[2]!r} is not a number")
            if reps < 1:
                raise ValueError(f"cell term {term!r}: repetitions must be at least 1")
        terms.append((memory, interface, reps))
    if not terms:
        raise ValueError("--cells was given but names no cell")
    return terms


def create_or_load_ledger(args: argparse.Namespace, artifact_dir: Path) -> Ledger:
    json_path = Path(args.coverage_ledger) if args.coverage_ledger else artifact_dir / "coverage-ledger.json"
    md_path = artifact_dir / "coverage-ledger.md"
    memories = selected_values(args.memory, MEMORY_MODES)
    interfaces = selected_values(args.ui, INTERFACES)
    if args.resume and json_path.exists():
        rows = json.loads(json_path.read_text(encoding="utf-8"))
    elif getattr(args, "cells", ""):
        rows = [
            default_row(memory, interface, rep)
            for memory, interface, reps in parse_cell_selection(args.cells, args.reps)
            for rep in range(1, reps + 1)
        ]
    else:
        rows = [
            default_row(memory, interface, rep)
            for memory in memories
            for interface in interfaces
            for rep in range(1, args.reps + 1)
        ]
    for row in rows:
        row["artifact_dir"] = str(artifact_dir / row["cell_id"])
    ledger = Ledger(rows, json_path, md_path)
    ledger.save()
    return ledger


def progress_line(rows: list[dict[str, Any]], opcode_status: str) -> str:
    total = len(rows)
    passed = sum(1 for r in rows if r["status"] == "PASS")
    failed = sum(1 for r in rows if r["status"] == "FAIL")
    blocked = sum(1 for r in rows if r["status"] == "BLOCKED_WITH_EVIDENCE")
    skipped = sum(1 for r in rows if r["status"] == "SKIPPED_UNSUPPORTED")
    pending = sum(1 for r in rows if r["status"] == "PENDING")
    completed = passed + failed + blocked + skipped
    return (
        f"Matrix progress: completed={completed}/{total} passed={passed} "
        f"failed={failed} blocked={blocked} skipped={skipped} pending={pending} "
        f"inferred=0 invalid=0 1000_opcode={opcode_status}"
    )


def stop_after_cell_failure(args: argparse.Namespace) -> bool:
    return args.fail_fast or args.strict or not args.continue_after_cell_failure


def skipped_opcode_summary(reason: str) -> dict[str, Any]:
    return {
        "opcode_requirement_status": "FAIL",
        "opcode_count": 0,
        "skipped": True,
        "skip_reason": reason,
    }


def mark_pending_blocked(ledger: Ledger, artifact_dir: Path, message: str) -> None:
    evidence_dir = artifact_dir / "device-unavailable-evidence"
    evidence_dir.mkdir(parents=True, exist_ok=True)
    evidence = {
        "time": now_stamp(),
        "message": message,
    }
    (evidence_dir / "evidence.json").write_text(
        json.dumps(evidence, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    for row in ledger.rows:
        if row["status"] not in ("PENDING", "BLOCKED_WITH_EVIDENCE"):
            continue
        cell_dir = Path(row["artifact_dir"])
        cell_dir.mkdir(parents=True, exist_ok=True)
        row["status"] = "BLOCKED_WITH_EVIDENCE"
        for field_name in (
            "step_over", "step_into", "step_out", "continue_to_cursor",
            "continue_to_breakpoint", "continue", "reset",
        ):
            if row.get(field_name) == "PENDING":
                row[field_name] = "BLOCKED_WITH_EVIDENCE"
        row["failure"] = {
            "classification": "BLOCKED_WITH_EVIDENCE",
            "message": message,
            "next_action": "Restore REST/telnet reachability or power-cycle/redeploy hardware, then rerun with --resume.",
            "evidence_dir": str(evidence_dir),
        }
        (cell_dir / "blocked.txt").write_text(message + "\n", encoding="utf-8")
    ledger.save()


def preflight_commands(args: argparse.Namespace,
                       log_dir: Path) -> list[tuple[str, list[str]]]:
    """The preflight command line for each tool, bound to the run's topology.

    On a split session the local-UI tools must send their keystrokes to the
    C64U, because the cartridge's own machine:input is compiled out and answers
    HTTP 501. The Telnet debug suite is the exception: it drives the
    cartridge's own remote session, so it takes --target rather than a machine
    host. A single-host run passes neither flag and each tool keeps its own
    default.
    """
    split_host = ["--c64-host", args.c64_host] if args.c64_host else []
    debug_target = ["--target", debug_suite_target(args)] if args.c64_host else []
    return [
        ("git-status", ["git", "status", "--short"]),
        ("git-log", ["git", "log", "--oneline", "-8"]),
        ("mcm6502-selftest", ["python3", str(MCM_DIR / "mcm6502.py"), "--selftest"]),
        ("quick-telnet-debug", [
            "python3", str(MCM_DIR / "monitor_debug_test.py"),
            "--host", args.host,
            "--rest-host", args.rest_host,
            "--port", str(args.port),
            "--timeout", str(args.timeout),
            "--test", "step-out-target,nested-out",
            "--keep-going",
        ] + debug_target),
        ("freeze-reentry", [
            "python3", str(MCM_DIR / "freeze_reentry_guard.py"),
            args.rest_host, "3",
        ] + split_host),
        ("localui-soak", [
            "python3", str(MCM_DIR / "mcm_localui.py"),
            "soak", args.rest_host,
            "--mode", "disciplined",
            "--cycles", "2",
            "--ui", "both",
            "--log", str(log_dir / "mcm_localui_soak.log"),
        ] + split_host),
    ]


def run_preflight(args: argparse.Namespace, artifact_dir: Path) -> dict[str, Any]:
    log_dir = artifact_dir / "preflight"
    log_dir.mkdir(parents=True, exist_ok=True)
    results: dict[str, Any] = {
        "rest_liveness": make_rest(args).alive(),
        "telnet_liveness": tcp_probe(args.host, args.port),
        "device_identity": SR.device_identity(make_rest(args)),
        "commands": [],
    }
    # A split session's checks run 3-4x an ordinary target's, since every
    # keystroke crosses the cartridge bus (measured throughout this branch's
    # own checks: 20-280s where the equivalent U64 check is 10-70s). 180s cut
    # quick-telnet-debug off after 2 of its checks on a U2+L and crashed the
    # whole gate with an uncaught TimeoutExpired; 600s gives the same command
    # the same margin a single-host run already has.
    command_timeout = 600.0 if args.c64_host else 180.0
    for name, cmd in preflight_commands(args, log_dir):
        rc = run_cmd(cmd, REPO_ROOT, log_dir / f"{name}.log", timeout=command_timeout)
        results[name] = rc
        results["commands"].append({"name": name, "cmd": cmd, "rc": rc})
    results["vice-oracle"] = run_vice_oracle_check(artifact_dir)
    (artifact_dir / "preflight-results.json").write_text(
        json.dumps(results, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    failed = [entry for entry in results["commands"] if entry["rc"] != 0]
    if failed:
        names = ", ".join(f"{entry['name']}={entry['rc']}" for entry in failed)
        raise GateError(f"preflight command failure: {names}")
    if not results["rest_liveness"] or not results["telnet_liveness"]:
        raise GateError(
            f"preflight liveness failure: REST={results['rest_liveness']} "
            f"telnet={results['telnet_liveness']}")
    return results


def load_preflight_results(artifact_dir: Path) -> dict[str, Any]:
    path = artifact_dir / "preflight-results.json"
    if not path.exists():
        return {"skipped": True}
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except json.JSONDecodeError as exc:
        return {"skipped": True, "load_error": str(exc), "path": str(path)}


def device_identity_lines(args: argparse.Namespace) -> list[str]:
    """The device's self-reported identity, or an explicit note that it could
    not be read. Never a claim the run did not observe."""
    try:
        _status, payload = make_rest(args).req("GET", "/v1/info")
        info = json.loads(payload)
    except Exception as exc:  # noqa: BLE001
        return [f"- Firmware identity: unavailable ({type(exc).__name__}: {exc})"]
    fields = ("product", "firmware_version", "fpga_version", "core_version",
              "hostname", "unique_id")
    return [f"- `{name}`: `{info[name]}`" for name in fields if name in info]


def opcode_volume_command(args: argparse.Namespace, op_dir: Path) -> list[str]:
    """The stress driver's command line for this run's topology. On a split
    session the driver needs the C64U as its machine host, or its first
    keystroke reaches the cartridge's compiled-out machine:input."""
    split_host = ["--c64-host", args.c64_host] if args.c64_host else []
    return [
        "python3", str(MCM_DIR / "monitor_debug_stress.py"),
        "--host", args.rest_host,
        "--ui", "overlay",
        "--focus", "all",
        "--iterations", str(getattr(args, "opcode_iterations", 12)),
        "--prog-len", "120",
        "--max-steps", "120",
        "--jsr-depths", str(max(32, args.required_step_into_depth)),
        "--seed", "9001",
        "--artifact-dir", str(op_dir),
    ] + split_host


def run_opcode_volume(args: argparse.Namespace, artifact_dir: Path) -> dict[str, Any]:
    op_dir = artifact_dir / "opcode-1000"
    op_dir.mkdir(parents=True, exist_ok=True)
    cmd = opcode_volume_command(args, op_dir)
    rc = run_cmd(cmd, REPO_ROOT, op_dir / "opcode-volume.log", timeout=1800)
    summaries = sorted(op_dir.glob("*.summary.json"))
    summary: dict[str, Any] = {"rc": rc, "cmd": cmd, "memory_mode": "ram", "interface": "overlay"}
    if summaries:
        data = json.loads(summaries[-1].read_text(encoding="utf-8"))
        summary.update(data)
    steps = int(summary.get("steps", 0)) + int(summary.get("jsr_steps", 0))
    summary["opcode_requirement_status"] = "PASS" if rc == 0 and steps >= args.opcode_run else "FAIL"
    summary["opcode_count"] = steps
    (artifact_dir / "opcode-1000-summary.json").write_text(
        json.dumps(summary, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    return summary


def final_hygiene(args: argparse.Namespace, artifact_dir: Path,
                  preflight: dict[str, Any] | None = None) -> dict[str, Any]:
    results = {
        "rest_liveness": make_rest(args).alive(),
        "telnet_liveness": tcp_probe(args.host, args.port),
        "device_identity": SR.device_identity(make_rest(args)),
    }
    # The run must be able to say it measured one image on one board. A device
    # reflashed or swapped between the preflight stamp and this one was not.
    results["identity_changed"] = SR.identity_changes(
        (preflight or {}).get("device_identity", {}), results["device_identity"])
    for name, cmd in [
        ("git-diff-check", ["git", "diff", "--check"]),
        ("git-status-short", ["git", "status", "--short"]),
    ]:
        rc = run_cmd(cmd, REPO_ROOT, artifact_dir / "final-hygiene" / f"{name}.log", timeout=120)
        results[name] = rc
    (artifact_dir / "final-hygiene.json").write_text(
        json.dumps(results, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    return results


def final_teardown(args: argparse.Namespace, artifact_dir: Path) -> dict[str, Any]:
    """Leave the device with no held input and no open menu.

    A run driven by ./run-tests gets the runner's own teardown; a direct
    invocation of this gate does not. An open on-device menu holds the C64, so
    the next health sweep reports raster and jiffy as skipped and reads as a
    dead machine. This runs after the verdict is decided, records what it did,
    and never raises: teardown must not change or mask a result.
    """
    result: dict[str, Any] = {"time": now_stamp()}
    try:
        rest = make_rest(args, timeout=8.0)
        try:
            rest.release_all()
            result["release_input"] = True
        except Exception as exc:  # noqa: BLE001 - teardown must not mask a verdict
            result["release_input"] = f"{type(exc).__name__}: {exc}"
        result["menu_closed"] = L.ensure_menu_closed(rest)
        result["c64_running"] = _c64_running(machine_host(args))
    except Exception as exc:  # noqa: BLE001 - teardown must not mask a verdict
        result["error"] = f"{type(exc).__name__}: {exc}"
    try:
        artifact_dir.mkdir(parents=True, exist_ok=True)
        (artifact_dir / "final-teardown.json").write_text(
            json.dumps(result, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    except Exception as exc:  # noqa: BLE001 - teardown must not mask a verdict
        print(f"final teardown not recorded: {type(exc).__name__}: {exc}", flush=True)
    print(f"final teardown: menu_closed={result.get('menu_closed')} "
          f"c64_running={result.get('c64_running')} "
          f"release_input={result.get('release_input')}", flush=True)
    return result


def write_final_report(args: argparse.Namespace, artifact_dir: Path, ledger: Ledger,
                       preflight: dict[str, Any], opcode: dict[str, Any],
                       hygiene: dict[str, Any]) -> str:
    rows = ledger.rows
    all_done = all(row["status"] in FINAL_STATUSES for row in rows)
    all_pass = all(row["status"] in ("PASS", "SKIPPED_UNSUPPORTED")
                   for row in rows)
    genuine_failures = [row for row in rows
                        if row["status"] in ("FAIL", "BLOCKED_WITH_EVIDENCE")]
    masking_violations = COUNTERS.violations()
    opcode_pass = opcode.get("opcode_requirement_status") == "PASS"
    if not all_done or masking_violations or genuine_failures or not opcode_pass:
        verdict = "NOT PRODUCTION READY"
    else:
        verdict = "PRODUCTION READY"
    report = artifact_dir / "FINAL_REPORT.md"
    branch = subprocess.check_output(["git", "branch", "--show-current"], cwd=REPO_ROOT,
                                     text=True).strip()
    commit = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=REPO_ROOT,
                                     text=True).strip()
    dirty = subprocess.check_output(["git", "status", "--short"], cwd=REPO_ROOT,
                                    text=True)
    def count_summary(key: str) -> list[str]:
        lines_out = []
        values = sorted({str(row[key]) for row in rows})
        for value in values:
            subset = [row for row in rows if str(row[key]) == value]
            counts = {status: sum(1 for row in subset if row["status"] == status)
                      for status in ("PASS", "FAIL", "BLOCKED_WITH_EVIDENCE",
                                     "SKIPPED_UNSUPPORTED", "PENDING")}
            lines_out.append(
                f"- `{value}`: PASS={counts['PASS']} FAIL={counts['FAIL']} "
                f"BLOCKED_WITH_EVIDENCE={counts['BLOCKED_WITH_EVIDENCE']} "
                f"SKIPPED_UNSUPPORTED={counts['SKIPPED_UNSUPPORTED']} "
                f"PENDING={counts['PENDING']}"
            )
        return lines_out

    canonical_cmd = (
        "python3 tests/e2e/monitor/monitor_debug_matrix_test.py "
        f"--host {args.host} --rest-host {args.rest_host} --artifact-dir {artifact_dir} "
        f"--memory {args.memory} --ui {args.ui} --reps {args.reps} "
        f"--required-step-into-depth {args.required_step_into_depth} "
        f"--opcode-run {args.opcode_run} --strict --resume"
    )
    current_cmd = " ".join(sys.argv)
    lines = [f"Verdict: {verdict}", ""]
    lines += [
        "## Branch, Commit, Dirty State",
        f"- Branch: `{branch}`",
        f"- Commit: `{commit}`",
        "- Dirty state:",
        "```text",
        dirty.rstrip() or "<clean>",
        "```",
        "",
        "## Hardware Identity And Firmware Version",
        f"- REST host: `{args.rest_host}`",
        f"- Telnet host: `{args.host}:{args.port}`",
        *device_identity_lines(args),
        "",
        "## Build And Deploy Evidence",
        # This runner does not build or flash anything: it drives whatever
        # firmware the device is already running. Stating build steps here
        # would assert something the run never observed, so the report gives
        # the device's own reported identity above and says only what is true
        # of this process.
        "- This runner performs no build, deploy or push. It exercises the "
        "firmware already running on the device named above.",
        "- The device identity reported above is what that firmware returned "
        "over REST during this run.",
        "",
        "## Preflight Results",
        "```json",
        json.dumps(preflight, indent=2, sort_keys=True),
        "```",
        "",
        "## Blocker-A Regression Probe Results",
        f"- freeze_reentry_guard.py rc: `{preflight.get('freeze-reentry')}`",
        f"- mcm_localui.py soak rc: `{preflight.get('localui-soak')}`",
        "",
        "## Coverage Ledger",
        ledger.to_markdown(),
        "",
        "## 1000-Opcode Live Run Evidence",
        f"- Status: `{opcode.get('opcode_requirement_status')}`",
        f"- Count: `{opcode.get('opcode_count')}`",
        "- Mapped cell: `memory_mode=ram`, `interface=overlay`",
        f"- Artifact: `{artifact_dir / 'opcode-1000'}`",
        "",
        "## Per-Memory Summary",
        *count_summary("memory_mode"),
        "",
        "## Per-Interface Summary",
        *count_summary("interface"),
        "",
        "## Failure Classifications",
    ]
    failures = [row for row in rows if row.get("failure")]
    if not failures:
        lines.append("- None")
    else:
        for row in failures:
            failure = row["failure"]
            lines.append(
                f"- `{row['cell_id']}`: {failure.get('classification')}: "
                f"{failure.get('message')}"
            )
    lines += [
        "",
        "## Harness Changes Made",
        "- Added `monitor_debug_matrix_test.py`.",
        "- Updated `monitor_debug_stress.py` so active-Debug write readback is not the default progress oracle.",
        "",
        "## Firmware Changes Made",
        "- None by this matrix runner.",
        "",
        "## Exact Commands Used",
        "```text",
        canonical_cmd,
        current_cmd,
        "```",
        "",
        "## Artifact Paths",
        f"- Artifact directory: `{artifact_dir}`",
        f"- JSON ledger: `{ledger.json_path}`",
        f"- Markdown ledger: `{ledger.md_path}`",
        "",
        "## Final Hygiene Audit",
        "```json",
        json.dumps(hygiene, indent=2, sort_keys=True),
        "```",
        "",
        "## Remaining Blockers",
    ]
    blockers = [row for row in rows if row["status"] == "BLOCKED_WITH_EVIDENCE"]
    if not blockers:
        lines.append("- None")
    else:
        for row in blockers:
            lines.append(f"- `{row['cell_id']}`: {row['failure']['message']}")
    lines += [
        "",
        "## Smallest Repro Commands",
    ]
    for row in rows:
        if row["status"] != "PASS":
            lines.append(
                f"- `{row['cell_id']}`: rerun this gate with "
                f"`--memory {row['memory_mode']} --ui {row['interface']} --reps {row['repetition']} --resume` "
                f"and inspect `{row['artifact_dir']}`."
            )
    if all_pass:
        lines.append("- None (no genuine debugger failure)")
    lines += [
        "",
        "## Reset / Retry Instrumentation (anti-masking)",
        "Prohibited categories MUST be zero. `recovery_reset`, `command_retry`, "
        "`session_replay`, `transport_reconnect`-replay and `breakpoint_replant` "
        "recoveries are the masking this gate must never use.",
        "```json",
        json.dumps(COUNTERS.counts, indent=2, sort_keys=True),
        "```",
        (f"- ANTI-MASKING VIOLATION: {masking_violations}" if masking_violations
         else "- Anti-masking invariant HELD (all prohibited counters == 0)"),
        "",
        "",
        "## Commit And Push Status",
        "- Committed: no",
        "- Pushed: no",
        "",
    ]
    report.write_text("\n".join(lines), encoding="utf-8")
    return verdict


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Run the Machine Code Monitor debugger matrix gate")
    parser.add_argument("--host", required=True)
    parser.add_argument("--rest-host", required=True)
    parser.add_argument("--c64-host", default=None,
                        help="Split-session mode for a U2+L cartridge: the C64U "
                             "host it is plugged into. When set, machine ops "
                             "(readmem/writemem/reset/input) route here while "
                             "menu_screen/menu_button stay on --rest-host (the "
                             "cartridge). --host telnet also stays on the cartridge.")
    parser.add_argument("--port", type=int, default=23)
    parser.add_argument("--password")
    parser.add_argument("--memory", choices=MEMORY_MODES + ("all",), default="all")
    parser.add_argument("--ui", choices=INTERFACES + ("all",), default="all")
    parser.add_argument("--cells", default="",
                        help="Run a named set of cells instead of the "
                             "--memory x --ui product: a comma-separated list "
                             "of MEMORY:UI or MEMORY:UI:REPS terms, e.g. "
                             "'ram:telnet,rom:freeze:2'. A term without its own "
                             "repetition count uses --reps. Overrides --memory "
                             "and --ui.")
    parser.add_argument("--focus",
                        choices=("matrix", "alerts", "banking", "entry-footer"),
                        default="matrix",
                        help="matrix = full debugger matrix; alerts = focused "
                             "Debug alert/manual wording contract check; banking "
                             "= focused split-session U2 CPU-banking check; "
                             "entry-footer = focused split-session check that "
                             "the monitor's first frame already carries the "
                             "running program's CPU and VIC banking.")
    parser.add_argument("--reps", type=int,
                        help="Cell repetitions (default: 3; 1 for split U2 sessions).")
    parser.add_argument("--required-step-into-depth", type=int,
                        help="Nested Step Into depth (default: 32; 8 for split U2 sessions).")
    parser.add_argument("--straight-calls", type=int,
                        help="Consecutive Step Overs (default: 32; 8 for split U2 sessions).")
    parser.add_argument("--opcode-run", type=int, default=1000)
    parser.add_argument("--opcode-iterations", type=int, default=12,
                        help="Random programs the closing opcode-volume run "
                             "drives (default 12, which lands ~2592 opcodes "
                             "against the 1000 --opcode-run requirement). "
                             "Lower it to trade the requirement's headroom for "
                             "wall clock; the requirement itself does not move.")
    parser.add_argument("--strict", action="store_true")
    parser.add_argument("--resume", action="store_true")
    parser.add_argument("--preflight-only", action="store_true")
    parser.add_argument("--fresh-deploy-between-cells", action="store_true")
    parser.add_argument("--continue-after-cell-failure", action="store_true")
    parser.add_argument("--fail-fast", action="store_true")
    parser.add_argument("--artifact-dir", default=None,
                        help="Where to write the ledger, per-cell evidence and "
                             "FINAL_REPORT.md. Defaults to a timestamped "
                             "directory under the system temp dir.")
    parser.add_argument("--run-ledger", default="",
                        help="Directory holding the cross-run history "
                             "(history.jsonl, HISTORY.md and one folder per "
                             "run). Defaults to $MCM_RUN_LEDGER, else "
                             "doc/research/machine-code-monitor/matrix-runs.")
    parser.add_argument("--no-run-ledger", action="store_true",
                        help="Do not record this run in the cross-run history.")
    parser.add_argument("--coverage-ledger", default="")
    parser.add_argument("--timeout", type=float, default=5.0)
    parser.add_argument("--skip-preflight", action="store_true",
                        help="Developer escape hatch for harness iteration; do not use for final evidence.")
    parser.add_argument("--skip-opcode-run", action="store_true",
                        help="Developer escape hatch for harness iteration; strict final runs should not use it.")
    parser.add_argument("--classify-pending-device-blocked", action="store_true",
                        help="Mark pending rows BLOCKED_WITH_EVIDENCE when the device is unreachable.")
    args = parser.parse_args(argv)
    if args.c64_host:
        if args.reps is None:
            args.reps = U2_REPS
        if args.required_step_into_depth is None:
            args.required_step_into_depth = U2_STEP_INTO_DEPTH
        if args.straight_calls is None:
            args.straight_calls = U2_STRAIGHT_CALLS
    else:
        if args.reps is None:
            args.reps = DEFAULT_REPS
        if args.required_step_into_depth is None:
            args.required_step_into_depth = DEFAULT_STEP_INTO_DEPTH
        if args.straight_calls is None:
            args.straight_calls = DEFAULT_STRAIGHT_CALLS
    if args.cells:
        # Validated here rather than when the ledger is built, so a mistyped
        # cell is a usage error before the device is touched at all.
        try:
            parse_cell_selection(args.cells, args.reps)
        except ValueError as exc:
            parser.error(str(exc))
    return args


def _banking_bootstrap(bank: int) -> bytes:
    """Seed RAM under the three banked windows, select `bank`, then run C000."""
    code = bytearray([
        0x78,                         # SEI
        0xA9, 0x2F, 0x85, 0x00,       # $00=$2F: bits 0-2 are outputs
        0xA9, 0x30, 0x85, 0x01,       # all three banked windows expose RAM
    ])
    for address, data in BANKING_RAM_BYTES.items():
        for offset, byte in enumerate(data):
            target = address + offset
            code.extend((0xA9, byte, 0x8D, target & 0xFF, target >> 8))
    code.extend((
        0xA9, 0x30 | (bank & 0x07),
        0x85, 0x01,
        0x4C, 0x00, 0xC0,
    ))
    return bytes(code)


def _banking_row_bytes(row: str, address: int) -> bytes:
    fields = row.strip("| ").split()
    if not fields or fields[0].upper() != f"{address:04X}":
        raise GateError(
            f"could not parse displayed instruction bytes at ${address:04X}: {row!r}")
    raw = []
    for field in fields[1:4]:
        if re.fullmatch(r"[0-9A-Fa-f]{2}", field) is None:
            break
        raw.append(int(field, 16))
    if not raw:
        raise GateError(
            f"could not parse displayed instruction bytes at ${address:04X}: {row!r}")
    return bytes(raw)


def _wait_banking_status(rest, expected: str, timeout: float = 8.0) -> str:
    deadline = time.time() + timeout
    last = ""
    while time.time() < deadline:
        last = overlay_lifecycle.status_line(rest)
        if expected in last:
            return last
        time.sleep(0.2)
    raise GateError(f"status did not show {expected!r}: {last!r}\n{rest.screen_text()}")


def _wait_banking_row(rest, address: int, source: str,
                      timeout: float = 8.0) -> str:
    deadline = time.time() + timeout
    last = ""
    while time.time() < deadline:
        last = overlay_lifecycle.line_for_address(rest, address)
        if f"[{source}]" in last:
            return last
        time.sleep(0.2)
    raise GateError(
        f"${address:04X} did not show [{source}]: {last!r}\n{rest.screen_text()}")


def run_banking_scope(args: argparse.Namespace, artifact_dir: Path) -> int:
    """Focused U2 check for captured $01 and CPU-visible DMA disassembly.

    The C64U runs the bootstrap and provides the independent DMA readback; the
    U2 owns the monitor overlay. A real 6510 instruction writes $01 immediately
    before the breakpoint, so the status row can only pass when Debug captures
    the pre-diversion banking state.
    """
    results: dict[str, Any] = {
        "focus": "banking",
        "machine_host": args.c64_host,
        "overlay_host": args.rest_host,
        "states": [],
    }
    if not args.c64_host:
        results.update({
            "status": "FAIL",
            "error": "--focus banking requires --c64-host for split-session U2 coverage",
        })
        (artifact_dir / "banking-results.json").write_text(
            json.dumps(results, indent=2) + "\n", encoding="utf-8")
        print(results["error"], flush=True)
        return 1

    # Isolation runs: one state per process, so a failure cannot be inherited
    # from whatever the previous state left armed. Unset, every state runs.
    # The listed order is honoured, not the declaration order, so a state can be
    # run in a chosen position. That is what distinguishes a per-bank failure
    # from one that follows position because a predecessor left something behind.
    only = os.environ.get("MCM_BANKING_ONLY", "").strip()
    banking_states = BANKING_STATES
    if only:
        by_bank = {bank: (bank, sources) for bank, sources in BANKING_STATES}
        banking_states = tuple(by_bank[int(part)] for part in only.split(",")
                               if part.strip())

    for bank, sources in banking_states:
        state_dir = artifact_dir / f"cpu{bank}"
        state_dir.mkdir(parents=True, exist_ok=True)
        row = {"cell_id": f"BANKING_CPU{bank}", "interface": "overlay"}
        state_result: dict[str, Any] = {
            "bank": bank,
            "port": f"{0x30 | bank:02X}",
            "expected_sources": dict(zip(("A000", "D020", "E000"), sources)),
            "rows": {},
        }
        driver: RestDebugDriver | None = None
        with (state_dir / "trace.jsonl").open(
                "a", encoding="utf-8", buffering=1) as trace:
            try:
                driver = RestDebugDriver(args, row, state_dir, trace)
                driver.reset_baseline()
                fixture = build_fixture("ram", 1)
                fixture.bank = bank
                fixture.bootstrap = _banking_bootstrap(bank)
                fixture.chunks = [(0xC000, bytes([0xEA, 0xEA, 0x4C, 0x00, 0xC0]))]
                driver.install_fixture(fixture)
                driver.open_monitor()
                entry = driver.enter_debug_at(0xC000)
                assert_state_pc_sp(entry, 0xC000, None, f"CPU{bank} entry")

                expected_status = (
                    f"CPU{bank} $A:{sources[0]} $D:{sources[1]} $E:{sources[2]}")
                state_result["status_line"] = _wait_banking_status(
                    driver.rest, expected_status)

                for address, source in zip((0xA000, 0xD020, 0xE000), sources):
                    driver.goto(address)
                    screen_row = _wait_banking_row(driver.rest, address, source)
                    displayed = _banking_row_bytes(screen_row, address)
                    # The U2 freezer intentionally forces Ultimax.  Its C64U
                    # DMA aperture consequently sees I/O at $D000 even when
                    # the stopped 6510 had banked RAM there.  The captured
                    # CPU-port footer and the row source tag remain observable,
                    # but no frozen-DMA byte oracle exists for that one case.
                    dma_observable = not (address == 0xD020 and source == "RAM")
                    dma = driver.read_oracle_bytes(address, len(displayed))
                    if dma_observable and displayed != dma:
                        raise GateError(
                            f"CPU{bank} ${address:04X} display/DMA mismatch: "
                            f"display={displayed.hex().upper()} dma={dma.hex().upper()} "
                            f"row={screen_row!r}")
                    if source == "RAM" and dma_observable:
                        ram = driver.read_oracle_bytes(
                            address, len(BANKING_RAM_BYTES[address]))
                        if ram != BANKING_RAM_BYTES[address]:
                            raise GateError(
                                f"CPU{bank} ${address:04X} did not expose seeded RAM: "
                                f"expected={BANKING_RAM_BYTES[address].hex().upper()} "
                                f"dma={ram.hex().upper()}")
                    state_result["rows"][f"{address:04X}"] = {
                        "source": source,
                        "row": screen_row,
                        "displayed_bytes": displayed.hex().upper(),
                        "dma_bytes": dma.hex().upper(),
                        "dma_observable": dma_observable,
                    }
                state_result["status"] = "PASS"
                print(f"banking CPU{bank}: PASS", flush=True)
            except Exception as exc:  # noqa: BLE001 - record every banking state
                state_result.update({
                    "status": "FAIL",
                    "error": f"{type(exc).__name__}: {exc}",
                    "traceback": traceback.format_exc(),
                })
                print(f"banking CPU{bank}: FAIL: {exc}", flush=True)
            finally:
                if driver is not None:
                    released = driver.release_machine_hold(f"post-CPU{bank}")
                    state_result["machine_released"] = released
                    if not released and state_result["status"] == "PASS":
                        # A state that passed but did not hand the machine back
                        # has broken every state after it, so it is not a pass.
                        state_result.update({
                            "status": "FAIL",
                            "error": "state passed but left the machine held",
                        })
                        print(f"banking CPU{bank}: FAIL: passed but left the "
                              f"machine held", flush=True)
        results["states"].append(state_result)

    failed = [state for state in results["states"] if state["status"] != "PASS"]
    results["status"] = "FAIL" if failed else "PASS"
    (artifact_dir / "banking-results.json").write_text(
        json.dumps(results, indent=2) + "\n", encoding="utf-8")
    print(f"banking scope: {results['status']} "
          f"({len(results['states']) - len(failed)}/{len(results['states'])} states)",
          flush=True)
    return 1 if failed else 0


def _entry_footer_bootstrap(ddr: int, port: int, vic_bank: int) -> bytes:
    """Put the machine in a known CPU-port and VIC-bank state, then spin at C000.

    The order matters. $DD00/$DD02 are only reachable while I/O is mapped, and
    the RAM under the three banked windows is only writable while those windows
    expose RAM, so both are done under the default port before the requested
    port is selected last.
    """
    code = bytearray([
        0x78,                               # SEI
        0xA9, 0x2F, 0x85, 0x00,             # $00=$2F: bank bits are outputs
        0xA9, 0x37, 0x85, 0x01,             # $01=$37: I/O mapped for $DD0x
        0xAD, 0x02, 0xDD,                   # LDA $DD02
        0x09, 0x03, 0x8D, 0x02, 0xDD,       # ORA #$03 / STA $DD02: bank bits out
        0xAD, 0x00, 0xDD,                   # LDA $DD00
        0x29, 0xFC,                         # AND #$FC
        0x09, (3 - (vic_bank & 0x03)) & 0x03,
        0x8D, 0x00, 0xDD,                   # STA $DD00: VIC bank selected
        0xA9, 0x30, 0x85, 0x01,             # $01=$30: all banked windows are RAM
    ])
    for address, data in BANKING_RAM_BYTES.items():
        for offset, byte in enumerate(data):
            target = address + offset
            code.extend((0xA9, byte, 0x8D, target & 0xFF, target >> 8))
    code.extend((
        0xA9, port & 0xFF, 0x85, 0x01,      # data register first,
        0xA9, ddr & 0xFF, 0x85, 0x00,       # then the direction that resolves it
        0x4C, 0x00, 0xC0,
    ))
    return bytes(code)


def _parse_footer(line: str) -> dict[str, Any]:
    """Split a monitor status row into its CPU and VIC fields.

    Returns `complete=False` for the partial `CPU VIEW  VICn $XXXX` form, which
    carries no CPU banking at all.
    """
    text = line.strip()
    full = re.search(
        r"CPU(\d)\s+\$A:(\S+)\s+\$D:(\S+)\s+\$E:(\S+)\s+VIC(\d)\s+\$([0-9A-Fa-f]{4})",
        text)
    if full:
        return {
            "complete": True,
            "raw": text,
            "cpu": int(full.group(1)),
            "a000": full.group(2),
            "d000": full.group(3),
            "e000": full.group(4),
            "vic": int(full.group(5)),
            "vic_base": int(full.group(6), 16),
        }
    partial = re.search(r"CPU\s+VIEW\s+VIC(\d)\s+\$([0-9A-Fa-f]{4})", text)
    if partial:
        return {
            "complete": False,
            "raw": text,
            "form": "cpu-view-partial",
            "vic": int(partial.group(1)),
            "vic_base": int(partial.group(2), 16),
        }
    return {"complete": False, "raw": text, "form": "unrecognised"}


def _read_entry_footer(rest, timeout: float = 6.0) -> dict[str, Any]:
    """Read the status row on the first frames after the monitor opens.

    Polls only until a row appears at all -- deliberately NOT until it becomes
    complete, because the point of the check is what the footer says on entry.
    """
    deadline = time.time() + timeout
    parsed = {"complete": False, "raw": "", "form": "absent"}
    while time.time() < deadline:
        line = overlay_lifecycle.status_line(rest)
        if line.strip():
            parsed = _parse_footer(line)
            break
        time.sleep(0.2)
    return parsed


ENTRY_FOOTER_CPU_STATES = (
    # label, $0000 (DDR), $0001 (data), resolved bank, ($A000, $D000, $E000)
    ("cpu7", 0x2F, 0x37, 7, ("BAS", "I/O", "KRN")),
    ("cpu5", 0x2F, 0x35, 5, ("RAM", "I/O", "RAM")),
    ("cpu4", 0x2F, 0x34, 4, ("RAM", "RAM", "RAM")),
    ("cpu3", 0x2F, 0x33, 3, ("BAS", "CHR", "KRN")),
    ("cpu0", 0x2F, 0x30, 0, ("RAM", "RAM", "RAM")),
    # The DDR-resolved state: the data register holds 0 in all three bank bits,
    # but the direction register makes them inputs, so the pull-ups drive them
    # high and the machine is banked exactly as CPU7. A footer sourced from the
    # data register alone reports CPU0 here; the machine is running the KERNAL.
    ("cpu7-ddr", 0x28, 0x30, 7, ("BAS", "I/O", "KRN")),
)

ENTRY_FOOTER_VIC_BANKS = (0, 1, 2, 3)
VIC_BANK_BASES = (0x0000, 0x4000, 0x8000, 0xC000)
ENTRY_FOOTER_PROGRESS = 0xC1F0


def _launch_fixture_from_basic(driver: RestDebugDriver, fixture: MatrixFixture) -> None:
    """Start the fixture from the BASIC prompt, outside the monitor.

    The monitor must open onto a machine that is already running the program in
    its chosen banking, so the launch cannot come from the monitor's own `G`:
    on a U2+L that goes through the boot cartridge and resets the C64 first.
    Typing SYS at the C64U keyboard leaves the monitor entirely out of it.
    """
    driver.rest.write_mem(ENTRY_FOOTER_PROGRESS, bytes([0x00]))
    # Lower case here on purpose: char_to_combo() shifts an upper-case letter,
    # and a shifted key at the BASIC prompt types a graphic character, not "S".
    driver.rest.send_text(f"sys {fixture.bootstrap_addr}\r")
    driver.event("fixture_launched_from_basic",
                 address=f"{fixture.bootstrap_addr:04X}")
    driver.wait_progress_change(ENTRY_FOOTER_PROGRESS,
                                "fixture running before monitor open",
                                timeout=6.0)


def run_entry_footer_scope(args: argparse.Namespace, artifact_dir: Path) -> int:
    """Split-host check that the monitor's first frame carries full banking.

    On the frame the monitor draws when it opens -- before Debug is pressed and
    before any debugged instruction has executed -- the status row must report
    the CPU banking and the VIC bank of the program that was running, and the
    first Debug capture must then agree with it.
    """
    results: dict[str, Any] = {
        "focus": "entry-footer",
        "machine_host": args.c64_host,
        "overlay_host": args.rest_host,
        "cells": [],
    }
    if not args.c64_host:
        results.update({
            "status": "FAIL",
            "error": "--focus entry-footer requires --c64-host for split-session U2 coverage",
        })
        (artifact_dir / "entry-footer-results.json").write_text(
            json.dumps(results, indent=2) + "\n", encoding="utf-8")
        print(results["error"], flush=True)
        return 1

    # Same isolation lever as MCM_BANKING_ONLY: "cpu7:0,cpu3:2" runs those two
    # cells, each in the listed order, so a per-cell failure can be told apart
    # from one that follows position because a predecessor left state behind.
    only = os.environ.get("MCM_ENTRY_FOOTER_ONLY", "").strip()
    cells: list[tuple[tuple, int]] = []
    if only:
        by_label = {state[0]: state for state in ENTRY_FOOTER_CPU_STATES}
        for part in only.split(","):
            part = part.strip()
            if not part:
                continue
            label, _, bank = part.partition(":")
            cells.append((by_label[label], int(bank or 0)))
    else:
        for state in ENTRY_FOOTER_CPU_STATES:
            for vic_bank in ENTRY_FOOTER_VIC_BANKS:
                cells.append((state, vic_bank))

    for (label, ddr, port, bank, sources), vic_bank in cells:
        cell_dir = artifact_dir / f"{label}-vic{vic_bank}"
        cell_dir.mkdir(parents=True, exist_ok=True)
        row = {"cell_id": f"ENTRY_FOOTER_{label.upper()}_VIC{vic_bank}",
               "interface": "overlay"}
        expected_footer = (
            f"CPU{bank} $A:{sources[0]} $D:{sources[1]} $E:{sources[2]} "
            f"VIC{vic_bank} ${VIC_BANK_BASES[vic_bank]:04X}")
        cell: dict[str, Any] = {
            "cell_id": row["cell_id"],
            "cpu_state": label,
            "ddr": f"{ddr:02X}",
            "port": f"{port:02X}",
            "expected_cpu_bank": bank,
            "expected_sources": dict(zip(("A000", "D020", "E000"), sources)),
            "expected_vic_bank": vic_bank,
            "expected_footer": expected_footer,
        }
        driver: RestDebugDriver | None = None
        with (cell_dir / "trace.jsonl").open(
                "a", encoding="utf-8", buffering=1) as trace:
            try:
                driver = RestDebugDriver(args, row, cell_dir, trace)
                driver.reset_baseline()
                fixture = build_fixture("ram", 1)
                fixture.bank = bank
                fixture.bootstrap = _entry_footer_bootstrap(ddr, port, vic_bank)
                # INC $C1F0 / JMP $C000: an infinite loop with a liveness
                # counter, so "the program is running" is a measured fact.
                fixture.chunks = [(0xC000, bytes([0xEE, 0xF0, 0xC1,
                                                  0x4C, 0x00, 0xC0]))]
                driver.install_fixture(fixture)

                # The program must be the thing running when the monitor opens,
                # so launch it and prove it reached its loop before freezing.
                _launch_fixture_from_basic(driver, fixture)
                # Recorded, not asserted on: what a DMA read of the port
                # mirror says while the program runs is evidence about the
                # channel, and is exactly the value that must not be trusted
                # blindly.
                cell["dma_0000_before_open"] = \
                    f"{driver.read_oracle_bytes(0x0000, 1)[0]:02X}"
                cell["dma_0001_before_open"] = \
                    f"{driver.read_oracle_bytes(0x0001, 1)[0]:02X}"

                driver.open_monitor()
                entry_footer = _read_entry_footer(driver.rest)
                cell["entry_footer"] = entry_footer
                (cell_dir / "entry-screen.txt").write_text(
                    driver.rest.screen_text(), encoding="utf-8")

                if not entry_footer.get("complete"):
                    raise GateError(
                        f"entry footer is not populated: {entry_footer.get('raw')!r} "
                        f"(form={entry_footer.get('form')}); expected "
                        f"{expected_footer!r}")
                mismatches = []
                if entry_footer["cpu"] != bank:
                    mismatches.append(
                        f"CPU{entry_footer['cpu']} != CPU{bank}")
                for field_name, got, want in (
                        ("$A", entry_footer["a000"], sources[0]),
                        ("$D", entry_footer["d000"], sources[1]),
                        ("$E", entry_footer["e000"], sources[2])):
                    if got != want:
                        mismatches.append(f"{field_name}:{got} != {field_name}:{want}")
                if entry_footer["vic"] != vic_bank:
                    mismatches.append(
                        f"VIC{entry_footer['vic']} != VIC{vic_bank}")
                if entry_footer["vic_base"] != VIC_BANK_BASES[vic_bank]:
                    mismatches.append(
                        f"VIC base ${entry_footer['vic_base']:04X} != "
                        f"${VIC_BANK_BASES[vic_bank]:04X}")
                if mismatches:
                    raise GateError(
                        "entry footer disagrees with the running program: "
                        + "; ".join(mismatches)
                        + f"; row={entry_footer['raw']!r}")

                # Only now enter Debug. The first captured context must report
                # the same CPU and VIC facts the entry frame already showed.
                debug_state = driver.enter_debug_at(0xC000)
                assert_state_pc_sp(debug_state, 0xC000, None, f"{label} entry")
                debug_footer = _parse_footer(
                    _wait_banking_status(
                        driver.rest,
                        f"CPU{bank} $A:{sources[0]} $D:{sources[1]} $E:{sources[2]}"))
                cell["debug_footer"] = debug_footer
                (cell_dir / "debug-screen.txt").write_text(
                    driver.rest.screen_text(), encoding="utf-8")
                for key in ("cpu", "a000", "d000", "e000", "vic", "vic_base"):
                    if debug_footer.get(key) != entry_footer.get(key):
                        raise GateError(
                            f"first Debug capture disagrees with the entry footer "
                            f"on {key}: entry={entry_footer.get(key)!r} "
                            f"debug={debug_footer.get(key)!r}; "
                            f"entry_row={entry_footer['raw']!r} "
                            f"debug_row={debug_footer['raw']!r}")

                cell["status"] = "PASS"
                print(f"entry-footer {label} VIC{vic_bank}: PASS", flush=True)
            except Exception as exc:  # noqa: BLE001 - record every cell
                cell.update({
                    "status": "FAIL",
                    "error": f"{type(exc).__name__}: {exc}",
                    "traceback": traceback.format_exc(),
                })
                print(f"entry-footer {label} VIC{vic_bank}: FAIL: {exc}", flush=True)
            finally:
                if driver is not None:
                    released = driver.release_machine_hold(f"post-{label}-vic{vic_bank}")
                    cell["machine_released"] = released
                    if not released and cell.get("status") == "PASS":
                        cell.update({
                            "status": "FAIL",
                            "error": "cell passed but left the machine held",
                        })
                        print(f"entry-footer {label} VIC{vic_bank}: FAIL: passed "
                              f"but left the machine held", flush=True)
        results["cells"].append(cell)

    failed = [cell for cell in results["cells"] if cell.get("status") != "PASS"]
    results["status"] = "FAIL" if failed else "PASS"
    (artifact_dir / "entry-footer-results.json").write_text(
        json.dumps(results, indent=2) + "\n", encoding="utf-8")
    print(f"entry-footer scope: {results['status']} "
          f"({len(results['cells']) - len(failed)}/{len(results['cells'])} cells)",
          flush=True)
    return 1 if failed else 0


def run_alert_scope(args: argparse.Namespace, artifact_dir: Path) -> int:
    """Focused Debug alert/manual wording contract check.

    Hardware-free by design so it is deterministic in CI; if the REST host is
    reachable it adds a non-fatal menu_screen smoke. Returns 0 when the contract
    holds, 1 otherwise (and always when --strict and any check fails).
    """
    manual = REPO_ROOT / "doc" / "machine_code_monitor.md"

    problems: list[str] = []

    problems += validate_debug_alerts()

    if not manual.exists():
        problems.append(f"missing {manual}")
    else:
        problems += validate_manual_text(manual.read_text())

    # Best-effort REST smoke; never fatal, just recorded.
    rest_reachable = False
    rest_note = "not attempted"
    try:
        client = make_rest(args, timeout=3.0)
        client.menu_screen_raw()
        rest_reachable = True
        rest_note = "menu_screen reachable"
    except Exception as exc:  # noqa: BLE001 - best effort only
        rest_note = f"rest smoke skipped: {exc}"

    result = {
        "focus": "alerts",
        "alerts_checked": len(DEBUG_ALERTS),
        "alert_max_width": DEBUG_ALERT_MAX_WIDTH,
        "rest_reachable": rest_reachable,
        "rest_note": rest_note,
        "problems": problems,
        "status": "PASS" if not problems else "FAIL",
    }
    artifact_dir.mkdir(parents=True, exist_ok=True)
    (artifact_dir / "alert-scope-results.json").write_text(
        json.dumps(result, indent=2) + "\n")
    for line in problems:
        print(f"alert-scope: {line}", flush=True)
    print(f"alert-scope: {result['status']} "
          f"(alerts={len(DEBUG_ALERTS)}, rest_reachable={rest_reachable})", flush=True)
    return 0 if not problems else 1


# Set by a caller that wraps this runner and reports each cell through
# tests/lib/report.py. It is called with (row, seconds) once a cell has reached
# its terminal status, which is the only moment a wrapper can name a verdict
# and its wall time without duplicating the ledger. Left None here: this
# runner's own console output is its progress line.
CELL_OBSERVER: Any | None = None


def main(argv: list[str] | None = None) -> int:
    args = parse_args(argv)
    mt.set_target(debug_suite_target(args))
    if not args.artifact_dir:
        args.artifact_dir = str(
            Path(tempfile.gettempdir())
            / f"mcm-matrix-{time.strftime('%Y%m%d-%H%M%S')}")
    artifact_dir = Path(args.artifact_dir)
    artifact_dir.mkdir(parents=True, exist_ok=True)
    print(f"artifact dir: {artifact_dir}", flush=True)
    if args.focus == "alerts":
        # A wording contract over the alert strings and the manual. It never
        # drives the UI, so it has nothing to hand back.
        return run_alert_scope(args, artifact_dir)
    _TEARDOWN_CONTEXT.update({"args": args, "artifact_dir": artifact_dir})
    if args.focus == "banking":
        return run_banking_scope(args, artifact_dir)
    if args.focus == "entry-footer":
        return run_entry_footer_scope(args, artifact_dir)
    ledger = create_or_load_ledger(args, artifact_dir)

    run_ledger_root = (Path(args.run_ledger) if args.run_ledger
                       else RUNLEDGER.default_root(REPO_ROOT))
    run_record: dict[str, Any] | None = None
    if not args.no_run_ledger:
        try:
            run_ledger_root.mkdir(parents=True, exist_ok=True)
            run_record = RUNLEDGER.start_run(REPO_ROOT, args, artifact_dir)
            print(f"run id: {run_record['run_id']}", flush=True)
            # Published so the entry point can still record a run that dies on
            # an unhandled exception. A crashed run is exactly the kind the
            # history needs to show.
            _RUN_LEDGER_CONTEXT.update(
                {"root": run_ledger_root, "record": run_record, "ledger": ledger})
        except Exception as exc:  # noqa: BLE001 - history must not block a run
            print(f"run ledger disabled: {type(exc).__name__}: {exc}", flush=True)

    preflight = load_preflight_results(artifact_dir) if args.skip_preflight else {"skipped": True}
    if not args.skip_preflight:
        preflight = run_preflight(args, artifact_dir)
    if args.preflight_only:
        final_hygiene(args, artifact_dir, preflight)
        return 0

    if args.classify_pending_device_blocked:
        mark_pending_blocked(
            ledger,
            artifact_dir,
            "Device/network path unavailable during matrix run: 192.168.1.13 unreachable; "
            "fallback 192.168.1.70 accepted TCP but reset REST requests and telnet recovery failed.",
        )

    opcode_status = "PENDING"
    stopped_after_required_cell_failure = False
    for row in ledger.rows:
        if args.resume and row["status"] in FINAL_STATUSES:
            print(progress_line(ledger.rows, opcode_status), flush=True)
            continue
        skip_reason = unsupported_cell_reason(args, row["memory_mode"])
        if skip_reason:
            row["status"] = "SKIPPED_UNSUPPORTED"
            row["skip_reason"] = skip_reason
            ledger.save()
            cell_dir = Path(row["artifact_dir"])
            cell_dir.mkdir(parents=True, exist_ok=True)
            (cell_dir / "skipped.txt").write_text(
                f"{row['cell_id']}: not run on this target.\n{skip_reason}\n",
                encoding="utf-8")
            print(f"{row['cell_id']}: SKIPPED ({skip_reason})", flush=True)
            print(progress_line(ledger.rows, opcode_status), flush=True)
            continue
        if args.fresh_deploy_between_cells:
            deploy_log = Path(row["artifact_dir"]) / "fresh-deploy.log"
            if (REPO_ROOT / "build").exists():
                run_cmd(["./build"], REPO_ROOT, deploy_log, timeout=900)
        # Hard per-cell wall-clock bound: a degraded telnet/httpd read that never
        # returns must not hang the run. On the watchdog the cell is failed cleanly
        # (never masked) and the run proceeds to its fail-fast / continue policy.
        signal.signal(signal.SIGALRM, _cell_watchdog_handler)
        signal.alarm(CELL_WATCHDOG_SECONDS)
        cell_started = time.monotonic()
        try:
            run_cell(args, row, ledger)
        except CellTimeout as exc:
            row["status"] = "FAIL"
            row["failure"] = {"classification": exc.classification,
                              "message": str(exc),
                              "next_action": "Watchdog fired in run_cell teardown; "
                                             "investigate the stalled transport."}
            ledger.save()
            print(f"{row['cell_id']}: CELL WATCHDOG TIMEOUT", flush=True)
        finally:
            signal.alarm(0)
            if CELL_OBSERVER is not None:
                # Never allowed to raise: a wrapper's reporting must not turn a
                # passing cell into a failing run.
                try:
                    CELL_OBSERVER(row, time.monotonic() - cell_started)
                except Exception as exc:  # noqa: BLE001
                    print(f"cell observer failed: {type(exc).__name__}: {exc}",
                          flush=True)
        print(progress_line(ledger.rows, opcode_status), flush=True)
        # Detect a hard C64 wedge left by this cell (jiffy frozen even though REST
        # is up). Log it with the pre-wedge cell so the report captures exactly what
        # preceded each wedge, then stop: continuing would fail every later cell on
        # a dead C64 and mask the wedge as a cascade of unrelated failures.
        if row["status"] != "PASS" and not _c64_running(args.rest_host):
            _log_wedge_incident(artifact_dir, row, args.rest_host)
            row["device_wedged"] = True
            ledger.save()
            print(f"{row['cell_id']}: C64 HARD WEDGE DETECTED (jiffy frozen); "
                  f"logged to wedge-incidents.jsonl; stopping for recovery", flush=True)
            stopped_after_required_cell_failure = True
            break
        if (row["status"] != "PASS"
                and stop_after_cell_failure(args)):
            stopped_after_required_cell_failure = True
            break

    opcode = {"opcode_requirement_status": "PENDING", "opcode_count": 0}
    if stopped_after_required_cell_failure:
        opcode = skipped_opcode_summary("required_cell_failure_fail_fast")
    elif args.skip_opcode_run:
        opcode = skipped_opcode_summary("skip_opcode_run_option")
    else:
        opcode = run_opcode_volume(args, artifact_dir)
    opcode_status = opcode.get("opcode_requirement_status", "FAIL")
    print(progress_line(ledger.rows, opcode_status), flush=True)

    hygiene = final_hygiene(args, artifact_dir, preflight)
    verdict = write_final_report(args, artifact_dir, ledger, preflight, opcode, hygiene)
    # A run is clean when every cell is terminal, there is no GENUINE failure
    # (FAIL/BLOCKED), the opcode gate passed, and no prohibited masking counter
    rows_done = all(row["status"] in FINAL_STATUSES for row in ledger.rows)
    genuine_failures = any(
        row["status"] in ("FAIL", "BLOCKED_WITH_EVIDENCE") for row in ledger.rows)
    masking_violations = COUNTERS.violations()
    identity_changed = hygiene.get("identity_changed") or {}
    clean = rows_done and not genuine_failures and opcode_status == "PASS" \
        and not masking_violations and not identity_changed
    if masking_violations:
        print(f"ANTI-MASKING VIOLATION (prohibited reset/retry counters): "
              f"{masking_violations}", flush=True)
    if identity_changed:
        print(f"DEVICE IDENTITY CHANGED DURING THE RUN: {identity_changed}; "
              f"this run did not measure one image and its result is not "
              f"evidence", flush=True)
    exit_code = 0 if clean else 1

    # Append this run to the cross-run history before returning, so the trend
    # files describe every run that got this far, not only the clean ones.
    if run_record is not None:
        try:
            _RUN_LEDGER_CONTEXT.clear()      # recorded here, not by the crash path
            run_dir = RUNLEDGER.finish_run(run_ledger_root, run_record,
                                           ledger.rows, opcode, verdict,
                                           exit_code)
            print(f"run ledger: {run_dir}", flush=True)
            print(f"run history: {run_ledger_root / RUNLEDGER.HISTORY_MD}",
                  flush=True)
        except Exception as exc:  # noqa: BLE001 - history must not change a verdict
            print(f"run ledger not written: {type(exc).__name__}: {exc}",
                  flush=True)
    return exit_code


def _record_crashed_run(exc: BaseException) -> None:
    context = _RUN_LEDGER_CONTEXT
    if not context.get("record"):
        return
    try:
        run_dir = RUNLEDGER.finish_run(
            context["root"], context["record"], context["ledger"].rows,
            {"opcode_requirement_status": "NOT_REACHED", "opcode_count": 0},
            f"CRASHED ({type(exc).__name__})", 2)
        print(f"run ledger (crashed run): {run_dir}", flush=True)
    except Exception as ledger_exc:  # noqa: BLE001
        print(f"run ledger not written: {type(ledger_exc).__name__}: {ledger_exc}",
              flush=True)


def _run_final_teardown() -> None:
    context = _TEARDOWN_CONTEXT
    if not context:
        return
    try:
        final_teardown(context["args"], context["artifact_dir"])
    except Exception as exc:  # noqa: BLE001 - teardown must not mask a verdict
        print(f"final teardown not run: {type(exc).__name__}: {exc}", flush=True)


if __name__ == "__main__":
    try:
        _exit_code = main()
    except BaseException as _exc:      # noqa: BLE001 - record, then re-raise
        _record_crashed_run(_exc)
        raise
    finally:
        _run_final_teardown()
    if _exit_code:
        suite_fail("machine-code-monitor-matrix", "matrix scenario failed")
    else:
        suite_ok("machine-code-monitor-matrix")
    raise SystemExit(_exit_code)
