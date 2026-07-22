#!/usr/bin/env python3
"""Small harness-only checks for monitor_debug_matrix_gate."""

from __future__ import annotations

import argparse
import tempfile
from pathlib import Path
import unittest

import monitor_debug_matrix_gate as gate


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
    unittest.main()
