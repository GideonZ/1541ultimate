#!/usr/bin/env python3
# E2E: Verifies the file browser's CBM names view (C=+U) and renaming in it.

"""Validate the browser's CBM names view through the real menu UI (issue 976).

A file the IEC drive stores under a shifted name has a host name such as
{C6CFCF}.prg. C=+U switches the browser between those host names and the
names the drive lists, shown in the lower/upper case set, and the status line
says "CBM L/U" while the view is on. This suite checks, on real firmware:

- the rows and the status line in both views, including an entry whose host
  name stores no CBM name and so keeps its host name;
- that quick seek matches the name the row shows;
- that Rename in the view edits the CBM name and stores it the way the drive
  does, leaves an unchanged name alone and refuses text that is no CBM name;
- that the help screen lists the shortcut.

Drives the on-device browser through tests/e2e/lib/ui_backend.py's Browser,
so --mode selects telnet, freeze or overlay.
"""

import argparse
import io
import os
import re
import sys
import time
from pathlib import Path

# The one stanza that puts the shared library on sys.path; see tests/lib/bootstrap.py.
sys.path.insert(0, str(next(p for p in Path(__file__).resolve().parents
                            if (p / "tests" / "lib").is_dir()) / "tests" / "lib"))
import bootstrap  # noqa: E402,F401
import cli  # noqa: E402
import ftp as ftp_lib
from api import UltimateApi
from report import Failure, teardown_step, check, format_exception, suite_fail, suite_ok
from ui_backend import Browser, add_mode_argument, make_browser, strip_frame

TEST_DIR_PREFIX = "zcbm-"
PRG = b"\x01\x08\x00\x00"

# Host name -> the name the CBM names view shows for it.
SEED = {
    "{C6CFCF}.prg": "FOO",      # shifted letters, as saved in lower/upper case mode
    "{C2C1C4}.prg": "BAD",
    "bar.prg": "bar",           # unshifted letters, whatever case the host gives them
    "MY{X}.prg": "MY{X}.prg",   # braces that open no escape: no CBM name, host name kept
}
RENAMED_TEXT = "Hello World"
RENAMED_HOST = "{C8}ELLO {D7}ORLD.prg"
STATUS_TAG = "CBM L/U"
CBM_TITLE = "New CBM name (L/U).."
HOST_TITLE = "Give a new name.."
REFUSED_POPUP = "Not a CBM name"
HELP_LINE = "C= U:       Toggle CBM names (L/U)"

# The same listing geometry browser_long_filename_test.py uses.
ENTRY_ROWS = range(2, 24)
STATUS_ROW = 24
TELNET_ENTRY_ROWS = range(2, 23)
TELNET_STATUS_ROW = 23
TELNET_WIDTH = 60
TELNET_HEIGHT = 24

ROW_RE = re.compile(r"^(?P<name>.+?)\s{2,}PRG\b")


def host_names(host: str, password: str, test_dir: str) -> set[str]:
    ftp = ftp_lib.connect(host, password, timeout=20)
    try:
        return set(ftp_lib.names(ftp, f"/Temp/{test_dir}"))
    finally:
        ftp_lib.close(ftp)


def seed(host: str, password: str, test_dir: str) -> None:
    ftp = ftp_lib.connect(host, password, timeout=20)
    try:
        ftp_lib.quietly(lambda: ftp.mkd(f"/Temp/{test_dir}"))
        for name in SEED:
            ftp.storbinary(f"STOR /Temp/{test_dir}/{name}", io.BytesIO(PRG))
    finally:
        ftp_lib.close(ftp)
    found = host_names(host, password, test_dir)
    if found != set(SEED):
        raise Failure(f"seeded host names are {sorted(found)}, expected {sorted(SEED)}")


def cleanup(host: str, password: str, test_dir: str) -> None:
    ftp = ftp_lib.connect(host, password, timeout=20)
    try:
        for name in ftp_lib.names(ftp, f"/Temp/{test_dir}"):
            ftp_lib.delete_quietly(ftp, f"/Temp/{test_dir}/{name}")
        ftp_lib.delete_quietly(ftp, f"/Temp/{test_dir}")
    finally:
        ftp_lib.close(ftp)


def listed(browser: Browser) -> list[str]:
    rows = browser.rows()
    names = []
    for index in browser.entry_rows:
        if index < len(rows):
            match = ROW_RE.match(strip_frame(rows[index]).strip())
            if match:
                names.append(match.group("name"))
    return names


def status(browser: Browser) -> str:
    return browser.rows()[browser.status_row]


def wait_for_listing(browser: Browser, expected: set[str], tagged: bool, what: str,
                     timeout: float = 8.0) -> None:
    deadline = time.monotonic() + timeout
    while True:
        names = set(listed(browser))
        line = status(browser)
        if names == expected and ((STATUS_TAG in line) == tagged):
            return
        if time.monotonic() >= deadline:
            raise Failure(f"{what}: listing {sorted(names)}, status {line!r}; expected "
                          f"{sorted(expected)} with the tag {'shown' if tagged else 'absent'}; "
                          f"screen was:\n{browser.screen()}")
        time.sleep(0.15)


def selected_name(browser: Browser) -> str:
    match = ROW_RE.match(strip_frame(browser.selected_text()).strip())
    return match.group("name") if match else ""


def open_rename(browser: Browser, name: str, title: str) -> None:
    browser.select_entry(name)
    if selected_name(browser) != name:
        raise Failure(f"expected {name!r} selected, got {browser.selected_text()!r}")
    browser.choose_overlay_item(browser.open_context_menu(), "Rename")
    browser.wait_for_text(title)


def field_text(browser: Browser, title: str) -> str:
    rows = browser.rows()
    title_row = next(i for i, row in enumerate(rows) if title in row)
    return strip_frame(rows[title_row + 2]).strip()


def run_views(browser: Browser, test_dir: str) -> None:
    browser.go_to_directory(f"Temp/{test_dir}")
    wait_for_listing(browser, set(SEED), False, "host names view")
    browser.select_entry("{C6CFCF}.prg")
    row = browser.selected_row()
    browser.press("CBM_U")
    wait_for_listing(browser, set(SEED.values()), True, "CBM names view")
    # Switching views keeps the cursor on the entry it was on.
    if (selected_name(browser) != "FOO") or (browser.selected_row() != row):
        raise Failure(f"after C=+U the cursor is on {browser.selected_text()!r} at row "
                      f"{browser.selected_row()}, expected FOO at row {row}")


def run_quick_seek(browser: Browser) -> None:
    # "bad" matches the label BAD only; bar comes first in the listing and
    # shares "ba", so the cursor has to move on to the row the label is on.
    browser.go_to_top()
    for ch in "bad":
        browser.type_menu_char(ch)
    if selected_name(browser) != "BAD":
        raise Failure(f"quick seek 'bad' selected {browser.selected_text()!r}")
    browser.press("DOWN")  # clears the seek string
    browser.press("UP")


def run_rename(host: str, password: str, browser: Browser, test_dir: str) -> None:
    open_rename(browser, "BAD", CBM_TITLE)
    if field_text(browser, CBM_TITLE) != "BAD":
        raise Failure(f"rename field shows {field_text(browser, CBM_TITLE)!r}, expected 'BAD'")
    browser.fill_edit_field(RENAMED_TEXT, clear_taps=8)
    expected = (set(SEED) - {"{C2C1C4}.prg"}) | {RENAMED_HOST}
    deadline = time.monotonic() + 8.0
    while host_names(host, password, test_dir) != expected:
        if time.monotonic() >= deadline:
            raise Failure(f"host names after rename are "
                          f"{sorted(host_names(host, password, test_dir))}, expected {sorted(expected)}")
        time.sleep(0.25)
    wait_for_listing(browser, (set(SEED.values()) - {"BAD"}) | {RENAMED_TEXT}, True,
                     "CBM names view after rename")


def run_unchanged_rename(host: str, password: str, browser: Browser, test_dir: str) -> None:
    # Stored again, "bar" would become BAR.prg; an unchanged name is left alone.
    before = host_names(host, password, test_dir)
    open_rename(browser, "bar", CBM_TITLE)
    browser.press("ENTER")
    browser.wait_until_gone(CBM_TITLE)
    after = host_names(host, password, test_dir)
    if after != before:
        raise Failure(f"an unchanged name changed the host names from {sorted(before)} to {sorted(after)}")


def run_refused_rename(host: str, password: str, browser: Browser, test_dir: str) -> None:
    # Seventeen letters: one more than a CBM name holds, so it is refused rather than cut.
    # Letters, because every transport can type them.
    before = host_names(host, password, test_dir)
    open_rename(browser, "bar", CBM_TITLE)
    browser.fill_edit_field("abcdefghijklmnopq", clear_taps=8)
    browser.wait_for_text(REFUSED_POPUP)
    browser.press_popup_button("o")
    browser.wait_until_gone(REFUSED_POPUP)
    after = host_names(host, password, test_dir)
    if after != before:
        raise Failure(f"refused text changed the host names from {sorted(before)} to {sorted(after)}")


def run_host_name_fallback(browser: Browser) -> None:
    open_rename(browser, "MY{X}.prg", HOST_TITLE)
    if field_text(browser, HOST_TITLE) != "MY{X}.prg":
        raise Failure(f"fallback rename field shows {field_text(browser, HOST_TITLE)!r}")
    browser.press("RUNSTOP")
    browser.wait_until_gone(HOST_TITLE)


def run_help(browser: Browser) -> None:
    browser.press(browser.backend.machine.help_key)
    try:
        for _ in range(6):
            if HELP_LINE in browser.screen():
                return
            browser.press("F7")
        raise Failure(f"help never showed {HELP_LINE!r}; last screen was:\n{browser.screen()}")
    finally:
        browser.press("RUNSTOP")


def run_back_to_host_names(browser: Browser) -> None:
    browser.press("CBM_U")
    wait_for_listing(browser, (set(SEED) - {"{C2C1C4}.prg"}) | {RENAMED_HOST}, False,
                     "host names view again")


def leave_host_names_view(browser: Browser) -> None:
    # The view belongs to the user interface, which outlives this session on
    # the freeze and overlay transports.
    if STATUS_TAG in status(browser):
        browser.press("CBM_U")


def main() -> int:
    parser = argparse.ArgumentParser(description="Validate the browser's CBM names view on real firmware.")
    cli.add_device_arguments(parser, timeout=5.0, colour=False)
    parser.add_argument("--telnet-port", type=int, default=int(os.environ.get("U64_TELNET_PORT", "23")))
    parser.add_argument("--test-dir", default=f"{TEST_DIR_PREFIX}{int(time.time())}-{os.getpid()}")
    add_mode_argument(parser)
    args = parser.parse_args()

    with check("reset the machine to a clean starting state"):
        UltimateApi(args.host, args.password).machine.reset(force=True, wait=False)
        time.sleep(0.5)

    browser = make_browser(
        args.mode, args.host, args.password or None, args.timeout,
        entry_rows=ENTRY_ROWS, status_row=STATUS_ROW,
        telnet_port=args.telnet_port, telnet_width=TELNET_WIDTH, telnet_height=TELNET_HEIGHT,
        telnet_entry_rows=TELNET_ENTRY_ROWS, telnet_status_row=TELNET_STATUS_ROW,
    )

    try:
        with check("seed host names"):
            seed(args.host, args.password, args.test_dir)
        with check("C=+U switches the rows and the status line"):
            run_views(browser, args.test_dir)
        with check("quick seek matches the CBM name"):
            run_quick_seek(browser)
        with check("rename edits and stores the CBM name"):
            run_rename(args.host, args.password, browser, args.test_dir)
        with check("an unchanged CBM name renames nothing"):
            run_unchanged_rename(args.host, args.password, browser, args.test_dir)
        with check("text that is no CBM name is refused"):
            run_refused_rename(args.host, args.password, browser, args.test_dir)
        with check("an entry without a CBM name is renamed by its host name"):
            run_host_name_fallback(browser)
        with check("help lists C=+U"):
            run_help(browser)
        with check("C=+U switches back to host names"):
            run_back_to_host_names(browser)
        suite_ok("browser_cbm_names_test")
        return 0
    except Exception as exc:  # noqa: BLE001
        suite_fail("browser_cbm_names_test", format_exception(exc))
        return 1
    finally:
        teardown_step("leave the host names view", lambda: leave_host_names_view(browser))
        teardown_step("close the browser session", browser.close)
        teardown_step("remove the fixture directory", lambda: cleanup(args.host, args.password, args.test_dir))


if __name__ == "__main__":
    raise SystemExit(main())
