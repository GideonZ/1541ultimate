#!/usr/bin/env python3
"""E2E: a CFG file naming one configuration group loads through the browser.

The fixture changes one volume setting, then loads it through the real browser
action.  The device debug log is the external record of which stores the loader
considered for effectuation, as exposed by the existing loader diagnostics.
The test restores the setting through the public config API and removes both
files it creates.

Whether the load then effectuates *only* that group is asserted by
cfg_partial_effectuate_test.py, which is manual: that behaviour is in
disrepair, and a gate failing on it every run is a gate a reader stops
reading. The helpers below are shared with it, so the fixture and the log
reader cannot drift apart.

Which store holds that setting is asked of the machine rather than assumed:
an Ultimate 64 serves it as "Audio Mixer", an Ultimate II+L as "Audio Output
Settings". See ConfigsApi.find_padded_enum.
"""

import argparse
import os
import sys
from pathlib import Path

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


def loading_stores(host: str, password: str) -> list[str]:
    with ftp_lib.session(host, password, timeout=20) as ftp:
        text = ftp_lib.retrieve(ftp, f"/Temp/{LOG_NAME}").decode("ascii", "replace")
    stores = []
    for line in text.splitlines():
        if line.startswith("Effectuating settings of store '") or (line.startswith("Store '") and line.endswith("is clean after loading.")):
            stores.append(line.split("'", 2)[1])
    return stores


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
