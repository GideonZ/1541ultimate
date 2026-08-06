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


def report_for(actual_bytes, limit_bytes=1000, variant="u2"):
    return app_space.FirmwareSizeReport(variant, actual_bytes, limit_bytes)


class LimitTest(unittest.TestCase):
    def test_limits_read_from_the_flash_tables_are_the_partition_sizes(self):
        # Pins the parse against the real sources. A change here means a partition
        # moved, which has to be a deliberate decision rather than a silent one.
        self.assertEqual(
            app_space.load_limits(),
            {
                "u2": 0xC6000,
                "u2plus": 0x140000,
                "u2pl": 0x160000,
                "u64": 0x170000,
                "u64e2_50t": 0x1E0000,
                "u64e2_100t": 0x1C0000,
            },
        )

    def test_no_limit_is_written_down_outside_the_flash_tables(self):
        for source in ("tools/app_space.py", "Makefile", ".github/workflows/build.yml"):
            text = (REPO_ROOT / source).read_text()

            for limit in app_space.load_limits().values():
                self.assertNotIn("0x%X" % limit, text, source)
                self.assertNotIn(str(limit), text, source)

    def test_size_is_taken_from_the_last_field_of_the_appl_entry(self):
        limits = app_space.read_partition_limits("""
            static const t_flash_address flash_addresses[] = {
                { FLASH_ID_BOOTFPGA,   0x01, 0x000000, 0x000000, 0x53CA0 },
                { FLASH_ID_APPL,       0x01, 0x062000, 0x062000, 0xC6000 }, // max 792K
                { FLASH_ID_CONFIG,     0x00, 0x1F0000, 0x1F0000, 0x10000 } };
            static const t_flash_address other[] = {
                { FLASH_ID_BOOTFPGA,   0x00, 0x000000, 0x000000, 0x0C0000 } };
        """)

        self.assertEqual(limits, {"flash_addresses": 0xC6000})

    def test_a_table_shared_by_two_devices_uses_the_smaller_partition(self):
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            (root / "software/io/flash").mkdir(parents=True)
            entry = "{ FLASH_ID_APPL, 0x01, 0x062000, 0x062000, %s }"
            for name, size in zip(app_space.FLASH_TABLE_SOURCES, ("0xC6000", "0xB0000")):
                (root / name).write_text(
                    "\n".join(
                        "static const t_flash_address %s[] = { %s };"
                        % (details["partition"], entry % size)
                        for details in app_space.FIRMWARE_VARIANTS.values()
                    )
                )

            self.assertEqual(app_space.load_limits(root)["u2"], 0xB0000)

    def test_a_missing_partition_table_is_reported_clearly(self):
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            (root / "software/io/flash").mkdir(parents=True)
            for name in app_space.FLASH_TABLE_SOURCES:
                (root / name).write_text("static const t_flash_address other[] = { };")

            with self.assertRaises(app_space.LimitError) as raised:
                app_space.load_limits(root)

        self.assertIn("no FLASH_ID_APPL entry for U2", str(raised.exception))

    def test_an_unreadable_source_is_reported_clearly(self):
        with self.assertRaises(app_space.LimitError) as raised:
            app_space.load_limits(pathlib.Path("/does/not/exist"))

        self.assertIn("cannot read the flash partition table", str(raised.exception))

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
    def artifact_at(self, variant, artifact):
        variants = dict(app_space.FIRMWARE_VARIANTS)
        variants[variant] = dict(variants[variant], artifact=str(artifact))
        with mock.patch.object(app_space, "FIRMWARE_VARIANTS", variants):
            yield

    @contextlib.contextmanager
    def artifact_of_size(self, variant, size_bytes):
        with tempfile.TemporaryDirectory() as directory:
            artifact = pathlib.Path(directory) / "ultimate.app"
            artifact.write_bytes(b"x" * size_bytes)
            with self.artifact_at(variant, artifact):
                yield

    @contextlib.contextmanager
    def no_artifacts(self):
        variants = {
            variant: dict(details, artifact="/does/not/exist/" + variant)
            for variant, details in app_space.FIRMWARE_VARIANTS.items()
        }
        with mock.patch.object(app_space, "FIRMWARE_VARIANTS", variants):
            yield

    def run_main(self, argv):
        stdout, stderr = io.StringIO(), io.StringIO()
        with contextlib.redirect_stdout(stdout):
            with contextlib.redirect_stderr(stderr):
                status = app_space.main(argv)
        return status, stdout.getvalue(), stderr.getvalue()

    def test_measure_prints_a_header_and_one_row(self):
        with self.artifact_of_size("u64", 1024):
            status, stdout, _ = self.run_main(["measure", "u64"])

        self.assertEqual(status, 0)
        self.assertEqual(len(stdout.strip().splitlines()), 2)
        self.assertIn("Firmware", stdout)
        self.assertIn("U64", stdout)

    def test_measure_does_not_stop_a_build_that_is_over_the_limit(self):
        with self.artifact_of_size("u2", 0xC6000 + 1):
            status, _, stderr = self.run_main(["measure", "u2"])

        self.assertEqual(status, 0)
        self.assertIn("OVER LIMIT: U2 is 1 bytes over its application space limit", stderr)
        self.assertIn("fails at the final application space report", stderr)

    def test_measure_stops_the_build_when_the_artifact_was_not_produced(self):
        with self.artifact_at("u2", "/does/not/exist/ultimate.bin"):
            status, _, stderr = self.run_main(["measure", "u2"])

        self.assertEqual(status, 2)
        self.assertIn("expected firmware artifact for U2 was not produced", stderr)

    def test_measure_does_not_duplicate_the_github_annotations(self):
        with self.artifact_of_size("u2", 0xC6000 + 1):
            with mock.patch.dict("os.environ", {"GITHUB_ACTIONS": "true"}):
                status, stdout, stderr = self.run_main(["measure", "u2"])

        self.assertEqual(status, 0)
        self.assertIn("OVER LIMIT: U2 is", stderr)
        self.assertNotIn("::error::", stdout)
        self.assertNotIn("::warning::", stdout)

    def test_report_tabulates_every_variant_even_when_none_are_built(self):
        with self.no_artifacts():
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
            with self.no_artifacts():
                with mock.patch.dict("os.environ", {"GITHUB_STEP_SUMMARY": str(summary)}):
                    self.run_main(["report"])

            written = summary.read_text()

        self.assertTrue(written.startswith("earlier step\n"))
        self.assertIn("| Firmware | Limit | Size | Free | Free % | Status |", written)

    def test_report_survives_an_unwritable_job_summary(self):
        with self.no_artifacts():
            with mock.patch.dict(
                "os.environ", {"GITHUB_STEP_SUMMARY": "/does/not/exist/summary.md"}
            ):
                status, _, stderr = self.run_main(["report"])

        self.assertEqual(status, 0)
        self.assertIn("could not write the CI job summary", stderr)


class BuildIntegrationTest(unittest.TestCase):
    def setUp(self):
        self.makefile = (REPO_ROOT / "Makefile").read_text()
        self.workflow = (REPO_ROOT / ".github/workflows/build.yml").read_text()

    def test_every_firmware_target_measures_its_own_image(self):
        for variant in app_space.FIRMWARE_VARIANTS:
            self.assertIn("$(APP_SPACE) measure " + variant + "\n", self.makefile)

    def test_no_build_target_gates_on_size_so_every_firmware_is_still_built(self):
        self.assertNotIn("$(APP_SPACE) check", self.makefile)

    def test_artifact_paths_are_not_duplicated_into_the_makefile(self):
        invocations = re.findall(r"\$\(APP_SPACE\)[^\n]*", self.makefile)

        self.assertTrue(invocations)
        for invocation in invocations:
            self.assertRegex(invocation, r"^\$\(APP_SPACE\) (report|measure \w+)$")

    def test_a_local_full_build_ends_with_the_report(self):
        recipe = self.makefile.split("all: esp32", 1)[1].splitlines()[1]

        self.assertEqual(recipe.strip(), "@$(APP_SPACE) report")

    def test_ci_runs_the_report_as_a_dedicated_final_step(self):
        step = self.workflow.split("- name: Check Firmware Application Space", 1)[1]
        step = step.split("- name:", 1)[0]

        self.assertIn("if: always()", step)
        self.assertIn("make app_space", step)
        self.assertIn("-e GITHUB_ACTIONS", step)
        self.assertIn("-e GITHUB_STEP_SUMMARY", step)
        self.assertIn("-v $GITHUB_STEP_SUMMARY:$GITHUB_STEP_SUMMARY:rw", step)


if __name__ == "__main__":
    unittest.main()
