#!/usr/bin/env python3
"""Require repeated JSON discovery replies to return their heap."""

import argparse
import sys
from pathlib import Path

sys.path.insert(0, str(next(p for p in Path(__file__).resolve().parents
                            if (p / "tests" / "lib").is_dir()) / "tests" / "lib"))
import bootstrap  # noqa: E402,F401
import cli  # noqa: E402
import leak  # noqa: E402
from api import UltimateApi  # noqa: E402
from report import Failure, format_exception, suite_fail, suite_ok  # noqa: E402

sys.path.insert(0, bootstrap.directory("e2e", "network"))
from ident_service_switch_test import identify  # noqa: E402


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    cli.add_device_arguments(parser, timeout=5.0, colour=False)
    args = parser.parse_args()
    api = UltimateApi(args.host, args.password or None, args.timeout)
    if not leak.heap_is_served(api.machine.heap, "ident_leak_test"):
        return 0

    def once() -> None:
        if not identify(args.host):
            raise Failure("ident did not return a matching JSON reply (no retry)")

    leak.slope(once, api.machine.heap_free, warmup=3, iterations=20,
               tolerance_bytes_per_op=128, unit="reply", units="replies",
               settle_seconds=2,
               title="JSON discovery returns the heap it borrows")
    suite_ok("ident_leak_test")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:
        suite_fail("ident_leak_test", format_exception(exc))
        raise SystemExit(1)
