#!/usr/bin/env python3
# E2E: Exercises every REST operation the firmware serves, checks what each call
# actually did, and checks the device survives it.
#
# Why this exists. A lockup in files:create_d64 shipped because no suite ever
# called that route: the API surface was covered by whichever endpoints the
# feature suites happened to need. This suite reads the operation list off the
# device's own contract and requires every operation to be classified, so a new
# one fails the suite until somebody decides what to do with it.
#
# Where the operation list comes from. doc/api/rest_api_openapi_*.yaml when it
# is in the tree, because that is generated from the route table and lists the
# path-parameter variants that behave differently: the per-category flash
# operations, and a category read against an item read. Otherwise the API_CALL
# macros in software/api/*.cc, which name every handler but collapse those
# variants into one entry.
#
# Side effects. Where a call has an outcome the API can see, the case reads it
# back: drives:off is followed by a listing that says the drive is off,
# menu_button by a menu_screen that answers, writemem by a readmem of the byte.
# Where there is none REST exposes, the case says so instead of letting a status
# code stand in for a result.
#
# Calls go through tests/lib/api.py rather than hand-built URLs. That is where
# the URL shapes live, and building them here is how an earlier version of this
# suite ended up calling /v1/drives:mount, which is not the route: the drive is
# a path element, so it answered 400 for the wrong reason and passed.
#
# Cost and safety. Negative cases drive the argument path, which is free and
# leaves nothing behind. Cases that change the machine are exclusive, never run
# concurrently, record what they found first, and cleanup() puts it back however
# the run ended.
#
# Repetition. The lockup did not fire on a cold device on the first call, so
# every case runs --repeat times. By default the repetitions are spread across
# worker threads so different operations are in flight at once, which overlaps
# request teardown on one connection with argument parsing on another.
# --order sequential runs each case's repetitions back to back instead, for when
# one operation is suspect and the calls have to be attributable.
#
# Targets: any, including a cartridge. Operations a product does not build are
# reported SKIP with the reason, not passed over: machine:input answers 501 on a
# cartridge, machine:debugreg and the streams are not in a cartridge's route
# table at all, and readmem's zero-length rejection is a firmware fix that
# tests/lib/machine.py knows some releases lack.

import argparse
import os
import posixpath
import re
import socket
import sys
import threading
import time
import urllib.error
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
from typing import Callable, Dict, List, Optional, Set, Tuple

# tests/lib holds the reporting rules, the typed API and the one REST client;
# tests/e2e/lib holds the settings fixtures the cfg-* suites share.
sys.path.insert(0, os.path.join(
    os.path.dirname(os.path.abspath(__file__)), "..", "..", "lib"))
sys.path.insert(0, os.path.join(
    os.path.dirname(os.path.abspath(__file__)), "..", "lib"))
import ftp as ftp_lib  # noqa: E402  (needs tests/lib on sys.path first)
import machine as machine_lib  # noqa: E402  (needs tests/lib on sys.path first)
import rest as rest_lib  # noqa: E402  (needs tests/lib on sys.path first)
from api import UltimateApi  # noqa: E402  (needs tests/lib on sys.path first)
from report import (  # noqa: E402  (needs tests/lib on sys.path first)
    Failure, check_count, check_fail, check_ok, check_skip, check_start, detail,
    format_exception, section, suite_fail, suite_ok, warn)
from temp_settings import AUTO_CLEANUP_ITEM, CONFIG_CATEGORY  # noqa: E402

SUITE = "rest_api_coverage_test"
REPO_ROOT = Path(__file__).resolve().parents[3]
API_SOURCE_DIR = REPO_ROOT / "software" / "api"
OPENAPI_DIR = REPO_ROOT / "doc" / "api"
API_CALL_RE = re.compile(
    r"^API_CALL\(\s*(\w+)\s*,\s*(\w+)\s*,\s*(\w+)\s*,", re.MULTILINE)

# The category the reset case uses. Printer settings change nothing the harness
# depends on, unlike User Interface Settings, whose Interface Type defaults to
# Freeze. See the reset case for why that matters.
RESETTABLE_CATEGORY = "Printer Settings"

SCRATCH = "/Temp"
# A path whose parent does not exist either, so a route that creates a file on
# its way to failing still creates nothing.
MISSING = f"{SCRATCH}/rest_api_coverage_absent/no_such_file"
MOUNT_IMAGE = f"{SCRATCH}/rest_api_coverage.d64"
# Cassette buffer: RAM the C64 is not using at a BASIC prompt.
SCRATCH_ADDRESS = 0x0334
# Longer than MAX_WRITEMEM_HEX_BYTES, so api.writemem takes the POST form. That
# is what gives POST machine:writemem a happy case.
POST_WRITE_LENGTH = 256

LIVENESS_TIMEOUT_SECONDS = 10.0
# How long the menu may take to appear or go after the button is pressed. The
# press is handed to the UI task, so it is not done when the call returns.
MENU_SETTLE_SECONDS = 5.0
# The firmware serves MAX_HTTP_CLIENT = 4 connections (httpd/c-version/lib/
# server.h). A worker holds one for the call and one for the liveness read that
# follows, never both at once, so the ceiling is one per worker. Three leaves a
# slot free: saturating the last one turns a slow answer into a refused
# connection, which reads as a device failure rather than as queueing.
DEFAULT_WORKERS = 3
DEFAULT_REPEAT = 3

Key = Tuple[str, str]                       # (METHOD, operation path)

# The only operation this suite will not call. Everything else is reachable
# without leaving the device worse off, given the restore each case does.
EXCLUDED: Dict[Key, str] = {
    ("PUT", "/v1/machine:poweroff"):
        "turns the device off, and no test can turn it back on",
}

# Operations whose happy path belongs to another suite, named so that "no happy
# case here" is a decision rather than an omission.
HAPPY_ELSEWHERE: Dict[Key, str] = {
    ("PUT", "/v1/files/{path}:create_d64"): "create-disk-image",
    ("PUT", "/v1/files/{path}:create_d71"): "create-disk-image",
    ("PUT", "/v1/files/{path}:create_d81"): "create-disk-image",
    ("PUT", "/v1/files/{path}:create_dnp"): "create-disk-image",
    ("PUT", "/v1/runners:load_prg"): "prg-load-path-trim",
    ("PUT", "/v1/runners:run_prg"): "prg-load-path-trim",
    ("POST", "/v1/runners:load_prg"): "prg-load-path-trim",
    ("POST", "/v1/runners:run_prg"): "prg-load-path-trim",
    ("POST", "/v1/machine:input"): "input",
}

# Operations with no happy path anywhere, and why. Printed on every run, so the
# gaps this suite knows about stay visible instead of passing as covered.
NEGATIVE_ONLY: Dict[Key, str] = {
    ("PUT", "/v1/drives/{drive}:load_rom"):
        "a wrong file leaves the drive with a broken ROM until the next reboot, "
        "and the tree has no drive ROM to load a good one from",
    ("POST", "/v1/drives/{drive}:load_rom"): "as the PUT form",
    ("PUT", "/v1/runners:sidplay"): "needs a SID file and takes over the machine",
    ("PUT", "/v1/runners:modplay"): "needs a MOD file and takes over the machine",
    ("PUT", "/v1/runners:run_crt"): "needs a cartridge image and resets the C64",
    ("POST", "/v1/runners:sidplay"): "as the PUT form",
    ("POST", "/v1/runners:modplay"): "as the PUT form",
    ("POST", "/v1/runners:run_crt"): "as the PUT form",
}

# Maps an API_CALL handler to the operation path the contract gives it, for the
# fallback gate. Only the routes that take path parameters need an entry.
HANDLER_PATHS: Dict[Tuple[str, str, str], List[str]] = {
    ("GET", "files", "info"): ["/v1/files/{path}:info"],
    ("PUT", "files", "create_d64"): ["/v1/files/{path}:create_d64"],
    ("PUT", "files", "create_d71"): ["/v1/files/{path}:create_d71"],
    ("PUT", "files", "create_d81"): ["/v1/files/{path}:create_d81"],
    ("PUT", "files", "create_dnp"): ["/v1/files/{path}:create_dnp"],
    ("GET", "configs", "none"): ["/v1/configs", "/v1/configs/{category}",
                                 "/v1/configs/{category}/{item}"],
    ("PUT", "configs", "none"): ["/v1/configs/{category}/{item}"],
    ("POST", "configs", "none"): ["/v1/configs"],
    ("PUT", "configs", "save_to_flash"): ["/v1/configs:save_to_flash",
                                          "/v1/configs/{category}:save_to_flash"],
    ("PUT", "configs", "load_from_flash"): ["/v1/configs:load_from_flash",
                                            "/v1/configs/{category}:load_from_flash"],
    ("PUT", "configs", "reset_to_default"): ["/v1/configs:reset_to_default",
                                             "/v1/configs/{category}:reset_to_default"],
    ("GET", "drives", "none"): ["/v1/drives"],
    ("GET", "version", "none"): ["/v1/version"],
    ("GET", "info", "none"): ["/v1/info"],
    ("GET", "help", "none"): ["/v1/help"],
}
for _action in ("mount", "reset", "remove", "on", "off", "unlink", "load_rom",
                "set_mode"):
    HANDLER_PATHS[("PUT", "drives", _action)] = [f"/v1/drives/{{drive}}:{_action}"]
for _action in ("mount", "load_rom"):
    HANDLER_PATHS[("POST", "drives", _action)] = [f"/v1/drives/{{drive}}:{_action}"]
for _action in ("start", "stop"):
    HANDLER_PATHS[("PUT", "streams", _action)] = [f"/v1/streams/{{stream}}:{_action}"]


class Skip(Exception):
    """Raised by a case that cannot apply to this device."""


class Case:
    """One thing to call, and what has to be true afterwards."""

    def __init__(self, key: Optional[Key], name: str, kind: str,
                 run: Callable[["Ctx"], None], *, exclusive: bool = False) -> None:
        self.key = key                  # None for protocol cases
        self.name = name
        self.kind = kind                # happy | negative | protocol
        self.run = run
        self.exclusive = exclusive

    @property
    def label(self) -> str:
        if self.key is None:
            return f"protocol: {self.name}"
        return f"{self.key[0]} {self.key[1]} - {self.name}"


class Ctx:
    """What a case is handed: one thread's client and the suite's fixtures."""

    def __init__(self, api: UltimateApi, suite: "SuiteRunner") -> None:
        self.api = api
        self.suite = suite

    def refused(self, method: str, path: str, call, *, allow=(400, 403, 404, 412,
                                                              500, 501)) -> None:
        """Assert a call the device should turn down does exactly that.

        The typed API raises Failure carrying the status, so the status is
        recovered from the message rather than by bypassing the library and
        rebuilding the URL here.
        """
        try:
            call()
        except Failure as exc:
            match = re.search(r"HTTP (\d+)", str(exc))
            if match and int(match.group(1)) in allow:
                return
            raise Failure(f"{method} {path}: {exc}")
        raise Failure(f"{method} {path} was accepted, expected one of "
                      f"{sorted(allow)}")

    def require_fix(self, name: str) -> None:
        """Skip when this machine's firmware predates the behaviour asserted.

        tests/lib/machine.py owns the table of outstanding gaps and the wording,
        so a backport is one deletion there rather than an edit in every suite.
        """
        reason = self.suite.machine.missing_fix(name)
        if reason:
            raise Skip(reason)

    def refuse_status(self, method: str, path: str, *,
                      params: Optional[Dict[str, object]] = None,
                      allow=(400, 403, 404, 412, 500, 501)) -> None:
        """Assert a refusal for a call the typed API has no method for.

        Used where the point is a shape the library will not build, such as an
        unknown drive letter or a missing required argument.
        """
        code, _, body = self.api.rest.request(method, path, params=params)
        if code not in allow:
            raise Failure(f"{method} {path} answered HTTP {code}, expected one "
                          f"of {sorted(allow)}: {body[:160]!r}")


def build_cases() -> List[Case]:
    c: List[Case] = []

    def case(key, name, kind, fn, exclusive=False):
        c.append(Case(key, name, kind, fn, exclusive=exclusive))

    # ---- identity and help ----------------------------------------------
    def _version(ctx: Ctx) -> None:
        if not ctx.api.version():
            raise Failure("no version string in the answer")
    case(("GET", "/v1/version"), "returns a version", "happy", _version)

    def _info(ctx: Ctx) -> None:
        info = ctx.api.info()
        if not info.product or not info.firmware_version:
            raise Failure(f"product or firmware missing: {info}")
    case(("GET", "/v1/info"), "names product and firmware", "happy", _info)

    def _help(ctx: Ctx) -> None:
        if not ctx.api.help("version"):
            raise Failure("the help page was empty")
    case(("GET", "/v1/help"), "answers for a known command", "happy", _help)

    def _help_bad(ctx: Ctx) -> None:
        # No parameter at all: command="" is still a parameter, and the route
        # accepts it.
        ctx.refuse_status("GET", "/v1/help")
    case(("GET", "/v1/help"), "refuses a missing command", "negative", _help_bad)

    # ---- configuration reads --------------------------------------------
    def _configs(ctx: Ctx) -> None:
        if not ctx.api.configs.categories():
            raise Failure("the configuration listing is empty")
    case(("GET", "/v1/configs"), "lists categories", "happy", _configs)

    def _config_category(ctx: Ctx) -> None:
        if not ctx.api.configs.category(CONFIG_CATEGORY):
            raise Failure(f"{CONFIG_CATEGORY} came back empty")
    case(("GET", "/v1/configs/{category}"), "reads one category", "happy",
         _config_category)

    def _config_item(ctx: Ctx) -> None:
        item = ctx.api.configs.item(CONFIG_CATEGORY, AUTO_CLEANUP_ITEM)
        if "current" not in item:
            raise Failure(f"no current value in {item}")
    case(("GET", "/v1/configs/{category}/{item}"), "reads one item", "happy",
         _config_item)

    def _config_unknown(ctx: Ctx) -> None:
        ctx.refuse_status("GET", "/v1/configs/No%20Such%20Category", allow=(404,))
    case(("GET", "/v1/configs/{category}"), "refuses an unknown category",
         "negative", _config_unknown)

    # ---- configuration writes -------------------------------------------
    def _config_write(ctx: Ctx) -> None:
        value = ctx.suite.config_value(ctx)
        ctx.api.configs.set(CONFIG_CATEGORY, AUTO_CLEANUP_ITEM, value)
        after = ctx.api.configs.current(CONFIG_CATEGORY, AUTO_CLEANUP_ITEM)
        if after != value:
            raise Failure(f"wrote {value!r}, reads {after!r}")
    case(("PUT", "/v1/configs/{category}/{item}"),
         "writing the current value back is a no-op", "happy", _config_write,
         exclusive=True)

    def _config_write_path(ctx: Ctx) -> None:
        # The other accepted form: the value as a third path element.
        value = ctx.suite.config_value(ctx)
        ctx.api.configs.set_by_path(CONFIG_CATEGORY, AUTO_CLEANUP_ITEM, value)
        after = ctx.api.configs.current(CONFIG_CATEGORY, AUTO_CLEANUP_ITEM)
        if after != value:
            raise Failure(f"wrote {value!r} through the path form, reads {after!r}")
    case(("PUT", "/v1/configs/{category}/{item}"),
         "the value-in-path form sets the same item", "happy", _config_write_path,
         exclusive=True)

    def _config_write_bad(ctx: Ctx) -> None:
        ctx.refused("PUT", "/v1/configs/{category}/{item}",
                    lambda: ctx.api.configs.set("No Such Category", "No Such Item", "1"))
    case(("PUT", "/v1/configs/{category}/{item}"), "refuses an unknown category",
         "negative", _config_write_bad)

    def _config_apply(ctx: Ctx) -> None:
        value = ctx.suite.config_value(ctx)
        ctx.api.configs.apply({CONFIG_CATEGORY: {AUTO_CLEANUP_ITEM: value}})
        after = ctx.api.configs.current(CONFIG_CATEGORY, AUTO_CLEANUP_ITEM)
        if after != value:
            raise Failure(f"the JSON form wrote {value!r}, reads {after!r}")
    case(("POST", "/v1/configs"), "the JSON form sets an item", "happy",
         _config_apply, exclusive=True)

    def _config_apply_bad(ctx: Ctx) -> None:
        ctx.refused("POST", "/v1/configs",
                    lambda: ctx.api.configs.apply({"No Such Category": {"x": "1"}}))
    case(("POST", "/v1/configs"), "refuses an unknown category", "negative",
         _config_apply_bad)

    # ---- flash-backed settings ------------------------------------------
    # Saving writes what is already in force, because no case changes a setting
    # without putting it back first, so the store ends where it started.
    def _flash_roundtrip(ctx: Ctx) -> None:
        before = ctx.api.configs.current(CONFIG_CATEGORY, AUTO_CLEANUP_ITEM)
        ctx.api.configs.save_to_flash()
        ctx.api.configs.load_from_flash()
        after = ctx.api.configs.current(CONFIG_CATEGORY, AUTO_CLEANUP_ITEM)
        if after != before:
            raise Failure(f"save then load changed {AUTO_CLEANUP_ITEM} from "
                          f"{before!r} to {after!r}")
    case(("PUT", "/v1/configs:save_to_flash"), "save then load leaves the value",
         "happy", _flash_roundtrip, exclusive=True)
    case(("PUT", "/v1/configs:load_from_flash"), "covered by the save case above",
         "happy", _flash_roundtrip, exclusive=True)

    def _flash_roundtrip_category(ctx: Ctx) -> None:
        before = ctx.api.configs.current(CONFIG_CATEGORY, AUTO_CLEANUP_ITEM)
        ctx.api.configs.save_to_flash(CONFIG_CATEGORY)
        ctx.api.configs.load_from_flash(CONFIG_CATEGORY)
        after = ctx.api.configs.current(CONFIG_CATEGORY, AUTO_CLEANUP_ITEM)
        if after != before:
            raise Failure(f"per-category save then load changed "
                          f"{AUTO_CLEANUP_ITEM} from {before!r} to {after!r}")
    case(("PUT", "/v1/configs/{category}:save_to_flash"),
         "save then load leaves the value", "happy", _flash_roundtrip_category,
         exclusive=True)
    case(("PUT", "/v1/configs/{category}:load_from_flash"),
         "covered by the save case above", "happy", _flash_roundtrip_category,
         exclusive=True)

    def _reset_to_default(ctx: Ctx) -> None:
        # Not User Interface Settings: the default for Interface Type there is
        # Freeze, and a menu opened in Freeze takes down a device whose core
        # lacks the GideonZ#733 fix. Resetting a category is a real reset even
        # when it is undone a moment later, so the category has to be one whose
        # defaults cannot cost the run its device.
        category = RESETTABLE_CATEGORY
        snapshot = ctx.suite.snapshot_category(ctx, category)
        probe = next(iter(snapshot))
        try:
            ctx.api.configs.reset_to_default(category)
        finally:
            ctx.suite.restore_category(ctx, category, snapshot)
        after = ctx.api.configs.current(category, probe)
        if after != snapshot[probe]:
            raise Failure(f"restoring after the reset left {probe} at {after!r}, "
                          f"expected {snapshot[probe]!r}")
    case(("PUT", "/v1/configs/{category}:reset_to_default"),
         "resets one category and the values restore", "happy", _reset_to_default,
         exclusive=True)

    # The global form resets every category at once, so it only runs when the
    # operator asks for it. Skipped otherwise, which still classifies it.
    case(("PUT", "/v1/configs:reset_to_default"),
         "resets everything, needs --allow-global-reset", "happy",
         lambda ctx: ctx.suite.global_reset(ctx), exclusive=True)

    # ---- files -----------------------------------------------------------
    def _files_info(ctx: Ctx) -> None:
        if ctx.api.files.info(SCRATCH) is None:
            raise Failure(f"{SCRATCH} is missing")
    case(("GET", "/v1/files/{path}:info"), f"describes {SCRATCH}", "happy",
         _files_info)

    def _files_info_missing(ctx: Ctx) -> None:
        if ctx.api.files.info(MISSING) is not None:
            raise Failure(f"{MISSING} was reported as existing")
    case(("GET", "/v1/files/{path}:info"), "reports a missing path as absent",
         "negative", _files_info_missing)

    for kind, extra in (("d64", {"tracks": 35}), ("d71", {}), ("d81", {}),
                        ("dnp", {"tracks": 1})):
        def _create(ctx: Ctx, kind=kind, extra=extra) -> None:
            create = getattr(ctx.api.files, f"create_{kind}")
            ctx.refused("PUT", f"/v1/files/{{path}}:create_{kind}",
                        lambda: create(f"{MISSING}.{kind}", **extra))
        case(("PUT", f"/v1/files/{{path}}:create_{kind}"),
             "refuses an unwritable path", "negative", _create)

    # ---- machine reads ---------------------------------------------------
    def _readmem(ctx: Ctx) -> None:
        data = ctx.api.machine.readmem(SCRATCH_ADDRESS, 16)
        if len(data) != 16:
            raise Failure(f"asked for 16 bytes, got {len(data)}")
    case(("GET", "/v1/machine:readmem"), "returns the length asked for", "happy",
         _readmem)

    def _readmem_bad(ctx: Ctx) -> None:
        # Raw, because the typed call range-checks the length before sending and
        # the point here is what the device does with it.
        ctx.require_fix(machine_lib.READMEM_REJECTS_ZERO_LENGTH)
        ctx.refuse_status("GET", "/v1/machine:readmem",
                          params={"address": f"{SCRATCH_ADDRESS:04X}", "length": 0})
    case(("GET", "/v1/machine:readmem"), "refuses a zero length", "negative",
         _readmem_bad)

    def _heap(ctx: Ctx) -> None:
        reading = ctx.api.machine.heap()
        if reading is None:
            raise Failure("machine:heap is not on this firmware")
        if not 0 < int(reading["free"]) <= int(reading["total"]):
            raise Failure(f"implausible heap reading: {reading}")
    case(("GET", "/v1/machine:heap"), "reports free within total", "happy", _heap)

    def _measure(ctx: Ctx) -> None:
        ctx.api.machine.measure()      # None where the FPGA cannot measure
    case(("GET", "/v1/machine:measure"), "answers, 501 without bus measurement",
         "happy", _measure)

    def _input_get(ctx: Ctx) -> None:
        try:
            ctx.api.machine.input_state()
        except Failure as exc:
            if "501" in str(exc):
                raise Skip("key injection is not built into this product")
            raise
    case(("GET", "/v1/machine:input"), "reports the input state", "happy",
         _input_get)

    def _debugreg_get(ctx: Ctx) -> None:
        if ctx.api.machine.debugreg() is None:
            raise Skip("the debug register is not built into this product")
    case(("GET", "/v1/machine:debugreg"), "reads the register", "happy",
         _debugreg_get)

    def _debugreg_put(ctx: Ctx) -> None:
        before = ctx.api.machine.debugreg()
        if before is None:
            raise Skip("the debug register is not built into this product")
        after = ctx.api.machine.set_debugreg(f"0x{before}")
        if after != before:
            raise Failure(f"wrote back {before!r}, reads {after!r}")
    case(("PUT", "/v1/machine:debugreg"), "writing the held value back is a no-op",
         "happy", _debugreg_put, exclusive=True)

    def _menu_screen(ctx: Ctx) -> None:
        # Covered for real by the menu_button case, which opens the menu and
        # reads the screen while it is open. Here only that the closed answer
        # is the documented 404 rather than a hang.
        if ctx.api.machine.menu_open():
            raise Failure("a menu was already open before this suite touched it")
    case(("GET", "/v1/machine:menu_screen"), "answers 404 with no menu open",
         "happy", _menu_screen)

    # ---- machine writes --------------------------------------------------
    def _writemem_put(ctx: Ctx) -> None:
        before = ctx.api.machine.readmem(SCRATCH_ADDRESS, 1)
        probe = bytes([before[0] ^ 0x5A])
        ctx.api.machine.writemem(SCRATCH_ADDRESS, probe)
        try:
            after = ctx.api.machine.readmem(SCRATCH_ADDRESS, 1)
            if after != probe:
                raise Failure(f"wrote {probe.hex()}, reads {after.hex()}")
        finally:
            ctx.api.machine.writemem(SCRATCH_ADDRESS, before)
    case(("PUT", "/v1/machine:writemem"), "the byte written is the byte read back",
         "happy", _writemem_put, exclusive=True)

    def _writemem_post(ctx: Ctx) -> None:
        # Over MAX_WRITEMEM_HEX_BYTES the library uploads instead, which is the
        # POST form of the same operation.
        before = ctx.api.machine.readmem(SCRATCH_ADDRESS, POST_WRITE_LENGTH)
        probe = bytes((b ^ 0xA5) for b in before)
        ctx.api.machine.writemem(SCRATCH_ADDRESS, probe)
        try:
            after = ctx.api.machine.readmem(SCRATCH_ADDRESS, POST_WRITE_LENGTH)
            if after != probe:
                raise Failure("the uploaded block is not what reads back")
        finally:
            ctx.api.machine.writemem(SCRATCH_ADDRESS, before)
    case(("POST", "/v1/machine:writemem"), "the uploaded block reads back",
         "happy", _writemem_post, exclusive=True)

    def _writemem_bad(ctx: Ctx) -> None:
        ctx.refuse_status("PUT", "/v1/machine:writemem",
                          params={"address": f"{SCRATCH_ADDRESS:04X}"})
    case(("PUT", "/v1/machine:writemem"), "refuses a missing data argument",
         "negative", _writemem_bad)

    def _input_post_bad(ctx: Ctx) -> None:
        ctx.refuse_status("POST", "/v1/machine:input")
    case(("POST", "/v1/machine:input"), "refuses an empty event list", "negative",
         _input_post_bad)

    # ---- machine control -------------------------------------------------
    def _pause_resume(ctx: Ctx) -> None:
        # Neither has an outcome REST can see: the VIC keeps running while the
        # CPU is held, so nothing readable stops changing. Both are in one case
        # so a failure cannot leave the machine paused.
        ctx.api.machine.pause()
        ctx.api.machine.resume()
    case(("PUT", "/v1/machine:pause"), "pauses and resumes (status only)", "happy",
         _pause_resume, exclusive=True)
    case(("PUT", "/v1/machine:resume"), "covered by the pause case above", "happy",
         _pause_resume, exclusive=True)

    def _menu_button(ctx: Ctx) -> None:
        # Opening the menu with Interface Type = Freeze stops a device whose
        # core lacks the fix for GideonZ#733 answering anything at all, and
        # recovery is physical. tests/lib/machine.py owns that table.
        ctx.require_fix(machine_lib.FREEZE_MENU_OPENS)
        ctx.api.machine.menu_button()
        # Poll rather than read once: the press is queued for the UI task, and
        # a second press sent while the first is still opening used to take the
        # device down. Only press again once the menu is actually there.
        if not ctx.suite.wait_for_menu(ctx, True):
            raise Failure("the menu did not open")
        ctx.api.machine.menu_button()
        if not ctx.suite.wait_for_menu(ctx, False):
            raise Failure("the menu did not close again")
    case(("PUT", "/v1/machine:menu_button"), "opens the menu and closes it again",
         "happy", _menu_button, exclusive=True)

    def _reset(ctx: Ctx) -> None:
        if not ctx.api.machine.reset(force=True, wait=True):
            raise Failure("the C64 did not reach the BASIC prompt after a reset")
    case(("PUT", "/v1/machine:reset"), "the C64 comes back to READY", "happy",
         _reset, exclusive=True)

    def _reboot(ctx: Ctx) -> None:
        ctx.api.machine.reboot()
        if not ctx.api.machine.wait_until_ready():
            raise Failure("the C64 did not reach the BASIC prompt after a reboot")
    case(("PUT", "/v1/machine:reboot"), "the C64 comes back to READY", "happy",
         _reboot, exclusive=True)

    # ---- drives ----------------------------------------------------------
    def _drives_list(ctx: Ctx) -> None:
        found = ctx.api.drives.list()
        for slot in ("a", "b"):
            if slot not in found:
                raise Failure(f"drive {slot} missing from the listing: {sorted(found)}")
    case(("GET", "/v1/drives"), "lists both drive slots", "happy", _drives_list)

    def _drive_on_off(ctx: Ctx) -> None:
        before = ctx.api.drives.get("a").enabled
        (ctx.api.drives.off if before else ctx.api.drives.on)("a")
        if ctx.api.drives.get("a").enabled == before:
            raise Failure(f"the drive stayed enabled={before}")
        (ctx.api.drives.on if before else ctx.api.drives.off)("a")
        if ctx.api.drives.get("a").enabled != before:
            raise Failure(f"the drive did not go back to enabled={before}")
    case(("PUT", "/v1/drives/{drive}:on"), "flips the drive and the listing agrees",
         "happy", _drive_on_off, exclusive=True)
    case(("PUT", "/v1/drives/{drive}:off"), "covered by the on/off case above",
         "happy", _drive_on_off, exclusive=True)

    def _drive_mount(ctx: Ctx) -> None:
        ctx.suite.ensure_mount_image(ctx)
        ctx.api.drives.mount("a", MOUNT_IMAGE)
        try:
            mounted = ctx.api.drives.get("a")
            if posixpath.basename(MOUNT_IMAGE) not in mounted.image_file:
                raise Failure(f"mounted {MOUNT_IMAGE}, listing shows "
                              f"image_file={mounted.image_file!r}")
        finally:
            ctx.api.drives.remove("a")
    case(("PUT", "/v1/drives/{drive}:mount"), "the listing shows the image",
         "happy", _drive_mount, exclusive=True)

    def _drive_unlink(ctx: Ctx) -> None:
        # unlink breaks the connection to the file and leaves the disk in the
        # drive, so the listing still names it. That is the documented
        # difference from remove, which ejects it, and asserting the opposite
        # here would be asserting the wrong contract.
        ctx.suite.ensure_mount_image(ctx)
        ctx.api.drives.mount("a", MOUNT_IMAGE)
        try:
            ctx.api.drives.unlink("a")
            still = ctx.api.drives.get("a").image_file
            if posixpath.basename(MOUNT_IMAGE) not in still:
                raise Failure(f"unlink ejected the disk as well: image_file={still!r}")
        finally:
            ctx.api.drives.remove("a")
    case(("PUT", "/v1/drives/{drive}:unlink"), "keeps the disk, drops the file",
         "happy", _drive_unlink, exclusive=True)

    def _drive_remove(ctx: Ctx) -> None:
        ctx.suite.ensure_mount_image(ctx)
        ctx.api.drives.mount("a", MOUNT_IMAGE)
        ctx.api.drives.remove("a")
        left = ctx.api.drives.get("a").image_file
        if left:
            raise Failure(f"remove left {left!r} on the drive")
    case(("PUT", "/v1/drives/{drive}:remove"), "clears the mounted image", "happy",
         _drive_remove, exclusive=True)

    def _drive_mount_upload(ctx: Ctx) -> None:
        # The uploaded part needs a name with a usable extension: the default
        # image type comes from it, and an upload without one is refused with
        # "Invalid Type".
        payload, content_type = rest_lib.multipart_body(
            "file", posixpath.basename(MOUNT_IMAGE),
            ctx.suite.mount_image_bytes(ctx))
        code, _, body = ctx.api.rest.request(
            "POST", "/v1/drives/a:mount", body=payload,
            headers={"Content-Type": content_type})
        if code != 200:
            raise Failure(f"POST drives/a:mount returned HTTP {code}: {body[:160]!r}")
        try:
            if not ctx.api.drives.get("a").image_file:
                raise Failure("the upload mounted nothing")
        finally:
            ctx.api.drives.remove("a")
    case(("POST", "/v1/drives/{drive}:mount"), "an uploaded image mounts", "happy",
         _drive_mount_upload, exclusive=True)

    def _drive_set_mode(ctx: Ctx) -> None:
        before = ctx.api.drives.get("a").type
        ctx.api.drives.set_mode("a", before)
        after = ctx.api.drives.get("a").type
        if after != before:
            raise Failure(f"setting the current mode {before!r} changed it to {after!r}")
    case(("PUT", "/v1/drives/{drive}:set_mode"), "setting the current mode is a no-op",
         "happy", _drive_set_mode, exclusive=True)

    def _drive_reset(ctx: Ctx) -> None:
        # A drive reset has no outcome the API exposes; this exercises the route
        # and its request teardown.
        ctx.api.drives.reset("a")
    case(("PUT", "/v1/drives/{drive}:reset"), "resets the drive (status only)",
         "happy", _drive_reset, exclusive=True)

    def _drive_unknown(ctx: Ctx) -> None:
        ctx.refuse_status("PUT", "/v1/drives/z:reset")
    case(("PUT", "/v1/drives/{drive}:reset"), "refuses an unknown drive letter",
         "negative", _drive_unknown)

    def _mount_missing(ctx: Ctx) -> None:
        ctx.refused("PUT", "/v1/drives/{drive}:mount",
                    lambda: ctx.api.drives.mount("a", MISSING + ".d64"))
    case(("PUT", "/v1/drives/{drive}:mount"), "refuses a missing image", "negative",
         _mount_missing)

    def _load_rom_missing(ctx: Ctx) -> None:
        ctx.refused("PUT", "/v1/drives/{drive}:load_rom",
                    lambda: ctx.api.drives.load_rom("a", MISSING + ".rom"))
    case(("PUT", "/v1/drives/{drive}:load_rom"), "refuses a missing ROM file",
         "negative", _load_rom_missing)

    def _load_rom_post(ctx: Ctx) -> None:
        ctx.refuse_status("POST", "/v1/drives/a:load_rom")
    case(("POST", "/v1/drives/{drive}:load_rom"), "refuses a request with no body",
         "negative", _load_rom_post)

    def _set_mode_bad(ctx: Ctx) -> None:
        ctx.refused("PUT", "/v1/drives/{drive}:set_mode",
                    lambda: ctx.api.drives.set_mode("a", "no-such-mode"))
    case(("PUT", "/v1/drives/{drive}:set_mode"), "refuses an unknown mode",
         "negative", _set_mode_bad)

    for action in ("remove", "on", "off", "unlink"):
        def _unknown_drive(ctx: Ctx, action=action) -> None:
            ctx.refuse_status("PUT", f"/v1/drives/z:{action}")
        case(("PUT", f"/v1/drives/{{drive}}:{action}"),
             "refuses an unknown drive letter", "negative", _unknown_drive)

    # ---- runners ---------------------------------------------------------
    for action in ("sidplay", "modplay", "load_prg", "run_prg", "run_crt"):
        def _runner_missing(ctx: Ctx, action=action) -> None:
            call = getattr(ctx.api.runners, action)
            ctx.refused("PUT", f"/v1/runners:{action}",
                        lambda: call(MISSING + ".prg"))
        case(("PUT", f"/v1/runners:{action}"), "refuses a missing file", "negative",
             _runner_missing)

        def _runner_no_body(ctx: Ctx, action=action) -> None:
            ctx.refuse_status("POST", f"/v1/runners:{action}")
        case(("POST", f"/v1/runners:{action}"), "refuses a request with no body",
             "negative", _runner_no_body)

    # ---- streams ---------------------------------------------------------
    def _stream_roundtrip(ctx: Ctx) -> None:
        # The debug stream is the cheap one; the video stream shares a multicast
        # group with anything else on the network. Sent to this host and stopped
        # in the same case so nothing keeps streaming afterwards.
        # Not a firmware vintage question on every machine: where the device
        # has two interfaces, the streamer looks the destination up on the
        # wrong one. tests/lib/machine.py carries that as an open gap.
        ctx.require_fix(machine_lib.STREAM_FINDS_DESTINATION_MAC)
        # A route the product does not build is not in the table and the
        # dispatcher answers 404 with an empty body. The handler's own refusals
        # are 404 too but carry a JSON error, so the body is what tells a
        # missing operation from a rejected one. Anything else is a failure:
        # mapping every 404 to "not built" once hid a resolver error here.
        code, _, body = ctx.api.rest.request(
            "PUT", "/v1/streams/debug:start",
            params={"ip": ctx.suite.local_ip(ctx)})
        if code == 404 and not body.strip():
            raise Skip("this product does not serve the data streams")
        if code != 200:
            raise Failure(f"streams/debug:start answered HTTP {code}: {body[:160]!r}")
        ctx.api.streams.stop("debug")
    case(("PUT", "/v1/streams/{stream}:start"), "starts and stops the debug stream",
         "happy", _stream_roundtrip, exclusive=True)
    case(("PUT", "/v1/streams/{stream}:stop"), "covered by the start case above",
         "happy", _stream_roundtrip, exclusive=True)

    def _stream_unknown(ctx: Ctx) -> None:
        ctx.refused("PUT", "/v1/streams/{stream}:start",
                    lambda: ctx.api.streams.start("no-such-stream", ip="127.0.0.1"))
    case(("PUT", "/v1/streams/{stream}:start"), "refuses an unknown stream",
         "negative", _stream_unknown)

    def _stream_stop_unknown(ctx: Ctx) -> None:
        ctx.refused("PUT", "/v1/streams/{stream}:stop",
                    lambda: ctx.api.streams.stop("no-such-stream"))
    case(("PUT", "/v1/streams/{stream}:stop"), "refuses an unknown stream",
         "negative", _stream_stop_unknown)

    # ---- protocol --------------------------------------------------------
    def _unknown_route(ctx: Ctx) -> None:
        code, _, _b = ctx.api.rest.request("GET", "/v1/no_such_group:no_such_command")
        if code != 404:
            raise Failure(f"an unknown route answered HTTP {code}, expected 404")
    case(None, "an unknown route is 404", "protocol", _unknown_route)

    def _wrong_method(ctx: Ctx) -> None:
        code, _, _b = ctx.api.rest.request("GET", "/v1/machine:pause")
        if code not in (404, 405):
            raise Failure(f"GET on a PUT-only command answered HTTP {code}")
    case(None, "a command asked for with the wrong method does not run", "protocol",
         _wrong_method)

    def _no_password(ctx: Ctx) -> None:
        if not ctx.suite.args.password:
            raise Skip("no password is set on this device")
        code, _, _b = ctx.api.rest.request("GET", "/v1/info", use_password=False)
        if code != 403:
            raise Failure(f"an unauthenticated read answered HTTP {code}, expected 403")
    case(None, "an unauthenticated read is refused", "protocol", _no_password)

    return c


class SuiteRunner:
    def __init__(self, args) -> None:
        self.args = args
        self.api = UltimateApi(args.host, args.password, args.timeout)
        self.cases = build_cases()
        self.local = threading.local()
        self.local.api = self.api
        self.state_lock = threading.Lock()
        self.dead = threading.Event()
        self._image_ready = False
        self._image_bytes: Optional[bytes] = None
        self._config_value: Optional[str] = None
        self._local_ip: Optional[str] = None
        self.restore_drive: Optional[Tuple[bool, str, str]] = None
        self.restore_config: Optional[Dict[str, str]] = None

    # -- fixtures -----------------------------------------------------------
    def ensure_mount_image(self, ctx: Ctx) -> None:
        with self.state_lock:
            if self._image_ready:
                return
            if ctx.api.files.info(MOUNT_IMAGE) is None:
                ctx.api.files.create_d64(MOUNT_IMAGE, diskname="restapi")
            self._image_ready = True

    def mount_image_bytes(self, ctx: Ctx) -> bytes:
        with self.state_lock:
            if self._image_bytes is not None:
                return self._image_bytes
        self.ensure_mount_image(ctx)
        with ftp_lib.session(self.args.host, self.args.password) as client:
            data = ftp_lib.retrieve(client, MOUNT_IMAGE)
        with self.state_lock:
            self._image_bytes = data
        return data

    def config_value(self, ctx: Ctx) -> str:
        """The settings item's value as found, so writes can be no-ops."""
        with self.state_lock:
            if self._config_value is not None:
                return self._config_value
        value = ctx.api.configs.current(CONFIG_CATEGORY, AUTO_CLEANUP_ITEM)
        if not value:
            raise Skip(f"{CONFIG_CATEGORY}/{AUTO_CLEANUP_ITEM} is not on this firmware")
        with self.state_lock:
            self._config_value = value
        return value

    def snapshot_category(self, ctx: Ctx, category: str) -> Dict[str, str]:
        items = ctx.api.configs.category(category)
        found: Dict[str, str] = {}
        for name in items:
            try:
                value = ctx.api.configs.current(category, name)
            except Failure:
                continue
            if value:
                found[name] = value
        if not found:
            raise Skip(f"{category} has no readable items")
        return found

    def restore_category(self, ctx: Ctx, category: str,
                         snapshot: Dict[str, str]) -> None:
        for name, value in snapshot.items():
            try:
                ctx.api.configs.set(category, name, value)
            except Failure:
                pass                    # read-only items cannot be written back

    def global_reset(self, ctx: Ctx) -> None:
        if not self.args.allow_global_reset:
            raise Skip("resets every category at once; pass --allow-global-reset")
        # This one does reach User Interface Settings, so it is opt-in and the
        # operator is expected to know the device may need its interface type
        # put back by hand if the restore below cannot run.
        snapshot = {c: self.snapshot_category(ctx, c)
                    for c in ctx.api.configs.categories()}
        try:
            ctx.api.configs.reset_to_default()
        finally:
            for category, items in snapshot.items():
                self.restore_category(ctx, category, items)

    def local_ip(self, ctx: Ctx) -> str:
        with self.state_lock:
            if self._local_ip:
                return self._local_ip
        probe = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        try:
            probe.connect((ctx.api.rest.host, 80))
            address = probe.getsockname()[0]
        finally:
            probe.close()
        with self.state_lock:
            self._local_ip = address
        return address

    def client(self) -> UltimateApi:
        """One UltimateApi per thread: the pool decides which thread runs which
        case, so a client picked by index would be shared across threads."""
        api = getattr(self.local, "api", None)
        if api is None:
            api = UltimateApi(self.args.host, self.args.password, self.args.timeout)
            self.local.api = api
        return api

    def wait_for_menu(self, ctx: Ctx, open_: bool,
                      budget: float = MENU_SETTLE_SECONDS) -> bool:
        """Whether the menu reaches `open_` within `budget`."""
        deadline = time.monotonic() + budget
        while True:
            if ctx.api.machine.menu_open() == open_:
                return True
            if time.monotonic() >= deadline:
                return False
            time.sleep(0.2)

    def alive(self) -> Optional[str]:
        return self.api.unreachable_reason(LIVENESS_TIMEOUT_SECONDS)

    @property
    def machine(self) -> machine_lib.Machine:
        """Which machine this is, for the checks that need a firmware fix."""
        info = self.api.info()
        return machine_lib.identify(self.api.host,
                                    lambda: (info.product, info.firmware_version))

    # -- coverage -----------------------------------------------------------
    def contract_operations(self) -> Tuple[Set[Key], str]:
        """Every operation the device serves, and where the list came from."""
        specs = sorted(OPENAPI_DIR.glob("rest_api_openapi_*.yaml"))
        if specs:
            try:
                import yaml                              # noqa: PLC0415
            except ImportError:
                specs = []
        if specs:
            found: Set[Key] = set()
            for spec in specs:
                document = yaml.safe_load(spec.read_text(encoding="utf-8"))
                for path, item in (document.get("paths") or {}).items():
                    for method in item:
                        if method.lower() in ("get", "put", "post", "delete", "patch"):
                            found.add((method.upper(), path))
            return found, f"{len(specs)} OpenAPI document(s)"

        found = set()
        for source in sorted(API_SOURCE_DIR.glob("*.cc")):
            for method, group, command in API_CALL_RE.findall(
                    source.read_text(encoding="utf-8", errors="replace")):
                key = (method.upper(), group, command)
                for path in HANDLER_PATHS.get(
                        key, [f"/v1/{group}" if command == "none"
                              else f"/v1/{group}:{command}"]):
                    found.add((method.upper(), path))
        return found, "the API_CALL macros"

    def check_coverage(self) -> bool:
        section("coverage")
        check_start("read the operation list")
        try:
            registered, where = self.contract_operations()
        except OSError as exc:
            check_fail(f"could not read the contract: {format_exception(exc)}")
            return False
        if not registered:
            check_fail("the contract lists no operations")
            return False
        check_ok(f"{len(registered)} operations, from {where}")

        exercised = {c.key for c in self.cases if c.key is not None}
        happy = {c.key for c in self.cases if c.key is not None and c.kind == "happy"}

        check_start("every operation is exercised or excluded")
        unclassified = sorted(registered - (exercised | set(EXCLUDED)))
        if unclassified:
            for method, path in unclassified:
                detail(f"unclassified: {method} {path}")
            check_fail(f"{len(unclassified)} operation(s) have no case and no "
                       "written reason to skip")
            return False
        check_ok(f"{len(exercised)} exercised, {len(EXCLUDED)} excluded")

        check_start("nothing is classified that the device does not serve")
        stale = sorted((exercised | set(EXCLUDED) | set(HAPPY_ELSEWHERE)
                        | set(NEGATIVE_ONLY)) - registered)
        if stale:
            for method, path in stale:
                detail(f"stale: {method} {path}")
            check_fail(f"{len(stale)} classified operation(s) are not served; "
                       "remove them from this suite")
            return False
        check_ok()

        check_start("every operation has a happy path somewhere")
        missing = sorted(registered - (happy | set(EXCLUDED) | set(HAPPY_ELSEWHERE)
                                       | set(NEGATIVE_ONLY)))
        if missing:
            for method, path in missing:
                detail(f"no happy path: {method} {path}")
            check_fail(f"{len(missing)} operation(s) are only called with arguments "
                       "they refuse. Add a happy case, name the suite that has one "
                       "in HAPPY_ELSEWHERE, or say in NEGATIVE_ONLY why there "
                       "cannot be one")
            return False
        check_ok(f"{len(happy)} here, {len(HAPPY_ELSEWHERE)} elsewhere, "
                 f"{len(NEGATIVE_ONLY)} with none")

        for (method, path), reason in sorted(EXCLUDED.items()):
            detail(f"not called: {method} {path} - {reason}")
        for (method, path), reason in sorted(NEGATIVE_ONLY.items()):
            detail(f"negative only: {method} {path} - {reason}")
        return True

    # -- execution ----------------------------------------------------------
    def run_once(self, case: Case) -> Optional[str]:
        api = self.client()
        try:
            case.run(Ctx(api, self))
        except Skip as exc:
            return f"SKIP {exc}"
        except (Failure, OSError, TimeoutError, urllib.error.URLError) as exc:
            reason = format_exception(exc)
            if self.alive():
                self.dead.set()
                return f"{reason}, and the device stopped answering. Power cycle it."
            return reason
        liveness = api.unreachable_reason(LIVENESS_TIMEOUT_SECONDS)
        if liveness:
            self.dead.set()
            return (f"the device stopped answering after this call: {liveness}. "
                    "Power cycle it.")
        return None

    def run_cases(self) -> bool:
        shared = [c for c in self.cases if not c.exclusive]
        exclusive = [c for c in self.cases if c.exclusive]
        results: Dict[int, str] = {}
        attempted: Set[int] = set()
        lock = threading.Lock()

        def task(case: Case) -> None:
            if self.dead.is_set():
                return
            with lock:
                if id(case) in results:
                    return              # already failed; do not pile on
                attempted.add(id(case))
            outcome = self.run_once(case)
            if outcome is not None:
                with lock:
                    results.setdefault(id(case), outcome)

        if self.args.order == "sequential":
            for case in shared:
                for _ in range(self.args.repeat):
                    task(case)
        else:
            with ThreadPoolExecutor(max_workers=self.args.workers) as pool:
                for _ in range(self.args.repeat):
                    if self.dead.is_set():
                        break
                    list(pool.map(task, shared))

        # Always sequential and always in list order, so a case that changes
        # state and puts it back is never interleaved with one that reads it.
        for _ in range(self.args.repeat):
            if self.dead.is_set():
                break
            for case in exclusive:
                task(case)

        ok = True
        for case in self.cases:
            check_start(f"{case.label} x{self.args.repeat}")
            outcome = results.get(id(case))
            if outcome is None:
                if id(case) not in attempted:
                    check_skip("the device was already down")
                    ok = False
                else:
                    check_ok()
            elif outcome.startswith("SKIP "):
                check_skip(outcome[5:])
            else:
                check_fail(outcome)
                ok = False
        return ok

    def run(self) -> bool:
        if not self.check_coverage():
            return False

        section("preconditions")
        check_start("device reachable")
        reason = self.alive()
        if reason:
            check_fail(f"device did not answer before the suite started: {reason}")
            return False
        check_ok()

        check_start("record the state to restore")
        try:
            drive = self.api.drives.get("a")
            self.restore_drive = (drive.enabled, drive.type, drive.image_path)
            self.restore_config = self.snapshot_category(
                Ctx(self.api, self), CONFIG_CATEGORY)
        except (Failure, Skip) as exc:
            check_fail(f"could not read the starting state: {format_exception(exc)}")
            return False
        check_ok(f"drive a: enabled={drive.enabled} type={drive.type!r}, "
                 f"{len(self.restore_config)} settings")

        section("operations")
        detail(f"{len(self.cases)} cases x{self.args.repeat}, {self.args.order}"
               + (f", {self.args.workers} workers"
                  if self.args.order == "concurrent" else ""))
        return self.run_cases()

    # -- cleanup ------------------------------------------------------------
    def cleanup(self) -> None:
        """Put back everything a case could have changed, however the run ended.

        Quiet and best effort: a cleanup failure on a device that has already
        gone down would bury the reason the suite failed.
        """
        def quietly(action) -> None:
            try:
                action()
            except Exception:                            # noqa: BLE001
                pass

        quietly(self.api.machine.resume)
        quietly(lambda: self.api.rest.request("PUT", "/v1/streams/debug:stop"))
        quietly(lambda: self.api.drives.unlink("a"))
        if self.restore_drive is not None:
            enabled, mode, image = self.restore_drive
            quietly(lambda: self.api.drives.set_mode("a", mode))
            quietly(lambda: (self.api.drives.on if enabled else self.api.drives.off)("a"))
            if image:
                quietly(lambda: self.api.drives.mount("a", image))
        if self.restore_config:
            quietly(lambda: self.restore_category(
                Ctx(self.api, self), CONFIG_CATEGORY, self.restore_config))
        quietly(self._remove_scratch)

    def _remove_scratch(self) -> None:
        with ftp_lib.session(self.args.host, self.args.password) as client:
            ftp_lib.delete_quietly(client, MOUNT_IMAGE)
            ftp_lib.quietly(lambda: client.rmd(posixpath.dirname(MISSING)))


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("-H", "--host", default=os.environ.get("U64_HOST", "u64"))
    parser.add_argument("-p", "--password", default=os.environ.get("U64_PASS"))
    parser.add_argument("-t", "--timeout", type=float,
                        default=float(os.environ.get("U64_TIMEOUT", "10.0")))
    parser.add_argument("-r", "--repeat", type=int, default=DEFAULT_REPEAT,
                        help="how many times each case runs (default: %(default)s)")
    parser.add_argument("--order", choices=("concurrent", "sequential"),
                        default="concurrent",
                        help="concurrent spreads the repetitions across workers so "
                             "different operations overlap; sequential runs each "
                             "case's repetitions back to back on one thread "
                             "(default: %(default)s)")
    parser.add_argument("-w", "--workers", type=int, default=DEFAULT_WORKERS,
                        help="concurrent workers, one HTTP connection each "
                             "(default: %(default)s)")
    parser.add_argument("--allow-global-reset", action="store_true",
                        help="also call configs:reset_to_default without a "
                             "category, which resets every category at once")
    args = parser.parse_args()
    if args.repeat < 1:
        parser.error("--repeat must be at least 1")
    if args.workers < 1:
        parser.error("--workers must be at least 1")

    runner = SuiteRunner(args)
    try:
        passed = runner.run()
    except Failure as exc:
        suite_fail(SUITE, format_exception(exc))
        return 1
    except (OSError, TimeoutError, urllib.error.URLError) as exc:
        if rest_lib.looks_unreachable(exc):
            suite_fail(SUITE, f"connection failure: {format_exception(exc)}")
        else:
            suite_fail(SUITE, f"REST failure: {format_exception(exc)}")
        return 1
    finally:
        try:
            runner.cleanup()
        except Exception as exc:                          # noqa: BLE001
            warn(f"cleanup failed: {format_exception(exc)}")

    if passed:
        suite_ok(SUITE, f"{check_count()} checks")
        return 0
    suite_fail(SUITE, "see the failed check above")
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
