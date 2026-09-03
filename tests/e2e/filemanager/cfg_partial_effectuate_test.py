#!/usr/bin/env python3
"""E2E: a CFG file effectuates only the stores it names, and no others.

Split out of cfg_single_group_test.py and registered as manual, because the
behaviour it asserts is in disrepair rather than because the check is wrong.
Loading a partial .cfg is supposed to leave every store the file does not
mention alone. Measured on a C64 Ultimate 1.2.0: a file naming one store had
nineteen stores in its loader diagnostics, so the file was applied as though
it named all of them.

The half that still holds everywhere is that such a file loads at all, and
that stays in cfg_single_group_test.py and in the gate. Only this assertion,
about which stores were considered, is manual: a gate that fails on it every
run teaches a reader to ignore the suite, and the loading half is what would
then stop being watched.

Run it by hand, or with --manual, when the loader is changed:

    ./run-tests --suite cfg-partial-effectuate --manual u64

It shares the fixture and the log reader with cfg_single_group_test rather
than repeating them, so the two cannot drift apart.
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
import cli  # noqa: E402
sys.path.insert(0, bootstrap.directory("e2e", "filemanager"))

from api import UltimateApi
from report import (Failure, best_effort, check, check_skip, check_start, format_exception,
                    suite_fail, suite_ok)
from ui_backend import add_mode_argument, make_browser

from cfg_single_group_test import (ENTRY_ROWS, STATUS_ROW, TELNET_ENTRY_ROWS,
                                   TELNET_STATUS_ROW, alternate_value, cleanup,
                                   load_fixture, loading_stores, upload_fixture)

SUITE = "cfg_partial_effectuate_test"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    cli.add_device_arguments(parser, timeout=5.0, colour=False)
    parser.add_argument("--telnet-port", type=int,
                        default=int(os.environ.get("U64_TELNET_PORT", "23")))
    add_mode_argument(parser)
    args = parser.parse_args()

    api = UltimateApi(args.host, args.password or None, args.timeout)
    chosen = api.configs.find_padded_enum()
    if chosen is None:
        check_start("only the CFG group is considered after loading")
        check_skip("no store this machine serves has an enum item to flip and put back")
        suite_ok(SUITE)
        return 0
    store, item = chosen
    original = api.configs.current(store, item)
    browser = make_browser(
        args.mode, args.host, args.password or None, args.timeout,
        entry_rows=ENTRY_ROWS, status_row=STATUS_ROW, telnet_port=args.telnet_port,
        telnet_entry_rows=TELNET_ENTRY_ROWS, telnet_status_row=TELNET_STATUS_ROW,
    )
    try:
        with check("only the CFG group is considered after loading"):
            upload_fixture(args.host, args.password, store, item,
                           alternate_value(api, store, item, original))
            load_fixture(browser)
            stores = loading_stores(args.host, args.password)
            if stores != [store]:
                raise Failure(f"Expected [{store!r}] after CFG load, got {stores!r}")

        suite_ok(SUITE)
        return 0
    except Failure as exc:
        suite_fail(SUITE, str(exc))
        return 1
    except Exception as exc:  # noqa: BLE001
        suite_fail(SUITE, format_exception(exc))
        return 1
    finally:
        best_effort(f"restore {store}/{item}",
                    lambda: api.configs.set(store, item, original))
        best_effort("close the browser session", browser.close)
        cleanup(args.host, args.password)


if __name__ == "__main__":
    raise SystemExit(main())
