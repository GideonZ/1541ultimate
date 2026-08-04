#!/usr/bin/env python3
# E2E helper: shared setup for the suites that toggle the managed /Temp settings.

"""Capture, apply and restore the two managed-/Temp settings, over REST and FTP.

`prg_load_path_trim_test.py` and `temp_auto_cleanup_test.py` both drive the
same two items under "User Interface Settings", both have to put them back
however the run ends, and both reach the device over REST and FTP without
asserting anything about the on-device UI. Each carried its own copy of the
same eleven helpers, and the copies had already drifted: only one of them
retried the config read that the device answers empty while it is busy, and
only one explained the failure when the category was missing.

A suite subclasses `TempSettingsSuite`, which owns the device handles and the
capture/restore pair, and adds its own scenario on top.
"""

import os
import sys
import time
from typing import Callable, Dict, TypeVar

# tests/lib holds the device API and the reporting rules every suite shares.
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                "..", "..", "lib"))
import ftp as ftp_lib  # noqa: E402  (needs tests/lib on sys.path first)
from api import UltimateApi  # noqa: E402  (needs tests/lib on sys.path first)
from report import (  # noqa: E402  (needs tests/lib on sys.path first)
    Failure, check_fail, check_ok, check_start, check_warn, detail, section, warn)

from ui_backend import make_backend  # noqa: E402  (same directory)

T = TypeVar("T")

CONFIG_CATEGORY = "User Interface Settings"
AUTO_CLEANUP_ITEM = "Temp Auto Cleanup"
SUBFOLDERS_ITEM = "Temp Subfolders"
MANAGED_ITEMS = (AUTO_CLEANUP_ITEM, SUBFOLDERS_ITEM)
TOGGLE_VALUES = ("Enabled", "Disabled")

# Where a managed upload lands, with and without the subfolder setting.
UPLOAD_DIR_WITH_SUBFOLDERS = "/Temp/cache/upload"
UPLOAD_DIR_WITHOUT_SUBFOLDERS = "/Temp"

REST_TIMEOUT_SECONDS = 10.0
FTP_TIMEOUT_SECONDS = 10
# A config read is idempotent and the device answers it empty while it is busy,
# so a read that comes back blank is asked again rather than believed.
CONFIG_READ_ATTEMPTS = 3
CONFIG_READ_PAUSE_SECONDS = 0.5
# The C64 needs about this long to reach the BASIC prompt after a reset.
MACHINE_RESET_SETTLE_SECONDS = 1.0


class TempSettingsSuite:
    """Device handles, the managed-/Temp settings, and a clean starting state.

    `args` has to carry `host`, `password`, `assertions` and `mode`; every
    suite here already parses those.
    """

    def __init__(self, args) -> None:
        self.args = args
        self.device = UltimateApi(args.host, args.password or None, REST_TIMEOUT_SECONDS)
        self.initial: Dict[str, str] = {}
        self.config_restored = False

    # -- FTP ----------------------------------------------------------------
    def ftp(self, action: Callable[..., T]) -> T:
        """Run `action` against a freshly opened FTP session, always closed."""
        with ftp_lib.session(self.args.host, self.args.password,
                             timeout=FTP_TIMEOUT_SECONDS) as client:
            return action(client)

    # -- assertions ---------------------------------------------------------
    def fail(self, message: str) -> None:
        """Fail the check, or warn and continue when --no-assertions is set."""
        check_fail(message)
        if self.args.assertions:
            raise Failure(message)
        warn("assertions disabled; continuing")

    @staticmethod
    def require_toggle_value(flag: str, value: str) -> None:
        if value not in TOGGLE_VALUES:
            raise Failure(f"Invalid value for {flag}: {value}\n"
                          f"Expected: {' or '.join(TOGGLE_VALUES)}")

    @property
    def upload_dir(self) -> str:
        """Where a managed upload lands under the subfolder setting in force.

        Read from `args` each time rather than resolved once in __init__, so a
        suite that changes the setting mid-run cannot go on using a stale path.
        """
        return (UPLOAD_DIR_WITH_SUBFOLDERS if self.args.subfolder == "Enabled"
                else UPLOAD_DIR_WITHOUT_SUBFOLDERS)

    # -- configuration ------------------------------------------------------
    def get_config_current(self, item: str) -> str:
        for attempt in range(CONFIG_READ_ATTEMPTS):
            try:
                value = self.device.configs.current(CONFIG_CATEGORY, item)
            except Failure:
                value = ""
            if value:
                return value
            if attempt + 1 < CONFIG_READ_ATTEMPTS:
                time.sleep(CONFIG_READ_PAUSE_SECONDS)
        return ""

    def apply_config_setting(self, item: str, value: str, mode: str = "strict") -> bool:
        """Set one item, reporting the attempt as its own check.

        `mode` is "strict" while the suite is setting up, where a device that
        will not take the setting means the scenario cannot run, and "restore"
        on the way out, where the run is over and a warning is all that is
        left to do.
        """
        check_start(f"set {item} to {value}")
        try:
            self.device.configs.set(CONFIG_CATEGORY, item, value)
        except Failure as exc:
            if mode == "strict" and self.args.assertions:
                check_fail(str(exc))
                detail(f"category {CONFIG_CATEGORY!r} not found or refused the "
                       "value; the firmware may be too old")
                raise
            check_warn(str(exc))
            detail(f"could not restore {item!r} to {value!r}" if mode == "restore"
                   else f"assertions disabled; skipping config {item!r}")
            return False
        check_ok()
        return True

    def capture_initial_config(self) -> None:
        section("1. Capture Current Configuration")
        for item in MANAGED_ITEMS:
            self.initial[item] = self.get_config_current(item)
            self.require_toggle_value(f"captured {item}", self.initial[item])
            detail(f"{item}: {self.initial[item]}")

    def restore_initial_config(self) -> None:
        """Put both settings back, once, whatever the run did.

        Called from a finally path that may itself run twice, so it is guarded
        rather than idempotent by accident. Nothing is restored if the capture
        never produced a value, because writing a blank would be worse than
        leaving the device as it is.
        """
        if self.config_restored:
            return
        self.config_restored = True
        if not all(self.initial.get(item) for item in MANAGED_ITEMS):
            return
        section("restore User Interface Settings")
        for item in MANAGED_ITEMS:
            self.apply_config_setting(item, self.initial[item], "restore")

    # -- machine ------------------------------------------------------------
    def close_active_menu(self) -> None:
        # --mode affects only this cleanup; the assertions never read the UI.
        make_backend(self.args.mode, self.args.host, self.args.password or None).close()

    def machine_reset(self) -> None:
        try:
            self.device.machine.reset()
        except Failure as exc:
            self.fail(f"Machine reset failed: {exc}")
        time.sleep(MACHINE_RESET_SETTLE_SECONDS)

    def reset_to_clean_slate(self) -> None:
        self.close_active_menu()
        self.machine_reset()


def add_toggle_arguments(parser, cleanup: str = "Enabled", subfolder: str = "Enabled") -> None:
    """The two toggles and the assertion switch, named the same way everywhere."""
    parser.add_argument("-n", "--no-assertions", dest="assertions",
                        action="store_false", default=True,
                        help="Warn instead of failing on assertion mismatches.")
    parser.add_argument("--cleanup", default=cleanup, choices=TOGGLE_VALUES,
                        help=f"Value for {AUTO_CLEANUP_ITEM} (default: {cleanup}).")
    parser.add_argument("--subfolder", default=subfolder, choices=TOGGLE_VALUES,
                        help=f"Value for {SUBFOLDERS_ITEM} (default: {subfolder}).")
