#!/usr/bin/env python3
"""E2E: a hand-edited CFG may space its names and values how a person would.

Enum labels are padded so the menu can right align them, and a volume ladder is
the plainest example on any machine: its values are " 0 dB", "-6 dB", "+3 dB".
A .cfg the firmware wrote carries that padding verbatim, but nobody typing one
by hand writes "Vol Master= 0 dB" with the leading space, and before this change
"Vol Master=0 dB" was answered with "not a valid choice" and the load failed.

Which store holds that ladder is asked of the machine rather than assumed: an
Ultimate 64 serves it as "Audio Mixer", an Ultimate II+L as "Audio Output
Settings". See ConfigsApi.find_padded_enum.

Two checks, both through the real loader on real hardware:

  - an unpadded value matches its padded label
  - spaces around the item name, and around the value, are ignored

The inner space is the interesting part of both: every value here contains one,
so a fix that merely stripped all whitespace would match nothing and these would
fail.

What is deliberately *not* here: that a value which is not a choice is still an
error, and that a string value keeps its spaces. Both are decided in
S_read_store_element and are covered by the host unit tests, which can assert
them without driving a popup and an editor. This suite exists to prove the path
works end to end, not to re-test the parser.
"""

import argparse
import os
import sys
from pathlib import Path

# tests/lib holds the shared library; importing bootstrap adds tests/e2e/lib.
# The search walks up rather than counting directories, so this is the same in
# every entry point and a suite that moves needs no edit. See tests/lib/bootstrap.py.
sys.path.insert(0, str(next(p for p in Path(__file__).resolve().parents
                            if (p / "tests" / "lib").is_dir()) / "tests" / "lib"))
import bootstrap  # noqa: E402,F401

# cfg_fixture is beside this file, which is on the path when this runs as
# a script but not when another suite imports it.
sys.path.insert(0, bootstrap.directory("e2e", "filemanager"))
import cfg_fixture  # noqa: E402
import cli  # noqa: E402

from api import UltimateApi
import machine as machine_lib
import targets
from report import (Failure, teardown_step, check, check_skip, check_start, detail,
                    format_exception, section, suite_fail, suite_ok)
from ui_backend import add_mode_argument, make_browser

CFG_NAME = "cfg-sp.cfg"

ENTRY_ROWS = range(2, 24)
STATUS_ROW = 24
TELNET_ENTRY_ROWS = range(2, 23)
TELNET_STATUS_ROW = 23


def padded_value(api: UltimateApi, store: str, item: str) -> str:
    """The value carrying leading padding, which is what makes this test mean
    something. On a volume ladder that is " 0 dB", padded to the width of the
    negative values beside it."""
    values = api.configs.item(store, item).get("values", [])
    for value in values:
        if isinstance(value, str) and value != value.strip() and " " in value.strip():
            return value
    raise Failure(f"{store}/{item} has no padded value to test with: {values!r}")


def plain_value(api: UltimateApi, store: str, item: str, avoid: str) -> str:
    """A value with no padding but an inner space, e.g. "-6 dB"."""
    values = api.configs.item(store, item).get("values", [])
    for value in values:
        if isinstance(value, str) and value != avoid and value == value.strip() and " " in value:
            return value
    raise Failure(f"{store}/{item} has no unpadded value to test with: {values!r}")


def upload(host: str, password: str, body: str) -> None:
    cfg_fixture.upload(host, password, CFG_NAME, body)


def load_cfg(browser) -> None:
    cfg_fixture.load(browser, CFG_NAME)


def cleanup(host: str, password: str) -> None:
    cfg_fixture.cleanup(host, password, CFG_NAME)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    cli.add_device_arguments(parser, timeout=5.0, colour=False)
    parser.add_argument("--telnet-port", type=int,
                        default=int(os.environ.get("U64_TELNET_PORT", "23")))
    add_mode_argument(parser)
    args = parser.parse_args()

    api = UltimateApi(args.host, args.password or None, args.timeout)
    info = api.info()
    device = machine_lib.identify(
        targets.device_of(args.host),
        lambda: (info.product, info.firmware_version))
    if device.skip_without_fix(machine_lib.CFG_LOADS_UNKNOWN_AND_PADDED,
                               "a CFG written the way a person would loads and applies"):
        suite_ok("cfg_whitespace_test")
        return 0
    chosen = api.configs.find_padded_enum()
    if chosen is None:
        check_start("a CFG written the way a person would loads and applies")
        check_skip("no store this machine serves has an enum whose labels are padded")
        suite_ok("cfg_whitespace_test")
        return 0
    store, item = chosen
    original = api.configs.current(store, item)
    browser = make_browser(
        args.mode, args.host, args.password or None, args.timeout,
        entry_rows=ENTRY_ROWS, status_row=STATUS_ROW, telnet_port=args.telnet_port,
        telnet_entry_rows=TELNET_ENTRY_ROWS, telnet_status_row=TELNET_STATUS_ROW,
    )

    try:
        section("an unpadded value matches its padded label")
        wanted = padded_value(api, store, item)
        # Start somewhere else, so applying the file is what moves it.
        api.configs.set(store, item, plain_value(api, store, item, wanted))
        with check(f"a CFG saying {item}={wanted.strip()!r} loads and applies"):
            # Written the way a person would: no leading padding, but the space
            # inside the label kept, because that one is part of the value.
            upload(args.host, args.password, f"[{store}]\n{item}={wanted.strip()}\n")
            load_cfg(browser)
            now = api.configs.current(store, item)
            if now != wanted:
                raise Failure(
                    f"{store}/{item} is {now!r}, expected the padded label {wanted!r}")
            detail(f"file said {wanted.strip()!r}, device holds {now!r}")

        section("spaces around the name and the value are ignored")
        spaced = plain_value(api, store, item, wanted)
        api.configs.set(store, item, wanted)
        with check("a CFG with spaces around both loads and applies"):
            upload(args.host, args.password, f"[{store}]\n  {item}  =  {spaced}  \n")
            load_cfg(browser)
            now = api.configs.current(store, item)
            if now != spaced:
                raise Failure(f"{store}/{item} is {now!r}, expected {spaced!r}")
            detail(f"file said '  {item}  =  {spaced}  ', device holds {now!r}")

        suite_ok("cfg_whitespace_test")
        return 0
    except Failure as exc:
        suite_fail("cfg_whitespace_test", str(exc))
        return 1
    except Exception as exc:  # noqa: BLE001
        suite_fail("cfg_whitespace_test", format_exception(exc))
        return 1
    finally:
        teardown_step(f"restore {store}/{item}",
                    lambda: api.configs.set(store, item, original))
        teardown_step("close the browser session", browser.close)
        cleanup(args.host, args.password)


if __name__ == "__main__":
    raise SystemExit(main())
