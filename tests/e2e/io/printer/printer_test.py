#!/usr/bin/env python3
# E2E: Verifies virtual IEC printer output and stability using a real C64 workload.

"""End-to-end virtual-printer harness for a real Ultimate 64 / 64e.

Drives the IEC virtual printer (device 4/5) with a dedicated 6510 assembly
workload, captures crash/hang behaviour, and verifies the resulting PNG/ASCII
output over FTP. The workload is the committed printer_e2e.prg, assembled from
printer_e2e.asm beside it, so running this needs no assembler. Pure REST
(http.client) + FTP (ftplib); no MCP/bridge dependency.

Style follows tests/e2e/filemanager/temp_auto_cleanup_perf_test.py:
argparse CLI, -H/--host, -p/--password, -n/--no-assertions, http.client with
Connection: close, X-Password only when a password is supplied, capture/
restore original settings, ftplib for on-device file verification.
"""

import argparse
import ftplib
import http.client
import io
import json
import os
import sys
import time

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
# The two C64 programs this suite runs are committed as assembled PRGs, so the
# suite needs no assembler and no BASIC tokenizer to run. Their sources sit
# beside them and the Makefile in this directory regenerates them; see the
# README for when that is needed.
WORKLOAD_PRG_PATH = os.path.join(SCRIPT_DIR, "printer_e2e.prg")
ISSUE_717_PRG_PATH = os.path.join(SCRIPT_DIR, "issue_717_basic.prg")
sys.path.insert(0, SCRIPT_DIR)
import png_lite  # noqa: E402  (local module, needs SCRIPT_DIR on sys.path first)

# tests/lib holds the reporting rules every suite shares.
sys.path.insert(0, os.path.join(SCRIPT_DIR, "..", "..", "..", "lib"))
import ftp as ftp_lib  # noqa: E402  (needs tests/lib on sys.path first)
import machine as machine_lib  # noqa: E402  (needs tests/lib on sys.path first)
import pacing  # noqa: E402  (needs tests/lib on sys.path first)
import rest as rest_lib
import targets  # noqa: E402  (needs tests/lib on sys.path first)
import wait  # noqa: E402  (needs tests/lib on sys.path first)
from api import UltimateApi  # noqa: E402  (needs tests/lib on sys.path first)
from report import (  # noqa: E402  (needs tests/lib on sys.path first)
    Failure, check_fail, check_ok, check_start, detail, section, warn)

try:
    from PIL import Image, ImageOps
    import pytesseract
    OCR_AVAILABLE = True
except ImportError:
    OCR_AVAILABLE = False

CONFIG_CATEGORY = "Printer Settings"
CONFIG_ITEMS = (
    "IEC printer",
    "Bus ID",
    "Output file",
    "Output type",
    "Ink density",
    "Page top margin (default is 5)",
    "Page height (default is 60)",
    "Emulation",
    "Commodore charset",
    "Epson charset",
    "IBM table 2",
)

PARAM_BASE = 0xC010
STATUS_BASE = 0xC000
STATUS_LEN = 12

EMU_CODE = {"epson": 1, "commodore": 2}
MODE_CODE = {"bitmap": 1, "text": 2}
EMU_CONFIG_VALUE = {"epson": "Epson FX-80/JX-80", "commodore": "Commodore MPS"}

PHASE_NAMES = {
    0x00: "not started",
    0x10: "opening printer",
    0x11: "printer opened",
    0x20: "printing",
    0x30: "form feed sent",
    0x40: "closing printer",
    0x41: "printer closed",
    0x7F: "complete",
    0x80: "failed",
}

FTP_USER_DEFAULT = "user"
FTP_PASSWORD_DEFAULT = "password"
POLL_INTERVAL_SECONDS = 0.5
# Shared with every suite; see tests/lib/pacing.py.
MENU_SETTLE_SECONDS = pacing.MENU_TOGGLE_SETTLE_SECONDS
# The menu toggle is observable, so it is waited for rather than slept on.
MENU_CLOSE_TIMEOUT_SECONDS = 5.0
# Enough Back presses to climb out of the deepest screen a launcher leads to,
# and one descent into the browser; and deeper than the launcher's own list,
# so Back reaches its first entry. See enter_file_browser.
LAUNCHER_DESCENT_STEPS = 10
LAUNCHER_ENTRY_LIMIT = 24
SCREEN_WIDTH = 40
SCREEN_HEIGHT = 25
SCREEN_CELLS = SCREEN_WIDTH * SCREEN_HEIGHT

# print_bitmap_row's default seed-pattern repeat count (16 bytes/repeat),
# matching the pre-existing "visible band" behaviour when full-page mode is off.
DEFAULT_BIM_REPEATS = 4

# Full-page bitmap mode: fill the whole configured printable page, not just a
# band. Mirrors the firmware's own geometry constants in mps_printer.h/.cc
# (MPS_PRINTER_HEAD_HEIGHT, the ESC A 8 / BIM interline, and the horizontal
# step per bit-image byte) so the requested row/repeat counts land just
# inside the current page instead of triggering an extra automatic
# FormFeed() partway through (which would silently split the fill across two
# pages instead of producing the single full page this mode is meant to
# verify).
MPS_PRINTER_HEAD_HEIGHT = 27
TEXT_LINE_PX = 36
EPSON_BIM_INTERLINE_PX = 24   # set via "ESC A 8" in print_bitmap_row's job init
CBM_BIM_INTERLINE_PX = 21     # fixed by the firmware's BIM entry (spacing_y[0][7])
EPSON_FULL_WIDTH_REPEATS = 120  # 120 * 16 = 1920px at 1px/byte (ESC Z step)
CBM_FULL_WIDTH_REPEATS = 30     # 30 * 16 = 480 bytes * 4px/byte = 1920px
FULL_PAGE_MIN_WIDTH_FRACTION = 0.85
FULL_PAGE_MIN_HEIGHT_FRACTION = 0.5
FULL_PAGE_MIN_INK_FRACTION = 0.05


def full_page_bitmap_params(emulation, page_height):
    """Return (rows, repeats) that fill the configured page for `emulation`
    without overrunning it into an extra automatic page break. Matches the
    firmware's own MPS_PRINTER_MAX_MARGIN_BOTTOM, which depends only on the
    configured page height, not the top margin."""
    interline = EPSON_BIM_INTERLINE_PX if emulation == "epson" else CBM_BIM_INTERLINE_PX
    margin_bottom = (page_height * TEXT_LINE_PX) - MPS_PRINTER_HEAD_HEIGHT - 1
    rows = max(1, (margin_bottom // interline) - 1)  # -1 row of safety margin
    rows = min(rows, 255)
    repeats = EPSON_FULL_WIDTH_REPEATS if emulation == "epson" else CBM_FULL_WIDTH_REPEATS
    return rows, repeats


def assert_or_warn(assertions_enabled, condition, message):
    if condition:
        return
    if assertions_enabled:
        raise Failure(message)
    warn(message)


class U64Client:
    """Minimal REST client mirroring temp_auto_cleanup_perf_test.py's style."""

    def __init__(self, host, password, timeout=10):
        self.target = targets.parse(host)
        self.host = self.target.device
        self.password = password
        self.timeout = timeout
        # For the calls this suite makes no assertion about, so that the menu
        # teardown has one implementation across the tree.
        self.api = UltimateApi(host, password, timeout)

    def _headers(self, body, extra_headers=None):
        headers = {"Connection": "close"}
        if self.password:
            headers["X-Password"] = self.password
        if body is not None:
            headers["Content-Length"] = str(len(body))
        if extra_headers:
            headers.update(extra_headers)
        return headers

    def request(self, method, path, body=None, extra_headers=None, timeout=None):
        # Transport and retry policy come from tests/lib/rest.py, the one place
        # that decides them. A request without a payload carries its arguments
        # in the query string, so applying it twice is the same as applying it
        # once; one with a payload would run a PRG or upload a file again, and
        # so is only resent when it never left the client.
        return rest_lib.retrying_http_request(
            self.target.host_for(path), method, path,
            body=body,
            headers=self._headers(body, extra_headers),
            timeout=timeout or self.timeout,
            idempotent=body is None,
        )

    def require_ok(self, method, path, body=None, description=None, extra_headers=None, timeout=None):
        status, _headers, payload = self.request(method, path, body=body, extra_headers=extra_headers, timeout=timeout)
        if status != 200:
            message = f"{description or path} failed with HTTP {status}"
            try:
                document = json.loads(payload.decode("utf-8"))
                if document.get("errors"):
                    message += f": {document['errors']}"
            except (ValueError, UnicodeDecodeError):
                message += f": {payload[:160]!r}"
            raise Failure(message)
        return payload

    def is_alive(self, timeout=3.0):
        try:
            status, _headers, _payload = self.request("GET", "/v1/version", timeout=timeout)
            return status == 200
        except (OSError, http.client.HTTPException):
            return False

    def get_config(self, category, item):
        path = f"/v1/configs/{urlquote(category)}/{urlquote(item)}"
        payload = self.require_ok("GET", path, description=f"read {category}/{item}")
        document = json.loads(payload.decode("utf-8"))
        return document[category][item]["current"]

    def set_config(self, category, item, value):
        path = f"/v1/configs/{urlquote(category)}/{urlquote(item)}?value={urlquote(str(value))}"
        self.require_ok("PUT", path, description=f"set {category}/{item}={value}")

    def writemem(self, address, data):
        path = f"/v1/machine:writemem?address={address:04X}"
        self.require_ok(
            "POST",
            path,
            body=data,
            description=f"writemem @{address:04X}",
            extra_headers={"Content-Type": "application/octet-stream"},
        )

    def readmem(self, address, length):
        path = f"/v1/machine:readmem?address={address:04X}&length={length}"
        return self.require_ok("GET", path, description=f"readmem @{address:04X}")

    def run_prg(self, prg_bytes):
        self.require_ok(
            "POST",
            "/v1/runners:run_prg",
            body=prg_bytes,
            description="run_prg",
            extra_headers={"Content-Type": "application/octet-stream"},
        )

    def menu_button(self):
        self.require_ok("PUT", "/v1/machine:menu_button", description="menu_button")

    def get_menu_screen(self):
        status, headers, payload = self.request("GET", "/v1/machine:menu_screen")
        if status == 404:
            return None
        if status != 200:
            raise Failure(f"menu_screen failed with HTTP {status}")
        return payload

    def post_input(self, events):
        body = json.dumps({"events": events}).encode("utf-8")
        self.require_ok(
            "POST",
            "/v1/machine:input",
            body=body,
            description="input",
            extra_headers={"Content-Type": "application/json"},
        )

    @property
    def launcher_browser_entry(self):
        """The launcher entry leading to the file browser, or None."""
        return machine_lib.identify(
            self.host, self._fetch_product).launcher_browser_entry

    @property
    def task_menu_key(self):
        """The key this machine opens the task menu with, in matrix terms.

        A C64 Ultimate puts it on F1 and uses F5 for paging, so pressing F5
        there scrolls a listing instead of opening anything. See
        tests/lib/machine.py.
        """
        device = machine_lib.identify(self.host, self._fetch_product)
        return device.task_menu_key.lower()

    def _fetch_product(self):
        status, _, body = self.request("GET", "/v1/info")
        if status != 200:
            raise Failure(f"/v1/info returned HTTP {status}")
        payload = json.loads(body.decode("utf-8"))
        return (str(payload.get("product", "")),
                str(payload.get("firmware_version", "")))

    def tap_key(self, key):
        self.post_input([{"kind": "keyboard", "inputs": [key], "transition": "tap"}])

    def tap_keys(self, keys):
        self.post_input([{"kind": "keyboard", "inputs": keys, "transition": "tap"}])

    def close_menu_from_anywhere(self):
        self.api.machine.close_menu_from_anywhere()
        if self.get_menu_screen() is not None:
            raise Failure("could not dismiss active menu UI before reset")


def urlquote(value):
    import urllib.parse

    return urllib.parse.quote(str(value), safe="")


def menu_screen_text(body):
    chars = body[:SCREEN_CELLS]
    rows = []
    for row in range(SCREEN_HEIGHT):
        row_chars = chars[row * SCREEN_WIDTH:(row + 1) * SCREEN_WIDTH]
        rows.append("".join(
            chr(ch & 0x7F) if 0x20 <= (ch & 0x7F) <= 0x7E else " "
            for ch in row_chars
        ))
    return rows


class FtpInspector:
    def __init__(self, host, user, password, timeout=15):
        self.host = host
        self.user = user
        self.password = password
        self.timeout = timeout

    def _session(self):
        return ftp_lib.session(self.host, self.password, self.timeout, user=self.user)

    def list_dir(self, directory):
        with self._session() as ftp:
            return ftp_lib.listing(ftp, directory)

    def printer_root(self):
        """Use the first mounted USB volume; Temp is the last resort."""
        entries = "\n".join(self.list_dir("/"))
        for volume in ("USB0", "USB1", "USB2"):
            if volume in entries:
                return "/" + volume
        return "/Temp"

    def file_size(self, path):
        with self._session() as ftp:
            try:
                return ftp.size(path)
            except ftplib.Error:
                return None

    def download(self, path):
        with self._session() as ftp:
            return ftp_lib.retrieve(ftp, path)

    def upload_bytes(self, path, data):
        with self._session() as ftp:
            ftp_lib.store(ftp, path, data)

    def ensure_directory(self, directory):
        """Create `directory` (and its parents) if it doesn't already exist.
        The printer's fopen() does not create missing directories, so an
        Output file pointed at one silently fails to save anything."""
        if directory in ("", "/"):
            return
        with self._session() as ftp:
            path = ""
            for part in [p for p in directory.split("/") if p]:
                path += "/" + part
                ftp_lib.make_dir(ftp, path)


def load_prg(path):
    """Read one of this directory's committed PRG fixtures."""
    try:
        with open(path, "rb") as handle:
            data = handle.read()
    except OSError as exc:
        raise Failure(f"could not read {os.path.basename(path)}: {exc}") from exc
    if not data:
        raise Failure(f"{os.path.basename(path)} is empty")
    return data


def decode_status(payload):
    if len(payload) < STATUS_LEN:
        raise Failure(f"short status block read: {len(payload)} bytes")
    magic0, magic1, phase, emulation, mode, page, row, readst, final_status, last_op, hb_lo, hb_hi = payload[:12]
    return {
        "magic_ok": magic0 == 0x55 and magic1 == 0x36,
        "phase": phase,
        "phase_name": PHASE_NAMES.get(phase, f"unknown(${phase:02X})"),
        "emulation": emulation,
        "mode": mode,
        "page": page,
        "row": row,
        "readst": readst,
        "final_status": final_status,
        "last_op": last_op,
        "heartbeat": hb_lo | (hb_hi << 8),
    }


def classify_and_run(client, prg_bytes, emulation, mode, rows, pages, bus_id, timeout_seconds, poll_interval,
                      bim_repeats=DEFAULT_BIM_REPEATS, issue_717_basic=False):
    """Write params, launch the PRG, poll the status block. Returns (classification, status_or_none)."""
    if issue_717_basic:
        client.run_prg(prg_bytes)
        time.sleep(5.0)
        if not client.is_alive(timeout=3.0):
            return "FAIL_CRASH_HARD", None
        return "PASS_RUN", {"phase_name": "literal BASIC completed", "heartbeat": 0}

    params = bytes([
        EMU_CODE[emulation], MODE_CODE[mode], rows & 0xFF, pages & 0xFF,
        bus_id, bim_repeats & 0xFF, 1 if issue_717_basic else 0,
    ])
    client.writemem(PARAM_BASE, params)
    client.run_prg(prg_bytes)

    deadline = time.monotonic() + timeout_seconds
    last_status = None
    last_heartbeat = -1
    stall_since = None

    while time.monotonic() < deadline:
        if not client.is_alive(timeout=3.0):
            time.sleep(1.5)
            if not client.is_alive(timeout=3.0):
                return "FAIL_CRASH_HARD", last_status
            continue

        try:
            payload = client.readmem(STATUS_BASE, STATUS_LEN)
            status = decode_status(payload)
        except (Failure, OSError, http.client.HTTPException):
            time.sleep(poll_interval)
            continue

        last_status = status
        if not status["magic_ok"]:
            time.sleep(poll_interval)
            continue

        if status["heartbeat"] != last_heartbeat:
            last_heartbeat = status["heartbeat"]
            stall_since = None
        else:
            stall_since = stall_since or time.monotonic()

        if status["final_status"] == 0x7F:
            return "PASS_RUN", status
        if status["final_status"] == 0x80:
            return "FAIL_IEC", status

        time.sleep(poll_interval)

    if not client.is_alive(timeout=5.0):
        return "FAIL_CRASH_HARD", last_status
    return "FAIL_TIMEOUT", last_status


def enter_file_browser(client, settle):
    """Descend from a launcher into the file browser, where there is one.

    A no-op on a machine whose menu button opens the browser itself. A C64
    Ultimate opens a launcher instead, and the task menu below belongs to the
    browser: pressed on the launcher it opens the main menu, which has no
    Printer category at all. The browser is the launcher's first entry, so
    Back to the top of the list and then Return reaches it.
    """
    entry = client.launcher_browser_entry
    if entry is None:
        return
    for _ in range(LAUNCHER_DESCENT_STEPS):
        screen = client.get_menu_screen()
        if screen is None:
            return
        rows = menu_screen_text(screen)
        if rows[-1].lstrip().startswith("/"):
            return
        if any(entry in row for row in rows):
            for _ in range(LAUNCHER_ENTRY_LIMIT):
                client.tap_keys(["left_shift", "cursor_up_down"])
            client.tap_key("return")
        else:
            client.tap_keys(["left_shift", "cursor_left_right"])
        time.sleep(settle)


def flush_via_menu(client, assertions_enabled, settle=MENU_SETTLE_SECONDS):
    """Drive the Ultimate on-screen Tasks menu to trigger Printer > Flush/Eject."""
    if client.get_menu_screen() is not None:
        client.menu_button()  # close whatever is open
        wait.wait_until(lambda: client.get_menu_screen() is None,
                        "the menu to close before Flush/Eject",
                        timeout=MENU_CLOSE_TIMEOUT_SECONDS)

    client.menu_button()  # open the menu
    time.sleep(settle)
    enter_file_browser(client, settle)

    if client.get_menu_screen() is None:
        # No menu-screen endpoint on this build: the menu button has just been
        # pressed, so the menu is open, yet nothing can be read back. Drive the
        # Tasks menu blind, with Printer as the preselected entry.
        #
        # Which build this is has to be decided with the menu open. The check
        # used to run before the menu was opened, where the endpoint answers 404
        # on every build because there is no menu to return, so every run took
        # this branch: the step verified nothing, and its two RETURN presses
        # landed on the Tasks menu's real first entry, Assembly 64, whose query
        # form was then left open for the next suite (confirmed live).
        client.tap_key(client.task_menu_key)
        time.sleep(settle)
        client.tap_key("return")
        time.sleep(settle)
        client.tap_key("return")
        time.sleep(1.0)
        return

    client.tap_key("f5")  # open Tasks (context menu) for the current selection
    time.sleep(settle)

    body = client.get_menu_screen()
    if body is None:
        raise Failure("task menu did not open")
    rows = menu_screen_text(body)

    if not any("Printer" in row for row in rows):
        raise Failure("'Printer' task category not found in task menu")

    # Sought by name rather than walked to by row. The task menu is drawn beside
    # the browser's cursor, so where it starts moves with the selection: measured
    # on u2@c64u with the browser cursor on row 15, the menu frame opened at row
    # 6 and 'Printer' was on row 15, where arithmetic from a fixed first-entry
    # row put it nowhere near. ContextMenu::seek_char takes the first entry
    # beginning with the typed letter, which needs no row assumption at all.
    client.tap_key("p")
    time.sleep(settle)

    client.tap_key("return")  # expand the Printer category
    time.sleep(settle)

    body = client.get_menu_screen()
    if body is None:
        raise Failure("printer task submenu did not open")
    rows = menu_screen_text(body)
    # Only that the item is on screen: the expanded submenu's first item is not
    # drawn on the category's own row (measured: one above it), so asserting a
    # row here tested the layout rather than the action.
    assert_or_warn(
        assertions_enabled,
        any("Flush/Eject" in row for row in rows),
        f"'Flush/Eject' not in the expanded Printer submenu: {rows!r}",
    )

    client.tap_key("f")     # seek Flush/Eject within the submenu
    time.sleep(settle)
    client.tap_key("return")  # trigger it
    time.sleep(1.0)

    # Close what this function opened, so the caller's teardown does not have to
    # tap its way out with RETURN, which activates the entry under the cursor.
    client.close_menu_from_anywhere()


def capture_settings(client, assertions_enabled):
    snapshot = {}
    for item in CONFIG_ITEMS:
        try:
            snapshot[item] = client.get_config(CONFIG_CATEGORY, item)
        except Failure as exc:
            assert_or_warn(assertions_enabled, False, f"could not capture {item}: {exc}")
    return snapshot


def restore_settings(client, snapshot, assertions_enabled):
    if not snapshot:
        return
    section("restoring original Printer Settings")
    for item, value in snapshot.items():
        try:
            client.set_config(CONFIG_CATEGORY, item, value)
            detail(f"{item}: {value}")
        except Failure as exc:
            assert_or_warn(assertions_enabled, False, f"could not restore {item}: {exc}")


def apply_settings(client, output_base, output_type, ink_density, page_top_margin, page_height,
                    emulation, bus_id, assertions_enabled):
    changes = (
        ("IEC printer", "Enabled"),
        ("Bus ID", bus_id),
        ("Output file", output_base),
        ("Output type", output_type),
        ("Ink density", ink_density),
        ("Page top margin (default is 5)", page_top_margin),
        ("Page height (default is 60)", page_height),
        ("Emulation", EMU_CONFIG_VALUE[emulation]),
        ("Commodore charset", "USA/UK"),
        ("Epson charset", "Basic"),
        ("IBM table 2", "International 1"),
    )
    for item, value in changes:
        try:
            client.set_config(CONFIG_CATEGORY, item, value)
        except Failure as exc:
            assert_or_warn(assertions_enabled, False, f"could not set {item}={value}: {exc}")


def download_with_retry(inspector, path, timeout_seconds=10.0, interval_seconds=0.5):
    """The device writes the PNG asynchronously after Flush/Eject returns, so
    the file may not be visible over FTP for a moment; poll briefly."""
    deadline = time.monotonic() + timeout_seconds
    last_error = None
    while time.monotonic() < deadline:
        try:
            return inspector.download(path)
        except ftplib.Error as exc:
            last_error = exc
            time.sleep(interval_seconds)
    raise Failure(f"{path}: not available over FTP after {timeout_seconds}s ({last_error})")


def page_output_path(output_base, page_number):
    directory, base = os.path.split(output_base)
    directory = directory or "/"
    name = f"{base}-{page_number:03d}.png"
    return f"{directory}/{name}" if directory != "/" else f"/{name}"


def verify_png_output(inspector, output_base, expected_pages, assertions_enabled, first_page=1):
    for page in range(first_page, first_page + expected_pages):
        path = page_output_path(output_base, page)
        data = download_with_retry(inspector, path)
        assert_or_warn(assertions_enabled, len(data) > 0, f"{path}: file is empty")
        ok, reason = png_lite.png_is_well_formed(data)
        assert_or_warn(assertions_enabled, ok, f"{path}: not a well-formed PNG ({reason})")
        dims = png_lite.decode_png_dimensions(data)
        assert_or_warn(assertions_enabled, dims is not None, f"{path}: could not read IHDR dimensions")
        # assert_or_warn only warns under --no-assertions, so this line still
        # runs with dims unset; indexing it there would abort the whole run.
        size = f"{dims[0]}x{dims[1]}px" if dims else "size unreadable"
        detail(f"{path}: {len(data)} bytes, {size}, {'valid' if ok else 'INVALID'} PNG")


def verify_full_page_coverage(inspector, output_base, assertions_enabled, first_page=1):
    """Decode page 1's pixels and check the ink actually covers most of the
    page - proof this is a genuine full-page fill, not just a small band
    mislabeled as one (e.g. from a parameter that silently had no effect)."""
    path = page_output_path(output_base, first_page)

    data = download_with_retry(inspector, path)
    width, height, pixel_rows = png_lite.decode_indexed(data)
    bbox = png_lite.ink_bounding_box(pixel_rows)
    assert_or_warn(assertions_enabled, bbox is not None, f"{path}: full-page bitmap produced a blank page")
    if bbox is None:
        return

    min_x, min_y, max_x, max_y, ink_count = bbox
    bbox_width = max_x - min_x + 1
    bbox_height = max_y - min_y + 1
    width_fraction = bbox_width / width
    height_fraction = bbox_height / height
    ink_fraction = ink_count / (bbox_width * bbox_height)

    detail(
        f"{path}: ink spans {bbox_width}x{bbox_height}px at ({min_x},{min_y}) "
        f"= {width_fraction:.0%} of page width, {height_fraction:.0%} of page height, "
        f"{ink_fraction:.0%} ink density within that area ({ink_count} ink pixels)")
    assert_or_warn(
        assertions_enabled, width_fraction >= FULL_PAGE_MIN_WIDTH_FRACTION,
        f"{path}: bitmap only spans {width_fraction:.0%} of page width "
        f"(expected >= {FULL_PAGE_MIN_WIDTH_FRACTION:.0%} for a full-page fill)",
    )
    assert_or_warn(
        assertions_enabled, height_fraction >= FULL_PAGE_MIN_HEIGHT_FRACTION,
        f"{path}: bitmap only spans {height_fraction:.0%} of page height "
        f"(expected >= {FULL_PAGE_MIN_HEIGHT_FRACTION:.0%} for a full-page fill)",
    )
    assert_or_warn(
        assertions_enabled, ink_fraction >= FULL_PAGE_MIN_INK_FRACTION,
        f"{path}: ink density {ink_fraction:.0%} within the covered area looks too "
        f"sparse to be a real bitmap fill (expected >= {FULL_PAGE_MIN_INK_FRACTION:.0%})",
    )


def ocr_page_text(data):
    """OCR the ink region of a printed page PNG. Returns the recognized text
    (uppercased) or None if there is no ink to OCR."""
    image = Image.open(io.BytesIO(data)).convert("L")
    bbox = ImageOps.invert(image).getbbox()  # non-white region, without a numpy dependency
    if bbox is None:
        return None
    margin = 4
    left, upper, right, lower = bbox
    crop = image.crop((max(0, left - margin), max(0, upper - margin),
                        min(image.width, right + margin), min(image.height, lower + margin)))
    upscaled = crop.resize((crop.width * 4, crop.height * 4), Image.LANCZOS)
    thresholded = upscaled.point(lambda p: 0 if p < 180 else 255)
    return pytesseract.image_to_string(thresholded, config="--psm 6").upper()


def verify_text_ocr(inspector, output_base, emulation, pages, rows, assertions_enabled, first_page=1):
    """OCR-verify that the printed text pages actually contain the expected
    deterministic content (emulation tag, mode tag, and every PAGE=/ROW=
    marker) - not just that a plausible-looking PNG was produced.

    The dot-matrix "draft" font is not reliably OCRable even after image
    preprocessing (tested: garbage output). Text-mode rows therefore switch
    the printer into NLQ + bold + double-strike (see printer_e2e.asm), which
    OCRs correctly for the parts that matter: the emulation/mode tags and the
    PAGE=/ROW= counters that prove the assembly program's page/row loop and
    decimal-formatting routine are actually working. The fixed decorative
    A-Z/0-9 suffix on each row is intentionally not required to match
    character-for-character - several glyphs (e.g. Q, X, Z) are genuinely
    ambiguous at this resolution/font even to a human, and matching it isn't
    necessary to prove the printed content is correct.
    """
    if not OCR_AVAILABLE:
        detail("OCR verification skipped: pytesseract/PIL not installed "
              "(pip install pytesseract, apt install tesseract-ocr)")
        return

    expected_tag = EMU_CONFIG_VALUE[emulation].split()[0].upper()  # "EPSON" / "COMMODORE"
    for logical_page in range(1, pages + 1):
        page = first_page + logical_page - 1
        path = page_output_path(output_base, page)
        data = download_with_retry(inspector, path)
        text = ocr_page_text(data)

        assert_or_warn(assertions_enabled, text is not None, f"{path}: OCR found no ink to read")
        if text is None:
            continue

        preview = " / ".join(line.strip() for line in text.splitlines() if line.strip())
        detail(f"{path}: OCR read: {preview[:160]}{'...' if len(preview) > 160 else ''}")

        assert_or_warn(
            assertions_enabled, expected_tag in text,
            f"{path}: OCR did not find the expected '{expected_tag}' emulation tag",
        )
        assert_or_warn(
            assertions_enabled, "TEXT" in text,
            f"{path}: OCR did not find the expected 'TEXT' mode tag",
        )
        assert_or_warn(
            assertions_enabled, f"PAGE={logical_page:03d}" in text,
            f"{path}: OCR did not find 'PAGE={logical_page:03d}'",
        )
        for row in range(1, rows + 1):
            assert_or_warn(
                assertions_enabled, f"ROW={row:03d}" in text,
                f"{path}: OCR did not find 'ROW={row:03d}' (row {row} of {rows})",
            )


MAX_OUTPUT_FILE_LENGTH = 31  # CFG_PRINTER_FILENAME bound in iec_printer.cc; silently truncated beyond this


def combo_code(emulation, mode):
    return f"{emulation[0]}{mode[0]}"  # e.g. "eb" = epson/bitmap, "ct" = commodore/text


def unique_output_base(prefix, emulation, mode):
    suffix = f"{int(time.time()) % 0xFFFF:04x}"
    return f"{prefix}-{combo_code(emulation, mode)}{suffix}"


def require_output_file_length(output_base):
    if len(output_base) > MAX_OUTPUT_FILE_LENGTH:
        raise Failure(
            f"Output file '{output_base}' is {len(output_base)} chars; the firmware's "
            f"Output file setting truncates silently beyond {MAX_OUTPUT_FILE_LENGTH} "
            f"(CFG_PRINTER_FILENAME), which would make verification look for the wrong file"
        )


def seed_page_num_collision(inspector, output_base, last_page):
    """Force calcPageNum() through the same basename scan shape as issue #717:
    the parent directory contains both a directory named like the basename and
    several same-prefix non-matching files, plus one real <base>-NNN.png that
    should determine the next page number."""
    directory, base = os.path.split(output_base)
    directory = directory or "/"
    collision_dir = f"{directory}/{base}" if directory != "/" else f"/{base}"
    inspector.ensure_directory(collision_dir)

    seeded_paths = (
        page_output_path(output_base, last_page),
        f"{directory}/{base}-old.png" if directory != "/" else f"/{base}-old.png",
        f"{directory}/{base}-7.png" if directory != "/" else f"/{base}-7.png",
        f"{directory}/{base}-abc.png" if directory != "/" else f"/{base}-abc.png",
        f"{directory}/{base}-123.txt" if directory != "/" else f"/{base}-123.txt",
    )
    for path in seeded_paths:
        inspector.upload_bytes(path, b"seed")


PRESETS = {
    "crash-epson-png": dict(emulation="epson", mode="bitmap", output_type="PNG B&W",
                             ink_density="Medium", page_top_margin=1, page_height=66,
                             bus_id=4, rows=4, pages=1),
    "full-matrix": dict(output_type="PNG B&W", page_top_margin=1, page_height=66, bus_id=4),
    "issue-717-overflow": dict(
        emulation="epson", mode="bitmap", output_type="PNG B&W", ink_density="Medium",
        page_top_margin=1, page_height=66, bus_id=4, rows=126, pages=1,
        verify_output=True, expected_output_pages=2,
        seed_page_num_collision=True, seed_last_page=7,
    ),
    "issue-717-basic": dict(
        emulation="epson", mode="text", output_type="PNG B&W", ink_density="Medium",
        page_top_margin=1, page_height=66, bus_id=4, rows=100, pages=1,
        verify_output=True, expected_output_pages=1, issue_717_basic=True,
    ),
}


def parse_args():
    parser = argparse.ArgumentParser(
        description="Virtual-printer end-to-end harness for a real Ultimate 64/64e.",
        epilog="Captures original Printer Settings and restores them on exit unless "
               "--no-config-change is used. Runs the committed printer_e2e.prg "
               "fixture alongside this script; no assembler is needed.",
    )
    parser.add_argument("-H", "--host", default=os.environ.get("U64_HOST", "u64"),
                        help="IP or hostname of the U64 (default: $U64_HOST or u64)")
    parser.add_argument("-p", "--password", default=os.environ.get("U64_PASS", ""),
                        help="U64 REST password (default: $U64_PASS, empty)")
    parser.add_argument("-n", "--no-assertions", action="store_true",
                         help="Warn instead of failing on assertion mismatches")
    parser.add_argument("--seed-count", type=int, default=0, help=argparse.SUPPRESS)
    parser.add_argument("--test-count", type=int, default=1, help=argparse.SUPPRESS)
    parser.add_argument("--duration", type=float, default=0.0, help=argparse.SUPPRESS)
    parser.add_argument("--stage", choices=["reproduce", "verify", "matrix", "all"], default="reproduce",
                         help="reproduce: single run; matrix: all 4 emulation x mode combos; "
                              "verify: reproduce + output verification; all: matrix + verification")
    parser.add_argument("--no-config-change", action="store_true",
                         help="Do not change or restore Printer Settings (caller must configure)")
    parser.add_argument("--emulation", choices=["epson", "commodore", "both"], default="epson")
    # Not the UI transport: every other suite's --mode is telnet/freeze/overlay,
    # and run-tests substitutes that into @MODE@. This one selects what the
    # printer prints, so it says so.
    parser.add_argument("--print-mode", dest="mode",
                        choices=["bitmap", "text", "both"], default="bitmap",
                        help="What to print: bitmap, text, or both "
                             "(default: bitmap).")
    parser.add_argument("--rows", type=int, default=4, help="Bitmap lines or text rows per page (1-255)")
    parser.add_argument("--pages", type=int, default=1, help="Number of pages to print and eject (1-9)")
    parser.add_argument(
        "--full-page-bitmap", action="store_true",
        help="Print a bitmap that fully covers the configured page (width and height), "
             "instead of the default small band. Disabled by default: this sends far "
             "more IEC data per page (tens of thousands of bytes) and can take a "
             "couple of minutes per combination. Forces bitmap-only mode and 1 page, "
             "and verifies the resulting image's ink actually covers most of the page "
             "(not just that a PNG file was produced).",
    )
    parser.add_argument("--bus-id", type=int, choices=[4, 5], default=4)
    parser.add_argument("--output-base", default=None,
                         help="Printer 'Output file' base path (default: unique /Usb0/printer/e2e-<run-id>). "
                              "The base's directory is created over FTP if it doesn't already exist.")
    parser.add_argument("--expected-output-pages", type=int, default=None,
                         help="Expected number of output PNG pages to verify. Defaults to --pages, "
                              "but set this higher for a single continuous print that naturally "
                              "overflows onto an extra page before the final Flush/Eject.")
    parser.add_argument("--output-type", default="PNG B&W")
    parser.add_argument("--ink-density", default="Medium")
    parser.add_argument("--page-top-margin", type=int, default=1)
    parser.add_argument("--page-height", type=int, default=66)
    parser.add_argument("--seed-page-num-collision", action="store_true",
                         help="Pre-create a directory named like the output basename plus several "
                              "same-prefix junk files, and seed one existing <base>-NNN.png, so "
                              "calcPageNum() must ignore colliding names and continue at NNN+1.")
    parser.add_argument("--seed-last-page", type=int, default=7,
                         help="When --seed-page-num-collision is used, seed an existing "
                         "<base>-NNN.png with this page number (default: 7).")
    parser.add_argument("--issue-717-basic", action="store_true", help=argparse.SUPPRESS)
    parser.add_argument("--host-output-dir", default=None, help=argparse.SUPPRESS)
    parser.add_argument("--ftp-user", default=FTP_USER_DEFAULT)
    parser.add_argument("--ftp-password", default=FTP_PASSWORD_DEFAULT)
    parser.add_argument("--verify-output", action="store_true",
                         help="Download and structurally validate the resulting PNG(s) via FTP")
    parser.add_argument("--keep-output", action="store_true", help=argparse.SUPPRESS)
    parser.add_argument("--timeout-seconds", type=int, default=60,
                         help="Max seconds to wait for the on-device PRG to finish printing")
    parser.add_argument("--reset-before-run", action="store_true")
    parser.add_argument("--reset-after-run", action="store_true")
    parser.add_argument("--stop-on-crash", action="store_true", default=True)
    parser.add_argument("--limit", type=int, default=10, help=argparse.SUPPRESS)
    parser.add_argument("--preset", choices=list(PRESETS.keys()), default=None)
    args = parser.parse_args()

    if args.preset:
        for key, value in PRESETS[args.preset].items():
            setattr(args, key.replace("-", "_"), value)

    if args.rows < 1 or args.rows > 255:
        parser.error("--rows must be between 1 and 255")
    if args.pages < 1 or args.pages > 9:
        parser.error("--pages must be between 1 and 9")
    if args.expected_output_pages is not None and args.expected_output_pages < 1:
        parser.error("--expected-output-pages must be >= 1")
    if args.seed_last_page < 1 or args.seed_last_page > 999:
        parser.error("--seed-last-page must be between 1 and 999")

    if args.full_page_bitmap:
        if args.mode == "text":
            parser.error("--full-page-bitmap requires bitmap mode (--mode bitmap or both)")
        args.mode = "bitmap"
        args.pages = 1
        if args.timeout_seconds == 60:  # still the default; give full-page runs more room
            args.timeout_seconds = 240

    return args


def combos_for(args):
    emulations = ["epson", "commodore"] if args.emulation == "both" else [args.emulation]
    modes = ["bitmap", "text"] if args.mode == "both" else [args.mode]
    if args.stage in ("matrix", "all") and args.preset != "crash-epson-png" and not args.full_page_bitmap:
        emulations = ["epson", "commodore"]
        modes = ["bitmap", "text"]
    return [(e, m) for e in emulations for m in modes]


def run_combo(client, inspector, prg_bytes, args, emulation, mode, assertions_enabled, disambiguate):
    label = f"{emulation}/{mode}"
    check_start(f"print {label}: apply settings + run PRG")

    if args.output_base:
        base = args.output_base
    elif args.seed_page_num_collision:
        base = inspector.printer_root() + "/printer"
    else:
        base = inspector.printer_root() + "/printer/e2e"
    if args.output_base is None:
        output_base = unique_output_base(base, emulation, mode)
    elif disambiguate:
        # Multiple emulation/mode combos share one run; PNG page numbering is
        # scoped per output filename, so distinct bases avoid reading stale
        # pages left over from an earlier combo in the same run.
        output_base = f"{base}-{combo_code(emulation, mode)}"
    else:
        output_base = base
    require_output_file_length(output_base)

    output_directory = os.path.dirname(output_base)
    if output_directory:
        inspector.ensure_directory(output_directory)

    first_page = 1
    if args.seed_page_num_collision:
        seed_page_num_collision(inspector, output_base, args.seed_last_page)
        first_page = args.seed_last_page + 1

    if not args.no_config_change:
        apply_settings(
            client, output_base, args.output_type, args.ink_density,
            args.page_top_margin, args.page_height, emulation, args.bus_id,
            assertions_enabled,
        )

    if args.full_page_bitmap:
        rows, bim_repeats = full_page_bitmap_params(emulation, args.page_height)
        detail(f"full-page bitmap: {rows} rows x {bim_repeats * 16} bytes/row "
              f"(~{rows * (EPSON_BIM_INTERLINE_PX if emulation == 'epson' else CBM_BIM_INTERLINE_PX)}px "
              f"tall, ~{bim_repeats * 16 * (1 if emulation == 'epson' else 4)}px wide)")
    else:
        rows, bim_repeats = args.rows, DEFAULT_BIM_REPEATS

    classification, status = classify_and_run(
        client, prg_bytes, emulation, mode, rows, args.pages, args.bus_id,
        args.timeout_seconds, POLL_INTERVAL_SECONDS, bim_repeats=bim_repeats,
        issue_717_basic=args.issue_717_basic,
    )

    if classification == "FAIL_CRASH_HARD":
        check_fail("device became unresponsive (FAIL_CRASH_HARD)")
        detail("REST and ping are both unreachable. This matches the reported crash:\n"
               "screen off, unresponsive, C64/machine reset will not help.\n"
               "Recover by redeploying the firmware over JTAG, or by power-cycling the device.")
        return "FAIL_CRASH_HARD", output_base, status
    if classification == "FAIL_TIMEOUT":
        check_fail(f"timed out after {args.timeout_seconds}s, REST still responsive")
        return "FAIL_TIMEOUT", output_base, status
    if classification == "FAIL_IEC":
        check_fail(f"PRG reported IEC failure: {status}")
        return "FAIL_IEC", output_base, status

    check_ok(f"phase={status['phase_name']} heartbeat={status['heartbeat']}")

    check_start(f"print {label}: Flush/Eject via on-screen menu")
    try:
        flush_via_menu(client, assertions_enabled)
    except Failure as exc:
        if not client.is_alive(timeout=5.0):
            check_fail("device became unresponsive during Flush/Eject (FAIL_CRASH_HARD)")
            return "FAIL_CRASH_HARD", output_base, status
        check_fail(str(exc))
        return "FAIL_HARNESS", output_base, status

    if not client.is_alive(timeout=5.0):
        check_fail("device became unresponsive after Flush/Eject (FAIL_CRASH_HARD)")
        return "FAIL_CRASH_HARD", output_base, status
    check_ok()

    if args.full_page_bitmap:
        check_start(f"print {label}: verify full-page coverage over FTP")
        try:
            verify_png_output(inspector, output_base, args.pages, assertions_enabled, first_page=first_page)
            verify_full_page_coverage(inspector, output_base, assertions_enabled, first_page=first_page)
            check_ok()
            return "PASS", output_base, status
        except Failure as exc:
            check_fail(str(exc))
            return "FAIL_VERIFICATION", output_base, status

    if args.verify_output:
        check_start(f"print {label}: verify output over FTP")
        try:
            expected_pages = args.expected_output_pages or args.pages
            verify_png_output(
                inspector, output_base, expected_pages, assertions_enabled, first_page=first_page,
            )
            if mode == "text" and not args.issue_717_basic:
                verify_text_ocr(
                    inspector, output_base, emulation, expected_pages, rows, assertions_enabled,
                    first_page=first_page,
                )
            check_ok()
            return "PASS", output_base, status
        except Failure as exc:
            check_fail(str(exc))
            return "FAIL_VERIFICATION", output_base, status

    return "PASS_NO_VERIFY", output_base, status


def main():
    args = parse_args()
    assertions_enabled = not args.no_assertions

    client = U64Client(args.host, args.password)
    inspector = FtpInspector(args.host, args.ftp_user, args.ftp_password)

    check_start(f"REST reachable at {args.host}")
    if not client.is_alive():
        check_fail("no response from /v1/version")
        return 1
    check_ok()

    check_start("reset before run")
    client.close_menu_from_anywhere()
    client.api.machine.reset(force=True)
    check_ok()

    prg_path = ISSUE_717_PRG_PATH if args.issue_717_basic else WORKLOAD_PRG_PATH
    check_start(f"load {os.path.basename(prg_path)}")
    try:
        prg_bytes = load_prg(prg_path)
    except Failure as exc:
        check_fail(str(exc))
        return 1
    check_ok(f"{len(prg_bytes)} bytes")

    snapshot = {}
    if not args.no_config_change:
        check_start("capture original Printer Settings")
        snapshot = capture_settings(client, assertions_enabled)
        check_ok()

    results = []
    combos = combos_for(args)
    disambiguate = len(combos) > 1
    try:
        for emulation, mode in combos:
            classification, output_base, status = run_combo(
                client, inspector, prg_bytes, args, emulation, mode, assertions_enabled, disambiguate,
            )
            results.append((emulation, mode, classification, output_base))
            if classification == "FAIL_CRASH_HARD" and args.stop_on_crash:
                print("\nHARD CRASH detected - stopping the run immediately (--stop-on-crash).")
                break
    finally:
        if not args.no_config_change and all(c != "FAIL_CRASH_HARD" for *_, c, _ in results):
            restore_settings(client, snapshot, assertions_enabled)

    if args.reset_after_run and client.is_alive(timeout=5.0):
        check_start("reset after run")
        client.require_ok("PUT", "/v1/machine:reset", description="machine:reset")
        check_ok()

    section("summary")
    for emulation, mode, classification, output_base in results:
        detail(f"{emulation:10s} {mode:6s} {classification:20s} {output_base}")

    failed = [r for r in results if r[2] not in ("PASS", "PASS_NO_VERIFY")]
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
