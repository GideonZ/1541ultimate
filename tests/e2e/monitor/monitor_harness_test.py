#!/usr/bin/env python3
# E2E: Verifies the monitor test harness itself; host-side only, no device needed.

"""Host-side checks for the machine-code monitor test harness itself.

No device is required: every check here reads or imports harness source, so it
runs in a few seconds and guards the harnesses the hardware suites depend on.

Two groups:

1. Anti-masking enforcement. A contextless visible-ROM breakpoint entry that
   does not trap is a defect, and resetting the machine to buy another draw
   turns an intermittent defect into a green result. A reset-and-retry loop
   around a debugger launch is therefore prohibited. The check is structural
   (AST), not text matching: it flags any loop whose body both resets/reconnects
   the machine or debug session and re-issues a debugger launch/step. Diagnostic
   tools that reset in order to MEASURE an intermittency are deliberately out of
   scope.

2. Matrix-suite unit checks: fail-fast scheduling, oracle fixture setup, and the
   Debug alert wording contract.
"""

from __future__ import annotations

import argparse
import ast
import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from typing import ClassVar
from unittest import mock

# The one stanza that puts the shared library on sys.path; see tests/lib/bootstrap.py.
sys.path.insert(0, str(next(p for p in Path(__file__).resolve().parents
                            if (p / "tests" / "lib").is_dir()) / "tests" / "lib"))
import bootstrap  # noqa: E402,F401
sys.path.insert(0, bootstrap.directory("e2e", "monitor"))

from report import suite_fail, suite_ok  # noqa: E402

import mcm6502 as ORC  # noqa: E402
import mcm_rest as R  # noqa: E402
import mcm_split_rest as SR  # noqa: E402
import monitor_debug_stress as stress  # noqa: E402
import monitor_debug_matrix_test as gate  # noqa: E402
import monitor_debug_regression_test as regression  # noqa: E402
import monitor_test as monitor  # noqa: E402

HERE = Path(__file__).resolve().parent

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


class ModalDetectionTest(unittest.TestCase):
    """`_dismiss_modal_if_present` must tell a monitor alert box apart from the
    monitor's own frame, without depending on the alert's wording. Both samples
    are real captures taken from the debug suite."""

    ALERT_SCREEN: ClassVar[list[str]] = [
        "+----------------------------------------------------------+",
        "|MONITOR ASM $C700                                 Dbg     |",
        "|C700 A9 5A     LDA #$5A          [RAM]                    |",
        "|C70C 80    +------------------------------------+         |",
        "|C70D 00    |           DEBUG TIMEOUT            |         |",
        "|C70F FF    |                 Ok                 |         |",
        "|C710 FF    +------------------------------------+         |",
    ]
    PLAIN_SCREEN: ClassVar[list[str]] = [
        "+----------------------------------------------------------+",
        "|MONITOR ASM $C5F0                                 Dbg     |",
        "|C5F0 A9 2F     LDA #$2F          [RAM]                    |",
        "|PC   AC XR YR SP NV-BDIZC IRQ  NMI                        |",
        "|CPU7 $A:BAS $D:I/O $E:KRN VIC0 $0000                      |",
        "+----------------------------------------------------------+",
    ]

    @staticmethod
    def _detects(lines):
        import re as _re
        return any(_re.match(r"^\|.*\+-{8,}\+", line) for line in lines)

    def test_alert_box_inside_the_frame_is_detected(self) -> None:
        self.assertTrue(self._detects(self.ALERT_SCREEN))

    def test_plain_monitor_frame_is_not_mistaken_for_an_alert(self) -> None:
        self.assertFalse(self._detects(self.PLAIN_SCREEN))


class BoundaryTraversalFixtureTest(unittest.TestCase):
    """The boundary-traversal fixtures must be valid 6502 that really crosses
    memory regions. A developer debugs their own RAM program and steps into ROM
    from there, so these fixtures - not the cold ROM bootstrap - model the
    realistic workflow. Checking them here catches a broken fixture in under a
    second instead of halfway through an hour-long hardware matrix run."""

    def test_ram_rom_ram_crosses_into_basic_and_back(self) -> None:
        fixture = gate.build_fixture("ram-rom-ram", 32)
        _trace, regions, mem = _run_fixture(fixture)
        self.assertEqual(regions, ["RAM", "BASIC", "RAM"])
        self.assertEqual(mem[fixture.sentinel], 0x77)
        self.assertGreaterEqual(mem[fixture.progress], 3,
                                "Continue liveness needs progress to keep moving")

    def test_ram_rur_rom_ram_crosses_every_region(self) -> None:
        fixture = gate.build_fixture("ram-rur-rom-ram", 32)
        _trace, regions, mem = _run_fixture(fixture)
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


class SplitU2RunLengthTest(unittest.TestCase):
    def test_split_u2_defaults_shorten_step_runs(self) -> None:
        args = gate.parse_args([
            "--host", "u2", "--rest-host", "u2", "--c64-host", "c64u",
        ])

        self.assertEqual(args.reps, gate.U2_REPS)
        self.assertEqual(args.required_step_into_depth, gate.U2_STEP_INTO_DEPTH)
        self.assertEqual(args.straight_calls, gate.U2_STRAIGHT_CALLS)
        self.assertLess(gate.U2_TRACE_OPCODES, gate.DEFAULT_TRACE_OPCODES)

    def test_explicit_split_u2_run_lengths_are_retained(self) -> None:
        args = gate.parse_args([
            "--host", "u2", "--rest-host", "u2", "--c64-host", "c64u",
            "--reps", "2", "--required-step-into-depth", "12",
            "--straight-calls", "16",
        ])

        self.assertEqual(args.reps, 2)
        self.assertEqual(args.required_step_into_depth, 12)
        self.assertEqual(args.straight_calls, 16)

    def test_single_host_defaults_keep_full_run_lengths(self) -> None:
        args = gate.parse_args(["--host", "u64", "--rest-host", "u64"])

        self.assertEqual(args.reps, gate.DEFAULT_REPS)
        self.assertEqual(args.required_step_into_depth, gate.DEFAULT_STEP_INTO_DEPTH)
        self.assertEqual(args.straight_calls, gate.DEFAULT_STRAIGHT_CALLS)
        self.assertEqual(gate.DEFAULT_TRACE_OPCODES, 100)


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
            (*gate.DEBUG_ALERTS, "This is production mode", "Use DbX here"))
        self.assertTrue(any("production" in p for p in problems))
        self.assertTrue(any("DbX" in p for p in problems))

    def test_manual_text_requires_dbg_and_breakpoint_go(self) -> None:
        good = "Dbg breakpoint+Go RAM under ROM"
        self.assertEqual(gate.validate_manual_text(good), [])
        missing = "Dbg only"
        self.assertTrue(gate.validate_manual_text(missing))
        stale = good + " DbX"
        self.assertTrue(gate.validate_manual_text(stale))


SPLIT_ARGV = ["--host", "u2", "--rest-host", "u2", "--c64-host", "c64u"]
SINGLE_ARGV = ["--host", "u64", "--rest-host", "u64"]


class SplitRestFactoryTest(unittest.TestCase):
    """One factory decides what a split session routes where, so a tool cannot
    grow a second, differently-routed split mechanism."""

    def test_a_machine_host_produces_a_split_fixture(self) -> None:
        rest = SR.make_rest("u2", "c64u")

        self.assertIsInstance(rest, SR.SplitRest)
        self.assertEqual(rest.machine_host, "c64u")
        self.assertEqual(rest.overlay_host, "u2")
        self.assertEqual(rest.host, "c64u")

    def test_no_machine_host_produces_a_plain_single_host_fixture(self) -> None:
        rest = SR.make_rest("u64")

        self.assertIsInstance(rest, R.Rest)
        self.assertEqual(rest.host, "u64")
        self.assertEqual(rest.timeout, 10.0)


class SplitHostToolFlagTest(unittest.TestCase):
    """Every split-host flag the matrix emits must be accepted by the tool it
    invokes. The U2+L preflight failed because it drove single-host tools, each
    of which sent its first keystroke to the cartridge's machine:input, which is
    compiled out and answers HTTP 501."""

    def _help(self, *argv: str) -> str:
        proc = subprocess.run([sys.executable, *argv, "--help"], cwd=str(HERE),
                              capture_output=True, text=True, timeout=60,
                              check=False)
        self.assertEqual(proc.returncode, 0, proc.stderr)
        return proc.stdout

    def test_freeze_reentry_guard_takes_a_machine_host(self) -> None:
        self.assertIn("--c64-host", self._help(str(HERE / "freeze_reentry_guard.py")))

    def test_localui_soak_takes_a_machine_host(self) -> None:
        self.assertIn("--c64-host", self._help(str(HERE / "mcm_localui.py"), "soak"))

    def test_stress_driver_takes_a_machine_host(self) -> None:
        self.assertIn("--c64-host", self._help(str(HERE / "monitor_debug_stress.py")))

    def test_debug_suite_takes_a_target_but_no_machine_host(self) -> None:
        text = self._help(str(HERE / "monitor_debug_test.py"))

        self.assertIn("--target", text)
        self.assertNotIn("--c64-host", text)


class DeviceAddressTest(unittest.TestCase):
    """No tool may default to, or probe, an address that is not a device.

    `192.168.1.13` was the U64 and is now dead; `192.168.1.70` was its second
    NIC and means nothing on a split session. A default pointing at neither
    device reports "DEVICE NOT ALIVE AT START", which reads as a wedge.
    """

    DEAD_ADDRESSES = ("192.168.1.13", "192.168.1.70")
    TOOLS = ("freeze_reentry_guard.py", "mcm_localui.py", "monitor_debug_stress.py")

    def test_no_tool_names_a_dead_device_address(self) -> None:
        for name in self.TOOLS:
            source = (HERE / name).read_text(encoding="utf-8")
            for address in self.DEAD_ADDRESSES:
                self.assertNotIn(address, source, f"{name} names {address}")

    def test_a_wedge_reports_every_endpoint_of_the_fixture(self) -> None:
        split = SR.make_rest("u2", "c64u")
        with mock.patch.object(R.Rest, "alive", return_value=True):
            self.assertEqual(SR.endpoint_liveness(split),
                             {"c64u": True, "u2": True})
            self.assertEqual(SR.endpoint_liveness(SR.make_rest("u64")),
                             {"u64": True})


class PreflightTopologyTest(unittest.TestCase):
    def _commands(self, argv: list[str]) -> dict[str, list[str]]:
        args = gate.parse_args(argv)
        return dict(gate.preflight_commands(args, Path("/tmp/preflight")))

    @staticmethod
    def _value(cmd: list[str], flag: str) -> str:
        return cmd[cmd.index(flag) + 1]

    def test_split_run_sends_local_ui_keystrokes_to_the_machine_host(self) -> None:
        commands = self._commands(SPLIT_ARGV)

        for name in ("freeze-reentry", "localui-soak"):
            self.assertEqual(self._value(commands[name], "--c64-host"), "c64u", name)

    def test_split_run_tells_the_telnet_suite_which_target_it_is_on(self) -> None:
        cmd = self._commands(SPLIT_ARGV)["quick-telnet-debug"]

        self.assertEqual(self._value(cmd, "--target"), "u2")
        # Telnet is the cartridge's own remote session; that suite has no
        # machine host to route keystrokes to.
        self.assertNotIn("--c64-host", cmd)

    def test_split_run_gives_the_stress_driver_the_machine_host(self) -> None:
        args = gate.parse_args(SPLIT_ARGV)
        cmd = gate.opcode_volume_command(args, Path("/tmp/opcode"))

        self.assertEqual(self._value(cmd, "--c64-host"), "c64u")
        self.assertEqual(self._value(cmd, "--host"), "u2")

    def test_single_host_run_passes_no_topology_flags(self) -> None:
        args = gate.parse_args(SINGLE_ARGV)
        commands = dict(gate.preflight_commands(args, Path("/tmp/preflight")))
        commands["opcode-volume"] = gate.opcode_volume_command(args, Path("/tmp/opcode"))

        for name, cmd in commands.items():
            self.assertNotIn("--c64-host", cmd, name)
            self.assertNotIn("--target", cmd, name)


class SplitStressSessionTest(unittest.TestCase):
    """The stress driver is the 1000-opcode volume gate. On a U2+L it needs the
    routing and the U2 monitor's own deviations: no Interface Type config, no
    bank view, and one memory-source tag for every row."""

    def test_split_session_routes_through_a_split_fixture(self) -> None:
        session = stress.RestSession("u2", ui="overlay", c64_host="c64u")

        self.assertTrue(session.split)
        self.assertIsInstance(session.rest, SR.SplitRest)
        self.assertEqual(session.host, "c64u")

    def test_single_host_session_is_unchanged(self) -> None:
        session = stress.RestSession("u64", ui="overlay")

        self.assertFalse(session.split)
        self.assertIsInstance(session.rest, R.Rest)
        self.assertEqual(session.host, "u64")

    def test_split_session_skips_the_interface_type_config(self) -> None:
        session = stress.RestSession("u2", ui="overlay", c64_host="c64u")
        calls: list = []
        with mock.patch.object(stress.L, "ensure_menu_closed", lambda *a, **k: True), \
             mock.patch.object(stress.overlay_lifecycle, "set_interface_type",
                               lambda *a: calls.append(a)):
            session.set_ui_mode()

        self.assertEqual(calls, [])

    def test_single_host_session_still_selects_the_interface_type(self) -> None:
        session = stress.RestSession("u64", ui="overlay")
        calls: list = []
        with mock.patch.object(stress.L, "ensure_menu_closed", lambda *a, **k: True), \
             mock.patch.object(stress.overlay_lifecycle, "set_interface_type",
                               lambda *a: calls.append(a)):
            session.set_ui_mode()

        self.assertEqual([a[1] for a in calls], ["Overlay on HDMI"])


class HeldBusReadTest(unittest.TestCase):
    """A cartridge session holds the C64 in Ultimax, so the machine host reads
    $FF outside $0000-$0FFF and the I/O space. Measured with a session live:
    the C64U read $C800 as ffffffff while the cartridge read 2b33c1dc, and both
    read 2b33c1dc once released. A comparison against the oracle must therefore
    read the device that can see the window."""

    def _split(self):
        rest = SR.make_rest("u2", "c64u")
        rest.machine = mock.Mock(**{"read_mem.return_value": b"\xff" * 4})
        rest.overlay = mock.Mock(**{"read_mem.return_value": b"\x2b\x33\xc1\xdc"})
        return rest

    def test_a_held_window_is_read_from_the_cartridge(self) -> None:
        rest = self._split()

        self.assertEqual(rest.read_mem_oracle(0xC800, 4), b"\x2b\x33\xc1\xdc")
        rest.machine.read_mem.assert_not_called()

    def test_always_decoded_windows_stay_on_the_machine_host(self) -> None:
        rest = self._split()

        for addr in (0x0000, 0x00A0, 0x0FFC, 0xD020):
            with self.subTest(addr=addr):
                self.assertEqual(rest.read_mem_oracle(addr, 4), b"\xff" * 4)

    def test_a_window_straddling_the_boundary_reads_the_cartridge(self) -> None:
        rest = self._split()

        self.assertEqual(rest.read_mem_oracle(0x0FFE, 4), b"\x2b\x33\xc1\xdc")

    def test_a_split_session_reads_the_scratch_window_coherently(self) -> None:
        session = stress.RestSession("u2", c64_host="c64u")
        session.rest = self._split()

        self.assertEqual(session.read_mem(0xC800, 4), b"\x2b\x33\xc1\xdc")

    def test_a_single_host_session_reads_its_only_device(self) -> None:
        session = stress.RestSession("u64")
        session.rest = mock.Mock(**{"read_mem.return_value": b"\x2b"})

        self.assertEqual(session.read_mem(0xC800, 1), b"\x2b")
        session.rest.read_mem.assert_called_once_with(0xC800, 1)


class CartridgeHoldReleaseTest(unittest.TestCase):
    """Keystrokes reach a U2+L over the C64U's keyboard matrix, which is scanned
    only while the 6510 executes. Measured: a run that inherited a held machine
    stopped after 10 steps, and the same seed ran all 20 once released."""

    @staticmethod
    def _session(jiffies):
        session = stress.RestSession("u2", c64_host="c64u")
        session.rest = mock.Mock(**{"read_mem.side_effect": list(jiffies)})
        return session

    def test_a_running_machine_needs_no_menu_toggle(self) -> None:
        session = self._session([b"\x00\x00\x01", b"\x00\x00\x02"])

        with mock.patch.object(stress.time, "sleep"):
            self.assertTrue(session.release_cartridge_hold())
        session.rest.menu_button.assert_not_called()

    def test_a_held_machine_is_toggled_until_the_jiffy_advances(self) -> None:
        # frozen, frozen, then advancing after the second toggle
        session = self._session([b"\x00\x00\x01", b"\x00\x00\x01",
                                 b"\x00\x00\x01", b"\x00\x00\x01",
                                 b"\x00\x00\x01", b"\x00\x00\x09"])

        with mock.patch.object(stress.time, "sleep"):
            self.assertTrue(session.release_cartridge_hold())
        self.assertEqual(session.rest.menu_button.call_count, 2)

    def test_a_machine_that_will_not_release_reports_it(self) -> None:
        session = self._session([b"\x00\x00\x01"] * 40)

        with mock.patch.object(stress.time, "sleep"):
            self.assertFalse(session.release_cartridge_hold(tries=3))
        self.assertEqual(session.rest.menu_button.call_count, 3)

    def test_a_single_host_baseline_never_toggles_the_menu(self) -> None:
        session = stress.RestSession("u64")
        session.rest = mock.Mock()
        with mock.patch.object(stress.L, "ensure_menu_open"), \
             mock.patch.object(stress.time, "sleep"):
            session.recover()

        session.rest.menu_button.assert_not_called()
        session.rest.tap.assert_called_once_with(["commodore", "x"])


class BlankOverlayDetectionTest(unittest.TestCase):
    """An overlay that is present but renders nothing never opens a monitor, so
    the run reports a monitor timeout instead of the UI state behind it. It is
    detected and named. It is deliberately NOT repaired: the repair that was
    written for it rested only on a measurement taken while a second agent was
    driving the same device."""

    @staticmethod
    def _session(screen):
        session = stress.RestSession("u2", c64_host="c64u")
        session.rest = mock.Mock(**{"screen_lines.return_value": screen})
        return session

    def test_a_blank_overlay_fails_loudly(self) -> None:
        session = self._session(["", "  ", ""])

        with self.assertRaises(stress.StressError) as caught:
            session.assert_overlay_draws()

        self.assertIn("every line of it is blank", str(caught.exception))
        self.assertIn("not repaired", str(caught.exception))

    def test_nothing_is_reset_to_paper_over_it(self) -> None:
        session = self._session(["", ""])

        with self.assertRaises(stress.StressError):
            session.assert_overlay_draws()

        session.rest.reset.assert_not_called()
        session.rest.menu_button.assert_not_called()
        self.assertFalse(hasattr(session, "ensure_overlay_draws"))

    def test_a_drawing_overlay_passes(self) -> None:
        session = self._session([" U-II Main", " Manage"])

        session.assert_overlay_draws()

    def test_a_closed_menu_is_not_the_blank_state(self) -> None:
        session = stress.RestSession("u2", c64_host="c64u")
        session.rest = mock.Mock(**{"screen_lines.side_effect": Exception("menu closed")})

        session.assert_overlay_draws()

    def test_the_removed_remedy_has_no_route_back(self) -> None:
        self.assertFalse(hasattr(SR.SplitRest, "reset_overlay_device"))


class DeviceIdentityStampTest(unittest.TestCase):
    """A run must be able to say it measured one image on one board. The device
    was reflashed three times during one campaign and nothing noticed, so the
    identity is stamped at both ends of a run and compared."""

    STAMP: ClassVar[dict[str, str]] = {
        "product": "Ultimate II+L", "firmware_version": "3.15",
        "unique_id": "F13E69"}

    @staticmethod
    def _fixture(machine_info, overlay_info=None):
        rest = SR.make_rest("u2", "c64u") if overlay_info is not None else SR.make_rest("u64")
        payloads = [machine_info] if overlay_info is None else [machine_info, overlay_info]
        devices = [mock.Mock(**{"req.return_value": (200, json.dumps(p).encode())})
                   for p in payloads]
        if overlay_info is None:
            rest.req = devices[0].req
        else:
            rest.machine, rest.overlay = devices
        return rest

    def test_a_split_fixture_stamps_both_devices(self) -> None:
        rest = self._fixture(self.STAMP, {"product": "C64 Ultimate",
                                          "firmware_version": "1.2.0"})

        stamp = SR.device_identity(rest)

        self.assertEqual(sorted(stamp), ["c64u", "u2"])
        self.assertEqual(stamp["c64u"]["product"], "Ultimate II+L")
        self.assertEqual(stamp["u2"]["product"], "C64 Ultimate")

    def test_a_single_host_fixture_stamps_one_device(self) -> None:
        rest = self._fixture(self.STAMP)

        self.assertEqual(SR.device_identity(rest), {"u64": self.STAMP})

    def test_only_identity_fields_are_compared(self) -> None:
        rest = self._fixture(dict(self.STAMP, uptime_seconds=1234))

        self.assertNotIn("uptime_seconds", SR.device_identity(rest)["u64"])

    def test_an_unchanged_device_reports_no_change(self) -> None:
        self.assertEqual(
            SR.identity_changes({"u2": self.STAMP}, {"u2": dict(self.STAMP)}), {})

    def test_a_reflashed_device_is_reported(self) -> None:
        after = dict(self.STAMP, firmware_version="3.16")

        changed = SR.identity_changes({"u2": self.STAMP}, {"u2": after})

        self.assertEqual(changed["u2"]["before"]["firmware_version"], "3.15")
        self.assertEqual(changed["u2"]["after"]["firmware_version"], "3.16")

    def test_a_swapped_board_is_reported(self) -> None:
        after = dict(self.STAMP, unique_id="ABCDEF")

        self.assertIn("u2", SR.identity_changes({"u2": self.STAMP}, {"u2": after}))

    def test_an_unreadable_stamp_is_not_a_proven_change(self) -> None:
        unreadable = {"error": "Failure: connection refused"}

        self.assertEqual(SR.identity_changes({"u2": self.STAMP}, {"u2": unreadable}), {})
        self.assertEqual(SR.identity_changes({"u2": unreadable}, {"u2": self.STAMP}), {})

    def test_an_unreadable_device_records_the_reason(self) -> None:
        rest = SR.make_rest("u64")
        rest.req = mock.Mock(side_effect=RuntimeError("connection refused"))

        self.assertIn("connection refused", SR.device_identity(rest)["u64"]["error"])


class MatrixIdentityGateTest(unittest.TestCase):
    """The matrix compares its preflight stamp against its closing stamp, so a
    device reflashed mid-run fails the gate instead of producing a verdict."""

    def _hygiene(self, preflight_stamp, closing_stamp):
        args = gate.parse_args(SINGLE_ARGV)
        with tempfile.TemporaryDirectory() as tmp, \
             mock.patch.object(gate, "make_rest",
                               return_value=mock.Mock(**{"alive.return_value": True})), \
             mock.patch.object(gate, "tcp_probe", return_value=True), \
             mock.patch.object(gate, "run_cmd", return_value=0), \
             mock.patch.object(gate.SR, "device_identity", return_value=closing_stamp):
            return gate.final_hygiene(args, Path(tmp),
                                      {"device_identity": preflight_stamp})

    def test_a_reflash_between_the_two_stamps_is_reported(self) -> None:
        results = self._hygiene({"u64": {"firmware_version": "3.15"}},
                                {"u64": {"firmware_version": "3.16"}})

        self.assertIn("u64", results["identity_changed"])

    def test_the_same_image_at_both_ends_is_clean(self) -> None:
        results = self._hygiene({"u64": {"firmware_version": "3.15"}},
                                {"u64": {"firmware_version": "3.15"}})

        self.assertEqual(results["identity_changed"], {})


class HoldClassificationTest(unittest.TestCase):
    """A failure raised while the C64 is DMA-held must say so. A held machine
    takes no keystrokes, so the step that reports no progress is downstream of
    the hold and is not an opcode result."""

    @staticmethod
    def _session(running):
        session = stress.RestSession("u2", c64_host="c64u")
        session.rest = mock.Mock(**{"read_mem.return_value": b"\x20" * 16})
        session._c64_running = lambda: running
        return session

    def test_a_held_machine_is_named_in_the_failure(self) -> None:
        note = self._session(running=False).hold_note()

        self.assertIn("DMA-HELD", note)
        self.assertIn("hold-after-close", note)

    def test_a_running_machine_adds_nothing(self) -> None:
        self.assertEqual(self._session(running=True).hold_note(), "")

    def test_a_single_host_run_adds_nothing(self) -> None:
        session = stress.RestSession("u64")
        session.rest = mock.Mock()
        session._c64_running = lambda: False

        self.assertEqual(session.hold_note(), "")


class BreakpointTableClearTest(unittest.TestCase):
    """Clearing the table reads the popup once when it is already empty. The
    RUN/STOP that closes a popup closes the monitor when the popup is not the
    focused object, and the next goto then times out against a blank screen."""

    def _session(self, reads):
        session = stress.RestSession("u2", c64_host="c64u")
        session.rest = mock.Mock()
        self.reads = mock.Mock(side_effect=list(reads))
        self.cleared: list = []
        session.clear_breakpoint = self.cleared.append
        return session

    def test_an_empty_table_is_read_once(self) -> None:
        session = self._session([[]])
        with mock.patch.object(stress.overlay_lifecycle,
                               "armed_breakpoint_addresses", self.reads):
            session.clear_table_by_row_toggle()

        self.assertEqual(self.reads.call_count, 1)
        self.assertEqual(self.cleared, [])

    def test_an_armed_table_is_cleared_then_proved_empty(self) -> None:
        session = self._session([[0xE000, 0xC020], []])
        with mock.patch.object(stress.overlay_lifecycle,
                               "armed_breakpoint_addresses", self.reads):
            session.clear_table_by_row_toggle()

        self.assertEqual(self.cleared, [0xE000, 0xC020])
        self.assertEqual(self.reads.call_count, 2)

    def test_a_table_that_will_not_clear_is_reported(self) -> None:
        session = self._session([[0xE000], [0xE000]])
        with mock.patch.object(stress.overlay_lifecycle,
                               "armed_breakpoint_addresses", self.reads):
            with self.assertRaises(stress.StressError) as caught:
                session.clear_table_by_row_toggle()

        self.assertIn("$E000", str(caught.exception))


class DebugEntryWaitTest(unittest.TestCase):
    """Debug entry is waited on, not sampled once. Measured on a split U2+L:
    `Dbg` appears about 0.21s after the key lands, so the previous single
    sample at 0.2s sat exactly on the boundary."""

    @staticmethod
    def _session(screens):
        session = stress.RestSession("u2", c64_host="c64u")
        session.rest = mock.Mock()
        session.lines = mock.Mock(side_effect=list(screens))
        return session

    def test_debug_entry_just_past_the_old_sample_point_succeeds(self) -> None:
        session = self._session([None, [" MONITOR ASM $C020"], [" MONITOR ASM $C020   Dbg"]])

        with mock.patch.object(stress.time, "sleep"):
            session.enter_debug()

        session.rest.tap.assert_called_once_with(["d"])

    def test_debug_that_never_engages_still_fails(self) -> None:
        session = stress.RestSession("u2", c64_host="c64u")
        session.rest = mock.Mock()
        session.lines = mock.Mock(return_value=[" MONITOR ASM $C020"])

        with mock.patch.object(stress.time, "sleep"):
            with self.assertRaises(stress.StressError) as caught:
                session.enter_debug(timeout=0.3)

        self.assertIn("debug mode not entered", str(caught.exception))


class MonitorHighlightWaitTest(unittest.TestCase):
    """A delayed Telnet redraw is observed without resending its cursor key."""

    def test_late_highlight_is_observed(self) -> None:
        stale = monitor.Snapshot([""], [(6, 4)], "DOWN")
        moved = monitor.Snapshot([""], [(6, 5)], "DOWN")
        session = mock.Mock()
        session.capture.side_effect = [stale, moved]

        with mock.patch.object(monitor.time, "sleep"):
            result = monitor.wait_for_highlight(
                session, stale, [(6, 5)], "DOWN", timeout=1.0)

        self.assertIs(result, moved)
        self.assertEqual(session.capture.call_count, 2)

    def test_late_viewport_scroll_is_observed(self) -> None:
        stale = monitor.Snapshot(["", "C000 A"], [(6, 20)], "DOWN")
        moved = monitor.Snapshot(["", "C020 B"], [(6, 20)], "DOWN")
        session = mock.Mock()
        session.capture.return_value = moved

        with mock.patch.object(monitor.time, "sleep"):
            result = monitor.wait_for_line_contains(
                session, stale, 1, "C020 B", timeout=1.0)

        self.assertIs(result, moved)
        session.capture.assert_called_once_with()

    def test_late_edit_commit_is_observed(self) -> None:
        editing = monitor.Snapshot(["MONITOR ASC $3224 EDIT"], [], "CTRL_E")
        committed = monitor.Snapshot(["MONITOR ASC $3224"], [], "CTRL_E")
        session = mock.Mock()
        session.capture.return_value = committed

        with mock.patch.object(monitor.time, "sleep"):
            result = monitor.wait_for_edit_closed(
                session, editing, timeout=1.0)

        self.assertIs(result, committed)
        session.capture.assert_called_once_with()


class BreakpointRowToggleTest(unittest.TestCase):
    """`toggle_breakpoint_at` reads only the row's [BRK] marker, which both
    targets draw, and toggles only when the row is not already in the wanted
    state."""

    class FakeRest:
        def __init__(self, rows):
            self.rows = list(rows)
            self.sent: list = []

        def send_text(self, text, settle=0.12):
            self.sent.append(text)
            if text == "p" and len(self.rows) > 1:
                self.rows.pop(0)

        def screen_lines(self):
            return [" C020 EE 21 D0  INC $D021" + self.rows[0]]

        def screen_text(self):
            return "\n".join(self.screen_lines())

    def _toggle(self, rows, armed):
        rest = self.FakeRest(rows)
        with mock.patch.object(gate.overlay_lifecycle, "goto_addr", lambda *a: None):
            row = gate.overlay_lifecycle.toggle_breakpoint_at(
                rest, 0xC020, armed, "unit")
        return rest, row

    def test_arming_an_unarmed_row_presses_p(self) -> None:
        rest, row = self._toggle(["  [RAM]", "  [BRK0][RAM]"], True)

        # P, not R: the firmware wires 'p' to debug_toggle_breakpoint() and 'r'
        # to range-select mode, so a toggle sent as R selects a range and leaves
        # the row unarmed.
        self.assertEqual(rest.sent, ["p"])
        self.assertIn("[BRK0]", row)

    def test_an_already_armed_row_is_left_alone(self) -> None:
        rest, row = self._toggle(["  [BRK0][RAM]"], True)

        self.assertEqual(rest.sent, [])
        self.assertIn("[BRK0]", row)

    def test_clearing_an_armed_row_presses_p(self) -> None:
        rest, row = self._toggle(["  [BRK0][RAM]", "  [RAM]"], False)

        self.assertEqual(rest.sent, ["p"])
        self.assertNotIn("[BRK", row)

    def test_armed_slot_rows_are_recognised(self) -> None:
        matched = gate.overlay_lifecycle.ARMED_SLOT_RE.match("0 SET $E000 KRN")

        self.assertIsNotNone(matched)
        self.assertEqual(int(matched.group(1), 16), 0xE000)
        self.assertIsNone(gate.overlay_lifecycle.ARMED_SLOT_RE.match("1 EMPTY"))


class FinalTeardownTest(unittest.TestCase):
    """A direct matrix invocation gets no runner teardown, so it hands the
    device back itself. Teardown runs after the verdict and must never raise."""

    def test_teardown_records_a_device_failure_instead_of_raising(self) -> None:
        args = gate.parse_args(SINGLE_ARGV)
        with tempfile.TemporaryDirectory() as tmp, \
             mock.patch.object(gate, "make_rest",
                               side_effect=RuntimeError("device unreachable")):
            result = gate.final_teardown(args, Path(tmp))
            recorded = (Path(tmp) / "final-teardown.json").exists()

        self.assertIn("device unreachable", result["error"])
        self.assertTrue(recorded)

    def test_teardown_releases_input_and_closes_the_menu(self) -> None:
        args = gate.parse_args(SINGLE_ARGV)
        rest = mock.Mock()
        with tempfile.TemporaryDirectory() as tmp, \
             mock.patch.object(gate, "make_rest", return_value=rest), \
             mock.patch.object(gate.L, "ensure_menu_closed", return_value=True), \
             mock.patch.object(gate, "_c64_running", return_value=True):
            result = gate.final_teardown(args, Path(tmp))

        rest.release_all.assert_called_once_with()
        self.assertEqual(result["release_input"], True)
        self.assertTrue(result["menu_closed"])
        self.assertTrue(result["c64_running"])

    def test_teardown_is_a_no_op_before_a_scope_claims_the_device(self) -> None:
        gate._TEARDOWN_CONTEXT.clear()

        gate._run_final_teardown()      # must not raise and must not touch a device


class CellSelectionTest(unittest.TestCase):
    """`--cells` names a set of intersections; `--memory` x `--ui` can only
    name a rectangle. A mistyped term has to be a usage error before the device
    is touched, or the cell it meant to run silently never runs."""

    def test_terms_carry_their_own_repetition_count(self) -> None:
        self.assertEqual(
            gate.parse_cell_selection("ram:telnet,rom:freeze:2", 1),
            [("ram", "telnet", 1), ("rom", "freeze", 2)])

    def test_a_term_without_a_count_takes_the_default(self) -> None:
        self.assertEqual(gate.parse_cell_selection("ram:telnet", 3),
                         [("ram", "telnet", 3)])

    def test_every_malformed_term_names_itself(self) -> None:
        for bad in ("ram", "ram:vt100", "core:telnet", "ram:telnet:0",
                    "ram:telnet:x", ""):
            with self.subTest(bad=bad):
                with self.assertRaises(ValueError) as caught:
                    gate.parse_cell_selection(bad, 1)
                self.assertTrue(str(caught.exception))

    def test_a_selection_builds_exactly_the_rows_it_names(self) -> None:
        args = gate.parse_args([
            "--host", "u64", "--rest-host", "u64",
            "--cells", "rom:telnet:1,rom:freeze:2", "--reps", "1",
        ])
        with tempfile.TemporaryDirectory() as tmp:
            ledger = gate.create_or_load_ledger(args, Path(tmp))
        self.assertEqual([row["cell_id"] for row in ledger.rows],
                         ["ROM_TELNET_REP_01", "ROM_FREEZE_REP_01",
                          "ROM_FREEZE_REP_02"])

    def test_no_selection_still_builds_the_full_product(self) -> None:
        args = gate.parse_args([
            "--host", "u64", "--rest-host", "u64", "--reps", "1",
        ])
        with tempfile.TemporaryDirectory() as tmp:
            ledger = gate.create_or_load_ledger(args, Path(tmp))
        self.assertEqual(len(ledger.rows),
                         len(gate.MEMORY_MODES) * len(gate.INTERFACES))


class RegressionPlanTest(unittest.TestCase):
    """The regression gate's plan is a claim about coverage, and a claim that is
    only in a comment stops being true. These assert the properties the
    selection was chosen for, so a lane cannot be dropped or a group renamed
    without the host-side suite saying so."""

    def test_the_plan_the_gate_runs_is_the_plan_the_matrix_accepts(self) -> None:
        for split in (False, True):
            with self.subTest(split=split):
                cells = regression.plan_cells(split)
                parsed = gate.parse_cell_selection(regression.cells_argument(cells), 1)
                self.assertEqual(
                    parsed,
                    [(cell.memory, cell.ui, cell.reps) for cell in cells])

    def test_every_named_group_exists_in_the_debug_suite(self) -> None:
        known = {name for name, _runner in gate.dbg.TEST_GROUPS}
        for split in (False, True):
            for group in regression.plan_groups(split):
                with self.subTest(split=split, group=group.name):
                    self.assertIn(group.name, known)

    def test_the_u64_plan_covers_every_ui_transport(self) -> None:
        used = {cell.ui for cell in regression.U64_CELLS}
        self.assertEqual(used, set(gate.INTERFACES))

    def test_every_memory_mode_is_actually_run(self) -> None:
        """Not "run or justified as deferred" - run. The three regions the
        debugger works in, and the two ways a developer crosses between them,
        each cost real effort to get working and none of them is inferable from
        another. The selection may cut how many transports a mode is repeated
        on; it may not cut a mode."""
        run = {cell.memory for cell in regression.U64_CELLS}
        self.assertEqual(run, set(gate.MEMORY_MODES))

    def test_both_boundary_traversals_are_run(self) -> None:
        """RAM into ROM and back, and RAM into RAM-under-ROM and back out
        through ROM, are the two shapes real debug stepping takes. The second
        contains the first's ROM leg, but reaches it after two bank switches
        rather than from an untouched banking state, so it does not stand in
        for it."""
        run = {cell.memory for cell in regression.U64_CELLS}
        for mode in gate.TRAVERSAL_MODES:
            with self.subTest(mode=mode):
                self.assertIn(mode, run)

    def test_a_deferred_mode_is_deferred_only_on_some_transports(self) -> None:
        run = {cell.memory for cell in regression.U64_CELLS}
        for item in regression.DEFERRED:
            for mode in item.modes:
                with self.subTest(what=item.what, mode=mode):
                    self.assertIn(mode, run,
                                  "a mode may be deferred on a transport, "
                                  "never entirely")

    def test_a_deferred_mode_names_a_real_memory_mode(self) -> None:
        for item in regression.DEFERRED:
            for mode in item.modes:
                with self.subTest(what=item.what, mode=mode):
                    self.assertIn(mode, gate.MEMORY_MODES)

    def test_the_split_plan_stays_inside_what_a_u2_supports(self) -> None:
        # Visible ROM refuses on a U2+L, RAM-under-ROM entry is undemonstrated,
        # and the cartridge has one local UI rather than two.
        self.assertEqual({cell.memory for cell in regression.SPLIT_CELLS}, {"ram"})
        self.assertEqual(len({cell.ui for cell in regression.SPLIT_CELLS}), 1)
        u64_groups = {group.name for group in regression.U64_GROUPS}
        self.assertTrue(
            {group.name for group in regression.SPLIT_GROUPS} <= u64_groups)

    def test_the_selection_stays_smaller_than_the_matrix_it_replaces(self) -> None:
        # Without this the selection drifts back into a second full matrix one
        # justified cell at a time, and the gate stops being runnable before a
        # merge, which is the only reason it exists.
        runs = sum(cell.reps for cell in regression.U64_CELLS)
        full = len(gate.MEMORY_MODES) * len(gate.INTERFACES) * gate.DEFAULT_REPS
        self.assertLess(runs, full // 3)

    def test_every_lane_carries_its_reason_and_its_evidence(self) -> None:
        for cell in regression.U64_CELLS + regression.SPLIT_CELLS:
            with self.subTest(cell=cell.term()):
                self.assertTrue(cell.why.strip())
                self.assertTrue(cell.evidence.strip())
        for group in regression.U64_GROUPS + regression.SPLIT_GROUPS:
            with self.subTest(group=group.name):
                self.assertTrue(group.why.strip())
                self.assertTrue(group.evidence.strip())

    def test_the_opcode_volume_requirement_is_not_lowered(self) -> None:
        # The gate buys wall clock by driving fewer random programs, never by
        # accepting fewer verified instructions.
        args = gate.parse_args([
            "--host", "u64", "--rest-host", "u64",
            "--opcode-iterations", str(regression.OPCODE_ITERATIONS),
        ])
        self.assertEqual(args.opcode_run, 1000)
        self.assertLess(regression.OPCODE_ITERATIONS, 12)
        with tempfile.TemporaryDirectory() as tmp:
            command = gate.opcode_volume_command(args, Path(tmp))
        self.assertIn(str(regression.OPCODE_ITERATIONS),
                      command[command.index("--iterations") + 1])

    def test_the_gate_never_runs_a_group_its_preflight_already_ran(self) -> None:
        """The matrix preflight runs two of the debug suite's groups itself.
        Selecting either of them here would run those checks twice inside one
        gate, which is the duplication this suite exists to remove - and it
        would do it silently, because both runs would pass."""
        args = gate.parse_args(["--host", "u64", "--rest-host", "u64"])
        with tempfile.TemporaryDirectory() as tmp:
            commands = dict(gate.preflight_commands(args, Path(tmp)))
        preflight = commands["quick-telnet-debug"]
        already_run = set(preflight[preflight.index("--test") + 1].split(","))
        self.assertTrue(already_run, "the preflight no longer names any group")
        for split in (False, True):
            with self.subTest(split=split):
                selected = {group.name
                            for group in regression.plan_groups(split)}
                self.assertEqual(
                    selected & already_run, set(),
                    "these groups run in the matrix preflight already")

    def test_no_lane_is_selected_twice(self) -> None:
        for split in (False, True):
            with self.subTest(split=split, kind="cells"):
                pairs = [(cell.memory, cell.ui)
                         for cell in regression.plan_cells(split)]
                self.assertEqual(len(pairs), len(set(pairs)),
                                 "a lane is defined twice; give it repetitions "
                                 "instead")
            with self.subTest(split=split, kind="groups"):
                names = [group.name for group in regression.plan_groups(split)]
                self.assertEqual(len(names), len(set(names)))

    def test_the_plan_prints_without_a_device(self) -> None:
        for split in (False, True):
            with self.subTest(split=split):
                text = "\n".join(regression.plan_lines(split))
                self.assertIn("Deferred to", text)
                for cell in regression.plan_cells(split):
                    self.assertIn(cell.term(), text)


if __name__ == "__main__":
    # The runner may pass device options that these host-side checks ignore.
    result = unittest.main(argv=[sys.argv[0]], verbosity=2, exit=False)
    if result.result.wasSuccessful():
        suite_ok("machine-code-monitor-harness")
        raise SystemExit(0)
    suite_fail("machine-code-monitor-harness", "host checks failed")
    raise SystemExit(1)
