#!/usr/bin/env python3
# E2E: Verifies the monitor test harness itself; host-side only, no device needed.

"""Host-side checks for the machine-code monitor test harness itself.

No device is required: every check here reads or imports harness source, so it
runs in a few seconds and guards the harnesses the hardware suites depend on.

Two groups:

1. Anti-masking enforcement. The debugger's contextless visible-ROM breakpoint
   entry can, under concurrent REST/DMA load, miss the 6510's first fetch,
   because the closed U64 C64 core can serve a stale pre-patch ROM byte to the
   live instruction fetch (documented in
   doc/machine-code-monitor-rom-fetch-coherency.md). A reset does not create
   coherency, so masking that miss with a reset-and-retry loop fabricates a
   green result and is prohibited. The debugger reports the genuine miss as
   DBG_ROM_ENTRY_UNCOHERENT after a bounded, no-reset in-place relaunch. The
   check is structural (AST), not text matching: it flags any loop whose body
   both resets/reconnects the machine or debug session and re-issues a debugger
   launch/step. Diagnostic tools that reset in order to MEASURE the race are
   deliberately out of scope.

2. Matrix-suite unit checks: fail-fast scheduling, oracle fixture setup, and the
   Debug alert wording contract.
"""

from __future__ import annotations

import argparse
import ast
import sys
import tempfile
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

import mcm6502 as ORC  # noqa: E402
import monitor_debug_matrix_test as gate  # noqa: E402

# Gate harnesses whose green result must never depend on a hidden reset-retry.
GATE_FILES = ("monitor_debug_test.py", "monitor_debug_matrix_test.py")

# Calls that reset the machine or re-establish a debug session/transport.
RESET_CALLS = {
    "_reset_c64_core", "reset_baseline", "reset", "reset_machine",
    "reset_from_debug_ui", "reconnect", "_reconnect_rom_debug_view",
    "_reopen_monitor", "open_monitor",
}
# Calls/keys that release the CPU toward a debugger target (a launch/step).
LAUNCH_CALLS = {
    "enter_debug_at", "_bootstrap_hit_rom_breakpoint",
    "_contextless_visible_jsr_step_over", "_acquire_rom_context_at",
    "go", "step_over", "step_into", "step_out", "continue_run",
    "continue_to_cursor", "continue_to_breakpoint", "_wait_for_pc", "wait_pc",
}
LAUNCH_KEY_ARGS = {"G", "D", "T", "U", "K"}

# Symbols that must not exist at all (their sole purpose was reset-retry masking).
BANNED_SYMBOLS = ("ROM_ENTRY_MAX_ATTEMPTS", "_reconnect_rom_debug_view")

# Documented, reviewed exceptions, keyed by (file, enclosing function name). Each
# entry must state why the loop cannot mask a debugger-command result. Currently
# empty: no gate loop legitimately resets and re-launches. (Post-debug hygiene
# recovery in _restore_safe_banking_display_hygiene resets + re-verifies machine
# LIVENESS only - it never re-issues a debugger launch, so it is not flagged.)
ALLOW_LIST: set = set()


def _call_names(node: ast.AST) -> set:
    """All attribute/function names called anywhere under `node`."""
    names = set()
    for sub in ast.walk(node):
        if isinstance(sub, ast.Call):
            fn = sub.func
            if isinstance(fn, ast.Attribute):
                names.add(fn.attr)
            elif isinstance(fn, ast.Name):
                names.add(fn.id)
            # send_key("G") / send_char("G") style launches
            if isinstance(fn, ast.Attribute) and fn.attr in ("send_key", "send_char"):
                for arg in sub.args:
                    if isinstance(arg, ast.Constant) and arg.value in LAUNCH_KEY_ARGS:
                        names.add(f"__key_{arg.value}")
    return names


def _enclosing_func(path: list) -> str:
    for node in reversed(path):
        if isinstance(node, (ast.FunctionDef, ast.AsyncFunctionDef)):
            return node.name
    return "<module>"


def _find_violations(fname: str, src: str) -> list:
    tree = ast.parse(src)
    violations = []

    # Banned symbols anywhere.
    for sym in BANNED_SYMBOLS:
        if sym in src:
            # Confirm it is a real reference, not only inside this file's own name.
            for node in ast.walk(tree):
                if isinstance(node, ast.Name) and node.id == sym:
                    violations.append(
                        f"{fname}: banned masking symbol {sym!r} reintroduced "
                        f"(line {node.lineno})")
                    break

    # Loops that both reset and launch = reset-retry masking.
    parents: dict = {}
    for node in ast.walk(tree):
        for child in ast.iter_child_nodes(node):
            parents[child] = node

    def path_to(node):
        chain = []
        cur = node
        while cur in parents:
            cur = parents[cur]
            chain.append(cur)
        return list(reversed(chain))

    def is_attempt_loop(loop) -> bool:
        # A retry loop counts attempts: `while ...` or `for _ in range(...)`.
        # A `for x in <collection>` loop iterates distinct test cases, not retries.
        if isinstance(loop, ast.While):
            return True
        it = loop.iter
        return isinstance(it, ast.Call) and isinstance(it.func, ast.Name) \
            and it.func.id == "range"

    def reset_in_except(loop) -> bool:
        for handler in ast.walk(loop):
            if isinstance(handler, ast.ExceptHandler):
                if _call_names(handler) & RESET_CALLS:
                    return True
        return False

    for node in ast.walk(tree):
        if not isinstance(node, (ast.For, ast.While)):
            continue
        names = _call_names(node)
        resets = names & RESET_CALLS
        launches = (names & LAUNCH_CALLS) | {n for n in names if n.startswith("__key_")}
        if not (resets and launches):
            continue
        # Masking = an attempt/retry loop that resets and re-launches, OR any loop
        # that resets inside an except handler around a debugger launch. A plain
        # `for case in cases` iteration (per-case setup + one launch) is not masking.
        if not (is_attempt_loop(node) or reset_in_except(node)):
            continue
        func = _enclosing_func(path_to(node))
        if (fname, func) in ALLOW_LIST:
            continue
        violations.append(
            f"{fname}:{node.lineno}: attempt/except loop in {func}() both resets "
            f"({sorted(resets)}) and launches a debugger op "
            f"({sorted(launches)}) -> reset-retry masking. If legitimate, add "
            f"({fname!r}, {func!r}) to ALLOW_LIST with justification.")
    return violations


class AntiMaskingTest(unittest.TestCase):
    """The gate harnesses must not hide a ROM-entry miss behind a reset-retry."""

    def test_gate_harnesses_have_no_reset_retry_masking(self) -> None:
        violations: list[str] = []
        for fname in GATE_FILES:
            path = HERE / fname
            if not path.exists():
                violations.append(f"{fname}: missing (expected gate harness)")
                continue
            violations.extend(_find_violations(fname, path.read_text(encoding="utf-8")))
        self.assertEqual(violations, [], "\n".join(violations))

    def test_matrix_gate_declares_prohibited_reset_retry_counters(self) -> None:
        for required in ("recovery_reset", "command_retry", "session_replay",
                         "transparent_reset_restore_failure"):
            self.assertIn(required, gate.ResetRetryCounters.PROHIBITED)


# Canonical BASIC $BC0F (FAC copy): LDX #$06 / LDA $60,X / STA $68,X / DEX /
# BNE / STX $70 / RTS. The traversal fixtures call it as their ROM leg.
CANONICAL_BC0F = bytes([0xA2, 0x06, 0xB5, 0x60, 0x95, 0x68, 0xCA, 0xD0, 0xF9,
                        0x86, 0x70, 0x60])


def _region_of(pc: int) -> str:
    if 0xA000 <= pc <= 0xBFFF:
        return "BASIC"
    if pc >= 0xE000:
        return "E000"
    return "RAM"


def _run_fixture(fixture):
    """Execute a fixture in the mcm6502 oracle; return (pc trace, regions, mem)."""
    mem = bytearray(0x10000)
    for addr, data in fixture.chunks:
        mem[addr:addr + len(data)] = data
    mem[0xBC0F:0xBC0F + len(CANONICAL_BC0F)] = CANONICAL_BC0F
    mem[0x0000], mem[0x0001] = 0x2F, 0x37
    cpu = ORC.CPU6502(mem)
    cpu.set_state(0, 0, 0, 0xF8, fixture.entry, 0x24)
    trace, regions = [], []
    for _ in range(4000):
        trace.append(cpu.pc)
        region = _region_of(cpu.pc)
        if not regions or regions[-1] != region:
            regions.append(region)
        cpu.step()
        if mem[fixture.progress] >= 3:
            break
    return trace, regions, mem


class BoundaryTraversalFixtureTest(unittest.TestCase):
    """The boundary-traversal fixtures must be valid 6502 that really crosses
    memory regions. A developer debugs their own RAM program and steps into ROM
    from there, so these fixtures - not the cold ROM bootstrap - model the
    realistic workflow. Checking them here catches a broken fixture in under a
    second instead of halfway through an hour-long hardware matrix run."""

    def test_ram_rom_ram_crosses_into_basic_and_back(self) -> None:
        fixture = gate.build_fixture("ram-rom-ram", 32)
        trace, regions, mem = _run_fixture(fixture)
        self.assertEqual(regions, ["RAM", "BASIC", "RAM"])
        self.assertEqual(mem[fixture.sentinel], 0x77)
        self.assertGreaterEqual(mem[fixture.progress], 3,
                                "Continue liveness needs progress to keep moving")

    def test_ram_rur_rom_ram_crosses_every_region(self) -> None:
        fixture = gate.build_fixture("ram-rur-rom-ram", 32)
        trace, regions, mem = _run_fixture(fixture)
        # A bank switch cannot execute from the window it switches, so the walk
        # returns to RAM between the RAM-under-ROM and visible-ROM legs.
        self.assertEqual(regions,
                         ["RAM", "E000", "RAM", "BASIC", "RAM", "E000", "RAM"])
        self.assertEqual(mem[fixture.sentinel], 0x77)
        self.assertGreaterEqual(mem[fixture.progress], 3)

    def test_every_traversal_pc_is_actually_executed_in_order(self) -> None:
        for mode in gate.TRAVERSAL_MODES:
            with self.subTest(mode=mode):
                fixture = gate.build_fixture(mode, 32)
                trace, _, _ = _run_fixture(fixture)
                want = [pc for _, pc, _ in fixture.traversal]
                index = 0
                for pc in trace:
                    if index < len(want) and pc == want[index]:
                        index += 1
                self.assertEqual(
                    index, len(want),
                    f"{mode}: traversal never reached "
                    f"{[f'${p:04X}' for p in want[index:]]} in order")

    def test_traversal_crosses_a_boundary_in_both_directions(self) -> None:
        for mode in gate.TRAVERSAL_MODES:
            with self.subTest(mode=mode):
                fixture = gate.build_fixture(mode, 32)
                keys = [k for k, _, _ in fixture.traversal]
                self.assertIn("T", keys, "must step INTO another region")
                self.assertIn("U", keys, "must step OUT of it again")

    def test_traversal_modes_are_registered_as_matrix_rows(self) -> None:
        for mode in gate.TRAVERSAL_MODES:
            self.assertIn(mode, gate.MEMORY_MODES)


class FailFastSchedulingTest(unittest.TestCase):
    def test_strict_cell_failure_skips_opcode_volume(self) -> None:
        args = argparse.Namespace(
            fail_fast=False,
            strict=True,
            continue_after_cell_failure=False,
        )

        self.assertTrue(gate.stop_after_cell_failure(args))
        self.assertEqual(
            gate.skipped_opcode_summary("required_cell_failure_fail_fast"),
            {
                "opcode_requirement_status": "FAIL",
                "opcode_count": 0,
                "skipped": True,
                "skip_reason": "required_cell_failure_fail_fast",
            },
        )

    def test_explicit_continue_after_cell_failure_allows_later_lanes(self) -> None:
        args = argparse.Namespace(
            fail_fast=False,
            strict=False,
            continue_after_cell_failure=True,
        )

        self.assertFalse(gate.stop_after_cell_failure(args))


class FixtureOracleSetupTest(unittest.TestCase):
    def test_ram_under_rom_oracle_gets_cpu_port_side_effects(self) -> None:
        fixture = gate.build_fixture("ram-under-rom", 4)
        mem = bytearray([0xFF] * 0x10000)

        gate.apply_fixture_entry_side_effects(mem, fixture)

        self.assertEqual(mem[0x0000], 0x37)
        self.assertEqual(mem[0x0001], 0x35)

    def test_visible_rom_oracle_gets_visible_rom_cpu_port(self) -> None:
        fixture = gate.build_fixture("rom", 1)
        mem = bytearray([0xFF] * 0x10000)

        gate.apply_fixture_entry_side_effects(mem, fixture)

        self.assertEqual(mem[0x0000], 0x37)
        self.assertEqual(mem[0x0001], 0x37)

    def test_plain_ram_oracle_leaves_cpu_port_untouched(self) -> None:
        fixture = gate.build_fixture("ram", 4)
        mem = bytearray([0xFF] * 0x10000)

        gate.apply_fixture_entry_side_effects(mem, fixture)

        self.assertEqual(mem[0x0000], 0xFF)
        self.assertEqual(mem[0x0001], 0xFF)

    def test_visible_rom_oracle_uses_captured_rom_heads(self) -> None:
        mem = bytearray([0xFF] * 0x10000)
        with tempfile.TemporaryDirectory() as tmp:
            cell_dir = Path(tmp)
            (cell_dir / "live-kernal-e000.bin").write_bytes(bytes([0x85, 0x56, 0x20, 0x0F, 0xBC]))
            (cell_dir / "live-basic-bc00.bin").write_bytes(bytes([0xA2, 0x06, 0xB5, 0x60]))

            gate.apply_captured_rom_heads(mem, cell_dir)

        self.assertEqual(mem[0xE000:0xE005], bytes([0x85, 0x56, 0x20, 0x0F, 0xBC]))
        self.assertEqual(mem[0xBC00:0xBC04], bytes([0xA2, 0x06, 0xB5, 0x60]))


class AlertScopeContractTest(unittest.TestCase):
    def test_every_alert_is_single_line_and_fits_38_columns(self) -> None:
        self.assertEqual(gate.validate_debug_alerts(), [])
        for alert in gate.DEBUG_ALERTS:
            self.assertLessEqual(len(alert), 38, alert)
            self.assertNotIn("\n", alert)
            self.assertNotIn("\r", alert)

    def test_exact_canonical_alert_strings_present(self) -> None:
        self.assertIn("Step Into: run to a breakpoint 1st", gate.DEBUG_ALERTS)

    def test_alerts_use_no_corporate_or_dbx_terms(self) -> None:
        problems = gate.validate_debug_alerts(
            gate.DEBUG_ALERTS + ("This is production mode", "Use DbX here"))
        self.assertTrue(any("production" in p for p in problems))
        self.assertTrue(any("DbX" in p for p in problems))

    def test_manual_text_requires_dbg_and_breakpoint_go(self) -> None:
        good = "Dbg breakpoint+Go RAM under ROM"
        self.assertEqual(gate.validate_manual_text(good), [])
        missing = "Dbg only"
        self.assertTrue(gate.validate_manual_text(missing))
        stale = good + " DbX"
        self.assertTrue(gate.validate_manual_text(stale))


if __name__ == "__main__":
    # The runner may pass device options that these host-side checks ignore.
    unittest.main(argv=[sys.argv[0]], verbosity=2)
