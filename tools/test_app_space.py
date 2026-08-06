#!/usr/bin/env python3
"""Regression tests for the firmware application space guard."""

import contextlib
import io
import pathlib
import re
import sys
import tempfile
import unittest
from unittest import mock

sys.path.insert(0, str(pathlib.Path(__file__).parent))

import app_space


REPO_ROOT = pathlib.Path(__file__).resolve().parents[1]

# Variant -> the flash partition table in software/io/flash/w25q_flash.cc it is budgeted from.
FLASH_TABLES = {
    "u2": "flash_addresses",
    "u2plus": "flash_addresses_u2p",
    "u2pl": "flash_addresses_u2pl",
    "u64": "flash_addresses_u64",
    "u64e2_50t": "flash_addresses_u64ii_50t",
    "u64e2_100t": "flash_addresses_u64ii_100t",
}


def application_sizes_from_flash_tables():
    source = (REPO_ROOT / "software/io/flash/w25q_flash.cc").read_text()
    sizes = {}
    for table, body in re.findall(
        r"t_flash_address (\w+)\[\] = \{(.*?)\};", source, re.DOTALL
    ):
        entry = re.search(r"FLASH_ID_APPL,([^\n]*)", body)
        if entry:
            # The partition size is the last field of the entry.
            sizes[table] = int(re.findall(r"0x[0-9A-Fa-f]+", entry.group(1))[-1], 16)
    return sizes


def report_for(actual_bytes, limit_bytes=1000, variant="u2"):
    return app_space.FirmwareSizeReport(variant, actual_bytes, limit_bytes)


class LimitTest(unittest.TestCase):
    def test_limits_match_the_flash_partition_tables(self):
        sizes = application_sizes_from_flash_tables()

        self.assertEqual(len(sizes), 6)
        for variant, table in FLASH_TABLES.items():
            self.assertEqual(
                app_space.FIRMWARE_VARIANTS[variant]["limit_bytes"],
                sizes[table],
                "%s limit differs from %s in w25q_flash.cc" % (variant, table),
            )

    def test_artifact_paths_are_repo_relative_and_plausible(self):
        for variant, details in app_space.FIRMWARE_VARIANTS.items():
            artifact = pathlib.Path(details["artifact"])

            self.assertFalse(artifact.is_absolute(), variant)
            self.assertEqual(artifact.parent.name, "result", variant)
            self.assertTrue(
                (REPO_ROOT / artifact.parent.parent).is_dir(),
                "%s build directory is missing: %s" % (variant, artifact),
            )


class StatusTest(unittest.TestCase):
    def test_comfortably_below_limit_passes(self):
        report = report_for(100)

        self.assertEqual(report.remaining_bytes, 900)
        self.assertEqual(report.status, app_space.PASS)
        self.assertEqual(report.exit_status, 0)
        self.assertAlmostEqual(report.free_percent(), 90.0)

    def test_exactly_one_percent_remaining_passes_without_warning(self):
        report = report_for(99, limit_bytes=100)

        self.assertEqual(report.status, app_space.PASS)
        self.assertEqual(report.exit_status, 0)

    def test_less_than_one_percent_remaining_warns(self):
        report = report_for(1000, limit_bytes=1009)

        self.assertEqual(report.status, app_space.WARNING)
        self.assertEqual(report.exit_status, 0)
        self.assertIn("9 bytes (0.89%) of application space left", report.warning_message())

    def test_exactly_at_limit_warns(self):
        report = report_for(1000)

        self.assertEqual(report.status, app_space.WARNING)
        self.assertEqual(report.exit_status, 0)

    def test_one_byte_over_limit_fails(self):
        report = report_for(1001)

        self.assertEqual(report.status, app_space.FAIL)
        self.assertEqual(report.exit_status, 1)
        self.assertIn("U2 is 1 bytes over", report.failure_message())

    def test_unbuilt_artifact_is_reported_without_failing(self):
        report = report_for(None)

        self.assertEqual(report.status, app_space.MISSING)
        self.assertEqual(report.exit_status, 0)


class RenderTest(unittest.TestCase):
    def plain_renderer(self):
        renderer = app_space.Renderer(io.StringIO())
        renderer.color = False
        renderer.unicode = False
        return renderer

    def test_row_columns_line_up_with_the_header(self):
        renderer = self.plain_renderer()

        for report in (report_for(250), report_for(1001), report_for(None)):
            self.assertEqual(
                len(renderer.row(report)), len(renderer.header()), report.status
            )

    def test_row_reports_limit_size_free_and_percentage(self):
        row = self.plain_renderer().row(report_for(250, limit_bytes=1024))

        self.assertIn("1.0 KiB", row)
        self.assertIn("0.2 KiB", row)
        self.assertIn("0.8 KiB", row)
        self.assertIn("75.6 %", row)
        self.assertIn("PASS", row)

    def test_usage_bar_fills_in_proportion_and_saturates_when_over(self):
        renderer = self.plain_renderer()

        self.assertEqual(renderer._bar(report_for(500)), "#" * 10 + "." * 10)
        self.assertEqual(renderer._bar(report_for(1001)), "#" * 20)
        self.assertEqual(renderer._bar(report_for(1)), "#" + "." * 19)
        self.assertEqual(renderer._bar(report_for(None)), "not built".ljust(20))

    def test_unbuilt_row_shows_dashes_rather_than_numbers(self):
        row = self.plain_renderer().row(report_for(None))

        self.assertIn("n/a", row)
        self.assertIn("not built", row)
        self.assertNotIn("0.0 KiB", row)

    def test_table_lists_every_variant_once(self):
        reports = [report_for(250, variant=variant) for variant in app_space.FIRMWARE_VARIANTS]
        table = self.plain_renderer().table(reports)

        for details in app_space.FIRMWARE_VARIANTS.values():
            self.assertEqual(table.count(details["name"] + " "), 1, details["name"])

    def test_totals_line_counts_each_status(self):
        totals = self.plain_renderer().totals(
            [report_for(250), report_for(1000), report_for(1001), report_for(None)]
        )

        self.assertEqual(totals.split(), ["FAIL", "1", "WARNING", "1", "PASS", "1", "n/a", "1"])

    def test_colours_are_emitted_on_github_actions_but_not_in_plain_pipes(self):
        with mock.patch.dict("os.environ", {"GITHUB_ACTIONS": "true"}, clear=True):
            self.assertTrue(app_space.Renderer(io.StringIO()).color)
        with mock.patch.dict("os.environ", {}, clear=True):
            self.assertFalse(app_space.Renderer(io.StringIO()).color)
        with mock.patch.dict("os.environ", {"GITHUB_ACTIONS": "true", "NO_COLOR": "1"}, clear=True):
            self.assertFalse(app_space.Renderer(io.StringIO()).color)

    def test_ascii_is_used_when_the_stream_cannot_encode_box_drawing(self):
        ascii_stream = io.TextIOWrapper(io.BytesIO(), encoding="ascii")
        utf8_stream = io.TextIOWrapper(io.BytesIO(), encoding="utf-8")

        self.assertFalse(app_space.Renderer(ascii_stream).unicode)
        self.assertTrue(app_space.Renderer(utf8_stream).unicode)

    def test_markdown_table_is_one_row_per_variant_with_a_status_icon(self):
        markdown = app_space.markdown_table([report_for(250), report_for(1001), report_for(None)])

        self.assertEqual(markdown.count("\n|"), 5)
        self.assertIn("✅ PASS", markdown)
        self.assertIn("❌ FAIL", markdown)
        self.assertIn("⬜ n/a", markdown)


class CommandTest(unittest.TestCase):
    @contextlib.contextmanager
    def artifact_of_size(self, variant, size_bytes):
        with tempfile.TemporaryDirectory() as directory:
            artifact = pathlib.Path(directory) / "ultimate.app"
            artifact.write_bytes(b"x" * size_bytes)
            variants = dict(app_space.FIRMWARE_VARIANTS)
            variants[variant] = dict(variants[variant], artifact=artifact.name)
            with mock.patch.object(app_space, "FIRMWARE_VARIANTS", variants):
                with mock.patch.object(app_space, "REPO_ROOT", pathlib.Path(directory)):
                    yield

    def run_main(self, argv):
        stdout, stderr = io.StringIO(), io.StringIO()
        with contextlib.redirect_stdout(stdout):
            with contextlib.redirect_stderr(stderr):
                status = app_space.main(argv)
        return status, stdout.getvalue(), stderr.getvalue()

    def test_check_prints_a_header_and_one_row(self):
        with self.artifact_of_size("u64", 1024):
            status, stdout, _ = self.run_main(["check", "u64"])

        self.assertEqual(status, 0)
        self.assertEqual(len(stdout.strip().splitlines()), 2)
        self.assertIn("Firmware", stdout)
        self.assertIn("U64", stdout)

    def test_check_fails_the_build_when_over_the_limit(self):
        with self.artifact_of_size("u2", 0xC6000 + 1):
            status, _, stderr = self.run_main(["check", "u2"])

        self.assertEqual(status, 1)
        self.assertIn("U2 is 1 bytes over its application space limit", stderr)

    def test_check_fails_when_the_artifact_was_not_produced(self):
        status, _, stderr = self.run_main(["check", "u2"])

        self.assertEqual(status, 2)
        self.assertIn("expected firmware artifact for U2 was not produced", stderr)

    def test_check_does_not_duplicate_the_github_annotations(self):
        with self.artifact_of_size("u2", 0xC6000):
            with mock.patch.dict("os.environ", {"GITHUB_ACTIONS": "true"}):
                status, stdout, stderr = self.run_main(["check", "u2"])

        self.assertEqual(status, 0)
        self.assertIn("WARNING: U2 has", stderr)
        self.assertNotIn("::warning::", stdout)

    def test_report_tabulates_every_variant_even_when_none_are_built(self):
        status, stdout, _ = self.run_main(["report"])

        self.assertEqual(status, 0)
        for details in app_space.FIRMWARE_VARIANTS.values():
            self.assertIn(details["name"], stdout)
        self.assertIn("not built", stdout)

    def test_report_fails_and_annotates_when_any_firmware_is_over(self):
        with self.artifact_of_size("u64", 0x170000 + 4096):
            with mock.patch.dict("os.environ", {"GITHUB_ACTIONS": "true"}):
                status, stdout, stderr = self.run_main(["report"])

        self.assertEqual(status, 1)
        self.assertIn("::error::U64 is 4096 bytes over", stdout)
        self.assertIn("ERROR: U64 is 4096 bytes over", stderr)

    def test_report_annotates_the_near_limit_warning(self):
        with self.artifact_of_size("u64", 0x170000):
            with mock.patch.dict("os.environ", {"GITHUB_ACTIONS": "true"}):
                status, stdout, _ = self.run_main(["report"])

        self.assertEqual(status, 0)
        self.assertIn("::warning::U64 has 0 bytes", stdout)

    def test_report_appends_a_markdown_job_summary_for_github(self):
        with tempfile.TemporaryDirectory() as directory:
            summary = pathlib.Path(directory) / "summary.md"
            summary.write_text("earlier step\n")
            with mock.patch.dict("os.environ", {"GITHUB_STEP_SUMMARY": str(summary)}):
                self.run_main(["report"])

            written = summary.read_text()

        self.assertTrue(written.startswith("earlier step\n"))
        self.assertIn("| Firmware | Limit | Size | Free | Free % | Status |", written)

    def test_report_survives_an_unwritable_job_summary(self):
        with mock.patch.dict("os.environ", {"GITHUB_STEP_SUMMARY": "/does/not/exist/summary.md"}):
            status, _, stderr = self.run_main(["report"])

        self.assertEqual(status, 0)
        self.assertIn("could not write the CI job summary", stderr)


class BuildIntegrationTest(unittest.TestCase):
    def setUp(self):
        self.makefile = (REPO_ROOT / "Makefile").read_text()
        self.workflow = (REPO_ROOT / ".github/workflows/build.yml").read_text()

    def test_every_firmware_target_gates_its_own_image(self):
        for variant in app_space.FIRMWARE_VARIANTS:
            self.assertIn("$(APP_SPACE) check " + variant + "\n", self.makefile)

    def test_artifact_paths_are_not_duplicated_into_the_makefile(self):
        invocations = re.findall(r"\$\(APP_SPACE\)[^\n]*", self.makefile)

        self.assertTrue(invocations)
        for invocation in invocations:
            self.assertRegex(invocation, r"^\$\(APP_SPACE\) (report|check \w+)$")

    def test_a_local_full_build_ends_with_the_report(self):
        recipe = self.makefile.split("all: esp32", 1)[1].splitlines()[1]

        self.assertEqual(recipe.strip(), "@$(APP_SPACE) report")

    def test_ci_runs_the_report_as_a_dedicated_final_step(self):
        step = self.workflow.split("- name: Report Firmware Application Space", 1)[1]
        step = step.split("- name:", 1)[0]

        self.assertIn("if: always()", step)
        self.assertIn("make app_space", step)
        self.assertIn("-e GITHUB_ACTIONS", step)
        self.assertIn("-e GITHUB_STEP_SUMMARY", step)
        self.assertIn("-v $GITHUB_STEP_SUMMARY:$GITHUB_STEP_SUMMARY:rw", step)


if __name__ == "__main__":
    unittest.main()
