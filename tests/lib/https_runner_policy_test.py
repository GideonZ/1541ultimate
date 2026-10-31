"""Check the controlled HTTPS entry offline, using the common suite reporter."""

from pathlib import Path
import subprocess
import sys

sys.path.insert(0, str(next(p for p in Path(__file__).resolve().parents
                            if (p / "tests" / "lib").is_dir()) / "tests" / "lib"))
import bootstrap  # noqa: E402,F401
import cli  # noqa: E402
from report import Failure, check, detail, suite_fail, suite_ok  # noqa: E402

NAME = "https_runner_policy_test"


def main():
    cli.device_free_arguments(__doc__)
    root = Path(__file__).resolve().parents[2]
    try:
        with check("HTTPS selection, execution gates and saved-evidence reports"):
            result = subprocess.run(
                [sys.executable, str(root / "tools/tests/test_run_tests_https.py")],
                cwd=root, capture_output=True, text=True, check=False)
            if result.returncode:
                detail(result.stdout + result.stderr)
                raise Failure(f"HTTPS policy regressions exited {result.returncode}")
            detail("Six offline policy tests passed; no device execution")
    except Failure as exc:
        suite_fail(NAME, str(exc))
        return 1
    suite_ok(NAME)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
