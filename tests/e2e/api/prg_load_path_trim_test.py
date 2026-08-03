#!/usr/bin/env python3
"""E2E: verify PRG runner REST endpoints trim long boot-cart display names."""

import argparse
import ftplib
import io
import json
import os
import posixpath
import sys
import tempfile
import time
import urllib.error
import urllib.parse
import urllib.request
from pathlib import Path

SCRIPT_DIR = Path(__file__).resolve().parent
# tests/lib holds the reporting rules every suite shares; tests/e2e/lib
# holds the shared UI backend.
sys.path.insert(0, str(SCRIPT_DIR.parents[1] / "lib"))
sys.path.insert(0, str(SCRIPT_DIR.parent / "lib"))

import ftp as ftp_lib
import rest as rest_lib
from report import Failure, check_fail, check_ok, check_start, check_warn, detail, section, suite_ok, warn
from ui_backend import add_mode_argument, make_backend

SUITE = "prg_load_path_trim_test"
FTP_USER = "user"
FTP_DEFAULT_PASSWORD = "password"


class RestSession:
    def __init__(self, host, password):
        self.host = host
        self.password = password

    def request(self, method, path, params=None, data=None, headers=None):
        query = "?" + urllib.parse.urlencode(params) if params else ""
        request_headers = dict(headers or {})
        if self.password:
            request_headers["X-Password"] = self.password
        request = urllib.request.Request(f"http://{self.host}{path}{query}", data=data,
                                         headers=request_headers, method=method)
        try:
            with rest_lib.retrying_urlopen(request, 10) as response:
                return response.status, response.read()
        except urllib.error.HTTPError as exc:
            return exc.code, exc.read()
        except (OSError, urllib.error.URLError) as exc:
            raise Failure(f"{method} {path} failed: {exc}") from exc


class SuiteRunner:
    def __init__(self, args):
        self.args = args
        self.rest = RestSession(args.host, args.password or None)
        self.upload_root = "/Temp/cache/upload"
        self.initial_auto_cleanup = ""
        self.initial_use_cache = ""
        self.config_restored = False
        self.local_prg = Path(tempfile.gettempdir()) / "prg_load_path_trim_test.prg"

    def ftp(self, action):
        with ftp_lib.session(self.args.host, self.args.password, timeout=10) as client:
            return action(client)

    def fail(self, message):
        check_fail(message)
        if self.args.assertions:
            raise Failure(message)
        warn("assertions disabled; continuing")

    def require_toggle_value(self, flag, value):
        if value not in ("Enabled", "Disabled"):
            raise Failure(f"Invalid value for {flag}: {value}\nExpected: Enabled or Disabled")

    def refresh_upload_root(self):
        self.upload_root = "/Temp/cache/upload" if self.args.subfolder == "Enabled" else "/Temp"

    # -- REST configuration -------------------------------------------------
    def get_config_current(self, key):
        status, body = self.rest.request("GET", f"/v1/configs/User%20Interface%20Settings/{key}")
        if status != 200:
            return ""
        try:
            return json.loads(body)["User Interface Settings"][urllib.parse.unquote(key)]["current"]
        except (KeyError, TypeError, ValueError):
            return ""

    def apply_config_setting(self, key, value, mode="strict"):
        check_start(f"set {key} to {value}")
        status, _ = self.rest.request(
            "PUT", f"/v1/configs/User%20Interface%20Settings/{key}", params={"value": value})
        if status == 200:
            check_ok()
            return True
        if mode == "strict" and self.args.assertions:
            check_fail(f"HTTP {status}")
            raise Failure(f"setting {key} failed (HTTP {status})")
        check_warn(f"HTTP {status}")
        return False

    def capture_initial_config(self):
        section("1. Capture Current Configuration")
        self.initial_auto_cleanup = self.get_config_current("Temp%20Auto%20Cleanup")
        self.initial_use_cache = self.get_config_current("Temp%20Subfolders")
        self.require_toggle_value("captured Temp Auto Cleanup", self.initial_auto_cleanup)
        self.require_toggle_value("captured Temp Subfolders", self.initial_use_cache)
        detail(f"Temp Auto Cleanup: {self.initial_auto_cleanup}")
        detail(f"Temp Subfolders:   {self.initial_use_cache}")

    def restore_initial_config(self):
        if self.config_restored:
            return
        self.config_restored = True
        if not self.initial_auto_cleanup or not self.initial_use_cache:
            return
        section("restore User Interface Settings")
        self.apply_config_setting("Temp%20Auto%20Cleanup", self.initial_auto_cleanup, "restore")
        self.apply_config_setting("Temp%20Subfolders", self.initial_use_cache, "restore")

    # -- FTP fixture cleanup ------------------------------------------------
    def ftp_list_names(self, directory):
        return self.ftp(lambda client: ftp_lib.names(client, directory))

    def ftp_delete_name(self, directory, name):
        try:
            self.ftp(lambda client: client.delete(f"{directory}/{name}"))
        except ftplib.all_errors:
            pass

    def cleanup_matching_names(self, directory, prefix):
        stem, dot, extension = prefix.rpartition(".")
        if not dot:
            stem, extension = prefix, ""
        for name in self.ftp_list_names(directory):
            if name == prefix or (extension and name.startswith(stem + "_") and name.endswith("." + extension)) or (not extension and name.startswith(stem + "_")):
                self.ftp_delete_name(directory, name)

    def cleanup_remote_artifacts(self):
        self.cleanup_matching_names("/Temp", posixpath.basename(self.args.remote_file))
        self.cleanup_matching_names(self.upload_root, self.args.upload_name)

    def create_test_prg(self):
        self.local_prg.write_bytes(b"\x01\x08\x07\x08\x0a\x00\x80\x00\x00\x00")

    def upload_put_fixture(self):
        check_start(f"upload PUT fixture {posixpath.basename(self.args.remote_file)}")
        self.cleanup_matching_names("/Temp", posixpath.basename(self.args.remote_file))
        try:
            response = self.ftp(lambda client: ftp_lib.store(
                client, self.args.remote_file, self.local_prg.read_bytes()))
        except ftplib.all_errors + (Failure,) as exc:
            self.fail(f"Could not upload PUT fixture (FTP {exc})")
            return
        if response.startswith(("226", "200")):
            check_ok()
        else:
            self.fail(f"Could not upload PUT fixture (FTP {response})")

    # -- machine setup ------------------------------------------------------
    def close_active_menu(self):
        # --mode affects only this cleanup; assertions below never read the UI.
        backend = make_backend(self.args.mode, self.args.host, self.args.password or None)
        backend.close()

    def machine_reset(self):
        status, _ = self.rest.request("PUT", "/v1/machine:reset")
        if status != 200:
            self.fail(f"Machine reset failed (HTTP {status})")
        time.sleep(1)

    def reset_to_clean_slate(self):
        self.close_active_menu()
        self.machine_reset()

    # -- boot-cart observation ---------------------------------------------
    def read_bootcrt_name(self):
        status, body = self.rest.request("GET", "/v1/machine:readmem",
                                         params={"address": "0174", "length": 17})
        if status != 200:
            self.fail(f"Reading boot-cart name bytes failed (HTTP {status})")
            return ""
        return body[1:1 + body[0]].decode("latin-1") if body else ""

    @staticmethod
    def display_from_path(path):
        display = path
        if len(path) > 16 and ("/" in path or "\\" in path):
            display = "..." + path[-13:]
        display = display.upper()
        return display[:-4] if display.lower().endswith(".prg") else display

    def wait_for_expected_displays(self, description, expected):
        deadline = time.monotonic() + 8
        actual = ""
        while time.monotonic() < deadline:
            actual = self.read_bootcrt_name()
            if actual in expected:
                detail(f"{description}: {actual}")
                return True
            time.sleep(1)
        self.fail(f"{description}\n  Expected one of: {' '.join(expected)}\n  Actual: {actual or '<empty>'}")
        return False

    def wait_for_new_upload_paths(self, before_names):
        deadline = time.monotonic() + 8
        while time.monotonic() < deadline:
            new_names = sorted(set(self.ftp_list_names(self.upload_root)) - set(before_names))
            if new_names:
                return [f"{self.upload_root}/{name}" for name in new_names]
            time.sleep(1)
        self.fail(f"Managed upload file was not created in {self.upload_root}")
        return []

    def run_put_case(self, endpoint, label):
        self.machine_reset()
        expected = self.display_from_path(self.args.remote_file)
        check_start(f"exercise {endpoint}")
        status, _ = self.rest.request("PUT", endpoint, params={"file": self.args.remote_file})
        if status != 200:
            check_fail(f"HTTP {status}")
            if self.args.assertions:
                raise Failure(f"{label} failed (HTTP {status})")
            warn("assertions disabled; continuing")
            return
        check_ok()
        self.wait_for_expected_displays(f"{label} boot-cart name", [expected])

    def run_post_case(self, endpoint, label):
        self.cleanup_matching_names(self.upload_root, self.args.upload_name)
        self.machine_reset()
        before_names = self.ftp_list_names(self.upload_root)
        boundary = "----u64-prg-path-trim"
        payload = (f"--{boundary}\r\nContent-Disposition: form-data; name=\"file\"; filename=\"{self.args.upload_name}\"\r\n"
                   "Content-Type: application/octet-stream\r\n\r\n").encode() + self.local_prg.read_bytes() + f"\r\n--{boundary}--\r\n".encode()
        check_start(f"exercise {endpoint}")
        status, _ = self.rest.request("POST", endpoint, data=payload,
                                      headers={"Content-Type": f"multipart/form-data; boundary={boundary}"})
        if status != 200:
            check_fail(f"HTTP {status}")
            if self.args.assertions:
                raise Failure(f"{label} failed (HTTP {status})")
            warn("assertions disabled; continuing")
            return
        check_ok()
        paths = self.wait_for_new_upload_paths(before_names)
        self.wait_for_expected_displays(f"{label} boot-cart name", [self.display_from_path(path) for path in paths])

    def run(self):
        section("Ultimate 64 PRG load path trim")
        detail(f"host: {self.args.host}")
        detail(f"PUT file path: {self.args.remote_file}")
        detail(f"POST upload name: {self.args.upload_name}")
        if not self.args.assertions:
            warn("assertions disabled")
        self.reset_to_clean_slate()
        self.capture_initial_config()
        section("2. Configure Temp Settings")
        self.apply_config_setting("Temp%20Auto%20Cleanup", self.args.cleanup)
        self.apply_config_setting("Temp%20Subfolders", self.args.subfolder)
        self.refresh_upload_root()
        section("3. Prepare PRG Fixture")
        self.create_test_prg()
        self.upload_put_fixture()
        section("4. Validate PUT Endpoints")
        self.run_put_case("/v1/runners:load_prg", "PUT load_prg")
        self.run_put_case("/v1/runners:run_prg", "PUT run_prg")
        section("5. Validate POST Endpoints")
        self.run_post_case("/v1/runners:load_prg", "POST load_prg")
        self.run_post_case("/v1/runners:run_prg", "POST run_prg")
        suite_ok(SUITE)

    def cleanup(self):
        self.cleanup_remote_artifacts()
        self.local_prg.unlink(missing_ok=True)
        self.restore_initial_config()


def main():
    parser = argparse.ArgumentParser(description="Validate PRG runner boot-cart path trimming.")
    parser.add_argument("-H", "--host", default=os.environ.get("U64_HOST", "u64"))
    parser.add_argument("-p", "--password", default=os.environ.get("U64_PASS", ""))
    parser.add_argument("-n", "--no-assertions", dest="assertions", action="store_false", default=True)
    parser.add_argument("--cleanup", default="Enabled")
    parser.add_argument("--subfolder", default="Enabled")
    parser.add_argument("--remote-file", default="/Temp/rest-prg-path-trim-target-example.prg")
    parser.add_argument("--upload-name", default="rest-prg-path-trim-upload-example.prg")
    add_mode_argument(parser)
    args = parser.parse_args()
    runner = SuiteRunner(args)
    runner.require_toggle_value("--cleanup", args.cleanup)
    runner.require_toggle_value("--subfolder", args.subfolder)
    try:
        runner.run()
        return 0
    except Failure:
        return 1
    finally:
        # Leave the documented clean UI state after the REST/FTP assertions.
        try:
            runner.close_active_menu()
        except Failure:
            pass
        runner.cleanup()


if __name__ == "__main__":
    raise SystemExit(main())
