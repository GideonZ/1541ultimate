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

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "lib"))
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "lib"))

from api import UltimateApi
import ftp as ftp_lib
from report import (Failure, check, check_skip, check_start, detail,
                    format_exception, section, suite_fail, suite_ok)
from ui_backend import add_mode_argument, make_browser

CFG_NAME = "cfg-space-e2e.cfg"

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
    with ftp_lib.session(host, password, timeout=20) as ftp:
        ftp_lib.store(ftp, f"/Temp/{CFG_NAME}", body.encode("ascii"))


def load_cfg(browser) -> None:
    browser.go_to_directory("Temp")
    browser.select_entry(CFG_NAME)
    browser.invoke_context_action("Load Settings")
    browser.wait_for_text("Loading configuration successful!")
    browser.press_popup_button("o")


def cleanup(host: str, password: str) -> None:
    try:
        with ftp_lib.session(host, password, timeout=20) as ftp:
            ftp_lib.delete_quietly(ftp, f"/Temp/{CFG_NAME}")
    except Exception:
        pass


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("-H", "--host", default=os.environ.get("U64_HOST", "u64"))
    parser.add_argument("-p", "--password", default=os.environ.get("U64_PASS", ""))
    parser.add_argument("-t", "--timeout", type=float,
                        default=float(os.environ.get("U64_TIMEOUT", "5.0")))
    parser.add_argument("--telnet-port", type=int,
                        default=int(os.environ.get("U64_TELNET_PORT", "23")))
    add_mode_argument(parser)
    args = parser.parse_args()

    api = UltimateApi(args.host, args.password or None, args.timeout)
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
        try:
            api.configs.set(store, item, original)
        except Exception:
            pass
        try:
            browser.close()
        except Exception:
            pass
        cleanup(args.host, args.password)


if __name__ == "__main__":
    raise SystemExit(main())
