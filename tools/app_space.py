#!/usr/bin/env python3
"""Report firmware application-partition headroom during builds."""

import argparse
import pathlib
import re
import sys


FLASH_LAYOUT = pathlib.Path(__file__).parents[1] / "software/io/flash/w25q_flash.cc"
TARGET_TABLES = {
    "u2": ("flash_addresses",),
    "u2plus": ("flash_addresses_u2p",),
    "u2pl": ("flash_addresses_u2pl",),
    "u64": ("flash_addresses_u64",),
    "u64ii": ("flash_addresses_u64ii_50t", "flash_addresses_u64ii_100t"),
}
TARGET_NAMES = {
    "u2": "U2",
    "u2plus": "U2+",
    "u2pl": "U2+L",
    "u64": "U64",
    "u64ii": "U64-II",
}
WARNING_PERCENT = 5


def table_application_limit(source, table_name):
    table = re.search(
        r"static const t_flash_address " + re.escape(table_name) +
        r"\[\] = \{(.*?)\};",
        source,
        re.DOTALL,
    )
    if table is None:
        raise ValueError("could not find flash layout table " + table_name)

    application = re.search(
        r"\{\s*FLASH_ID_APPL\s*,\s*[^,]+,\s*[^,]+,\s*[^,]+,\s*"
        r"(0x[0-9A-Fa-f]+|[0-9]+)\s*\}",
        table.group(1),
    )
    if application is None:
        raise ValueError("could not find application partition in " + table_name)
    return int(application.group(1), 0)


def application_limit_bytes(target):
    try:
        table_names = TARGET_TABLES[target]
    except KeyError as error:
        raise ValueError("unknown target " + target) from error

    source = FLASH_LAYOUT.read_text()
    return min(table_application_limit(source, name) for name in table_names)


class ApplicationSpaceReport:
    def __init__(self, target, application_bytes, limit_bytes):
        self.target = target
        self.application_bytes = application_bytes
        self.limit_bytes = limit_bytes
        self.remaining_bytes = limit_bytes - application_bytes
        self.warning_bytes = limit_bytes * WARNING_PERCENT // 100

    @property
    def is_below_warning_threshold(self):
        return self.remaining_bytes < self.warning_bytes

    def message(self):
        return (
            "%s application: %.1f KiB remaining of %.1f KiB (%.1f%% free)" % (
                TARGET_NAMES[self.target],
                self.remaining_bytes / 1024,
                self.limit_bytes / 1024,
                self.remaining_bytes * 100 / self.limit_bytes,
            )
        )

    def warning_message(self):
        return (
            "%s is below the application-space warning threshold "
            "(%d%% is %.1f KiB)." % (
                TARGET_NAMES[self.target],
                WARNING_PERCENT,
                self.warning_bytes / 1024,
            )
        )


def check_application_space(target, application_bytes):
    return ApplicationSpaceReport(
        target, application_bytes, application_limit_bytes(target)
    )


def main(argv):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--warning-only",
        action="store_true",
        help="report threshold breaches without failing the build",
    )
    parser.add_argument("target", choices=sorted(TARGET_TABLES))
    parser.add_argument("application", type=pathlib.Path)
    args = parser.parse_args(argv)

    if not args.application.is_file():
        parser.error("application image not found: " + str(args.application))

    report = check_application_space(args.target, args.application.stat().st_size)
    print(report.message())
    if report.is_below_warning_threshold:
        prefix = "WARNING" if args.warning_only else "ERROR"
        print(prefix + ": " + report.warning_message(), file=sys.stderr)
        if not args.warning_only:
            return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
