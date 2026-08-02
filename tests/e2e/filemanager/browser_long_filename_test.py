#!/usr/bin/env python3
# E2E: Verifies browser actions resolve and operate on long FAT filenames.

"""Validate browser actions on a long filename through the real U64 menu UI.

The `/Temp` ingress paths can still be listed through a shortened FTP alias,
but after the fix the browser actions resolve and act on the real long FAT
filename. This test seeds that `/Temp` fixture and verifies Rename and Mount
against the full requested name on real firmware.

Drives the on-device browser through tests/e2e/lib/ui_backend.py's Browser,
so --mode selects telnet, freeze or overlay the same way every other
migrated suite does.
"""

import argparse
import ftplib
import json
import os
import sys
import tempfile
import time
import urllib.parse
import urllib.request
from pathlib import Path
from typing import Dict, List, Optional

# tests/lib holds the reporting rules every suite shares; tests/e2e/lib
# holds the shared UI backend.
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "lib"))
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "lib"))
import ftp as ftp_lib
from report import Failure, check, format_exception, suite_fail, suite_ok
from ui_backend import Browser, add_mode_argument, make_browser, strip_frame

FTP_USER = "user"
FTP_DEFAULT_PASSWORD = "password"
ROOT_PATH = "/"
TEMP_PATH = "/Temp/"
TEST_DIR_PREFIX = "zlfn-"
TEMP_SEED_NAME = "seed_fixture.d64"
RENAMED_NAME = "lfnok.d64"
REQUESTED_NAME = (
    "zzzz_long_filename_browser_regression_0123456789_0123456789_"
    "0123456789_0123456789.d64"
)
FIXTURE_PREFIX = "zzzz_long_filename_browser_regression"
MIN_BROWSER_LONG_NAME_LENGTH = 64
VALID_D64_SIZE = 174848

# The root browser's own listing geometry. REST/Overlay/Freeze render at the
# full physical 40x25; Telnet's remote session is not constrained to the
# 40-column display and renders this listing at 60 columns instead (more
# room for exactly the long filenames this suite exists to test), with one
# fewer row than REST/Overlay's 25-row physical screen (see ui_backend.py's
# module docstring for why).
ENTRY_ROWS = range(2, 24)
STATUS_ROW = 24
TELNET_ENTRY_ROWS = range(2, 23)
TELNET_STATUS_ROW = 23
TELNET_WIDTH = 60
TELNET_HEIGHT = 24

EDITOR_CHARS = {".": "period"}


def rest_headers(password: str) -> Dict[str, str]:
    headers = {}
    if password:
        headers["X-Password"] = password
    return headers


def rest_json(host: str, password: str, method: str, path: str) -> Dict[str, object]:
    request = urllib.request.Request(
        f"http://{host}{path}",
        data=b"" if method == "PUT" else None,
        headers=rest_headers(password),
        method=method,
    )
    with urllib.request.urlopen(request, timeout=10.0) as response:
        return json.loads(response.read().decode("utf-8"))


def default_test_dir() -> str:
    return f"{TEST_DIR_PREFIX}{int(time.time())}-{os.getpid()}"


def create_seed_d64(host: str, password: str, test_dir: str) -> None:
    quoted_diskname = urllib.parse.quote("ZZTEST", safe="")
    path = f"/v1/files/Temp/{test_dir}/{TEMP_SEED_NAME}:create_d64?diskname={quoted_diskname}"
    body = rest_json(host, password, "PUT", path)
    if body.get("errors") != []:
        raise Failure(f"Seed D64 creation failed: {body}")


def fixture_info(host: str, password: str, test_dir: str, name: str) -> Dict[str, object]:
    path = f"/v1/files/Temp/{test_dir}/{name}:info"
    body = rest_json(host, password, "GET", path)
    info = body.get("files")
    if not isinstance(info, dict):
        raise Failure(f"Fixture info missing for {name!r}: {body}")
    return info


def get_drive_a_image(host: str) -> Dict[str, object]:
    with urllib.request.urlopen(f"http://{host}/v1/drives", timeout=10.0) as response:
        payload = json.loads(response.read().decode("utf-8"))
    for entry in payload.get("drives", []):
        if "a" in entry:
            return entry["a"]
    raise Failure(f"Drive A info missing from {payload}")


def cleanup_fixture_files(ftp: ftplib.FTP, test_dir: str) -> None:
    directory = f"/Temp/{test_dir}"
    for name in ftp_lib.names(ftp, directory):
        if (
            name == TEMP_SEED_NAME
            or name == RENAMED_NAME
            or name.startswith(FIXTURE_PREFIX)
        ):
            ftp_lib.delete_quietly(ftp, f"{directory}/{name}")


def cleanup_remote_state(host: str, password: str, test_dir: str) -> None:
    try:
        rest_json(host, password, "PUT", "/v1/drives/a:remove")
    except Exception:
        pass

    ftp = ftp_lib.connect(host, password, timeout=20)
    try:
        cleanup_fixture_files(ftp, test_dir)
        ftp_lib.delete_quietly(ftp, f"/Temp/{test_dir}")
    finally:
        ftp_lib.close(ftp)


def seed_long_fixture(host: str, password: str, test_dir: str) -> str:
    ftp = ftp_lib.connect(host, password, timeout=20)
    try:
        ftp_lib.quietly(lambda: ftp.mkd(f"/Temp/{test_dir}"))
        cleanup_fixture_files(ftp, test_dir)
    finally:
        ftp_lib.close(ftp)

    create_seed_d64(host, password, test_dir)

    ftp = ftp_lib.connect(host, password, timeout=20)
    try:
        with tempfile.NamedTemporaryFile(delete=False) as temp_file:
            local_path = temp_file.name
        try:
            with open(local_path, "wb") as handle:
                ftp.retrbinary(f"RETR /Temp/{test_dir}/{TEMP_SEED_NAME}", handle.write)
            if os.path.getsize(local_path) != VALID_D64_SIZE:
                raise Failure(f"Seed D64 size mismatch at {local_path}")
            with open(local_path, "rb") as handle:
                ftp.storbinary(f"STOR /Temp/{test_dir}/{REQUESTED_NAME}", handle)
        finally:
            os.unlink(local_path)

        entries = ftp_lib.names(ftp, f"/Temp/{test_dir}")
        ftp_lib.delete_quietly(ftp, f"/Temp/{test_dir}/{TEMP_SEED_NAME}")
    finally:
        ftp_lib.close(ftp)

    candidates = [name for name in entries if name.startswith(FIXTURE_PREFIX)]
    if len(candidates) != 1:
        raise Failure(f"Expected one stored long-name fixture, got {candidates}")
    info = fixture_info(host, password, test_dir, REQUESTED_NAME)
    display_name = info.get("filename")
    if display_name != REQUESTED_NAME:
        raise Failure(f"Fixture lost its long browser name: {info}")
    if len(display_name) < MIN_BROWSER_LONG_NAME_LENGTH:
        raise Failure(f"Fixture browser name is not long enough: {display_name!r}")
    if info.get("extension") != "D64":
        raise Failure(f"Fixture did not remain a D64: {info}")
    return candidates[0]


def type_editor_text(browser: Browser, text: str) -> None:
    for ch in text:
        if ch.isalnum():
            browser.type_char(ch.lower())
        elif ch in EDITOR_CHARS:
            browser.type_char(ch)
        else:
            raise Failure(f"cannot type {ch!r} through the browser keyboard")


def open_fixture_directory(browser: Browser, test_dir: str) -> None:
    browser.go_to_directory(f"Temp/{test_dir}")


def open_fixture_context_menu(browser: Browser) -> List[str]:
    browser.select_entry("zz")
    if "D64" not in browser.selected_text():
        raise Failure(f"Expected D64 fixture selected, got {browser.selected_text()!r}")
    return browser.open_context_menu()


def clear_rename_field(browser: Browser, batch: int = 20, max_batches: int = 8) -> None:
    """Empty the rename prompt, which is pre-filled with the current name.

    The prompt holds the full name, not just the ~38-character window it
    renders (REQUESTED_NAME is 85 characters), so a single fixed backspace
    count tuned for a short name silently under-clears a long one. Checking
    for the absence of a distinctive substring of the original name is not
    reliable either: backspacing deletes from the end, so a substring can be
    reduced to an unmatched prefix (and so appear "gone") while a
    substantial remainder is still in the field. Read the field's own row
    directly and clear in batches until it is genuinely blank."""
    title_row = next((i for i, row in enumerate(browser.rows()) if "Give a new name.." in row), None)
    if title_row is None:
        raise Failure("Rename prompt title not found; cannot locate its field row")
    field_row = title_row + 2
    for _ in range(max_batches):
        if not strip_frame(browser.rows()[field_row]).strip():
            return
        browser.press_many("BACKSPACE", batch)
    remaining = strip_frame(browser.rows()[field_row])
    raise Failure(f"could not clear the rename field; still shows {remaining!r}")


def run_rename_test(host: str, password: str, browser: Browser, test_dir: str) -> None:
    open_fixture_directory(browser, test_dir)
    labels = open_fixture_context_menu(browser)
    browser.choose_overlay_item(labels, "Rename")

    if "Give a new name.." not in browser.screen():
        raise Failure("Rename prompt did not appear")

    clear_rename_field(browser)
    type_editor_text(browser, RENAMED_NAME)
    browser.press("ENTER")

    try:
        rest_json(host, password, "GET", f"/v1/files/Temp/{test_dir}/{REQUESTED_NAME}:info")
    except Exception:
        pass
    else:
        raise Failure("Browser rename left the original long filename in place")

    renamed_info = fixture_info(host, password, test_dir, RENAMED_NAME)
    if renamed_info.get("filename") == RENAMED_NAME:
        return

    text = browser.screen()
    if "Error: FILE DOESN'T EXIST" in text:
        raise Failure("Browser rename failed with FILE DOESN'T EXIST")
    raise Failure(f"Rename did not produce {RENAMED_NAME!r}; info={renamed_info!r}")


def run_mount_test(host: str, password: str, browser: Browser, test_dir: str) -> None:
    rest_json(host, password, "PUT", "/v1/drives/a:remove")

    open_fixture_directory(browser, test_dir)
    labels = open_fixture_context_menu(browser)
    browser.choose_overlay_item(labels, "Mount Disk")

    deadline = time.monotonic() + 2.0
    while time.monotonic() < deadline:
        drive_a = get_drive_a_image(host)
        if (
            drive_a.get("image_path") == f"/Temp/{test_dir}/"
            and drive_a.get("image_file") == REQUESTED_NAME
        ):
            return
        time.sleep(0.10)

    text = browser.screen()
    if "Opening disk file failed." in text:
        raise Failure("Browser mount failed with 'Opening disk file failed.'")
    raise Failure(f"Drive A did not mount fixture; drive_a={get_drive_a_image(host)!r}")


def reset_machine(host: str, password: str) -> None:
    headers = rest_headers(password)
    request = urllib.request.Request(
        f"http://{host}/v1/machine:input",
        data=json.dumps({"events": [{"kind": "release_all"}]}).encode("utf-8"),
        headers={**headers, "Content-Type": "application/json"},
        method="POST",
    )
    with urllib.request.urlopen(request, timeout=10.0):
        pass
    rest_json(host, password, "PUT", "/v1/machine:reset")
    time.sleep(0.5)


def main() -> int:
    parser = argparse.ArgumentParser(description="Validate long-filename browser actions on real firmware.")
    parser.add_argument("-H", "--host", default=os.environ.get("U64_INPUT_HOST", "u64"))
    parser.add_argument(
        "-p",
        "--password",
        default=os.environ.get("U64_INPUT_PASSWORD", os.environ.get("C64U_PASSWORD", "")),
    )
    parser.add_argument(
        "-t",
        "--timeout",
        type=float,
        default=float(os.environ.get("U64_INPUT_TIMEOUT", "5.0")),
    )
    parser.add_argument("--telnet-port", type=int, default=int(os.environ.get("U64_TELNET_PORT", "23")))
    parser.add_argument("--test-dir", default=default_test_dir())
    add_mode_argument(parser)
    args = parser.parse_args()

    with check("reset the machine to a clean starting state"):
        reset_machine(args.host, args.password)

    browser = make_browser(
        args.mode, args.host, args.password or None, args.timeout,
        entry_rows=ENTRY_ROWS, status_row=STATUS_ROW,
        telnet_port=args.telnet_port, telnet_width=TELNET_WIDTH, telnet_height=TELNET_HEIGHT,
        telnet_entry_rows=TELNET_ENTRY_ROWS, telnet_status_row=TELNET_STATUS_ROW,
    )

    try:
        with check("seed long-name fixture for rename"):
            seed_long_fixture(args.host, args.password, args.test_dir)

        with check("browser rename on long filename"):
            run_rename_test(args.host, args.password, browser, args.test_dir)

        with check("reseed long-name fixture for mount"):
            seed_long_fixture(args.host, args.password, args.test_dir)

        with check("browser mount on long filename"):
            run_mount_test(args.host, args.password, browser, args.test_dir)

        suite_ok("browser_long_filename_test")
        return 0
    except Failure as exc:
        suite_fail("browser_long_filename_test", str(exc))
        return 1
    except Exception as exc:  # noqa: BLE001
        suite_fail("browser_long_filename_test", format_exception(exc))
        return 1
    finally:
        try:
            browser.close()
        except Exception:
            pass
        cleanup_remote_state(args.host, args.password, args.test_dir)


if __name__ == "__main__":
    raise SystemExit(main())
