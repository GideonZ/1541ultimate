#!/usr/bin/env python3
"""E2E: a CFG file effectuates only the stores it names, and no others.

Split out of cfg_single_group_test.py and registered as manual. Loading a
partial .cfg is supposed to leave every store the file does not mention alone.

What the loader actually does, measured on a C64 Ultimate 1.2.0 on 2026-09-05:
it iterates every store and effectuates the ones whose staleEffect is set, so a
.cfg naming one store also flushes every other store that has an unapplied
change pending. The same file loaded twice in a row gives 19 stores effectuated
and then 1, because the first load cleared what was pending. GideonZ/1541ultimate
#765 fixed this by iterating only the stores the file loaded; commit 03284765
removed that again for CBM merge compatibility.

Two consequences for anyone changing this suite.

The failure is state dependent. It reports on the session's history as much as
on the loader, so it can pass on a settled machine and fail after a boot or a
run that left changes pending. Establishing that nothing else is stale before
loading the .cfg is what would make it a gate; until then it is manual for that
reason, not because the behaviour is beyond repair.

The two loader log lines mean opposite things and must not be counted together.
`Effectuating settings of store 'X' after loading.` is a store that was applied;
`Store 'X' is clean after loading.` is a store that was not. This suite used to
collect both into one list, which could not tell a loader that applied one store
from one that applied all of them, so it failed on correct firmware too. See
loader_report in cfg_single_group_test.py.

The half that holds everywhere is that such a file loads at all, and that stays
in cfg_single_group_test.py and in the gate.

Run it by hand, or with --manual, when the loader is changed:

    ./run-tests --suite cfg-partial-effectuate --manual u64

It shares the fixture and the log reader with cfg_single_group_test rather
than repeating them, so the two cannot drift apart.
"""

import argparse
import os
import sys
from pathlib import Path

# The one stanza that puts the shared library on sys.path; see tests/lib/bootstrap.py.
sys.path.insert(0, str(next(p for p in Path(__file__).resolve().parents
                            if (p / "tests" / "lib").is_dir()) / "tests" / "lib"))
import bootstrap  # noqa: E402,F401
import cli  # noqa: E402
sys.path.insert(0, bootstrap.directory("e2e", "filemanager"))

from api import UltimateApi
from report import (Failure, detail, teardown_step, check, check_skip, check_start,
                    format_exception, suite_fail, suite_ok)
from ui_backend import add_mode_argument

import cfg_fixture  # noqa: E402
from cfg_single_group_test import (alternate_value, cleanup, load_fixture,
                                   loader_report, upload_fixture)

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
    browser = cfg_fixture.browser_for(args)
    try:
        with check("only the CFG group is considered after loading"):
            upload_fixture(args.host, args.password, store, item,
                           alternate_value(api, store, item, original))
            load_fixture(browser)
            effectuated, clean = loader_report(args.host, args.password)
            detail(f"effectuated {len(effectuated)}, left clean {len(clean)}")
            if effectuated != [store]:
                extra = [name for name in effectuated if name != store]
                raise Failure(
                    f"the .cfg named only {store!r}, but the loader effectuated "
                    f"{len(effectuated)} store(s): {effectuated!r}. "
                    f"{len(extra)} store(s) the file never mentioned were applied: "
                    f"{extra!r}. Stores left alone: {clean!r}")

        suite_ok(SUITE)
        return 0
    except Exception as exc:  # noqa: BLE001
        suite_fail(SUITE, format_exception(exc))
        return 1
    finally:
        teardown_step(f"restore {store}/{item}",
                    lambda: api.configs.set(store, item, original))
        teardown_step("close the browser session", browser.close)
        cleanup(args.host, args.password)


if __name__ == "__main__":
    raise SystemExit(main())
