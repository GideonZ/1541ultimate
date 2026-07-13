#!/usr/bin/env python3
"""End-to-end virtual-printer harness for a real Ultimate 64 / 64e.

Drives the IEC virtual printer (device 4/5) with a dedicated 6510 assembly
workload (printer-e2e.asm, assembled with 64tass), captures crash/hang
behaviour, and verifies the resulting PNG/ASCII output over FTP. Pure REST
(http.client) + FTP (ftplib); no MCP/bridge dependency.

Style follows tools/io/temp-auto-cleanup/temp-auto-cleanup-perf-test.py:
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
import subprocess
import struct
import sys
import time
import zlib

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
DEFAULT_ASM_PATH = os.path.join(SCRIPT_DIR, "printer-e2e.asm")
sys.path.insert(0, SCRIPT_DIR)
import png_lite  # noqa: E402  (local module, needs SCRIPT_DIR on sys.path first)

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
MENU_SETTLE_SECONDS = 0.35
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


class Failure(RuntimeError):
    pass


CHECK_COUNT = 0


def check_start(label):
    global CHECK_COUNT
    CHECK_COUNT += 1
    print(f"[{CHECK_COUNT:02d}] {label} ... ", end="", flush=True)


def check_ok(extra=""):
    print("OK" + (f" ({extra})" if extra else ""), flush=True)


def check_fail(reason):
    print(f"FAIL ({reason})", flush=True)


def assert_or_warn(assertions_enabled, condition, message):
    if condition:
        return
    if assertions_enabled:
        raise Failure(message)
    print(f"WARNING: {message}")


class U64Client:
    """Minimal REST client mirroring temp-auto-cleanup-perf-test.py's style."""

    def __init__(self, host, password, timeout=10):
        self.host = host
        self.password = password
        self.timeout = timeout

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
        connection = http.client.HTTPConnection(self.host, timeout=timeout or self.timeout)
        try:
            connection.request(method, path, body=body, headers=self._headers(body, extra_headers))
            response = connection.getresponse()
            payload = response.read()
            return response.status, dict(response.getheaders()), payload
        finally:
            connection.close()

    def require_ok(self, method, path, body=None, description=None, extra_headers=None, timeout=None):
        status, _headers, payload = self.request(method, path, body=body, extra_headers=extra_headers, timeout=timeout)
        if status != 200:
            message = f"{description or path} failed with HTTP {status}"
            try:
                detail = json.loads(payload.decode("utf-8"))
                if detail.get("errors"):
                    message += f": {detail['errors']}"
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

    def tap_key(self, key):
        self.post_input([{"kind": "keyboard", "inputs": [key], "transition": "tap"}])


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

    def _open(self):
        ftp = ftplib.FTP()
        ftp.connect(self.host, 21, timeout=self.timeout)
        ftp.login(self.user, self.password)
        return ftp

    def list_dir(self, directory):
        ftp = self._open()
        try:
            entries = []
            ftp.retrlines(f"LIST {directory}", entries.append)
            return entries
        finally:
            try:
                ftp.quit()
            except (OSError, EOFError, ftplib.Error):
                ftp.close()

    def file_size(self, path):
        ftp = self._open()
        try:
            return ftp.size(path)
        except ftplib.Error:
            return None
        finally:
            try:
                ftp.quit()
            except (OSError, EOFError, ftplib.Error):
                ftp.close()

    def download(self, path):
        ftp = self._open()
        try:
            buf = io.BytesIO()
            ftp.retrbinary(f"RETR {path}", buf.write)
            return buf.getvalue()
        finally:
            try:
                ftp.quit()
            except (OSError, EOFError, ftplib.Error):
                ftp.close()

    def ensure_directory(self, directory):
        """Create `directory` (and its parents) if it doesn't already exist.
        The printer's fopen() does not create missing directories, so an
        Output file pointed at one silently fails to save anything."""
        if directory in ("", "/"):
            return
        ftp = self._open()
        try:
            parts = [p for p in directory.split("/") if p]
            path = ""
            for part in parts:
                path += "/" + part
                try:
                    ftp.mkd(path)
                except ftplib.error_perm:
                    pass  # already exists
        finally:
            try:
                ftp.quit()
            except (OSError, EOFError, ftplib.Error):
                ftp.close()


def assemble_prg(asm_path, assembler):
    out_path = asm_path + ".out.prg"
    result = subprocess.run(
        [assembler, "-q", "-o", out_path, asm_path],
        capture_output=True,
        text=True,
    )
    if result.returncode != 0:
        raise Failure(f"assembler failed: {result.stdout}\n{result.stderr}")
    with open(out_path, "rb") as handle:
        data = handle.read()
    os.remove(out_path)
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
                      bim_repeats=DEFAULT_BIM_REPEATS):
    """Write params, launch the PRG, poll the status block. Returns (classification, status_or_none)."""
    params = bytes([EMU_CODE[emulation], MODE_CODE[mode], rows & 0xFF, pages & 0xFF, bus_id, bim_repeats & 0xFF])
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


def flush_via_menu(client, assertions_enabled, settle=MENU_SETTLE_SECONDS):
    """Drive the Ultimate on-screen Tasks menu to trigger Printer > Flush/Eject."""
    body = client.get_menu_screen()
    if body is not None:
        client.menu_button()  # close whatever is open
        time.sleep(settle)

    client.menu_button()  # open root browser
    time.sleep(settle)
    client.tap_key("f5")  # open Tasks (context menu) for the current selection
    time.sleep(settle)

    body = client.get_menu_screen()
    if body is None:
        raise Failure("task menu did not open")
    rows = menu_screen_text(body)

    printer_row = None
    for index in range(8, SCREEN_HEIGHT):
        if "Printer" in rows[index]:
            printer_row = index
            break
    if printer_row is None:
        raise Failure("'Printer' task category not found in task menu")

    downs = printer_row - 8
    for _ in range(downs):
        client.tap_key("cursor_up_down")
        time.sleep(0.2)

    client.tap_key("return")  # expand Printer category (Flush/Eject preselected)
    time.sleep(settle)

    body = client.get_menu_screen()
    if body is None:
        raise Failure("printer task submenu did not open")
    rows = menu_screen_text(body)
    assert_or_warn(
        assertions_enabled,
        "Flush/Eject" in rows[printer_row],
        f"expected 'Flush/Eject' on row {printer_row}, got: {rows[printer_row]!r}",
    )

    client.tap_key("return")  # trigger Flush/Eject
    time.sleep(1.0)


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
    print("\nRestoring original Printer Settings")
    for item, value in snapshot.items():
        try:
            client.set_config(CONFIG_CATEGORY, item, value)
            print(f"  {item}: {value}")
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


PNG_SIGNATURE = b"\x89PNG\r\n\x1a\n"


def decode_png_dimensions(data):
    if data[:8] != PNG_SIGNATURE:
        return None
    if data[12:16] != b"IHDR":
        return None
    width, height = struct.unpack(">II", data[16:24])
    return width, height


def png_is_well_formed(data):
    """Validate PNG chunk structure and CRCs without needing a real decoder."""
    if data[:8] != PNG_SIGNATURE:
        return False, "missing PNG signature"
    offset = 8
    saw_ihdr = False
    saw_iend = False
    while offset + 8 <= len(data):
        length = struct.unpack(">I", data[offset:offset + 4])[0]
        chunk_type = data[offset + 4:offset + 8]
        chunk_data = data[offset + 8:offset + 8 + length]
        crc_stored = struct.unpack(">I", data[offset + 8 + length:offset + 12 + length])[0]
        crc_calc = zlib.crc32(chunk_type + chunk_data) & 0xFFFFFFFF
        if crc_calc != crc_stored:
            return False, f"bad CRC in chunk {chunk_type!r}"
        if chunk_type == b"IHDR":
            saw_ihdr = True
        if chunk_type == b"IEND":
            saw_iend = True
            break
        offset += 12 + length
    if not saw_ihdr:
        return False, "no IHDR chunk"
    if not saw_iend:
        return False, "no IEND chunk (truncated file)"
    return True, "ok"


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


def verify_png_output(inspector, output_base, expected_pages, assertions_enabled):
    directory, base = os.path.split(output_base)
    directory = directory or "/"
    for page in range(1, expected_pages + 1):
        name = f"{base}-{page:03d}.png"
        path = f"{directory}/{name}" if directory != "/" else f"/{name}"
        data = download_with_retry(inspector, path)
        assert_or_warn(assertions_enabled, len(data) > 0, f"{path}: file is empty")
        ok, reason = png_is_well_formed(data)
        assert_or_warn(assertions_enabled, ok, f"{path}: not a well-formed PNG ({reason})")
        dims = decode_png_dimensions(data)
        assert_or_warn(assertions_enabled, dims is not None, f"{path}: could not read IHDR dimensions")
        print(f"    {path}: {len(data)} bytes, {dims[0]}x{dims[1]}px, {'valid' if ok else 'INVALID'} PNG")


def verify_full_page_coverage(inspector, output_base, assertions_enabled):
    """Decode page 1's pixels and check the ink actually covers most of the
    page - proof this is a genuine full-page fill, not just a small band
    mislabeled as one (e.g. from a parameter that silently had no effect)."""
    directory, base = os.path.split(output_base)
    directory = directory or "/"
    name = f"{base}-001.png"
    path = f"{directory}/{name}" if directory != "/" else f"/{name}"

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

    print(
        f"    {path}: ink spans {bbox_width}x{bbox_height}px at ({min_x},{min_y}) "
        f"= {width_fraction:.0%} of page width, {height_fraction:.0%} of page height, "
        f"{ink_fraction:.0%} ink density within that area ({ink_count} ink pixels)"
    )
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


def verify_text_ocr(inspector, output_base, emulation, pages, rows, assertions_enabled):
    """OCR-verify that the printed text pages actually contain the expected
    deterministic content (emulation tag, mode tag, and every PAGE=/ROW=
    marker) - not just that a plausible-looking PNG was produced.

    The dot-matrix "draft" font is not reliably OCRable even after image
    preprocessing (tested: garbage output). Text-mode rows therefore switch
    the printer into NLQ + bold + double-strike (see printer-e2e.asm), which
    OCRs correctly for the parts that matter: the emulation/mode tags and the
    PAGE=/ROW= counters that prove the assembly program's page/row loop and
    decimal-formatting routine are actually working. The fixed decorative
    A-Z/0-9 suffix on each row is intentionally not required to match
    character-for-character - several glyphs (e.g. Q, X, Z) are genuinely
    ambiguous at this resolution/font even to a human, and matching it isn't
    necessary to prove the printed content is correct.
    """
    if not OCR_AVAILABLE:
        print("    OCR verification skipped: pytesseract/PIL not installed "
              "(pip install pytesseract, apt install tesseract-ocr)")
        return

    expected_tag = EMU_CONFIG_VALUE[emulation].split()[0].upper()  # "EPSON" / "COMMODORE"
    directory, base = os.path.split(output_base)
    directory = directory or "/"

    for page in range(1, pages + 1):
        name = f"{base}-{page:03d}.png"
        path = f"{directory}/{name}" if directory != "/" else f"/{name}"
        data = download_with_retry(inspector, path)
        text = ocr_page_text(data)

        assert_or_warn(assertions_enabled, text is not None, f"{path}: OCR found no ink to read")
        if text is None:
            continue

        preview = " / ".join(line.strip() for line in text.splitlines() if line.strip())
        print(f"    {path}: OCR read: {preview[:160]}{'...' if len(preview) > 160 else ''}")

        assert_or_warn(
            assertions_enabled, expected_tag in text,
            f"{path}: OCR did not find the expected '{expected_tag}' emulation tag",
        )
        assert_or_warn(
            assertions_enabled, "TEXT" in text,
            f"{path}: OCR did not find the expected 'TEXT' mode tag",
        )
        assert_or_warn(
            assertions_enabled, f"PAGE={page:03d}" in text,
            f"{path}: OCR did not find 'PAGE={page:03d}'",
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


PRESETS = {
    "crash-epson-png": dict(emulation="epson", mode="bitmap", output_type="PNG B&W",
                             ink_density="Medium", page_top_margin=1, page_height=66,
                             bus_id=4, rows=4, pages=1),
    "full-matrix": dict(output_type="PNG B&W", page_top_margin=1, page_height=66, bus_id=4),
}


def parse_args():
    parser = argparse.ArgumentParser(
        description="Virtual-printer end-to-end harness for a real Ultimate 64/64e.",
        epilog="Captures original Printer Settings and restores them on exit unless "
               "--no-config-change is used. Requires printer-e2e.asm alongside this "
               "script and a 64tass binary on PATH (or --assembler).",
    )
    parser.add_argument("-H", "--host", default="u64", help="IP or hostname of the U64")
    parser.add_argument("-p", "--password", default="", help="U64 REST password")
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
    parser.add_argument("--mode", choices=["bitmap", "text", "both"], default="bitmap")
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
    parser.add_argument("--output-type", default="PNG B&W")
    parser.add_argument("--ink-density", default="Medium")
    parser.add_argument("--page-top-margin", type=int, default=1)
    parser.add_argument("--page-height", type=int, default=66)
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
    parser.add_argument("--asm-path", default=DEFAULT_ASM_PATH)
    parser.add_argument("--assembler", default="64tass")
    args = parser.parse_args()

    if args.preset:
        for key, value in PRESETS[args.preset].items():
            setattr(args, key.replace("-", "_"), value)

    if args.rows < 1 or args.rows > 255:
        parser.error("--rows must be between 1 and 255")
    if args.pages < 1 or args.pages > 9:
        parser.error("--pages must be between 1 and 9")

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

    base = args.output_base or "/Usb0/printer/e2e"
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

    if not args.no_config_change:
        apply_settings(
            client, output_base, args.output_type, args.ink_density,
            args.page_top_margin, args.page_height, emulation, args.bus_id,
            assertions_enabled,
        )

    if args.full_page_bitmap:
        rows, bim_repeats = full_page_bitmap_params(emulation, args.page_height)
        print(f"    full-page bitmap: {rows} rows x {bim_repeats * 16} bytes/row "
              f"(~{rows * (EPSON_BIM_INTERLINE_PX if emulation == 'epson' else CBM_BIM_INTERLINE_PX)}px "
              f"tall, ~{bim_repeats * 16 * (1 if emulation == 'epson' else 4)}px wide)")
    else:
        rows, bim_repeats = args.rows, DEFAULT_BIM_REPEATS

    classification, status = classify_and_run(
        client, prg_bytes, emulation, mode, rows, args.pages, args.bus_id,
        args.timeout_seconds, POLL_INTERVAL_SECONDS, bim_repeats=bim_repeats,
    )

    if classification == "FAIL_CRASH_HARD":
        check_fail("device became unresponsive (FAIL_CRASH_HARD)")
        print("    REST and ping are both unreachable. This matches the reported crash:")
        print("    screen off, unresponsive, C64/machine reset will not help.")
        print("    Recover with: bash tooling/build_and_deploy_u64.sh (JTAG redeploy)")
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
            verify_png_output(inspector, output_base, args.pages, assertions_enabled)
            verify_full_page_coverage(inspector, output_base, assertions_enabled)
            check_ok()
            return "PASS", output_base, status
        except Failure as exc:
            check_fail(str(exc))
            return "FAIL_VERIFICATION", output_base, status

    if args.verify_output:
        check_start(f"print {label}: verify output over FTP")
        try:
            verify_png_output(inspector, output_base, args.pages, assertions_enabled)
            if mode == "text":
                verify_text_ocr(inspector, output_base, emulation, args.pages, rows, assertions_enabled)
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

    check_start(f"assemble {os.path.basename(args.asm_path)} with {args.assembler}")
    try:
        prg_bytes = assemble_prg(args.asm_path, args.assembler)
    except Failure as exc:
        check_fail(str(exc))
        return 1
    check_ok(f"{len(prg_bytes)} bytes")

    snapshot = {}
    if not args.no_config_change:
        check_start("capture original Printer Settings")
        snapshot = capture_settings(client, assertions_enabled)
        check_ok()

    if args.reset_before_run:
        check_start("reset before run")
        client.require_ok("PUT", "/v1/machine:reset", description="machine:reset")
        time.sleep(2.0)
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

    print("\nSummary")
    for emulation, mode, classification, output_base in results:
        print(f"  {emulation:10s} {mode:6s} {classification:20s} {output_base}")

    failed = [r for r in results if r[2] not in ("PASS", "PASS_NO_VERIFY")]
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
