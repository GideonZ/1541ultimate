#!/usr/bin/env python3
"""E2E: a CFG file naming one configuration group loads through the browser.

The fixture changes one volume setting, then loads it through the real browser
action.  The device debug log is the external record of which stores the loader
considered for effectuation, as exposed by the existing loader diagnostics.
The test restores the setting through the public config API and removes both
files it creates.

Whether the load then effectuates *only* that group is asserted by
cfg_partial_effectuate_test.py, which is manual because that assertion is state
dependent: the loader also flushes any other store with an unapplied change
pending, so the answer depends on what ran before it. The helpers below are
shared with it, so the fixture and the log reader cannot drift apart.

Which store holds that setting is asked of the machine rather than assumed:
an Ultimate 64 serves it as "Audio Mixer", an Ultimate II+L as "Audio Output
Settings". See ConfigsApi.find_padded_enum.
"""

import argparse
import os
import sys
from pathlib import Path
from typing import NamedTuple

# The one stanza that puts the shared library on sys.path; see tests/lib/bootstrap.py.
sys.path.insert(0, str(next(p for p in Path(__file__).resolve().parents
                            if (p / "tests" / "lib").is_dir()) / "tests" / "lib"))
import bootstrap  # noqa: E402,F401

# cfg_fixture is beside this file, which is on the path when this runs as
# a script but not when another suite imports it.
sys.path.insert(0, bootstrap.directory("e2e", "filemanager"))
import cfg_fixture  # noqa: E402
import cli  # noqa: E402

from api import UltimateApi
import ftp as ftp_lib
from report import (Failure, teardown_step, check, check_skip, check_start, format_exception,
                    suite_fail, suite_ok)
from ui_backend import add_mode_argument


CFG_NAME = "cfg-sgrp.cfg"
LOG_NAME = "cfg-sgrp.log"


def alternate_value(api: UltimateApi, store: str, item: str, current: str) -> str:
    values = api.configs.item(store, item).get("values", [])
    for value in values:
        if isinstance(value, str) and value != current:
            return value
    raise Failure(f"{store}/{item} has no alternative value: {values!r}")


def upload_fixture(host: str, password: str, store: str, item: str, value: str) -> None:
    """The one-group .cfg this suite loads: one store, one item, one value."""
    cfg_fixture.upload(host, password, CFG_NAME,
                       f"[{store}]\n{item}={value}\n")


def load_fixture(browser) -> None:
    """Load it, and keep the debug log that says which stores were considered."""
    cfg_fixture.load(browser, CFG_NAME, log_name=LOG_NAME)


EFFECTUATED_PREFIX = "Effectuating settings of store '"
EFFECTUATED_SUFFIX = "' after loading."
CLEAN_PREFIX = "Store '"
CLEAN_SUFFIX = "' is clean after loading."


class LoaderReport(NamedTuple):
    """What the debug log says about one .cfg load.

    `effectuated` and `clean` are separate lists, because the loader prints a
    line per store either way and the two mean opposite things. Collecting
    both into one cannot tell a loader that applied one store from one that
    applied every store, which is the distinction
    cfg_partial_effectuate_test exists to make.

    `damaged` holds the log lines that carry part of a loader line but are not
    a whole one, so a caller can say the log was unreadable instead of
    reporting a store name that no store has.
    """

    effectuated: list[str]
    clean: list[str]
    damaged: list[str]


def store_name(line: str, prefix: str, suffix: str) -> str | None:
    """The store name in an intact loader line, or None when the line is not one.

    Both ends are required. The device writes the debug log with printf, which
    emits one character at a time through outbyte() with no lock (see
    software/system/small_printf.cc and software/application/ultimate/
    ultimate.cc), so a task switch part way through a line splices another
    task's output into the middle of it. The spliced text ends at the
    interrupting task's own newline, which cuts the loader line in two: the
    first half keeps the prefix and loses the suffix, and the second half
    keeps the suffix and has no prefix. Requiring the prefix and the suffix on
    the same line rejects both halves.

    A name containing a quote is rejected for the same reason: the loader
    passes get_store_name() straight to printf, and no store this suite has
    seen has a quote in its name, so a quote inside the name is a second
    record spliced into the first rather than a store.
    """
    if not line.startswith(prefix) or not line.endswith(suffix):
        return None
    name = line[len(prefix):len(line) - len(suffix)]
    if not name or "'" in name:
        return None
    return name


def looks_like_loader_line(line: str) -> bool:
    """Whether a line carries part of a loader record without being a whole one.

    Containment rather than startswith/endswith, because a splice can land
    anywhere. Measured on c64u: one run reported no effectuated store at all,
    because the record had been appended to another task's line and so no
    longer began with the prefix. Either half of a cut record is caught as
    well: the first half still holds a prefix, the second half a suffix.

    The two prefixes cannot be confused with each other. "Effectuating
    settings of store '" spells "store" in lower case and "Store '" in upper
    case, so a line holding one does not hold the other.
    """
    marks = (EFFECTUATED_PREFIX, CLEAN_PREFIX,
             EFFECTUATED_SUFFIX, CLEAN_SUFFIX)
    return any(mark in line for mark in marks)


def parse_loader_log(text: str) -> LoaderReport:
    """Read one saved debug log into the three lists.

    A line that holds one of the two prefixes or one of the two suffixes but
    is not a whole record is put in `damaged` rather than dropped. Dropping it
    would let an interleaved log read as a load that touched fewer stores than
    it did, which is the failure cfg_partial_effectuate_test is here to detect.

    Separate from `loader_report` so that tests/e2e/filemanager/
    cfg_loader_log_test.py can run it over a recorded log without a device.
    """
    effectuated: list[str] = []
    clean: list[str] = []
    damaged: list[str] = []
    for line in text.splitlines():
        applied = store_name(line, EFFECTUATED_PREFIX, EFFECTUATED_SUFFIX)
        untouched = store_name(line, CLEAN_PREFIX, CLEAN_SUFFIX)
        if applied is not None:
            effectuated.append(applied)
        elif untouched is not None:
            clean.append(untouched)
        elif looks_like_loader_line(line):
            damaged.append(line)
    return LoaderReport(effectuated, clean, damaged)


def loader_report(host: str, password: str) -> LoaderReport:
    """Fetch the debug log this run saved on the device, and read it."""
    with ftp_lib.session(host, password, timeout=20) as ftp:
        text = ftp_lib.retrieve(ftp, f"/Temp/{LOG_NAME}").decode("ascii", "replace")
    return parse_loader_log(text)


def cleanup(host: str, password: str) -> None:
    cfg_fixture.cleanup(host, password, CFG_NAME, LOG_NAME)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    cli.add_device_arguments(parser, timeout=5.0, colour=False)
    parser.add_argument("--telnet-port", type=int, default=int(os.environ.get("U64_TELNET_PORT", "23")))
    add_mode_argument(parser)
    args = parser.parse_args()

    api = UltimateApi(args.host, args.password or None, args.timeout)
    chosen = api.configs.find_padded_enum()
    if chosen is None:
        check_start("load a one-group CFG file through the browser")
        check_skip("no store this machine serves has an enum item to flip and put back")
        suite_ok("cfg_single_group_test")
        return 0
    store, item = chosen
    original = api.configs.current(store, item)
    browser = cfg_fixture.browser_for(args)
    try:
        with check("load a one-group CFG file through the browser"):
            upload_fixture(args.host, args.password, store, item,
                           alternate_value(api, store, item, original))
            load_fixture(browser)

        suite_ok("cfg_single_group_test")
        return 0
    except Exception as exc:  # noqa: BLE001
        suite_fail("cfg_single_group_test", format_exception(exc))
        return 1
    finally:
        teardown_step(f"restore {store}/{item}",
                    lambda: api.configs.set(store, item, original))
        teardown_step("close the browser session", browser.close)
        cleanup(args.host, args.password)


if __name__ == "__main__":
    raise SystemExit(main())
