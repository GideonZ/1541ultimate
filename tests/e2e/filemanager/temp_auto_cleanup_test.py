#!/usr/bin/env python3
"""E2E: verify managed /Temp cleanup across mounted images and uploads."""

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
from typing import Callable, Dict, List, Optional, Tuple, TypeVar

SCRIPT_DIR = Path(__file__).resolve().parent
# tests/lib holds the reporting rules every suite shares; tests/e2e/lib
# holds the shared UI backend.
sys.path.insert(0, str(SCRIPT_DIR.parents[1] / "lib"))
sys.path.insert(0, str(SCRIPT_DIR.parent / "lib"))

import ftp as ftp_lib
from report import Failure, check_fail, check_ok, check_start, check_warn, detail, section, suite_ok, warn
from ui_backend import add_mode_argument, make_backend

SUITE = "temp_auto_cleanup_test"
MANAGED_SIZE = 524288
MOUNT_SOURCE_8 = "/Temp/mount-drive-8.d64"
MOUNT_SOURCE_9 = "/Temp/mount-drive-9.d64"
MOUNT_LOCAL_8 = Path(tempfile.gettempdir()) / "mount-drive-8.d64"
MOUNT_LOCAL_9 = Path(tempfile.gettempdir()) / "mount-drive-9.d64"

T = TypeVar("T")


class RestSession:
    def __init__(self, host: str, password: Optional[str]) -> None:
        self.host = host
        self.password = password

    def request(
            self,
            method: str,
            path: str,
            params: Optional[Dict[str, str]] = None,
            data: Optional[bytes] = None,
            headers: Optional[Dict[str, str]] = None,
    ) -> Tuple[int, bytes]:
        query = "?" + urllib.parse.urlencode(params) if params else ""
        request_headers = dict(headers or {})
        if self.password:
            request_headers["X-Password"] = self.password
        request = urllib.request.Request(f"http://{self.host}{path}{query}", data=data,
                                         headers=request_headers, method=method)
        try:
            with urllib.request.urlopen(request, timeout=10) as response:
                return response.status, response.read()
        except urllib.error.HTTPError as exc:
            return exc.code, exc.read()
        except (OSError, urllib.error.URLError) as exc:
            raise Failure(f"{method} {path} failed: {exc}") from exc


class TempCleanup:
    def __init__(self, args: argparse.Namespace) -> None:
        self.args = args
        self.rest = RestSession(args.host, args.password or None)
        self.upload_dir = "/Temp/cache/upload"
        self.mounted_path_8 = ""
        self.mounted_path_9 = ""
        self.mounted_base_8 = ""
        self.mounted_base_9 = ""
        self.initial_auto_cleanup = ""
        self.initial_use_cache = ""
        self.config_restored = False

    def ftp(self, action: Callable[[ftplib.FTP], T]) -> T:
        with ftp_lib.session(self.args.host, self.args.password, timeout=10) as client:
            return action(client)

    def fail(self, message: str) -> None:
        check_fail(message)
        if self.args.assertions:
            raise Failure(message)
        warn("assertions disabled; continuing")

    @staticmethod
    def require_toggle_value(flag: str, value: str) -> None:
        if value not in ("Enabled", "Disabled"):
            raise Failure(f"Invalid value for {flag}: {value}\nExpected: Enabled or Disabled")

    def refresh_managed_paths(self) -> None:
        self.upload_dir = "/Temp/cache/upload" if self.args.subfolder == "Enabled" else "/Temp"

    def get_config_current(self, key: str) -> str:
        # Config reads are idempotent, so retain the shell suite's three retries.
        for _ in range(3):
            status, body = self.rest.request("GET", f"/v1/configs/User%20Interface%20Settings/{key}")
            try:
                value = json.loads(body)["User Interface Settings"][urllib.parse.unquote(key)]["current"] if status == 200 else ""
            except (KeyError, TypeError, ValueError):
                value = ""
            if value:
                return value
            time.sleep(.5)
        return ""

    def apply_config_setting(self, key: str, value: str, mode: str = "strict") -> bool:
        check_start(f"set {key} to {value}")
        status, _ = self.rest.request("PUT", f"/v1/configs/User%20Interface%20Settings/{key}", params={"value": value})
        if status == 200:
            check_ok()
            return True
        if mode == "strict" and self.args.assertions:
            check_fail(f"HTTP {status}")
            detail("category 'User Interface Settings' not found; the firmware may be too old")
            raise Failure(f"setting {key} failed (HTTP {status})")
        check_warn(f"HTTP {status}")
        detail(f"could not restore '{key}' to '{value}'" if mode == "restore" else f"assertions disabled; skipping config '{key}'")
        return False

    def capture_initial_config(self) -> None:
        section("1. Capture Current Configuration")
        self.initial_auto_cleanup = self.get_config_current("Temp%20Auto%20Cleanup")
        self.initial_use_cache = self.get_config_current("Temp%20Subfolders")
        self.require_toggle_value("captured Temp Auto Cleanup", self.initial_auto_cleanup)
        self.require_toggle_value("captured Temp Subfolders", self.initial_use_cache)
        detail(f"Temp Auto Cleanup: {self.initial_auto_cleanup}")
        detail(f"Temp Subfolders:   {self.initial_use_cache}")

    def restore_initial_config(self) -> None:
        if self.config_restored:
            return
        self.config_restored = True
        if not self.initial_auto_cleanup or not self.initial_use_cache:
            return
        section("restore User Interface Settings")
        self.apply_config_setting("Temp%20Auto%20Cleanup", self.initial_auto_cleanup, "restore")
        self.apply_config_setting("Temp%20Subfolders", self.initial_use_cache, "restore")

    def close_active_menu(self) -> None:
        # --mode affects only this cleanup; assertions below never read the UI.
        backend = make_backend(self.args.mode, self.args.host, self.args.password or None)
        backend.close()

    def machine_reset(self) -> None:
        status, _ = self.rest.request("PUT", "/v1/machine:reset")
        if status != 200:
            self.fail(f"Machine reset failed (HTTP {status})")
        time.sleep(1)

    def reset_to_clean_slate(self) -> None:
        self.close_active_menu()
        self.machine_reset()

    def names(self, directory: str) -> List[str]:
        return self.ftp(lambda c: ftp_lib.names(c, directory))

    def listing(self, directory: str) -> List[str]:
        return self.ftp(lambda c: ftp_lib.listing(c, directory))

    def exists(self, path: str) -> bool:
        directory, name = posixpath.split(path)
        return name in self.names(directory)

    def size(self, path: str) -> int:
        try:
            value = self.ftp(lambda c: c.size(path))
            if value:
                return value
        except ftplib.all_errors:
            pass
        directory, name = posixpath.split(path)
        for line in self.listing(directory):
            fields = line.split()
            if len(fields) >= 9 and fields[-1] == name:
                return int(fields[4])
        return 0

    def delete(self, directory: str, name: str) -> None:
        self.ftp(lambda c: ftp_lib.delete_quietly(c, f"{directory}/{name}"))

    def download(self, path: str) -> bytes:
        return self.ftp(lambda c: ftp_lib.retrieve(c, path))

    def upload(self, path: str, data: bytes) -> None:
        self.ftp(lambda c: ftp_lib.store(c, path, data))

    def verify_managed_temp_path(self, path: str, context: str) -> None:
        if path.startswith(self.upload_dir + "/"):
            check_ok()
        else:
            self.fail(f"{context}\n  Unexpected path: {path}")

    def verify_remote_file_exists(self, path: str, message: str) -> None:
        if self.exists(path):
            check_ok()
        else:
            self.fail(f"{message}\n  Missing path: {path}")

    def verify_remote_file_missing(self, path: str, message: str) -> None:
        if self.exists(path):
            self.fail(f"{message}\n  Unexpected path: {path}")
        else:
            check_ok()

    def verify_file_size(self, path: str, expected: int, message: str) -> None:
        actual = self.size(path)
        if actual == expected:
            check_ok()
        else:
            self.fail(f"{message}\n  Path: {path}\n  Expected: {expected}\n  Actual: {actual}")

    def persistent_mounts(self) -> int:
        names = self.names(self.upload_dir)
        return sum(base in names for base in (self.mounted_base_8, self.mounted_base_9) if base)

    def stats(self) -> Tuple[int, int, int, int, int]:
        root = self.listing("/Temp")
        cache = self.listing(self.upload_dir)
        root_size = sum(int(x.split()[4]) for x in root if x.startswith("-"))
        cache_rows = [x for x in cache if x.startswith("-")]
        count, size = len(cache_rows), sum(int(x.split()[4]) for x in cache_rows)
        if self.args.subfolder == "Disabled":
            count -= self.args.seed_count
            size -= self.args.seed_count * 768000
        persistent = self.persistent_mounts()
        return root_size, count, size, persistent, count - persistent

    def verify_user_files_untouched(self) -> bool:
        missing = [f"base_{i}.bin" for i in range(1, self.args.seed_count + 1) if not self.exists(f"/Temp/base_{i}.bin")]
        for name in missing:
            detail(f"missing user file: {name}")
        if not missing:
            detail("user files intact")
        return not missing

    def upload_rest_managed_temp(self, label: str) -> str:
        before = set(self.names(self.upload_dir))
        local = Path(tempfile.gettempdir()) / f"{label}.bin"
        local.write_bytes(os.urandom(MANAGED_SIZE))
        check_start(f"upload {label}.bin over REST")
        self.rest.request("POST", "/v1/runners:run_prg", data=local.read_bytes())
        time.sleep(1.5)
        new = sorted(set(self.names(self.upload_dir)) - before)
        if not new:
            self.fail(f"Not in cache: {label}.bin")
            return ""
        latest = new[0]
        self.verify_file_size(f"{self.upload_dir}/{latest}", MANAGED_SIZE, f"Managed file {label}")
        return latest

    def upload_until_removed(
            self,
            path: str,
            prefix: str,
            message: str,
            remaining_path: str,
            remaining_message: str,
    ) -> None:
        for attempt in range(1, self.args.limit + 3):
            self.upload_rest_managed_temp(f"{prefix}_{attempt}")
            if remaining_path:
                self.verify_remote_file_exists(remaining_path, remaining_message)
            if not self.exists(path):
                detail(f"removed after {attempt} uploads")
                return
        self.fail(f"{message}\n  Path remained: {path}")

    def run_config(self) -> None:
        section("2. Configuration Setup")
        self.apply_config_setting("Temp%20Auto%20Cleanup", self.args.cleanup)
        self.apply_config_setting("Temp%20Subfolders", self.args.subfolder)

    def run_purge(self) -> None:
        section("3. Purging /Temp")
        for drive in "ab":
            self.rest.request("PUT", f"/v1/drives/{drive}:remove")
        for name in self.names("/Temp"):
            if name != "cache":
                self.delete("/Temp", name)
        for directory in ("/Temp/cache/upload", "/Temp/upload"):
            for name in self.names(directory):
                self.delete(directory, name)
        detail("purge complete")

    def run_mount_setup(self) -> None:
        section("4. Mounting D64 Images")
        for remote, disk, local, drive in ((MOUNT_SOURCE_8, "Drive8", MOUNT_LOCAL_8, "a"), (MOUNT_SOURCE_9, "Drive9", MOUNT_LOCAL_9, "b")):
            check_start(f"create {remote}")
            status, _ = self.rest.request("PUT", f"/v1/files{remote}:create_d64", params={"diskname": disk})
            if status == 200:
                check_ok()
            else:
                self.fail(f"D64 creation failed ({status})")
            check_start(f"download {posixpath.basename(remote)} for the upload mount")
            local.write_bytes(self.download(remote))
            self.verify_file_size(remote, 174848, f"Source image {posixpath.basename(remote)}")
            check_start(f"remove staging source {posixpath.basename(remote)}")
            self.delete("/Temp", posixpath.basename(remote))
            check_ok()
            check_start(f"mount drive {drive.upper()}")
            status, body = self.rest.request("POST", f"/v1/drives/{drive}:mount", params={"type":"d64", "mode":"readwrite"}, data=local.read_bytes())
            try:
                path = json.loads(body).get("file", "")
            except ValueError:
                path = ""
            if not path:
                self.fail(f"Mount failed: {body.decode(errors='replace')}")
            else:
                if drive == "a":
                    self.mounted_path_8, self.mounted_base_8 = path, posixpath.basename(path)
                else:
                    self.mounted_path_9, self.mounted_base_9 = path, posixpath.basename(path)
                check_ok()
                self.verify_managed_temp_path(path, f"Drive {drive.upper()} mount")

    def run_seed(self) -> None:
        section(f"5. Seeding Baseline ({self.args.seed_count} files)")
        for i in range(1, self.args.seed_count + 1):
            local = Path(tempfile.gettempdir()) / f"base_{i}.bin"
            local.write_bytes(os.urandom(768000))
            check_start(f"upload base_{i}.bin")
            self.upload(f"/Temp/base_{i}.bin", local.read_bytes())
            self.verify_file_size(f"/Temp/base_{i}.bin", 768000, f"Baseline {i}")

    def run_count_limit_test(self) -> None:
        section(f"6. Managed Temp File Limit (Target: {self.args.limit})")
        first = last = ""
        for i in range(1, self.args.test_count + 1):
            name = self.upload_rest_managed_temp(f"managed_{i}")
            if name:
                first = first or f"{self.upload_dir}/{name}"
                last = f"{self.upload_dir}/{name}"
            _, _, _, _, managed = self.stats()
            detail(f"managed count={managed} (limit={self.args.limit})")
            if self.args.cleanup == "Enabled" and managed > self.args.limit:
                self.fail(f"Limit exceeded ({managed} > {self.args.limit})")
        if self.args.cleanup == "Enabled":
            self.verify_remote_file_missing(first, "Oldest managed file should be evicted first")
            self.verify_remote_file_exists(last, "Newest managed file should be retained")
        else:
            *_, managed = self.stats()
            if managed != self.args.test_count:
                self.fail(f"Cleanup disabled should retain all managed uploads ({managed} != {self.args.test_count})")
            self.verify_remote_file_exists(first, "Cleanup disabled should not delete oldest managed file")
            self.verify_remote_file_exists(last, "Cleanup disabled should retain newest managed file")

    def run_unmount_cleanup_test(self) -> None:
        section("7. Unmounted Uploads Rejoin Cleanup")
        for drive, path, other, removed in (("a", self.mounted_path_8, self.mounted_path_9, "Drive 8 removed"), ("b", self.mounted_path_9, "", "Drive 9 removed")):
            check_start(f"remove drive {drive.upper()}")
            status, _ = self.rest.request("PUT", f"/v1/drives/{drive}:remove")
            if status == 200:
                check_ok()
            else:
                self.fail(f"Drive {drive.upper()} removal failed")
            if self.args.cleanup == "Enabled":
                self.upload_until_removed(path, f"post_{drive}", removed, other, "Drive 9 remains")
            else:
                self.verify_remote_file_exists(path, f"Cleanup disabled should retain drive {drive.upper()} after unmount")

    def run_final_integrity(self) -> None:
        section("8. Final Integrity Check")
        _, _, _, persistent, managed = self.stats()
        if self.args.cleanup == "Enabled":
            if managed > self.args.limit:
                self.fail(f"Final count above limit ({managed})")
            if persistent != 0:
                self.fail(f"Persistent count not zero ({persistent})")
        elif managed < self.args.test_count:
            self.fail(f"Cleanup disabled should not remove managed uploads ({managed} < {self.args.test_count})")
        if not self.verify_user_files_untouched():
            self.fail("User files under /Temp were modified")
        suite_ok(SUITE)

    def run(self) -> None:
        section("Ultimate 64 Temp auto cleanup")
        detail(f"host: {self.args.host}")
        if not self.args.assertions:
            warn("assertions disabled")
        self.reset_to_clean_slate()
        self.capture_initial_config()
        self.run_config()
        self.run_purge()
        self.run_mount_setup()
        self.run_seed()
        self.run_count_limit_test()
        self.run_unmount_cleanup_test()
        self.run_final_integrity()


def main() -> int:
    parser = argparse.ArgumentParser(description="Validate Temp auto cleanup behavior on a real Ultimate 64.")
    parser.add_argument("-H", "--host", default=os.environ.get("U64_HOST", "u64"))
    parser.add_argument("-p", "--password", default=os.environ.get("U64_PASS", ""))
    parser.add_argument("-n", "--no-assertions", dest="assertions", action="store_false", default=True)
    parser.add_argument("-l", "--limit", type=int, default=10)
    parser.add_argument("--seed-count", type=int, default=10)
    parser.add_argument("--test-count", type=int, default=12)
    parser.add_argument("--cleanup", default="Enabled")
    parser.add_argument("--subfolder", default="Enabled")
    add_mode_argument(parser)
    args = parser.parse_args()
    suite = TempCleanup(args)
    try:
        suite.require_toggle_value("--cleanup", args.cleanup)
        suite.require_toggle_value("--subfolder", args.subfolder)
        suite.refresh_managed_paths()
        suite.run()
        return 0
    except Failure:
        return 1
    finally:
        # Leave the documented clean UI state after the REST/FTP assertions.
        try:
            suite.close_active_menu()
        except Failure:
            pass
        suite.restore_initial_config()


if __name__ == "__main__":
    raise SystemExit(main())
