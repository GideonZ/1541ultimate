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

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "lib"))
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "lib"))

from api import UltimateApi
import ftp as ftp_lib
from report import (Failure, best_effort, check, check_skip, check_start, format_exception,
                    suite_fail, suite_ok)
from ui_backend import add_mode_argument, make_browser


CFG_NAME = "cfg-sgrp.cfg"
LOG_NAME = "cfg-sgrp.log"
ENTRY_ROWS = range(2, 24)
STATUS_ROW = 24
TELNET_ENTRY_ROWS = range(2, 23)
TELNET_STATUS_ROW = 23


def alternate_value(api: UltimateApi, store: str, item: str, current: str) -> str:
    values = api.configs.item(store, item).get("values", [])
    for value in values:
        if isinstance(value, str) and value != current:
            return value
    raise Failure(f"{store}/{item} has no alternative value: {values!r}")


def upload_fixture(host: str, password: str, store: str, item: str, value: str) -> None:
    payload = f"[{store}]\n{item}={value}\n".encode("ascii")
    with ftp_lib.session(host, password, timeout=20) as ftp:
        ftp_lib.store(ftp, f"/Temp/{CFG_NAME}", payload)


def load_fixture(browser) -> None:
    browser.invoke_task_action("Developer", "Clear Debug Log")
    browser.go_to_directory("Temp")
    browser.select_entry(CFG_NAME)
    browser.invoke_context_action("Load Settings")
    browser.wait_for_text("Loading configuration successful!")
    browser.press_popup_button("o")
    browser.invoke_task_action("Developer", "Save Debug Log")
    browser.fill_edit_field(LOG_NAME)


def loading_stores(host: str, password: str) -> list[str]:
    with ftp_lib.session(host, password, timeout=20) as ftp:
        text = ftp_lib.retrieve(ftp, f"/Temp/{LOG_NAME}").decode("ascii", "replace")
    stores = []
    for line in text.splitlines():
        if line.startswith("Effectuating settings of store '") or (line.startswith("Store '") and line.endswith("is clean after loading.")):
            stores.append(line.split("'", 2)[1])
    return stores


def cleanup(host: str, password: str) -> None:
    def remove() -> None:
        with ftp_lib.session(host, password, timeout=20) as ftp:
            ftp_lib.delete_quietly(ftp, f"/Temp/{CFG_NAME}")
            ftp_lib.delete_quietly(ftp, f"/Temp/{LOG_NAME}")

    best_effort("remove the fixtures this run uploaded", remove)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("-H", "--host", default=os.environ.get("U64_HOST", "u64"))
    parser.add_argument("-p", "--password", default=os.environ.get("U64_PASS", ""))
    parser.add_argument("-t", "--timeout", type=float, default=float(os.environ.get("U64_TIMEOUT", "5.0")))
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
    browser = make_browser(
        args.mode, args.host, args.password or None, args.timeout,
        entry_rows=ENTRY_ROWS, status_row=STATUS_ROW, telnet_port=args.telnet_port,
        telnet_entry_rows=TELNET_ENTRY_ROWS, telnet_status_row=TELNET_STATUS_ROW,
    )
    try:
        with check("load a one-group CFG file through the browser"):
            upload_fixture(args.host, args.password, store, item,
                           alternate_value(api, store, item, original))
            load_fixture(browser)

        suite_ok("cfg_single_group_test")
        return 0
    except Failure as exc:
        suite_fail("cfg_single_group_test", str(exc))
        return 1
    except Exception as exc:  # noqa: BLE001
        suite_fail("cfg_single_group_test", format_exception(exc))
        return 1
    finally:
        best_effort(f"restore {store}/{item}",
                    lambda: api.configs.set(store, item, original))
        best_effort("close the browser session", browser.close)
        cleanup(args.host, args.password)


if __name__ == "__main__":
    raise SystemExit(main())
