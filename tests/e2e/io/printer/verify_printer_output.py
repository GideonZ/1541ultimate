#!/usr/bin/env python3
"""Standalone structural verification for virtual-printer PNG/ASCII output.

Downloads printer output files from an Ultimate 64/64e over FTP and checks
that they are well-formed (PNG chunk/CRC structure, or non-blank text) without
needing to run a print job first. Useful for re-checking output left behind by
printer_test.py, or output produced interactively (e.g. via the on-device menu).

This is a diagnostic tool rather than a registered suite: it asserts nothing
about a print job, only about files that already exist.

Usage:
    ./verify_printer_output.py -H u64 --output-base /Usb0/printer/e2e-abc --pages 2
    ./verify_printer_output.py -H u64 --path /Temp/mypage-001.png
"""

import argparse
import os
import sys
from pathlib import Path

SCRIPT_DIR = Path(__file__).resolve().parent
# tests/lib holds the shared library; importing bootstrap adds tests/e2e/lib.
# The search walks up rather than counting directories, so this is the same in
# every entry point and a suite that moves needs no edit. See tests/lib/bootstrap.py.
sys.path.insert(0, str(next(p for p in Path(__file__).resolve().parents
                            if (p / "tests" / "lib").is_dir()) / "tests" / "lib"))
import bootstrap  # noqa: E402,F401
import cli  # noqa: E402
sys.path.insert(0, bootstrap.directory("e2e", "io", "printer"))
import png_lite  # noqa: E402  (local module, needs SCRIPT_DIR on sys.path first)

# tests/lib holds the shared FTP and reporting helpers.
import ftp as ftp_lib  # noqa: E402  (needs tests/lib on sys.path first)
from report import (  # noqa: E402  (needs tests/lib on sys.path first)
    Failure, check_fail, check_ok, check_start, section, suite_fail, suite_ok)

FTP_USER_DEFAULT = ftp_lib.FTP_USER
FTP_PASSWORD_DEFAULT = ftp_lib.FTP_DEFAULT_PASSWORD


def download(host, user, password, path, timeout=15):
    with ftp_lib.session(host, password, timeout, user=user) as ftp:
        return ftp_lib.retrieve(ftp, path)


def verify_one(host, user, password, path, is_ascii):
    check_start(os.path.basename(path))
    try:
        data = download(host, user, password, path)
    except Failure as exc:
        check_fail(f"download error: {exc}")
        return False

    if not data:
        check_fail("empty file")
        return False

    if is_ascii:
        text = data.decode("ascii", errors="replace")
        non_blank_lines = [line for line in text.splitlines() if line.strip()]
        if not non_blank_lines:
            check_fail(f"{len(data)} bytes, but no non-blank text")
            return False
        check_ok(f"{len(data)} bytes, {len(non_blank_lines)} non-blank lines")
        return True

    ok, reason = png_lite.png_is_well_formed(data)
    if not ok:
        check_fail(f"{len(data)} bytes, {reason}")
        return False
    size = png_lite.decode_png_dimensions(data)
    if size is None:
        # png_is_well_formed proves IHDR is first, so this should not happen;
        # reporting it beats an unpack that aborts the whole verification run.
        check_fail(f"{len(data)} bytes, well formed but no readable IHDR")
        return False
    check_ok(f"{len(data)} bytes, {size[0]}x{size[1]}px")
    return True


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    cli.add_device_arguments(parser, colour=False)
    parser.add_argument("--ftp-user", default=FTP_USER_DEFAULT)
    parser.add_argument("--ftp-password", default=FTP_PASSWORD_DEFAULT)
    parser.add_argument("--output-base", help="Printer output file base, e.g. /Usb0/printer/e2e-abc")
    parser.add_argument("--pages", type=int, default=1, help="Expected page count for --output-base")
    parser.add_argument("--ascii", action="store_true", help="Treat --output-base as ASCII (<base>.txt)")
    parser.add_argument("--path", action="append", default=[], help="Explicit file path to verify (repeatable)")
    args = parser.parse_args()

    if not args.output_base and not args.path:
        parser.error("specify --output-base or one or more --path")

    paths = list(args.path)
    if args.output_base:
        if args.ascii:
            paths.append(f"{args.output_base}.txt")
        else:
            paths.extend(f"{args.output_base}-{page:03d}.png"
                         for page in range(1, args.pages + 1))

    section(f"{len(paths)} printer output file(s) on {args.host}")
    results = [verify_one(args.host, args.ftp_user, args.ftp_password, path,
                          args.ascii or path.endswith(".txt"))
               for path in paths]

    failed = len(results) - sum(results)
    if failed:
        suite_fail("verify_printer_output", f"{failed} of {len(results)} files are not well-formed")
        return 1
    suite_ok("verify_printer_output", f"{len(results)} files")
    return 0


if __name__ == "__main__":
    sys.exit(main())
