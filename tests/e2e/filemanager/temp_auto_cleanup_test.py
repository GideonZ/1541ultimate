#!/usr/bin/env python3
"""E2E: verify managed /Temp cleanup across mounted images and uploads."""

import argparse
import ftplib
import json
import os
import posixpath
import sys
import tempfile
import time
from pathlib import Path

# The one stanza that puts the shared library on sys.path; see tests/lib/bootstrap.py.
sys.path.insert(0, str(next(p for p in Path(__file__).resolve().parents
                            if (p / "tests" / "lib").is_dir()) / "tests" / "lib"))
import bootstrap  # noqa: E402,F401
import cli  # noqa: E402


import ftp as ftp_lib
from report import Failure, check_ok, check_start, detail, section, suite_ok, warn
from temp_settings import (
    AUTO_CLEANUP_ITEM, SUBFOLDERS_ITEM, TempSettingsSuite, add_toggle_arguments)
from ui_backend import add_mode_argument


SUITE = "temp_auto_cleanup_test"
MANAGED_SIZE = 524288
# The device answers the upload before it has written the managed copy, so a
# listing taken straight away can miss it.
MANAGED_WRITE_SETTLE_SECONDS = 1.5
D64_SIZE = 174848
SEED_SIZE = 768000
MOUNT_SOURCE_8 = "/Temp/mount-drive-8.d64"
MOUNT_SOURCE_9 = "/Temp/mount-drive-9.d64"
MOUNT_LOCAL_8 = Path(tempfile.gettempdir()) / "mount-drive-8.d64"
MOUNT_LOCAL_9 = Path(tempfile.gettempdir()) / "mount-drive-9.d64"


class TempCleanup(TempSettingsSuite):
    def __init__(self, args: argparse.Namespace) -> None:
        super().__init__(args)
        self.mounted_path_8 = ""
        self.mounted_path_9 = ""
        self.mounted_base_8 = ""
        self.mounted_base_9 = ""

    def names(self, directory: str) -> list[str]:
        return self.ftp(lambda c: ftp_lib.names(c, directory))

    def listing(self, directory: str) -> list[str]:
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

    def stats(self) -> tuple[int, int, int, int, int]:
        root = self.listing("/Temp")
        cache = self.listing(self.upload_dir)
        root_size = sum(int(x.split()[4]) for x in root if x.startswith("-"))
        cache_rows = [x for x in cache if x.startswith("-")]
        count, size = len(cache_rows), sum(int(x.split()[4]) for x in cache_rows)
        if self.args.subfolder == "Disabled":
            count -= self.args.seed_count
            size -= self.args.seed_count * SEED_SIZE
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
        self.device.runners.upload("run_prg", local.read_bytes())
        # The device writes the managed copy after it has answered, so the
        # listing below is read once it has had time to appear.
        time.sleep(MANAGED_WRITE_SETTLE_SECONDS)
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
        self.apply_config_setting(AUTO_CLEANUP_ITEM, self.args.cleanup)
        self.apply_config_setting(SUBFOLDERS_ITEM, self.args.subfolder)

    def run_purge(self) -> None:
        section("3. Purging /Temp")
        for drive in "ab":
            try:
                self.device.drives.remove(drive)
            except Failure:
                # Nothing mounted there is the normal case for a purge.
                pass
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
            try:
                self.device.files.create_d64(remote, diskname=disk)
                check_ok()
            except Failure as exc:
                self.fail(f"D64 creation failed: {exc}")
            check_start(f"download {posixpath.basename(remote)} for the upload mount")
            local.write_bytes(self.download(remote))
            self.verify_file_size(remote, D64_SIZE, f"Source image {posixpath.basename(remote)}")
            check_start(f"remove staging source {posixpath.basename(remote)}")
            self.delete("/Temp", posixpath.basename(remote))
            check_ok()
            check_start(f"mount drive {drive.upper()}")
            # The upload form of :mount, which is what makes the device take a
            # managed copy under /Temp; the typed API mounts a path in place.
            _code, _headers, body = self.device.rest.request(
                "POST", f"/v1/drives/{drive}:mount",
                params={"type": "d64", "mode": "readwrite"},
                body=local.read_bytes(),
                headers={"Content-Type": "application/octet-stream"})
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
            local.write_bytes(os.urandom(SEED_SIZE))
            check_start(f"upload base_{i}.bin")
            self.upload(f"/Temp/base_{i}.bin", local.read_bytes())
            self.verify_file_size(f"/Temp/base_{i}.bin", SEED_SIZE, f"Baseline {i}")

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
            try:
                self.device.drives.remove(drive)
                check_ok()
            except Failure as exc:
                self.fail(f"Drive {drive.upper()} removal failed: {exc}")
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
    cli.add_device_arguments(parser, colour=False, timeout=None)
    parser.add_argument("-l", "--limit", type=int, default=10)
    parser.add_argument("--seed-count", type=int, default=10)
    parser.add_argument("--test-count", type=int, default=12)
    add_toggle_arguments(parser)
    add_mode_argument(parser)
    args = parser.parse_args()
    suite = TempCleanup(args)
    try:
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
        # And a clean /Temp. This suite seeds ten 750K files there and mounts
        # two images from it, which it purges on the way in but used to leave
        # behind on the way out. Every later suite that works in /Temp then
        # saw them: on u2@c64u that turned the short listing prg-context-menu
        # builds into one longer than a screen, and nine of its actions failed
        # having been unable to find the entry they had just created.
        #
        # After run_final_integrity, so the check that user files under /Temp
        # were not modified still runs against the files this suite made.
        try:
            suite.run_purge()
        except Failure:
            pass
        suite.restore_initial_config()


if __name__ == "__main__":
    raise SystemExit(main())
