#!/usr/bin/env python3
"""E2E: a CFG file applies only the configuration groups it contains.

The fixture changes one Audio Mixer setting, then loads it through the real
browser action.  The device debug log is the external record of which stores
the loader considered for effectuation, as exposed by the existing loader
diagnostics.  The test restores the mixer setting through the public config
API and removes both files it creates.
"""

import argparse
import os
import sys
from pathlib import Path
from typing import List

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "lib"))
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "lib"))

from api import UltimateApi
import ftp as ftp_lib
from report import Failure, check, format_exception, suite_fail, suite_ok
from ui_backend import add_mode_argument, make_browser


STORE = "Audio Mixer"
ITEM = "Vol Master"
CFG_NAME = "cfg-single-group-e2e.cfg"
LOG_NAME = "cfg-single-group-e2e.log"
ENTRY_ROWS = range(2, 24)
STATUS_ROW = 24
TELNET_ENTRY_ROWS = range(2, 23)
TELNET_STATUS_ROW = 23


def alternate_value(api: UltimateApi, current: str) -> str:
    values = api.configs.item(STORE, ITEM).get("values", [])
    for value in values:
        if isinstance(value, str) and value != current:
            return value
    raise Failure(f"{STORE}/{ITEM} has no alternative value: {values!r}")


def upload_fixture(host: str, password: str, value: str) -> None:
    payload = f"[{STORE}]\n{ITEM}={value}\n".encode("ascii")
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


def loading_stores(host: str, password: str) -> List[str]:
    with ftp_lib.session(host, password, timeout=20) as ftp:
        text = ftp_lib.retrieve(ftp, f"/Temp/{LOG_NAME}").decode("ascii", "replace")
    stores = []
    for line in text.splitlines():
        if line.startswith("Effectuating settings of store '"):
            stores.append(line.split("'", 2)[1])
        elif line.startswith("Store '") and line.endswith("is clean after loading."):
            stores.append(line.split("'", 2)[1])
    return stores


def cleanup(host: str, password: str) -> None:
    try:
        with ftp_lib.session(host, password, timeout=20) as ftp:
            ftp_lib.delete_quietly(ftp, f"/Temp/{CFG_NAME}")
            ftp_lib.delete_quietly(ftp, f"/Temp/{LOG_NAME}")
    except Exception:
        pass


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("-H", "--host", default=os.environ.get("U64_HOST", "u64"))
    parser.add_argument("-p", "--password", default=os.environ.get("U64_PASS", ""))
    parser.add_argument("-t", "--timeout", type=float, default=float(os.environ.get("U64_TIMEOUT", "5.0")))
    parser.add_argument("--telnet-port", type=int, default=int(os.environ.get("U64_TELNET_PORT", "23")))
    add_mode_argument(parser)
    args = parser.parse_args()

    api = UltimateApi(args.host, args.password or None, args.timeout)
    original = api.configs.current(STORE, ITEM)
    browser = make_browser(
        args.mode, args.host, args.password or None, args.timeout,
        entry_rows=ENTRY_ROWS, status_row=STATUS_ROW, telnet_port=args.telnet_port,
        telnet_entry_rows=TELNET_ENTRY_ROWS, telnet_status_row=TELNET_STATUS_ROW,
    )
    try:
        with check("load a one-group CFG file through the browser"):
            upload_fixture(args.host, args.password, alternate_value(api, original))
            load_fixture(browser)

        with check("only the CFG group is considered after loading"):
            stores = loading_stores(args.host, args.password)
            if len(stores) != 1 or not stores[0].startswith("Audio M"):
                raise Failure(f"Expected [{STORE!r}] after CFG load, got {stores!r}")

        suite_ok("cfg_single_group_test")
        return 0
    except Failure as exc:
        suite_fail("cfg_single_group_test", str(exc))
        return 1
    except Exception as exc:  # noqa: BLE001
        suite_fail("cfg_single_group_test", format_exception(exc))
        return 1
    finally:
        try:
            api.configs.set(STORE, ITEM, original)
        except Exception:
            pass
        try:
            browser.close()
        except Exception:
            pass
        cleanup(args.host, args.password)


if __name__ == "__main__":
    raise SystemExit(main())
