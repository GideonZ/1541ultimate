#!/usr/bin/env python3
"""Device-free regression checks for native HTTP/HTTPS test evidence and cleanup."""

import io
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(next(p for p in Path(__file__).resolve().parents
                            if (p / "tests" / "lib").is_dir()) / "tests" / "lib"))
import bootstrap  # noqa: E402
import cli  # noqa: E402
from report import Failure, check, detail, suite_fail, suite_ok  # noqa: E402

sys.path.insert(0, bootstrap.directory("e2e", "io", "command_interface"))
sys.path.insert(0, bootstrap.directory("soak", "io", "command_interface"))


def main():
    cli.device_free_arguments(__doc__)
    try:
        with check("native HTTP/HTTPS harness regression cases"):
            cases = unittest.defaultTestLoader.discover(
                bootstrap.directory("lib", "https_harness"), pattern="test_*.py")
            output = io.StringIO()
            result = unittest.TextTestRunner(stream=output, verbosity=1).run(cases)
            if not result.wasSuccessful() or result.testsRun == 0:
                detail(output.getvalue())
                raise Failure("Harness regression failed")
        suite_ok("https-harness", f"{result.testsRun} device-free tests")
    except Failure as error:
        suite_fail("https-harness", str(error))
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
