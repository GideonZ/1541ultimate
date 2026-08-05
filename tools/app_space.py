#!/usr/bin/env python3
"""Validate final firmware images against their application-size limits."""

import argparse
import os
import pathlib
import sys


FIRMWARE_VARIANTS = {
    "u2": {"name": "U2", "limit_bytes": 0xC6000},
    "u2plus": {"name": "U2+", "limit_bytes": 0x140000},
    "u2pl": {"name": "U2+L", "limit_bytes": 0x160000},
    "u64": {"name": "U64", "limit_bytes": 0x170000},
    "u64e2_50t": {"name": "U64E2_50T", "limit_bytes": 0x1E0000},
    "u64e2_100t": {"name": "U64E2_100T", "limit_bytes": 0x1C0000},
}


def firmware_limit_bytes(variant):
    try:
        return FIRMWARE_VARIANTS[variant]["limit_bytes"]
    except KeyError as error:
        raise ValueError("unknown firmware variant " + variant) from error


class FirmwareSizeReport:
    def __init__(self, variant, actual_bytes, limit_bytes):
        self.variant = variant
        self.actual_bytes = actual_bytes
        self.limit_bytes = limit_bytes
        self.remaining_bytes = limit_bytes - actual_bytes

    @property
    def status(self):
        if self.remaining_bytes < 0:
            return "FAIL"
        if self.remaining_bytes * 100 < self.limit_bytes:
            return "WARNING"
        return "PASS"

    @property
    def exit_status(self):
        return 1 if self.status == "FAIL" else 0

    def percentage(self):
        basis_points = abs(self.remaining_bytes) * 10000 // self.limit_bytes
        whole, fraction = divmod(basis_points, 100)
        sign = "-" if self.remaining_bytes < 0 else ""
        return "%s%d.%02d%%" % (sign, whole, fraction)

    def message(self):
        return (
            "variant=%s, actual=0x%X (%d bytes), limit=0x%X (%d bytes), "
            "remaining=%d bytes, percentage=%s, status=%s" % (
                FIRMWARE_VARIANTS[self.variant]["name"],
                self.actual_bytes,
                self.actual_bytes,
                self.limit_bytes,
                self.limit_bytes,
                self.remaining_bytes,
                self.percentage(),
                self.status,
            )
        )

    def warning_message(self):
        return (
            "%s has less than 1%% of its firmware limit remaining." % (
                FIRMWARE_VARIANTS[self.variant]["name"],
            )
        )


def check_firmware_size(variant, actual_bytes):
    return FirmwareSizeReport(
        variant, actual_bytes, firmware_limit_bytes(variant)
    )


def main(argv):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("variant", choices=sorted(FIRMWARE_VARIANTS))
    parser.add_argument("artifact", type=pathlib.Path)
    args = parser.parse_args(argv)

    if not args.artifact.is_file():
        print(
            "ERROR: expected firmware artifact for %s was not produced: %s" % (
                FIRMWARE_VARIANTS[args.variant]["name"],
                args.artifact,
            ),
            file=sys.stderr,
        )
        return 2

    report = check_firmware_size(args.variant, args.artifact.stat().st_size)
    print(report.message())
    if report.status == "WARNING":
        warning = report.warning_message()
        print("WARNING: " + warning, file=sys.stderr)
        if os.environ.get("GITHUB_ACTIONS") == "true":
            print("::warning::" + warning)
    elif report.status == "FAIL":
        print("ERROR: firmware exceeds its limit.", file=sys.stderr)
    return report.exit_status


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
