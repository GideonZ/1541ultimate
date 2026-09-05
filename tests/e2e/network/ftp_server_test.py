#!/usr/bin/env python3
# E2E: the device's own FTP server, over a real control and data connection.

"""Check that a name the FTP server reports can be used to address the file.

Every FTP client works from the directory listing, so a name that LIST or NLST
reports has to be the name the server accepts back for SIZE, MDTM and DELE. A
name the server truncates on the way out cannot address anything, which makes
the file impossible to remove over FTP.

Names of about 100 characters are not hypothetical here: prg_context_menu_test
and browser_long_filename_test both build fixtures that long.
"""

import argparse
import ftplib
import sys
from pathlib import Path

# The one stanza that puts the shared library on sys.path; see tests/lib/bootstrap.py.
sys.path.insert(0, str(next(p for p in Path(__file__).resolve().parents
                            if (p / "tests" / "lib").is_dir()) / "tests" / "lib"))
import bootstrap  # noqa: E402,F401
import cli  # noqa: E402

import ftp as ftp_lib
from report import (
    Failure, check, check_count, detail, format_exception, section, suite_fail, suite_ok)

SUITE = "ftp_server_test"
TEST_DIR = "/Temp"

# A control name short enough that no plausible buffer truncates it, and a
# regression name at the length prg_context_menu_test and
# browser_long_filename_test already use for their own fixtures.
CONTROL_LENGTH = 40
REGRESSION_LENGTH = 100

PAYLOAD = b"\x01\x08" + bytes(64)


def make_name(length: int, tag: str) -> str:
    stem = f"ftpsrv_{tag}_"
    return stem + "0123456789" * ((length - len(stem) - 4) // 10 + 1)


def fit(name: str, length: int) -> str:
    return name[:length - 4] + ".prg"


class Server:
    def __init__(self, host: str, password: str, timeout: float) -> None:
        self.host = host
        self.password = password
        self.timeout = timeout

    def connect(self) -> ftplib.FTP:
        return ftp_lib.connect(self.host, self.password, self.timeout, TEST_DIR)

    def store(self, client: ftplib.FTP, name: str) -> None:
        ftp_lib.store(client, name, PAYLOAD)

    def listed(self, client: ftplib.FTP, prefix: str) -> str:
        found = ftp_lib.names(client, prefix=prefix)
        if len(found) != 1:
            raise Failure(f"expected exactly one entry starting with {prefix!r}, got {found}")
        return found[0]

    def remove(self, client: ftplib.FTP, name: str) -> None:
        # Deliberately not delete_quietly: a refused DELE is the defect this
        # suite exists to catch, so it has to propagate.
        client.delete(name)


def scenario_names_round_trip(server: Server) -> None:
    section("a name the listing reports can address the file it names")
    client = server.connect()
    control = fit(make_name(CONTROL_LENGTH, "ctl"), CONTROL_LENGTH)
    regression = fit(make_name(REGRESSION_LENGTH, "long"), REGRESSION_LENGTH)
    try:
        with check(f"a {len(control)}-character name is listed unchanged"):
            server.store(client, control)
            reported = server.listed(client, "ftpsrv_ctl_")
            if reported != control:
                raise Failure(f"stored {control!r} ({len(control)}), listed {reported!r} "
                              f"({len(reported)})")

        with check("that file is removed by the name the listing reported"):
            server.remove(client, server.listed(client, "ftpsrv_ctl_"))
            if ftp_lib.names(client, prefix="ftpsrv_ctl_"):
                raise Failure("the entry survived DELE")

        with check(f"a {len(regression)}-character name is listed unchanged"):
            server.store(client, regression)
            reported = server.listed(client, "ftpsrv_long_")
            if reported != regression:
                raise Failure(
                    f"stored {regression!r} ({len(regression)} characters), listed "
                    f"{reported!r} ({len(reported)}); a truncated name cannot "
                    f"address the file it names")
            detail(f"{len(reported)} characters survived the listing")

        with check("SIZE answers for the long name the listing reported"):
            reported = server.listed(client, "ftpsrv_long_")
            size = client.size(reported)
            if size != len(PAYLOAD):
                raise Failure(f"SIZE {reported!r} returned {size}, expected {len(PAYLOAD)}")

        with check("that file is removed by the name the listing reported"):
            reported = server.listed(client, "ftpsrv_long_")
            server.remove(client, reported)
            if ftp_lib.names(client, prefix="ftpsrv_long_"):
                raise Failure("the entry survived DELE")
    finally:
        # Remove anything the checks left behind, by the name actually stored,
        # which is reachable even when the listing truncates.
        for name in (control, regression):
            ftp_lib.delete_quietly(client, name)
        ftp_lib.close(client)


def parse_args(argv=None):
    parser = argparse.ArgumentParser(
        description="Verify the device's FTP server reports names it can address.")
    cli.add_device_arguments(parser, colour=False)
    return parser.parse_args(argv)


def main() -> int:
    args = parse_args()
    server = Server(args.host, args.password, args.timeout)
    try:
        scenario_names_round_trip(server)
    except Exception as exc:  # noqa: BLE001  (a lost device must not print a traceback alone)
        suite_fail(SUITE, format_exception(exc))
        return 1
    suite_ok(SUITE, f"{check_count()} checks")
    return 0


if __name__ == "__main__":
    sys.exit(main())
