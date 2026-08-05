#!/usr/bin/env python3
"""Regression tests for the final-firmware size build guard."""

import contextlib
import io
import pathlib
import sys
import tempfile
import unittest
from unittest import mock

sys.path.insert(0, str(pathlib.Path(__file__).parent))

import app_space


class AppSpaceTest(unittest.TestCase):
    def test_uses_supplied_firmware_limits(self):
        self.assertEqual(
            {
                variant: details["limit_bytes"]
                for variant, details in app_space.FIRMWARE_VARIANTS.items()
            },
            {
                "u2": 0xC6000,
                "u2plus": 0x140000,
                "u2pl": 0x160000,
                "u64": 0x170000,
                "u64e2_50t": 0x1E0000,
                "u64e2_100t": 0x1C0000,
            },
        )

    def test_build_checks_all_final_firmware_artifacts(self):
        makefile = pathlib.Path(__file__).parents[1] / "Makefile"
        source = makefile.read_text()

        for check in (
            "u2 target/u2/riscv/ultimate/result/ultimate.bin",
            "u2plus target/u2plus/nios/ultimate/result/ultimate.app",
            "u2pl target/u2plus_L/riscv/ultimate/result/ultimate.app",
            "u64 target/u64/nios2/ultimate/result/ultimate.app",
            "u64e2_50t target/u64ii/riscv/ultimate/result/ultimate.app",
            "u64e2_100t target/u64ii/riscv/ultimate/result/ultimate.app",
        ):
            self.assertIn("$(FIRMWARE_SIZE_CHECK) " + check, source)

    def test_github_actions_forwards_warning_context_to_builds(self):
        workflow = pathlib.Path(__file__).parents[1] / ".github/workflows/build.yml"

        self.assertIn("docker run --rm -e GITHUB_ACTIONS", workflow.read_text())

    def test_comfortably_below_limit_passes(self):
        report = app_space.FirmwareSizeReport("u2", 100, 1000)

        self.assertEqual(report.remaining_bytes, 900)
        self.assertEqual(report.status, "PASS")
        self.assertEqual(report.exit_status, 0)
        self.assertIn("variant=U2", report.message())
        self.assertIn("actual=0x64", report.message())
        self.assertIn("limit=0x3E8", report.message())
        self.assertIn("remaining=900 bytes", report.message())
        self.assertIn("percentage=90.00%", report.message())
        self.assertIn("status=PASS", report.message())

    def test_exactly_one_percent_remaining_passes_without_warning(self):
        report = app_space.FirmwareSizeReport("u2", 99, 100)

        self.assertEqual(report.status, "PASS")
        self.assertEqual(report.exit_status, 0)

    def test_less_than_one_percent_remaining_warns(self):
        report = app_space.FirmwareSizeReport("u2", 1000, 1009)

        self.assertEqual(report.status, "WARNING")
        self.assertEqual(report.exit_status, 0)

    def test_exactly_at_limit_warns(self):
        report = app_space.FirmwareSizeReport("u2", 1000, 1000)

        self.assertEqual(report.status, "WARNING")
        self.assertEqual(report.exit_status, 0)

    def test_one_byte_over_limit_fails(self):
        report = app_space.FirmwareSizeReport("u2", 1001, 1000)

        self.assertEqual(report.status, "FAIL")
        self.assertEqual(report.exit_status, 1)

    def test_missing_artifact_fails_clearly(self):
        missing = pathlib.Path("does-not-exist")
        stderr = io.StringIO()

        with contextlib.redirect_stderr(stderr):
            exit_status = app_space.main(["u2", str(missing)])

        self.assertEqual(exit_status, 2)
        self.assertIn("expected firmware artifact for U2 was not produced", stderr.getvalue())

    def test_main_returns_report_exit_status(self):
        with tempfile.TemporaryDirectory() as directory:
            artifact = pathlib.Path(directory) / "firmware.bin"
            artifact.write_bytes(b"x" * (0xC6000 + 1))

            self.assertEqual(app_space.main(["u2", str(artifact)]), 1)

    def test_github_actions_emits_a_native_warning(self):
        with tempfile.TemporaryDirectory() as directory:
            artifact = pathlib.Path(directory) / "firmware.bin"
            artifact.write_bytes(b"x" * 0xC6000)
            stdout = io.StringIO()

            with mock.patch.dict("os.environ", {"GITHUB_ACTIONS": "true"}):
                with contextlib.redirect_stdout(stdout):
                    self.assertEqual(app_space.main(["u2", str(artifact)]), 0)

        self.assertIn("::warning::U2 has less than 1%", stdout.getvalue())


if __name__ == "__main__":
    unittest.main()
