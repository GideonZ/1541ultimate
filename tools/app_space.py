#!/usr/bin/env python3
"""Report and enforce the application space budget of the final firmware images.

    app_space.py check <variant>   gate one freshly built image (used by the Makefile)
    app_space.py report            table across every firmware (used by CI and `make all`)
"""

import argparse
import os
import pathlib
import sys


REPO_ROOT = pathlib.Path(__file__).resolve().parents[1]

# The limits are the FLASH_ID_APPL partition sizes in software/io/flash/*_flash.cc.
# tools/test_app_space.py checks that these two definitions stay in sync.
FIRMWARE_VARIANTS = {
    "u2": {
        "name": "U2",
        "limit_bytes": 0xC6000,
        "artifact": "target/u2/riscv/ultimate/result/ultimate.bin",
    },
    "u2plus": {
        "name": "U2+",
        "limit_bytes": 0x140000,
        "artifact": "target/u2plus/nios/ultimate/result/ultimate.app",
    },
    "u2pl": {
        "name": "U2+L",
        "limit_bytes": 0x160000,
        "artifact": "target/u2plus_L/riscv/ultimate/result/ultimate.app",
    },
    "u64": {
        "name": "U64",
        "limit_bytes": 0x170000,
        "artifact": "target/u64/nios2/ultimate/result/ultimate.app",
    },
    "u64e2_50t": {
        "name": "U64E2_50T",
        "limit_bytes": 0x1E0000,
        "artifact": "target/u64ii/riscv/ultimate/result/ultimate.app",
    },
    "u64e2_100t": {
        "name": "U64E2_100T",
        "limit_bytes": 0x1C0000,
        "artifact": "target/u64ii/riscv/ultimate/result/ultimate.app",
    },
}

WARNING_THRESHOLD_PERCENT = 1

PASS = "PASS"
WARNING = "WARNING"
FAIL = "FAIL"
MISSING = "MISSING"

STATUS_LABELS = {MISSING: "n/a"}
MARKDOWN_ICONS = {PASS: "✅", WARNING: "⚠️", FAIL: "❌", MISSING: "⬜"}

TITLE = "Firmware application space (limit = FLASH_ID_APPL partition size)"
BAR_WIDTH = 20
INDENT = "  "
GAP = "  "
COLUMNS = (
    ("Firmware", 11, "<"),
    ("Limit", 11, ">"),
    ("Size", 11, ">"),
    ("Free", 11, ">"),
    ("Free %", 8, ">"),
    ("Usage", BAR_WIDTH, "<"),
    ("Status", 7, "<"),
)
ROW_WIDTH = sum(width for _, width, _ in COLUMNS) + len(GAP) * (len(COLUMNS) - 1)

ANSI_RESET = "\033[0m"
ANSI_BOLD = "\033[1m"
ANSI_COLORS = {PASS: "\033[32m", WARNING: "\033[33m", FAIL: "\033[31m", MISSING: "\033[90m"}


def label(status):
    return STATUS_LABELS.get(status, status)


def format_kib(size_bytes):
    return "%.1f KiB" % (size_bytes / 1024.0)


class FirmwareSizeReport:
    """Application space accounting for a single firmware image."""

    def __init__(self, variant, actual_bytes, limit_bytes):
        self.variant = variant
        self.actual_bytes = actual_bytes
        self.limit_bytes = limit_bytes
        self.remaining_bytes = (
            None if actual_bytes is None else limit_bytes - actual_bytes
        )

    @property
    def name(self):
        return FIRMWARE_VARIANTS[self.variant]["name"]

    @property
    def artifact(self):
        return REPO_ROOT / FIRMWARE_VARIANTS[self.variant]["artifact"]

    @property
    def status(self):
        if self.actual_bytes is None:
            return MISSING
        if self.remaining_bytes < 0:
            return FAIL
        if self.remaining_bytes * 100 < self.limit_bytes * WARNING_THRESHOLD_PERCENT:
            return WARNING
        return PASS

    @property
    def exit_status(self):
        return 1 if self.status == FAIL else 0

    def free_percent(self):
        return 100.0 * self.remaining_bytes / self.limit_bytes

    def failure_message(self):
        return "%s is %d bytes over its application space limit (%d of %d bytes)." % (
            self.name,
            -self.remaining_bytes,
            self.actual_bytes,
            self.limit_bytes,
        )

    def warning_message(self):
        return "%s has %d bytes (%.2f%%) of application space left, under the %d%% warning threshold." % (
            self.name,
            self.remaining_bytes,
            self.free_percent(),
            WARNING_THRESHOLD_PERCENT,
        )

    def missing_message(self):
        return "expected firmware artifact for %s was not produced: %s" % (
            self.name,
            self.artifact,
        )


def read_report(variant):
    details = FIRMWARE_VARIANTS[variant]
    artifact = REPO_ROOT / details["artifact"]
    actual_bytes = artifact.stat().st_size if artifact.is_file() else None
    return FirmwareSizeReport(variant, actual_bytes, details["limit_bytes"])


def _supports_unicode(stream):
    try:
        "─█░".encode(getattr(stream, "encoding", None) or "ascii")
    except (LookupError, UnicodeEncodeError):
        return False
    return True


def _supports_color(stream):
    if os.environ.get("NO_COLOR"):
        return False
    if os.environ.get("GITHUB_ACTIONS") == "true" or os.environ.get("FORCE_COLOR"):
        return True
    return bool(getattr(stream, "isatty", lambda: False)())


class Renderer:
    """Renders reports as aligned, optionally coloured terminal rows."""

    def __init__(self, stream=None):
        stream = sys.stdout if stream is None else stream
        self.unicode = _supports_unicode(stream)
        self.color = _supports_color(stream)

    def _pad(self, text, width, align):
        return text.ljust(width) if align == "<" else text.rjust(width)

    def _paint(self, text, status, bold=False):
        if not self.color:
            return text
        return "%s%s%s%s" % (
            ANSI_BOLD if bold else "",
            ANSI_COLORS[status],
            text,
            ANSI_RESET,
        )

    def _bar(self, report):
        if report.status == MISSING:
            return "not built".ljust(BAR_WIDTH)
        used = report.actual_bytes / float(report.limit_bytes)
        filled = BAR_WIDTH if used >= 1.0 else int(used * BAR_WIDTH)
        if filled == 0 and report.actual_bytes:
            filled = 1
        full, empty = ("█", "░") if self.unicode else ("#", ".")
        return full * filled + empty * (BAR_WIDTH - filled)

    def header(self):
        cells = [self._pad(name, width, align) for name, width, align in COLUMNS]
        return INDENT + GAP.join(cells)

    def rule(self):
        return INDENT + ("─" if self.unicode else "-") * ROW_WIDTH

    def row(self, report):
        if report.status == MISSING:
            size = free = percent = "-"
        else:
            size = format_kib(report.actual_bytes)
            free = format_kib(report.remaining_bytes)
            percent = "%.1f %%" % report.free_percent()
        values = (report.name, format_kib(report.limit_bytes), size, free, percent)
        cells = [
            self._pad(value, width, align)
            for value, (_, width, align) in zip(values, COLUMNS)
        ]
        cells.append(self._paint(self._bar(report), report.status))
        status = label(report.status).ljust(COLUMNS[-1][1])
        cells.append(self._paint(status, report.status, bold=True))
        return INDENT + GAP.join(cells)

    def totals(self, reports):
        parts = []
        for status in (FAIL, WARNING, PASS, MISSING):
            count = sum(1 for report in reports if report.status == status)
            if count:
                parts.append(self._paint("%s %d" % (label(status), count), status, bold=True))
        return INDENT + GAP.join(parts)

    def table(self, reports):
        lines = [TITLE, "", self.header(), self.rule()]
        lines.extend(self.row(report) for report in reports)
        lines.extend([self.rule(), self.totals(reports)])
        return "\n".join(lines)


def markdown_table(reports):
    lines = [
        "### " + TITLE,
        "",
        "| Firmware | Limit | Size | Free | Free % | Status |",
        "| --- | ---: | ---: | ---: | ---: | :--- |",
    ]
    for report in reports:
        if report.status == MISSING:
            size, free, percent = "-", "-", "-"
        else:
            size = format_kib(report.actual_bytes)
            free = format_kib(report.remaining_bytes)
            percent = "%.1f %%" % report.free_percent()
        lines.append(
            "| %s | %s | %s | %s | %s | %s %s |"
            % (
                report.name,
                format_kib(report.limit_bytes),
                size,
                free,
                percent,
                MARKDOWN_ICONS[report.status],
                label(report.status),
            )
        )
    return "\n".join(lines) + "\n"


def write_job_summary(text):
    path = os.environ.get("GITHUB_STEP_SUMMARY")
    if not path:
        return
    try:
        with open(path, "a", encoding="utf-8") as summary:
            summary.write(text)
    except OSError as error:
        print("WARNING: could not write the CI job summary: %s" % error, file=sys.stderr)


def announce(level, message):
    print(("ERROR: " if level == "error" else "WARNING: ") + message, file=sys.stderr)
    if os.environ.get("GITHUB_ACTIONS") == "true":
        print("::%s::%s" % (level, message))


def run_check(variant):
    """Gate a single freshly built image. Prints one row; annotations come from `report`."""
    report = read_report(variant)
    if report.status == MISSING:
        print("ERROR: " + report.missing_message(), file=sys.stderr)
        return 2

    renderer = Renderer()
    print(renderer.header())
    print(renderer.row(report))
    sys.stdout.flush()  # keep the row ahead of the diagnostics on stderr
    if report.status == FAIL:
        print("ERROR: " + report.failure_message(), file=sys.stderr)
    elif report.status == WARNING:
        print("WARNING: " + report.warning_message(), file=sys.stderr)
    return report.exit_status


def run_report():
    """Central signal: one table over every firmware, plus CI annotations and job summary."""
    reports = [read_report(variant) for variant in FIRMWARE_VARIANTS]
    print(Renderer().table(reports))
    sys.stdout.flush()  # keep the table ahead of the diagnostics on stderr
    write_job_summary(markdown_table(reports))

    for report in reports:
        if report.status == FAIL:
            announce("error", report.failure_message())
    for report in reports:
        if report.status == WARNING:
            announce("warning", report.warning_message())
    return max(report.exit_status for report in reports)


def main(argv):
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    commands = parser.add_subparsers(dest="command", required=True)
    check = commands.add_parser("check", help="gate one freshly built firmware image")
    check.add_argument("variant", choices=sorted(FIRMWARE_VARIANTS))
    commands.add_parser("report", help="tabulate the application space of every firmware")
    args = parser.parse_args(argv)

    if args.command == "check":
        return run_check(args.variant)
    return run_report()


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
