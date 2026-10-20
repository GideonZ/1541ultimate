#!/usr/bin/env python3
# Gate check: the .cfg loader's debug log is read correctly when it interleaves.

"""Verify parse_loader_log over a recorded debug log, intact and spliced.

The device writes its debug log with printf, which emits one character at a
time through outbyte() and takes no lock (software/system/small_printf.cc and
software/application/ultimate/ultimate.cc). A task switch part way through a
line therefore splices another task's output into the middle of it. The REST
poller that every overlay-mode suite runs prints "Accept client 0 on socket 7."
for each connection, so it is the task that lands in the loader's lines.

That is not reproducible on demand, so it is checked here against the text
measured on a C64 Ultimate 1.2RC rather than by driving a device. The intact
log below was captured from an Ultimate 64 Elite on firmware 3.15, harness
commit b3c76093.

Needs no device.
"""

import sys
from pathlib import Path

# The one stanza that puts the shared library on sys.path; see tests/lib/bootstrap.py.
sys.path.insert(0, str(next(p for p in Path(__file__).resolve().parents
                            if (p / "tests" / "lib").is_dir()) / "tests" / "lib"))
import bootstrap  # noqa: E402,F401

sys.path.insert(0, bootstrap.directory("e2e", "filemanager"))

from report import (  # noqa: E402
    Failure, add_colour_argument, apply_colour, check, suite_fail, suite_ok)
from selftest import expect  # noqa: E402

from cfg_single_group_test import parse_loader_log  # noqa: E402

NAME = "cfg_loader_log_test"

# Two records and the surrounding noise, as an Ultimate 64 Elite wrote them.
INTACT = """\
Accept client 0 on socket 7.  192.168.1.185:50898
HTTP GET /v1/machine:menu_screen
Effectuating settings of store 'Audio Mixer' after loading.
Store 'SID Sockets Configuration' is clean after loading.
Store 'Network Settings' is clean after loading.
Object level 1 returned 1.
"""

# What a C64 Ultimate 1.2RC actually wrote. The network task's line was spliced
# into the middle of the store name, and its newline cut the loader's line in
# two: the first half keeps the prefix, the second half keeps the suffix.
SPLICED = """\
Effectuating settings of store 'Audio MAccept client 0 on socket 7.  192.168.1.185:49362
ixer' after loading.
Store 'Network Settings' is clean after loading.
"""

# The other measured shape, from attempt 1 of the same c64u run: the record is
# appended to the line another task is part way through, so its prefix is not
# at the start of any line and a parse anchored there sees no store at all.
APPENDED = """\
HTTP GET /v1/machine:menu_screenEffectuating settings of store 'Audio Mixer' after loading.
Store 'Network Settings' is clean after loading.
"""

# The same splice landing in a "clean" record instead of an effectuated one.
SPLICED_CLEAN = """\
Effectuating settings of store 'Audio Mixer' after loading.
Store 'Network SetAccept client 0 on socket 7.  192.168.1.185:49362
tings' is clean after loading.
"""


def run_intact_checks() -> None:
    with check("an uninterrupted log gives the stores it names"):
        report = parse_loader_log(INTACT)
        expect("effectuated", report.effectuated, ["Audio Mixer"])
        expect("clean", report.clean,
               ["SID Sockets Configuration", "Network Settings"])
        expect("damaged", report.damaged, [])

    with check("a log with no loader lines in it reports nothing"):
        report = parse_loader_log(
            "Accept client 0 on socket 7.  192.168.1.185:50898\n"
            "HTTP GET /v1/machine:menu_screen\n")
        expect("effectuated", report.effectuated, [])
        expect("clean", report.clean, [])
        expect("damaged", report.damaged, [])


def run_spliced_checks() -> None:
    with check("a spliced effectuated record yields no store name"):
        report = parse_loader_log(SPLICED)
        # The measured defect: "Audio MAccept client 0 on socket 7.
        # 192.168.1.185:49362" was reported as a store the loader effectuated.
        expect("effectuated", report.effectuated, [])
        expect("clean", report.clean, ["Network Settings"])

    with check("both halves of a spliced effectuated record are reported"):
        report = parse_loader_log(SPLICED)
        expect("damaged", report.damaged, [
            "Effectuating settings of store 'Audio MAccept client 0 on socket 7."
            "  192.168.1.185:49362",
            "ixer' after loading.",
        ])

    with check("a record appended to another task's line is reported, not read"):
        report = parse_loader_log(APPENDED)
        expect("effectuated", report.effectuated, [])
        expect("clean", report.clean, ["Network Settings"])
        expect("damaged", report.damaged, [
            "HTTP GET /v1/machine:menu_screenEffectuating settings of store "
            "'Audio Mixer' after loading.",
        ])

    with check("a spliced clean record yields no store name and is reported"):
        report = parse_loader_log(SPLICED_CLEAN)
        expect("effectuated", report.effectuated, ["Audio Mixer"])
        expect("clean", report.clean, [])
        expect("damaged", report.damaged, [
            "Store 'Network SetAccept client 0 on socket 7.  192.168.1.185:49362",
            "tings' is clean after loading.",
        ])


def main() -> int:
    import argparse

    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    add_colour_argument(parser)
    apply_colour(parser.parse_args().color)
    try:
        run_intact_checks()
        run_spliced_checks()
    except Failure as exc:
        suite_fail(NAME, str(exc))
        return 1
    suite_ok(NAME)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
