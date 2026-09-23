#!/usr/bin/env python3
"""E2E: loading a Software IEC partition file replaces the drive's partitions (#934).

The file browser's Load on an .IPR file sets the drive's partition list to the one in the
file. A partition the file does not name goes, and a file with no usable entry changes
nothing. The drive list over REST (`/v1/drives`) is what the checks read back.

The suite first saves the drive's own list with Software IEC > Save Partitions, which is
how it puts the drive back at the end, and which also shows that a saved list loads.
"""

import argparse
import json
import os
import sys
from pathlib import Path

# The one stanza that puts the shared library on sys.path; see tests/lib/bootstrap.py.
sys.path.insert(0, str(next(p for p in Path(__file__).resolve().parents
                            if (p / "tests" / "lib").is_dir()) / "tests" / "lib"))
import bootstrap  # noqa: E402,F401

sys.path.insert(0, bootstrap.directory("e2e", "filemanager"))
import cfg_fixture  # noqa: E402
import cli  # noqa: E402
import ftp as ftp_lib  # noqa: E402
from api import UltimateApi  # noqa: E402
from report import Failure, check, detail, format_exception, suite_fail, suite_ok, teardown_step  # noqa: E402
from ui_backend import add_mode_argument  # noqa: E402

SUITE = "ipr_load_test"
SAVED = "e2eiprorig"
WIDER = "e2eiprwide.ipr"
NARROW = "e2eiprnarrow.ipr"
EMPTY = "e2eipremty.ipr"
# Partition numbers the bench does not use, for the entries the wider file adds.
EXTRA = (201, 202)


def partitions(api):
    """{number: path} of the drive's partitions, from the drive list."""
    for entry in api.rest.json("/v1/drives")["drives"]:
        if "IEC Drive" in entry:
            return {p["id"]: p["path"].rstrip("/") for p in entry["IEC Drive"]["partitions"]}
    raise Failure("the drive list has no IEC Drive")


def ipr(entries):
    """The text of a partition file holding `entries`, (number, path, name) each."""
    return json.dumps({"version": 1, "partitions": [
        {"number": n, "path": path, "name": name} for n, path, name in entries]})


def load(browser, name):
    browser.go_to_directory("Temp")
    browser.select_entry(name)
    browser.invoke_context_action("Load")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    cli.add_device_arguments(parser, timeout=5.0, colour=False)
    parser.add_argument("--telnet-port", type=int, default=int(os.environ.get("U64_TELNET_PORT", "23")))
    add_mode_argument(parser)
    args = parser.parse_args()

    api = UltimateApi(args.host, args.password or None, args.timeout)
    browser = cfg_fixture.browser_for(args)
    original = partitions(api)
    detail(f"the drive's partitions: {original}")
    saved = False
    try:
        with check("Save Partitions writes the drive's list to /Temp"):
            browser.go_to_directory("Temp")
            browser.invoke_task_action("Software IEC", "Save Partitions")
            browser.fill_edit_field(SAVED)
            with ftp_lib.session(args.host, args.password) as client:
                text = ftp_lib.retrieve(client, f"/Temp/{SAVED}.ipr").decode("utf-8")
            saved = True
            written = {p["number"]: p["path"].rstrip("/") for p in json.loads(text)["partitions"]}
            if written != original:
                raise Failure(f"the file holds {written}, the drive {original}")
            named = [(p["number"], p["path"], p["name"]) for p in json.loads(text)["partitions"]]

        with ftp_lib.session(args.host, args.password) as client:
            ftp_lib.store(client, f"/Temp/{WIDER}",
                          ipr(named + [(n, "/Temp/", f"EXTRA{n}") for n in EXTRA]).encode("ascii"))
            ftp_lib.store(client, f"/Temp/{NARROW}", ipr(named).encode("ascii"))
            ftp_lib.store(client, f"/Temp/{EMPTY}", ipr([]).encode("ascii"))

        with check("loading a list with two more partitions adds them"):
            load(browser, WIDER)
            now = partitions(api)
            detail(f"after {WIDER}: {now}")
            if set(now) != set(original) | set(EXTRA):
                raise Failure(f"the drive has partitions {sorted(now)}")

        with check("loading a list without them removes them"):
            load(browser, NARROW)
            now = partitions(api)
            detail(f"after {NARROW}: {now}")
            if now != original:
                raise Failure(f"the drive has {now}, the file names {original}")

        with check("loading a list with no partition changes nothing"):
            load(browser, EMPTY)
            now = partitions(api)
            if now != original:
                raise Failure(f"the drive has {now} after an empty list, {original} before it")

        suite_ok(SUITE)
        return 0
    except Exception as exc:  # noqa: BLE001
        suite_fail(SUITE, format_exception(exc))
        return 1
    finally:
        def restore():
            load(browser, f"{SAVED}.ipr")
            now = partitions(api)
            if now != original:
                raise Failure(f"the drive has {now}, not {original}")
        if saved:
            teardown_step("load the drive's own list back", restore)
        teardown_step("close the browser session", browser.close)

        def remove():
            with ftp_lib.session(args.host, args.password) as client:
                for name in (f"{SAVED}.ipr", WIDER, NARROW, EMPTY):
                    if name in ftp_lib.names(client, "/Temp"):
                        client.delete(f"/Temp/{name}")
        teardown_step("remove the partition files", remove)


if __name__ == "__main__":
    raise SystemExit(main())
