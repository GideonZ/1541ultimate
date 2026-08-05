#!/usr/bin/env python3
"""Regression tests for the firmware application-space build guard."""

import pathlib
import sys
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).parent))

import app_space


class AppSpaceTest(unittest.TestCase):
    def test_uses_application_partition_limits(self):
        self.assertEqual(app_space.application_limit_bytes("u2"), 0xC6000)
        self.assertEqual(app_space.application_limit_bytes("u2plus"), 0x140000)
        self.assertEqual(app_space.application_limit_bytes("u2pl"), 0x160000)
        self.assertEqual(app_space.application_limit_bytes("u64"), 0x170000)
        self.assertEqual(app_space.application_limit_bytes("u64ii"), 0x1C0000)

    def test_u2_guard_checks_the_flashed_binary(self):
        makefile = pathlib.Path(__file__).parents[1] / "Makefile"
        source = makefile.read_text()

        self.assertIn(
            "$(APP_SPACE_CHECK) u2 target/u2/riscv/ultimate/result/ultimate.bin",
            source,
        )
        self.assertNotIn(
            "$(APP_SPACE_CHECK) u2 target/u2/riscv/ultimate/result/ultimate.app",
            source,
        )

    def test_reports_remaining_space_in_kib(self):
        report = app_space.check_application_space("u64", 0x170000 - 100 * 1024)

        self.assertEqual(report.remaining_bytes, 100 * 1024)
        self.assertIn("100.0 KiB remaining", report.message())
        self.assertFalse(report.is_below_warning_threshold)

    def test_fails_only_below_five_percent_remaining(self):
        limit = app_space.application_limit_bytes("u2plus")
        threshold = limit * 5 // 100

        self.assertFalse(
            app_space.check_application_space("u2plus", limit - threshold)
            .is_below_warning_threshold
        )
        self.assertTrue(
            app_space.check_application_space("u2plus", limit - threshold + 1)
            .is_below_warning_threshold
        )

    def test_warning_identifies_the_threshold_as_such(self):
        report = app_space.check_application_space("u2", 0xC6000)

        self.assertIn("5% is 39.6 KiB", report.warning_message())


if __name__ == "__main__":
    unittest.main()
