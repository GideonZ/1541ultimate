#!/usr/bin/env python3
"""E2E: verify PRG runner REST endpoints trim long boot-cart display names."""

import argparse
import ftplib
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
from report import (Failure, check_ok, check_start, detail, format_exception, section,
                    suite_fail, suite_ok, warn)
from rest import multipart_body
from temp_settings import (
    AUTO_CLEANUP_ITEM, SUBFOLDERS_ITEM, TempSettingsSuite, add_toggle_arguments)
from ui_backend import add_mode_argument


SUITE = "prg_load_path_trim_test"


class SuiteRunner(TempSettingsSuite):
    def __init__(self, args):
        super().__init__(args)
        self.local_prg = Path(tempfile.gettempdir()) / "prg_load_path_trim_test.prg"

    @property
    def upload_root(self):
        return self.upload_dir

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
        except (*ftplib.all_errors, Failure) as exc:
            self.fail(f"Could not upload PUT fixture (FTP {exc})")
            return
        if response.startswith(("226", "200")):
            check_ok()
        else:
            self.fail(f"Could not upload PUT fixture (FTP {response})")

    # -- boot-cart observation ---------------------------------------------
    def read_bootcrt_name(self):
        """The boot-cart display name the firmware left at $0174.

        The first byte is the length, so the name is the bytes after it.
        """
        try:
            body = self.device.machine.readmem(0x0174, 17)
        except Failure as exc:
            self.fail(f"Reading boot-cart name bytes failed: {exc}")
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

    def run_put_case(self, action, label):
        self.machine_reset()
        expected = self.display_from_path(self.args.remote_file)
        check_start(f"exercise PUT runners:{action}")
        try:
            getattr(self.device.runners, action)(self.args.remote_file)
        except Failure as exc:
            self.fail(f"{label} failed: {exc}")
            return
        check_ok()
        self.wait_for_expected_displays(f"{label} boot-cart name", [expected])

    def run_post_case(self, action, label):
        """The upload form of the same runner: the name comes from the part.

        The uploaded file lands under a device-chosen managed name, so the
        expected display is derived from whatever path actually appeared
        rather than from the name that was sent.
        """
        self.cleanup_matching_names(self.upload_root, self.args.upload_name)
        self.machine_reset()
        before_names = self.ftp_list_names(self.upload_root)
        body, content_type = multipart_body("file", self.args.upload_name,
                                            self.local_prg.read_bytes())
        check_start(f"exercise POST runners:{action}")
        code, _, answer = self.device.rest.request(
            "POST", f"/v1/runners:{action}", body=body,
            headers={"Content-Type": content_type})
        if code != 200:
            self.fail(f"{label} failed (HTTP {code}): {answer[:160]!r}")
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
        self.apply_config_setting(AUTO_CLEANUP_ITEM, self.args.cleanup)
        self.apply_config_setting(SUBFOLDERS_ITEM, self.args.subfolder)
        section("3. Prepare PRG Fixture")
        self.create_test_prg()
        self.upload_put_fixture()
        section("4. Validate PUT Endpoints")
        self.run_put_case("load_prg", "PUT load_prg")
        self.run_put_case("run_prg", "PUT run_prg")
        section("5. Validate POST Endpoints")
        self.run_post_case("load_prg", "POST load_prg")
        self.run_post_case("run_prg", "POST run_prg")
        suite_ok(SUITE)

    def cleanup(self):
        self.cleanup_remote_artifacts()
        self.local_prg.unlink(missing_ok=True)
        self.restore_initial_config()


def main():
    parser = argparse.ArgumentParser(description="Validate PRG runner boot-cart path trimming.")
    cli.add_device_arguments(parser, colour=False, timeout=None)
    parser.add_argument("--remote-file", default="/Temp/rest-prg-path-trim-target-example.prg")
    parser.add_argument("--upload-name", default="rest-prg-path-trim-upload-example.prg")
    add_toggle_arguments(parser)
    add_mode_argument(parser)
    args = parser.parse_args()
    runner = SuiteRunner(args)
    try:
        runner.run()
        return 0
    except Failure as exc:
        # Without this the runner sees only the exit status and records the
        # suite as FAIL with an empty note and no checks.
        suite_fail(SUITE, format_exception(exc))
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
