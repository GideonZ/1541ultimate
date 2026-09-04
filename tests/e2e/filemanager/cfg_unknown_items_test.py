#!/usr/bin/env python3
"""E2E: a CFG file naming things this machine does not have still loads.

A .cfg saved on one Ultimate and loaded on another is the ordinary case, and
the two machines rarely have the same hardware: a SID replacement in a socket,
a different firmware version, a store that simply is not there. Those files
used to be reported as errors, which put a dialog in front of the user for
something that is not wrong.

Both fixtures here pair the unknown thing with a real setting change, so the
test proves two things at once: the load is not rejected, and the settings it
*could* apply were applied. The device debug log is the external record of what
the loader decided, the same mechanism cfg_single_group_test.py uses.

The store-selection half of this change -- which stores are written to a .cfg
-- is covered by the host unit tests instead. A SID replacement store only
exists when that cartridge is plugged in, and no such hardware is on the test
bench.

The known store both fixtures pair the unknown thing with is asked of the
machine rather than assumed: an Ultimate 64 serves it as "Audio Mixer", an
Ultimate II+L as "Audio Output Settings". See ConfigsApi.find_padded_enum.
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
import machine as machine_lib
import targets
import ftp as ftp_lib
from report import (Failure, teardown_step, check, check_skip, check_start, detail,
                    format_exception, section, suite_fail, suite_ok)
from ui_backend import add_mode_argument

# Named so a leftover from a failed run is obvious in /Temp.
CFG_NAME = "cfg-unk.cfg"
LOG_NAME = "cfg-unk.log"

UNKNOWN_ITEM = "No Such Item"
UNKNOWN_ITEM_VALUE = "12345"
UNKNOWN_STORE = "No Such Store"



def alternate_value(api: UltimateApi, store: str, item: str, current: str) -> str:
    values = api.configs.item(store, item).get("values", [])
    for value in values:
        if isinstance(value, str) and value != current:
            return value
    raise Failure(f"{store}/{item} has no alternative value: {values!r}")


def upload(host: str, password: str, body: str) -> None:
    cfg_fixture.upload(host, password, CFG_NAME, body)


def load_cfg(browser) -> None:
    """Load the fixture and capture the debug log it produced.

    wait_for_text is the assertion that matters: before this change a file
    with an unknown item answered "There were errors." and put the log in an
    editor, so reaching the success popup at all is the behaviour under test.
    """
    cfg_fixture.load(browser, CFG_NAME, log_name=LOG_NAME)


def debug_log(host: str, password: str) -> str:
    with ftp_lib.session(host, password, timeout=20) as ftp:
        return ftp_lib.retrieve(ftp, f"/Temp/{LOG_NAME}").decode("ascii", "replace")


def require_in_log(log: str, needles: list[str], what: str) -> None:
    missing = [n for n in needles if n not in log]
    if missing:
        raise Failure(f"{what}: the debug log never mentioned {missing!r}")


def cleanup(host: str, password: str) -> None:
    cfg_fixture.cleanup(host, password, CFG_NAME, LOG_NAME)


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
                               "a CFG with an unknown item loads without being called an error"):
        suite_ok("cfg_unknown_items_test")
        return 0
    chosen = api.configs.find_padded_enum()
    if chosen is None:
        check_start("a CFG with an unknown item loads without being called an error")
        check_skip("no store this machine serves has an enum item to flip and put back")
        suite_ok("cfg_unknown_items_test")
        return 0
    store, item = chosen
    original = api.configs.current(store, item)
    browser = cfg_fixture.browser_for(args)

    try:
        section("an item this firmware does not have")
        wanted = alternate_value(api, store, item, original)
        with check("a CFG with an unknown item loads without being called an error"):
            upload(args.host, args.password,
                   f"[{store}]\n{item}={wanted}\n{UNKNOWN_ITEM}={UNKNOWN_ITEM_VALUE}\n")
            load_cfg(browser)

        with check("the settings it could apply were applied"):
            now = api.configs.current(store, item)
            if now != wanted:
                raise Failure(f"{store}/{item} is {now!r}, expected {wanted!r}")
            detail(f"{store}/{item}: {original!r} -> {now!r}")

        with check("the log names the unknown item and its value"):
            log = debug_log(args.host, args.password)
            require_in_log(log, [UNKNOWN_ITEM, UNKNOWN_ITEM_VALUE], "unknown item")

        section("a store this machine does not have")
        api.configs.set(store, item, original)
        with check("a CFG naming an absent store loads without being called an error"):
            upload(args.host, args.password,
                   f"[{UNKNOWN_STORE}]\nWhatever=1\n\n[{store}]\n{item}={wanted}\n")
            load_cfg(browser)

        with check("the store that does exist was still applied"):
            now = api.configs.current(store, item)
            if now != wanted:
                raise Failure(f"{store}/{item} is {now!r}, expected {wanted!r}")

        with check("the log names the absent store"):
            log = debug_log(args.host, args.password)
            require_in_log(log, [UNKNOWN_STORE], "unknown store")

        suite_ok("cfg_unknown_items_test")
        return 0
    except Failure as exc:
        suite_fail("cfg_unknown_items_test", str(exc))
        return 1
    except Exception as exc:  # noqa: BLE001
        suite_fail("cfg_unknown_items_test", format_exception(exc))
        return 1
    finally:
        teardown_step(f"restore {store}/{item}",
                    lambda: api.configs.set(store, item, original))
        teardown_step("close the browser session", browser.close)
        cleanup(args.host, args.password)


if __name__ == "__main__":
    raise SystemExit(main())
