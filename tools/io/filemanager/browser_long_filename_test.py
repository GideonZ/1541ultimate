#!/usr/bin/env python3
"""Validate browser actions on a long filename through the real U64 menu UI.

The `/Temp` ingress paths expose a shortened actionable path, while `:info`
still reports the full long filename that the browser renders. This test seeds
that `/Temp` fixture, verifies the browser is acting on the long-name entry,
and then checks Rename and Mount on real firmware.
"""

import argparse
import ftplib
import json
import os
from pathlib import Path
import sys
import tempfile
import time
import urllib.error
import urllib.parse
import urllib.request
from typing import Dict, List

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "api"))

from menu_screen_test import Failure, MenuScreenInfo, RestSession, check, menu_screen_text


FTP_USER = "user"
FTP_DEFAULT_PASSWORD = "password"
MENU_SETTLE_SECONDS = 0.12
MENU_OPEN_SETTLE_SECONDS = 0.25
MENU_POPUP_SETTLE_SECONDS = 0.50
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


def ftp_password(password: str) -> str:
    return password or FTP_DEFAULT_PASSWORD


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


def fixture_info(host: str, password: str, test_dir: str, stored_name: str) -> Dict[str, object]:
    errors = []
    for name in (stored_name, REQUESTED_NAME):
        path = f"/v1/files/Temp/{test_dir}/{name}:info"
        try:
            body = rest_json(host, password, "GET", path)
        except urllib.error.HTTPError as exc:
            errors.append(f"{name!r}: HTTP {exc.code}")
            continue
        info = body.get("files")
        if isinstance(info, dict):
            return info
        errors.append(f"{name!r}: {body!r}")
    raise Failure(f"Fixture info missing for {stored_name!r}: {errors}")


def get_drive_a_image(host: str) -> Dict[str, object]:
    with urllib.request.urlopen(f"http://{host}/v1/drives", timeout=10.0) as response:
        payload = json.loads(response.read().decode("utf-8"))
    for entry in payload.get("drives", []):
        if "a" in entry:
            return entry["a"]
    raise Failure(f"Drive A info missing from {payload}")


def ftp_connect(host: str, password: str) -> ftplib.FTP:
    ftp = ftplib.FTP(host, timeout=20)
    ftp.login(FTP_USER, ftp_password(password))
    return ftp


def ftp_dir_entries(ftp: ftplib.FTP, directory: str) -> List[str]:
    ftp.cwd(directory)
    return ftp.nlst()


def ftp_delete_if_present(ftp: ftplib.FTP, path: str) -> None:
    try:
        ftp.delete(path)
    except ftplib.error_perm:
        pass


def ftp_rmdir_if_present(ftp: ftplib.FTP, path: str) -> None:
    try:
        ftp.rmd(path)
    except ftplib.error_perm:
        pass


def cleanup_fixture_files(ftp: ftplib.FTP, test_dir: str) -> None:
    directory = f"/Temp/{test_dir}"
    try:
        entries = ftp_dir_entries(ftp, directory)
    except ftplib.error_perm:
        return
    for name in entries:
        if (
            name == TEMP_SEED_NAME
            or name == RENAMED_NAME
            or name.startswith(FIXTURE_PREFIX)
        ):
            ftp_delete_if_present(ftp, f"{directory}/{name}")


def cleanup_remote_state(host: str, password: str, test_dir: str) -> None:
    try:
        rest_json(host, password, "PUT", "/v1/drives/a:remove")
    except Exception:
        pass

    ftp = ftp_connect(host, password)
    try:
        cleanup_fixture_files(ftp, test_dir)
        ftp_rmdir_if_present(ftp, f"/Temp/{test_dir}")
    finally:
        ftp.quit()


def seed_long_fixture(host: str, password: str, test_dir: str) -> str:
    ftp = ftp_connect(host, password)
    try:
        try:
            ftp.mkd(f"/Temp/{test_dir}")
        except ftplib.error_perm:
            pass
        cleanup_fixture_files(ftp, test_dir)
    finally:
        ftp.quit()

    create_seed_d64(host, password, test_dir)

    ftp = ftp_connect(host, password)
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

        entries = ftp_dir_entries(ftp, f"/Temp/{test_dir}")
        ftp_delete_if_present(ftp, f"/Temp/{test_dir}/{TEMP_SEED_NAME}")
    finally:
        ftp.quit()

    candidates = [name for name in entries if name.startswith(FIXTURE_PREFIX)]
    if len(candidates) != 1:
        raise Failure(f"Expected one stored long-name fixture, got {candidates}")
    info = fixture_info(host, password, test_dir, candidates[0])
    display_name = info.get("filename")
    if display_name != REQUESTED_NAME:
        raise Failure(f"Fixture lost its long browser name: {info}")
    if len(display_name) < MIN_BROWSER_LONG_NAME_LENGTH:
        raise Failure(f"Fixture browser name is not long enough: {display_name!r}")
    if info.get("extension") != "D64":
        raise Failure(f"Fixture did not remain a D64: {info}")
    return candidates[0]


def current_path(session: RestSession) -> str:
    return menu_screen_text(session.get_menu_screen()).splitlines()[-1].split()[0]


def current_selection(session: RestSession) -> str:
    return MenuScreenInfo(session.get_menu_screen()).selected_text


def tap(session: RestSession, inputs: List[str], settle: float = MENU_SETTLE_SECONDS) -> None:
    session.tap_keyboard(inputs)
    time.sleep(settle)


def quick_seek(session: RestSession, text: str, settle: float = MENU_OPEN_SETTLE_SECONDS) -> None:
    for ch in text:
        tap(session, [ch.lower()], settle)


def move_to_root(session: RestSession) -> None:
    for _ in range(12):
        if current_path(session) == ROOT_PATH:
            return
        tap(session, ["left_shift", "cursor_left_right"])
    raise Failure(f"Could not return to {ROOT_PATH!r}; now at {current_path(session)!r}")


def move_to_path(session: RestSession, path: str) -> None:
    for _ in range(12):
        if current_path(session) == path:
            return
        tap(session, ["left_shift", "cursor_left_right"])
    raise Failure(f"Could not return to {path!r}; now at {current_path(session)!r}")


def move_to_top(session: RestSession, count: int = 20) -> None:
    for _ in range(count):
        tap(session, ["left_shift", "cursor_up_down"], 0.03)


def move_selection_to_prefix(session: RestSession, prefix: str, max_steps: int = 24) -> None:
    for _ in range(max_steps):
        if current_selection(session).startswith(prefix):
            return
        tap(session, ["cursor_up_down"])
    raise Failure(f"Could not find selection starting with {prefix!r}")


def force_close_menu(session: RestSession) -> None:
    try:
        session.close_menu_from_anywhere()
        return
    except Exception:
        pass

    for _ in range(16):
        body = session.try_get_menu_screen()
        if body is None:
            return
        text = menu_screen_text(body)
        if (
            "Opening disk file failed." in text
            or "Error: FILE DOESN'T EXIST" in text
            or "Give a new name.." in text
        ):
            tap(session, ["return"], 0.20)
            continue
        tap(session, ["run_stop"], 0.20)

    if not session.menu_screen_unavailable():
        raise Failure("Could not close menu cleanly")


def open_fixture_directory(session: RestSession, test_dir: str) -> None:
    if not session.menu_screen_unavailable():
        force_close_menu(session)
    session.open_menu()
    move_to_root(session)
    move_to_top(session)
    move_selection_to_prefix(session, "Temp")
    tap(session, ["cursor_left_right"], MENU_OPEN_SETTLE_SECONDS)
    move_to_path(session, TEMP_PATH)
    move_to_top(session)
    quick_seek(session, test_dir[:2])
    move_selection_to_prefix(session, test_dir)
    tap(session, ["cursor_left_right"], MENU_OPEN_SETTLE_SECONDS)
    expected_path = f"/Temp/{test_dir}/"
    if current_path(session) != expected_path:
        raise Failure(f"Expected fixture directory {expected_path!r}, got {current_path(session)!r}")


def open_fixture_context_menu(session: RestSession) -> None:
    move_to_top(session)
    quick_seek(session, "zz")
    if "D64" not in current_selection(session):
        raise Failure(f"Expected D64 fixture selected, got {current_selection(session)!r}")
    tap(session, ["return"], MENU_OPEN_SETTLE_SECONDS)


def move_context_to_item(session: RestSession, label: str) -> None:
    for _ in range(16):
        if current_selection(session) == label:
            return
        tap(session, ["cursor_up_down"])
    raise Failure(f"Could not find context item {label!r}")


def clear_editor_field(session: RestSession) -> None:
    for _ in range(96):
        tap(session, ["inst_del"], 0.03)


def type_editor_text(session: RestSession, text: str) -> None:
    keymap = {
        ".": "period",
    }
    for ch in text:
        if ch.isalpha():
            key = ch.lower()
        elif ch.isdigit():
            key = ch
        else:
            key = keymap.get(ch)
        if not key:
            raise Failure(f"Unsupported editor character {ch!r}")
        tap(session, [key], 0.08)


def ftp_entries_in_test_dir(host: str, password: str, test_dir: str) -> List[str]:
    ftp = ftp_connect(host, password)
    try:
        return ftp_dir_entries(ftp, f"/Temp/{test_dir}")
    finally:
        ftp.quit()


def run_rename_test(host: str, password: str, session: RestSession, test_dir: str, stored_name: str) -> None:
    open_fixture_directory(session, test_dir)
    open_fixture_context_menu(session)
    move_context_to_item(session, "Rename")
    tap(session, ["return"], MENU_OPEN_SETTLE_SECONDS)

    body = session.get_menu_screen()
    if "Give a new name.." not in menu_screen_text(body):
        raise Failure("Rename prompt did not appear")

    clear_editor_field(session)
    type_editor_text(session, RENAMED_NAME)
    tap(session, ["return"], MENU_POPUP_SETTLE_SECONDS)

    entries = ftp_entries_in_test_dir(host, password, test_dir)
    if RENAMED_NAME in entries and stored_name not in entries:
        return

    body = session.get_menu_screen()
    text = menu_screen_text(body)
    if "Error: FILE DOESN'T EXIST" in text:
        raise Failure("Browser rename failed with FILE DOESN'T EXIST")
    raise Failure(f"Rename did not produce {RENAMED_NAME!r}; entries={entries}")


def run_mount_test(host: str, password: str, session: RestSession, test_dir: str, stored_name: str) -> None:
    rest_json(host, password, "PUT", "/v1/drives/a:remove")

    open_fixture_directory(session, test_dir)
    open_fixture_context_menu(session)
    move_context_to_item(session, "Mount Disk")
    tap(session, ["return"], MENU_POPUP_SETTLE_SECONDS)

    deadline = time.monotonic() + 2.0
    while time.monotonic() < deadline:
        drive_a = get_drive_a_image(host)
        if (
            drive_a.get("image_path") == f"/Temp/{test_dir}/"
            and drive_a.get("image_file") in (stored_name, REQUESTED_NAME)
        ):
            return
        time.sleep(0.10)

    body = session.get_menu_screen()
    text = menu_screen_text(body)
    if "Opening disk file failed." in text:
        raise Failure("Browser mount failed with 'Opening disk file failed.'")
    raise Failure(f"Drive A did not mount fixture; drive_a={get_drive_a_image(host)!r}")


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
    parser.add_argument("--test-dir", default=default_test_dir())
    args = parser.parse_args()

    session = RestSession(args.host, args.password or None, args.timeout)

    try:
        with check("seed long-name fixture for rename"):
            stored_name = seed_long_fixture(args.host, args.password, args.test_dir)

        with check("browser rename on long filename"):
            run_rename_test(args.host, args.password, session, args.test_dir, stored_name)

        with check("reseed long-name fixture for mount"):
            stored_name = seed_long_fixture(args.host, args.password, args.test_dir)

        with check("browser mount on long filename"):
            run_mount_test(args.host, args.password, session, args.test_dir, stored_name)

        print("browser_long_filename_test: OK")
        return 0
    finally:
        try:
            force_close_menu(session)
        except Exception:
            pass
        cleanup_remote_state(args.host, args.password, args.test_dir)


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Failure as exc:
        print(f"browser_long_filename_test: FAIL: {exc}")
        raise SystemExit(1)
