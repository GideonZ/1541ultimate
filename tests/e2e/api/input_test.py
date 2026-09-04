#!/usr/bin/env python3
# E2E: Verifies REST keyboard and joystick injection in the C64 and menu UI.

import argparse
import contextlib
import http.client
import json
import os
import re
import sys
import time
import urllib.error
import urllib.parse
import urllib.request
from collections import Counter
from pathlib import Path
from typing import Any

from PIL import Image

# tests/lib holds the shared library; importing bootstrap adds tests/e2e/lib.
# The search walks up rather than counting directories, so this is the same in
# every entry point and a suite that moves needs no edit. See tests/lib/bootstrap.py.
sys.path.insert(0, str(next(p for p in Path(__file__).resolve().parents
                            if (p / "tests" / "lib").is_dir()) / "tests" / "lib"))
import bootstrap  # noqa: E402,F401
import menu as menu_lib  # noqa: E402
import cli  # noqa: E402
import ftp as ftp_lib
import machine as machine_lib
import pacing
import wait
import rest as rest_lib
import targets
from api import UltimateApi
from report import (Failure, teardown_step, check, check_count, detail, format_exception,
                    suite_fail, suite_ok, warn)
from vic_video import MULTICAST_GROUP, VIDEO_PORT, VicStreamCapture

TEST_CHOICES = (
    "all",
    "contract",
    "joystick",
    "keyboard",
    "keyboard-echo-alphabet",
    "keyboard-echo-ab-20hz",
    "keyboard-echo-ab-5hz",
    "menu",
    "menu-shift",
    "menu-repeat-printable",
    "menu-repeat-cursor",
)
# Bounded retry for idempotent reads whose transport failed; see request().
TRANSPORT_RETRIES = 3
TRANSPORT_RETRY_PAUSE_SECONDS = 0.5
READY_SCREEN_CODES = bytes((0x12, 0x05, 0x01, 0x04, 0x19, 0x2E))
LETTER_SCREEN_CODES = {chr(ord("A") + index): index + 1 for index in range(26)}
KEYBOARD_ECHO_PROGRAM_ADDRESS = 0xC000
KEYBOARD_ECHO_SENTINEL = "x"
KEYBOARD_ECHO_PROGRAM = bytes(
    (
        0xA9, 0x93,             # LDA #$93; clear screen through CHROUT
        0x20, 0xD2, 0xFF,
        0xA9, 0x0E,             # LDA #$0E; lower/upper-case character set
        0x20, 0xD2, 0xFF,
        0x20, 0xE4, 0xFF,       # GETIN
        0xF0, 0xFB,             # retry while no key is available
        0x8D, 0x00, 0x04,       # STA $0400; self-modified after each key
        0xEE, 0x10, 0xC0,       # INC low byte of STA target
        0xD0, 0xF3,
        0xEE, 0x11, 0xC0,       # INC high byte of STA target on wrap
        0x4C, 0x0A, 0xC0,       # JMP loop
    )
)
MENU_KEY_SETTLE_SECONDS = float(os.environ.get("U64_INPUT_MENU_KEY_SETTLE", "0.30"))
MENU_POST_RELEASE_SETTLE_SECONDS = float(os.environ.get("U64_INPUT_MENU_POST_RELEASE_SETTLE", "0.35"))
# Bounds on the menu waits below, not expected durations: each of those waits
# returns as soon as the menu screen shows the state it asked for, and these
# only decide when to give up and report what the screen held instead.
#
# MENU_STATE_TIMEOUT covers one menu transition - the menu opening, F2 drawing
# the settings list, a settings list opening, an editor window opening or
# closing. MENU_STEP_TIMEOUT covers one cursor key moving the highlight by one
# entry, and is also what a highlight that has stopped at the end of a list
# costs before the walk turns around, so it is deliberately short.
MENU_STATE_TIMEOUT_SECONDS = float(os.environ.get("U64_INPUT_MENU_STATE_TIMEOUT", "10.0"))
# MENU_SELECT_TIMEOUT bounds a whole walk to a named entry.
MENU_SELECT_TIMEOUT_SECONDS = float(os.environ.get("U64_INPUT_MENU_SELECT_TIMEOUT", "45.0"))
MENU_STEP_TIMEOUT_SECONDS = float(os.environ.get("U64_INPUT_MENU_STEP_TIMEOUT", "1.5"))
MENU_EXIT_SETTLE_SECONDS = float(os.environ.get("U64_INPUT_MENU_EXIT_SETTLE", "0.25"))
MENU_TYPE_SETTLE_SECONDS = float(os.environ.get("U64_INPUT_MENU_TYPE_SETTLE", "0.25"))
# How long a shift pressed in its own request is given to reach the machine
# before the next request's letter. See the shift scenario for why this one
# cannot be replaced by waiting for the result.
MENU_SHIFT_BATCH_SETTLE_SECONDS = float(
    os.environ.get("U64_INPUT_MENU_SHIFT_BATCH_SETTLE", "0.30"))
KEYBOARD_RATE_BATCH_SIZE = 8
MENU_VIDEO_TIMEOUT_SECONDS = float(os.environ.get("U64_INPUT_MENU_VIDEO_TIMEOUT", "6.0"))
RESET_APPLY_SECONDS = float(os.environ.get("U64_RESET_APPLY_SECONDS", "3.0"))
MENU_EVIDENCE_DIR = os.environ.get("U64_INPUT_MENU_EVIDENCE_DIR")
# machine:menu_screen serves 25 rows of 40 screen codes, followed by the same
# number of colour bytes. The firmware draws the highlighted entry in colour 1,
# so one request reads both the text and which entry is selected. The window
# frame is drawn in colour 1 too, which is why the selected entry is recognised
# by the colour of its first character column rather than by the whole row.
MENU_SCREEN_BYTES = 2000
MENU_SCREEN_COLS = 40
MENU_SCREEN_ROWS = 25
MENU_SELECTED_COLOUR = 0x01
# The rows a settings list occupies: row 0 is the version banner, rows 1 and 2
# and row 23 are the window frame, and row 24 is the status line.
# The file browser puts its first entry a row above where the settings list
# and the launcher put theirs, so the scan starts at the higher of the two.
MENU_LIST_FIRST_ROW = 2
MENU_LIST_LAST_ROW = 22
FONT_PATH = Path(__file__).resolve().parents[3] / "roms" / "chars.bin"
FONT_BYTES = FONT_PATH.read_bytes()[: 256 * 8]
PRINTABLE_FALLBACK = {
    0x00: " ",
}
KEYBOARD_MATRIX: dict[str, tuple[int, int]] = {
    "inst_del": (0, 0),
    "return": (0, 1),
    "cursor_left_right": (0, 2),
    "f7": (0, 3),
    "f1": (0, 4),
    "f3": (0, 5),
    "f5": (0, 6),
    "cursor_up_down": (0, 7),
    "3": (1, 0),
    "w": (1, 1),
    "a": (1, 2),
    "4": (1, 3),
    "z": (1, 4),
    "s": (1, 5),
    "e": (1, 6),
    "left_shift": (1, 7),
    "5": (2, 0),
    "r": (2, 1),
    "d": (2, 2),
    "6": (2, 3),
    "c": (2, 4),
    "f": (2, 5),
    "t": (2, 6),
    "x": (2, 7),
    "7": (3, 0),
    "y": (3, 1),
    "g": (3, 2),
    "8": (3, 3),
    "b": (3, 4),
    "h": (3, 5),
    "u": (3, 6),
    "v": (3, 7),
    "9": (4, 0),
    "i": (4, 1),
    "j": (4, 2),
    "0": (4, 3),
    "m": (4, 4),
    "k": (4, 5),
    "o": (4, 6),
    "n": (4, 7),
    "plus": (5, 0),
    "p": (5, 1),
    "l": (5, 2),
    "minus": (5, 3),
    "period": (5, 4),
    "colon": (5, 5),
    "at": (5, 6),
    "comma": (5, 7),
    "pound": (6, 0),
    "star": (6, 1),
    "semicolon": (6, 2),
    "clr_home": (6, 3),
    "right_shift": (6, 4),
    "equals": (6, 5),
    "arrow_up": (6, 6),
    "slash": (6, 7),
    "1": (7, 0),
    "arrow_left": (7, 1),
    "ctrl": (7, 2),
    "2": (7, 3),
    "space": (7, 4),
    "commodore": (7, 5),
    "q": (7, 6),
    "run_stop": (7, 7),
}


def wants_test(selected: list[str] | None, name: str) -> bool:
    return selected is None or "all" in selected or name in selected


def wants_menu_tests(selected: list[str] | None) -> bool:
    return wants_test(selected, "menu") or any(item.startswith("menu-") for item in selected or [])


def wants_keyboard_echo_tests(selected: list[str] | None) -> bool:
    return any(item.startswith("keyboard-echo-") for item in selected or [])


class RestInputSession:
    def __init__(self, host: str, password: str | None, timeout: float) -> None:
        self.target = targets.parse(host)
        self.host = self.target.device
        self.password = password
        self.timeout = timeout
        # For the calls this suite makes no assertion about, so that the menu
        # teardown has one implementation across the tree.
        self.api = UltimateApi(host, password, timeout)

    @property
    def machine(self) -> machine_lib.Machine:
        """Which machine this is, asked once of the device.

        The three answer the menu differently -- a C64 Ultimate puts a
        launcher above the file browser -- so a suite that has to allow for
        that asks here rather than being told on the command line.
        """
        info = self.api.info()
        return machine_lib.identify(
            self.host, lambda: (info.product, info.firmware_version))

    def url(self, path: str, params: dict[str, Any] | None = None) -> str:
        query = ""
        if params:
            query = "?" + urllib.parse.urlencode(params)
        # Keyboard injection belongs to the C64-side computer on a cartridge
        # target; see tests/lib/targets.py.
        return f"http://{self.target.host_for(path)}{path}{query}"

    def request(
        self,
        method: str,
        path: str,
        params: dict[str, Any] | None = None,
        body: bytes | None = None,
        content_type: str | None = "application/json",
    ) -> bytes:
        headers = {}
        if self.password:
            headers["X-Password"] = self.password
        if body is not None and content_type is not None:
            headers["Content-Type"] = content_type
        request = urllib.request.Request(self.url(path, params), data=body, headers=headers, method=method)
        # Transport and retry policy come from tests/lib/rest.py; see
        # rest.may_retry, which this suite's own policy became.
        #
        # Retrying a POST cannot hide a double application here: every check
        # asserts the exact resulting keyboard or joystick state, so a
        # duplicated keystroke fails that assertion rather than passing.
        try:
            with rest_lib.retrying_urlopen(request, self.timeout) as response:
                return response.read()
        except urllib.error.HTTPError:
            raise
        except (OSError, TimeoutError, urllib.error.URLError) as exc:
            raise Failure(f"{method} {path} failed: {format_exception(exc)}") from exc

    def json_request(
        self,
        method: str,
        path: str,
        params: dict[str, Any] | None = None,
        payload: dict[str, Any] | None = None,
    ) -> dict[str, Any]:
        body = None if payload is None else json.dumps(payload).encode("utf-8")
        data = self.request(method, path, params=params, body=body)
        return json.loads(data.decode("utf-8"))

    def get_state(self) -> dict[str, Any]:
        return self.json_request("GET", "/v1/machine:input")

    def post_events(self, events: list[dict[str, Any]]) -> dict[str, Any]:
        return self.json_request("POST", "/v1/machine:input", payload={"events": events})

    def post_payload_expect_error(self, payload: dict[str, Any]) -> dict[str, Any]:
        try:
            self.json_request("POST", "/v1/machine:input", payload=payload)
        except urllib.error.HTTPError as exc:
            if exc.code != 400:
                raise
            return json.loads(exc.read().decode("utf-8"))
        raise Failure("Expected HTTP 400, but request succeeded")

    def post_events_expect_error(self, events: list[dict[str, Any]]) -> dict[str, Any]:
        return self.post_payload_expect_error({"events": events})

    def post_raw_expect_error(self, body: bytes, content_type: str | None) -> dict[str, Any]:
        try:
            self.request("POST", "/v1/machine:input", body=body, content_type=content_type)
        except urllib.error.HTTPError as exc:
            if exc.code != 400:
                raise
            return json.loads(exc.read().decode("utf-8"))
        raise Failure("Expected HTTP 400, but request succeeded")

    def post_without_body_expect_error(self, expected_code: int = 412) -> dict[str, Any]:
        headers: dict[str, str] = {"Content-Type": "application/json"}
        if self.password:
            headers["X-Password"] = self.password
        connection = None
        try:
            status, _headers, body = rest_lib.retrying_http_request(
                self.target.input_host, "POST", "/v1/machine:input", body=b"", headers=headers,
                timeout=self.timeout)
            if status != expected_code:
                raise Failure(f"Expected HTTP {expected_code}, got HTTP {status}")
            return json.loads(body.decode("utf-8"))
        except http.client.HTTPException as exc:
            raise Failure(f"HTTP client failure: {exc}") from exc
        finally:
            if connection is not None:
                teardown_step("close the HTTP connection", connection.close)

    def put(self, command: str) -> None:
        self.request("PUT", f"/v1/machine:{command}")

    def start_video_stream(self, ip: str = MULTICAST_GROUP, port: int = VIDEO_PORT) -> None:
        self.request("PUT", "/v1/streams/video:start", params={"ip": f"{ip}:{port}"})

    def stop_video_stream(self) -> None:
        self.request("PUT", "/v1/streams/video:stop")

    def reset(self) -> None:
        self.put("reset")

    def menu_screen(self) -> bytes | None:
        try:
            return self.request("GET", "/v1/machine:menu_screen")
        except urllib.error.HTTPError as exc:
            if exc.code == 404:
                return None
            raise

    def menu_screen_open(self) -> bool:
        return self.menu_screen() is not None

    def close_menu_from_anywhere(self) -> None:
        self.api.machine.close_menu_from_anywhere()

    def pause(self) -> None:
        self.put("pause")

    def resume(self) -> None:
        self.put("resume")

    def read_memory(self, address: int, length: int) -> bytes:
        return self.request("GET", "/v1/machine:readmem", params={"address": f"{address:04X}", "length": length})

    def write_memory(self, address: int, data: bytes) -> None:
        if not data:
            raise Failure("write_memory requires at least one byte")
        self.request("PUT", "/v1/machine:writemem", params={"address": f"{address:04X}", "data": data.hex().upper()})


class FrameText:
    def __init__(self, image: Image.Image, lines: list[str], codes: list[list[int]], bbox: tuple[int, int, int, int]) -> None:
        self.image = image
        self.lines = lines
        self.codes = codes
        self.bbox = bbox

    def text(self) -> str:
        return "\n".join(self.lines)

    def contains(self, needle: str) -> bool:
        needle_upper = needle.upper()
        return any(needle_upper in line for line in self.lines)


class C64FrameOCR:
    def __init__(self) -> None:
        self.font = FONT_BYTES

    def _cell_mask(self, cell: Image.Image) -> list[int]:
        bg = Counter(cell.getdata()).most_common(1)[0][0]
        mask = []
        for y in range(8):
            row = 0
            for x in range(8):
                if cell.getpixel((x, y)) != bg:
                    row |= (1 << (7 - x))
            mask.append(row)
        return mask

    def _best_match(self, mask: list[int]) -> tuple[int, int]:
        best_dist = 999
        best_code = 0
        for code in range(256):
            glyph = self.font[code * 8:(code + 1) * 8]
            if len(glyph) < 8:
                break
            dist = 0
            for y in range(8):
                dist += (mask[y] ^ glyph[y]).bit_count()
            if dist < best_dist:
                best_dist = dist
                best_code = code
                if dist == 0:
                    break
        return best_dist, best_code

    def _alignment_score(self, image: Image.Image, left: int, top: int) -> int:
        active = image.crop((left, top, left + 320, top + 200))
        score = 0
        for row in range(6):
            for col in range(40):
                cell = active.crop((col * 8, row * 8, (col + 1) * 8, (row + 1) * 8))
                score += self._best_match(self._cell_mask(cell))[0]
        return score

    def _active_bbox(self, image: Image.Image) -> tuple[int, int, int, int]:
        border = image.getpixel((0, 0))
        min_x, min_y = image.width, image.height
        max_x, max_y = -1, -1
        for y in range(image.height):
            for x in range(image.width):
                if image.getpixel((x, y)) != border:
                    min_x = min(min_x, x)
                    min_y = min(min_y, y)
                    max_x = max(max_x, x)
                    max_y = max(max_y, y)
        if max_x < min_x or max_y < min_y:
            raise Failure("Could not find active 320x200 area inside VIC frame.")

        left_candidates = range(max(0, min_x - 16), min(min_x + 16, image.width - 320) + 1)
        top_candidates = range(max(0, min_y - 4), min(min_y + 4, image.height - 200) + 1)
        best = None
        for top in top_candidates:
            for left in left_candidates:
                score = self._alignment_score(image, left, top)
                if best is None or score < best[0]:
                    best = (score, left, top)
        if best is None:
            raise Failure("Could not align 320x200 OCR grid.")
        _, left, top = best
        return left, top, left + 320, top + 200

    @staticmethod
    def _screen_code_to_char(code: int) -> str:
        if code in PRINTABLE_FALLBACK:
            return PRINTABLE_FALLBACK[code]
        if 2 <= code <= 27:
            return chr(ord("A") + code - 2)
        if code == 0x1B:
            return "["
        if code == 0x1C:
            return "\\"
        if code == 0x1D:
            return "]"
        if code == 0x1E:
            return "^"
        if code == 0x1F:
            return "_"
        if code == 0x20:
            return " "
        if 0x21 <= code <= 0x3F:
            return chr(code)
        if 0x41 <= code <= 0x5B:
            return chr(ord("A") + code - 0x41)
        return " "

    def decode(self, image: Image.Image) -> FrameText:
        left, top, right, bottom = self._active_bbox(image)
        active = image.crop((left, top, right, bottom))
        lines: list[str] = []
        codes: list[list[int]] = []
        for row in range(25):
            chars: list[str] = []
            row_codes: list[int] = []
            for col in range(40):
                cell = active.crop((col * 8, row * 8, (col + 1) * 8, (row + 1) * 8))
                _, code = self._best_match(self._cell_mask(cell))
                row_codes.append(code)
                chars.append(self._screen_code_to_char(code))
            lines.append("".join(chars))
            codes.append(row_codes)
        return FrameText(image, lines, codes, (left, top, right, bottom))


def wait_for_input_ready(session: RestInputSession, timeout: float) -> None:
    deadline = time.time() + timeout
    last_error: BaseException | None = None
    while time.time() < deadline:
        try:
            session.get_state()
            return
        except (OSError, TimeoutError, urllib.error.URLError) as exc:
            last_error = exc
            time.sleep(0.5)
    if last_error is not None:
        raise last_error
    raise TimeoutError(f"Timed out waiting for /v1/machine:input on {session.host}")


def wait_for_basic_ready(session: RestInputSession) -> None:
    deadline = time.time() + 6.0
    while time.time() < deadline:
        screen = session.read_memory(0x0400, 256)
        if READY_SCREEN_CODES in screen:
            return
        time.sleep(0.25)
    raise Failure("BASIC READY prompt not visible; device may be running a cartridge")


def reset_to_basic(session: RestInputSession) -> None:
    session.close_menu_from_anywhere()
    session.reset()
    # This suite's reset is left alone deliberately. Switching it to the shared
    # polling reset made wait_for_basic_ready below stop finding the prompt,
    # and the cause was not established; the shared reset is proven in the
    # other suites, so this one keeps its own path until that is understood.
    time.sleep(RESET_APPLY_SECONDS)
    wait_for_basic_ready(session)
    session.post_events([{"kind": "release_all"}])


def try_clear_basic_screen(session: RestInputSession) -> bool:
    session.post_events([{"kind": "release_all"}])
    time.sleep(0.35)
    for _ in range(2):
        session.post_events([{"kind": "keyboard", "inputs": ["left_shift", "clr_home"], "transition": "press"}])
        time.sleep(0.12)
        session.post_events([{"kind": "keyboard", "inputs": ["left_shift", "clr_home"], "transition": "release"}])
        deadline = time.monotonic() + 1.5
        while time.monotonic() < deadline:
            row0 = session.read_memory(0x0400, 8)
            if row0[0] in (0x20, 0xA0) and all(byte == 0x20 for byte in row0[1:]):
                return True
            time.sleep(0.05)
    return False


def find_cursor_row(session: RestInputSession) -> int | None:
    screen = session.read_memory(0x0400, 1000)
    for index, value in enumerate(screen):
        if value == 0xA0:
            return index // 40
    return None


def last_non_empty_screen_row(session: RestInputSession) -> int:
    screen = session.read_memory(0x0400, 1000)
    last = 0
    for row in range(25):
        line = screen[row * 40:(row + 1) * 40]
        if any(byte not in (0x20, 0xA0) for byte in line):
            last = row
    return last


def prepare_basic_entry_row(session: RestInputSession) -> int:
    if try_clear_basic_screen(session):
        return 0
    target_row = min(last_non_empty_screen_row(session) + 1, 24)
    session.post_events([{"kind": "keyboard", "inputs": ["return"], "transition": "tap"}])
    time.sleep(0.4)
    return target_row


def assert_joystick_ports(session: RestInputSession, port1: int, port2: int) -> None:
    actual_port_a, actual_port_b = read_joystick_cia(session)
    actual_port1 = actual_port_b & 0x1F
    actual_port2 = actual_port_a & 0x1F
    if actual_port1 != port1 or actual_port2 != port2:
        raise Failure(
            f"Expected joy1=${port1:02X} joy2=${port2:02X}, "
            f"got joy1=${actual_port1:02X} joy2=${actual_port2:02X}"
        )


EMPTY_JOYSTICKS = [{"port": 1, "inputs": []}, {"port": 2, "inputs": []}]

# A tap batch is accepted at once and drains through the C64's keyboard matrix
# afterwards, so the state is not empty the instant the request answers. It is
# not steadily non-empty either: each tap presses and releases, so between two
# taps the state is momentarily empty. A poll for "empty" therefore returns
# while the batch is still draining - measured here, it answered after 0.45s of
# a train that takes about six seconds, and the next check then read the tap
# that arrived after it. The condition is that the state has been empty for
# longer than the gap between taps.
BATCH_DRAIN_TIMEOUT_SECONDS = 12.0
BATCH_DRAIN_QUIET_SECONDS = 0.75


def state_is_empty(session: RestInputSession) -> bool:
    state = session.get_state()
    return (state.get("keyboard", {}).get("inputs") == []
            and state.get("joysticks") == EMPTY_JOYSTICKS)


def assert_state_empty(session: RestInputSession) -> None:
    state = session.get_state()
    if state.get("keyboard", {}).get("inputs") != []:
        raise Failure(f"Expected empty keyboard state, got {state}")
    if state.get("joysticks") != EMPTY_JOYSTICKS:
        raise Failure(f"Expected empty joystick state, got {state}")


def wait_state_empty(session: RestInputSession, what: str) -> None:
    """Wait for an injected batch to finish draining, then require it empty.

    "Empty" has to hold for BATCH_DRAIN_QUIET_SECONDS, not merely be true once:
    see the constant. The two sleeps this replaced were a flat 1.2s for ten
    taps and 6.0s for sixty, paid in full on every run whatever the device did.
    """
    def drained() -> bool:
        # Paced, because every state_is_empty() is a REST GET and the device
        # serves four connection slots: an unpaced loop would issue a couple of
        # hundred requests per quiet window and starve the rest of the run.
        quiet_until = time.monotonic() + BATCH_DRAIN_QUIET_SECONDS
        while time.monotonic() < quiet_until:
            if not state_is_empty(session):
                return False
            time.sleep(pacing.POLL_INTERVAL_SECONDS)
        return True

    wait.wait_until(drained, what, timeout=BATCH_DRAIN_TIMEOUT_SECONDS)
    assert_state_empty(session)


def assert_error_body_only(body: dict[str, Any]) -> None:
    if not body.get("errors"):
        raise Failure(f"Expected validation errors, got {body}")
    if "keyboard" in body or "joysticks" in body:
        raise Failure(f"Error response must not include state snapshots, got {body}")


def assert_input_state(
    session: RestInputSession,
    keyboard: list[str],
    joystick1: list[str],
    joystick2: list[str],
) -> None:
    state = session.get_state()
    if state.get("errors") != []:
        raise Failure(f"Expected no errors in state snapshot, got {state}")
    if state.get("keyboard", {}).get("inputs") != keyboard:
        raise Failure(f"Keyboard state mismatch; expected {keyboard}, got {state}")
    if state.get("joysticks") != [{"port": 1, "inputs": joystick1}, {"port": 2, "inputs": joystick2}]:
        raise Failure(f"Joystick state mismatch; expected {joystick1}/{joystick2}, got {state}")


def read_joystick_cia(session: RestInputSession) -> tuple[int, int]:
    session.pause()
    try:
        session.write_memory(0xDC02, b"\x00")
        session.write_memory(0xDC03, b"\x00")
        regs = session.read_memory(0xDC00, 2)
        port_a = regs[0]
        port_b = regs[1]
    finally:
        session.resume()
    return port_a, port_b


def read_keyboard_row(session: RestInputSession, row: int) -> int:
    session.pause()
    try:
        session.write_memory(0xDC02, b"\xFF")
        session.write_memory(0xDC03, b"\x00")
        session.write_memory(0xDC00, bytes([(~(1 << row)) & 0xFF]))
        regs = session.read_memory(0xDC00, 2)
        return regs[1]
    finally:
        session.resume()


def read_joystick_pots(session: RestInputSession, port: int) -> tuple[int, int]:
    if port not in (1, 2):
        raise Failure(f"Invalid joystick port for POT read: {port}")
    # Mirror Anykey's VIC-II probe: select the joystick port on CIA1 first,
    # leave the machine running briefly, then read SID POTX/POTY.
    session.write_memory(0xDC02, b"\xC0")
    session.write_memory(0xDC00, b"\x40" if port == 1 else b"\x80")
    time.sleep(0.10)
    regs = session.read_memory(0xD419, 2)
    return regs[0], regs[1]


def assert_joystick_pots(session: RestInputSession, port: int, potx: int, poty: int) -> None:
    actual_potx, actual_poty = read_joystick_pots(session, port)
    if (actual_potx, actual_poty) != (potx, poty):
        raise Failure(
            f"Joystick port {port} POT mismatch; expected ${potx:02X}/${poty:02X}, "
            f"got ${actual_potx:02X}/${actual_poty:02X}"
        )


def assert_anykey_extra_buttons(session: RestInputSession, port: int, fire2: bool, fire3: bool) -> None:
    potx, poty = read_joystick_pots(session, port)
    actual_fire2 = (potx & 0x80) == 0
    actual_fire3 = (poty & 0x80) == 0
    if (actual_fire2, actual_fire3) != (fire2, fire3):
        raise Failure(
            f"Joystick port {port} Anykey extra-button mismatch; "
            f"expected fire2/fire3={fire2}/{fire3}, got {actual_fire2}/{actual_fire3} "
            f"from POTX/POTY=${potx:02X}/${poty:02X}"
        )


def assert_keyboard_matrix(session: RestInputSession, input_name: str, active: bool) -> None:
    mapping = KEYBOARD_MATRIX.get(input_name)
    if mapping is None:
        raise Failure(f"No keyboard matrix mapping for {input_name!r}")
    row, bit = mapping
    row_value = read_keyboard_row(session, row)
    pressed = (row_value & (1 << bit)) == 0
    if pressed != active:
        state = "pressed" if active else "released"
        raise Failure(
            f"Expected {input_name} to be {state} on row {row}, bit {bit}; "
            f"read ${row_value:02X}"
        )


def assert_keyboard_matrix_inputs(session: RestInputSession, inputs: list[str]) -> None:
    for input_name in inputs:
        assert_keyboard_matrix(session, input_name, True)


def text_to_screen_codes(text: str) -> bytes:
    out = bytearray()
    for ch in text.upper():
        if ch in LETTER_SCREEN_CODES:
            out.append(LETTER_SCREEN_CODES[ch])
        elif "0" <= ch <= "9":
            out.append(ord(ch))
        elif ch == " ":
            out.append(0x20)
        else:
            raise Failure(f"Unsupported screen-code text {text!r}")
    return bytes(out)


def keyboard_echo_screen_byte(ch: str) -> int:
    if "a" <= ch <= "z":
        return 0x41 + ord(ch) - ord("a")
    if "A" <= ch <= "Z":
        return 0xC1 + ord(ch) - ord("A")
    if "0" <= ch <= "9":
        return ord(ch)
    if ch == " ":
        return 0x20
    raise Failure(f"Unsupported keyboard echo text {ch!r}")


def keyboard_echo_screen_bytes(text: str) -> bytes:
    return bytes(keyboard_echo_screen_byte(ch) for ch in text)


def keyboard_echo_byte_name(value: int) -> str:
    if 0x41 <= value <= 0x5A:
        return chr(ord("a") + value - 0x41)
    if 0xC1 <= value <= 0xDA:
        return chr(ord("A") + value - 0xC1)
    if value == 0x20:
        return "space"
    return f"${value:02X}"


# How much text the keyboard echo stress cases send, and so how long they take:
# the rate is the subject, so the duration is length/rate and nothing else.
#
# mixed_alphabet_text repeats every 26 characters, so 26 is every character it
# can produce, once each. That is the floor: fewer stops exercising part of the
# keymap, which is the whole point of using the alphabet here rather than one
# repeated letter. It was 52, which sent each character twice, and 200 before
# that. Sustained delivery over a long train has its own check, "keyboard long
# repeated tap train drains fully without sticky state".
ECHO_ALPHABET_LENGTH = 26
# alternating_text produces only two distinct characters, so length buys
# repetition rather than coverage and can be cut much harder than the alphabet
# above. 20 is one second at 20 Hz, still long enough for a device that cannot
# keep up to drop one. It was 60.
ECHO_ALTERNATING_LENGTH = 20
# The slow case runs at a quarter of that rate, so it carries its own smaller
# count: 8 characters is 1.6s at 5 Hz, where 20 would be four seconds.
ECHO_SLOW_ALTERNATING_LENGTH = 8


def mixed_alphabet_text(length: int) -> str:
    return "".join(
        chr(ord("a") + (index % 26)) if (index % 2) == 0 else chr(ord("A") + (index % 26))
        for index in range(length)
    )


def alternating_text(first: str, second: str, length: int) -> str:
    return "".join(first if (index % 2) == 0 else second for index in range(length))


def wait_for_screen_sequence(session: RestInputSession, expected: bytes, timeout: float) -> float:
    started = time.monotonic()
    deadline = time.monotonic() + timeout
    while True:
        screen = session.read_memory(0x0400, 1000)
        if expected in screen:
            return time.monotonic() - started
        if time.monotonic() >= deadline:
            raise Failure(f"Expected screen sequence {expected!r} not found before timeout")
        time.sleep(0.02)


def keyboard_echo_mismatch(expected_text: str, expected: bytes, actual: bytes, offset: int) -> str:
    for index, expected_byte in enumerate(expected):
        if index >= len(actual) or actual[index] != expected_byte:
            actual_byte = actual[index] if index < len(actual) else 0x20
            context_start = max(0, index - 10)
            context_end = min(len(expected), index + 10)
            expected_context = expected_text[context_start:context_end]
            actual_context = "".join(keyboard_echo_byte_name(byte) for byte in actual[context_start:context_end])
            return (
                f"Keyboard echo mismatch at press {index + 1}, screen ${0x0400 + offset + index:04X}: "
                f"expected {expected_text[index]!r}/${expected_byte:02X}, "
                f"got {keyboard_echo_byte_name(actual_byte)!r}/${actual_byte:02X}; "
                f"expected context {expected_context!r}, actual context {actual_context!r}"
            )
    return "Keyboard echo mismatch"


def wait_for_keyboard_echo_sequence(session: RestInputSession, expected_text: str, offset: int, timeout: float) -> float:
    expected = keyboard_echo_screen_bytes(expected_text)
    started = time.monotonic()
    deadline = started + timeout
    last = b""
    while True:
        last = session.read_memory(0x0400 + offset, len(expected))
        if last == expected:
            return time.monotonic() - started
        mismatch = next((index for index, byte in enumerate(expected) if last[index] != byte), None)
        if mismatch is not None and last[mismatch] != 0x20:
            raise Failure(keyboard_echo_mismatch(expected_text, expected, last, offset))
        if time.monotonic() >= deadline:
            raise Failure(keyboard_echo_mismatch(expected_text, expected, last, offset))
        time.sleep(0.05)


def read_basic_input_line(session: RestInputSession) -> bytes:
    screen = session.read_memory(0x0400, 1000)
    ready_offset = screen.find(READY_SCREEN_CODES)
    if ready_offset < 0:
        raise Failure("BASIC READY prompt not found on screen")
    line_start = ((ready_offset // 40) + 1) * 40
    return screen[line_start:line_start + 40]


def wait_for_basic_input_prefix(session: RestInputSession, expected_text: str, timeout: float) -> float:
    expected = text_to_screen_codes(expected_text)
    started = time.monotonic()
    deadline = started + timeout
    while True:
        line = read_basic_input_line(session)
        if line[:len(expected)] == expected:
            return time.monotonic() - started
        if time.monotonic() >= deadline:
            raise Failure(f"Expected BASIC input prefix {expected_text!r}, got {line[:len(expected)]!r}")
        time.sleep(0.02)


def wait_for_screen_row_prefix(session: RestInputSession, row: int, expected_text: str, timeout: float) -> float:
    expected = text_to_screen_codes(expected_text)
    started = time.monotonic()
    deadline = started + timeout
    while True:
        line = session.read_memory(0x0400 + (row * 40), 40)
        if line[:len(expected)] == expected:
            return time.monotonic() - started
        if time.monotonic() >= deadline:
            raise Failure(f"Expected screen row {row} prefix {expected_text!r}, got {line[:len(expected)]!r}")
        time.sleep(0.02)


def keyboard_tap_events_for_text(text: str) -> list[dict[str, Any]]:
    events: list[dict[str, Any]] = []
    for ch in text:
        if "a" <= ch <= "z" or "0" <= ch <= "9":
            events.append({"kind": "keyboard", "inputs": [ch], "transition": "tap"})
        elif "A" <= ch <= "Z":
            events.append({"kind": "keyboard", "inputs": ["left_shift", ch.lower()], "transition": "tap"})
        else:
            raise Failure(f"Unsupported keyboard tap text {text!r}")
    return events


def keyboard_press_inputs_for_char(ch: str) -> list[str]:
    if "a" <= ch <= "z" or "0" <= ch <= "9":
        return [ch]
    if "A" <= ch <= "Z":
        return ["left_shift", ch.lower()]
    raise Failure(f"Unsupported keyboard press text {ch!r}")


def keyboard_release_inputs_for_char(ch: str) -> list[str]:
    if "A" <= ch <= "Z":
        return [ch.lower(), "left_shift"]
    return keyboard_press_inputs_for_char(ch)


def keyboard_press_hold_seconds(hz: float) -> float:
    return max(0.018, min(0.080, (1.0 / hz) * 0.35))


def post_keyboard_char_press_release(session: RestInputSession, ch: str, hold_seconds: float) -> None:
    session.post_events([{"kind": "keyboard", "inputs": keyboard_press_inputs_for_char(ch), "transition": "press"}])
    time.sleep(hold_seconds)
    session.post_events([{"kind": "keyboard", "inputs": keyboard_release_inputs_for_char(ch), "transition": "release"}])


def post_keyboard_text_at_rate(session: RestInputSession, text: str, hz: float) -> None:
    interval = 1.0 / hz
    next_send = time.monotonic()
    for start in range(0, len(text), KEYBOARD_RATE_BATCH_SIZE):
        chunk = text[start:start + KEYBOARD_RATE_BATCH_SIZE]
        now = time.monotonic()
        if now < next_send:
            time.sleep(next_send - now)
        # Small ordered bursts exercise the API's documented batch path and
        # are harder on its input queue than evenly spaced single events,
        # while avoiding one short-lived TCP connection per character.
        events: list[dict[str, Any]] = []
        for ch in chunk:
            events.extend(keyboard_tap_events_for_text(ch))
        session.post_events(events)
        next_send += len(chunk) * interval
        now = time.monotonic()
        if next_send < now:
            next_send = now


def start_keyboard_echo_program(session: RestInputSession) -> int:
    session.write_memory(KEYBOARD_ECHO_PROGRAM_ADDRESS, KEYBOARD_ECHO_PROGRAM)
    start_command = f"SYS{KEYBOARD_ECHO_PROGRAM_ADDRESS}\r".encode("ascii")
    session.write_memory(0x0277, start_command)
    session.write_memory(0x00C6, bytes([len(start_command)]))

    expected = keyboard_echo_screen_bytes(KEYBOARD_ECHO_SENTINEL)
    deadline = time.monotonic() + 5.0
    while True:
        session.post_events(keyboard_tap_events_for_text(KEYBOARD_ECHO_SENTINEL))
        time.sleep(0.25)
        if session.read_memory(0x0400, 1) == expected:
            session.post_events([{"kind": "release_all"}])
            time.sleep(0.35)
            return len(KEYBOARD_ECHO_SENTINEL)
        if time.monotonic() >= deadline:
            screen = session.read_memory(0x0400, 40)
            raise Failure(f"Keyboard echo program did not start; first screen row is {list(screen)}")


def prepare_keyboard_echo_program(session: RestInputSession) -> int:
    session.post_events([{"kind": "keyboard", "inputs": ["return"], "transition": "tap"}])
    time.sleep(0.5)
    wait_for_basic_ready(session)
    return start_keyboard_echo_program(session)


def run_keyboard_echo_stress_case(session: RestInputSession, text: str, hz: float, offset: int) -> int:
    try:
        post_keyboard_text_at_rate(session, text, hz)
        wait_for_keyboard_echo_sequence(session, text, offset, timeout=max(6.0, len(text) * 0.25))
        time.sleep(0.25)
        assert_state_empty(session)
        return offset + len(text)
    finally:
        teardown_step("release every held key",
                    lambda: session.post_events([{"kind": "release_all"}]))


def keyboard_tap_events_for_menu_text(text: str) -> list[dict[str, Any]]:
    punctuation = {
        "/": "slash",
        ".": "period",
        "-": "minus",
        ":": "colon",
    }
    events: list[dict[str, Any]] = []
    for ch in text:
        if "a" <= ch <= "z" or "0" <= ch <= "9":
            events.append({"kind": "keyboard", "inputs": [ch], "transition": "tap"})
        elif "A" <= ch <= "Z":
            events.append({"kind": "keyboard", "inputs": ["left_shift", ch.lower()], "transition": "tap"})
        elif ch == " ":
            events.append({"kind": "keyboard", "inputs": ["space"], "transition": "tap"})
        elif ch in punctuation:
            events.append({"kind": "keyboard", "inputs": [punctuation[ch]], "transition": "tap"})
        else:
            raise Failure(f"Unsupported menu editor text {text!r}")
    return events


def ensure_menu_evidence_dir() -> str | None:
    if not MENU_EVIDENCE_DIR:
        return None
    os.makedirs(MENU_EVIDENCE_DIR, exist_ok=True)
    return MENU_EVIDENCE_DIR


def save_menu_note(tag: str, text: str) -> None:
    directory = ensure_menu_evidence_dir()
    if directory is None:
        return
    safe_tag = re.sub(r"[^A-Za-z0-9_.-]+", "_", tag)
    with open(os.path.join(directory, f"{safe_tag}.txt"), "w", encoding="utf-8") as handle:
        handle.write(text.rstrip())
        handle.write("\n")


def save_menu_frame(tag: str, frame: FrameText) -> None:
    directory = ensure_menu_evidence_dir()
    if directory is None:
        return
    safe_tag = re.sub(r"[^A-Za-z0-9_.-]+", "_", tag)
    if frame.image is not None:
        frame.image.save(os.path.join(directory, f"{safe_tag}.png"))
    with open(os.path.join(directory, f"{safe_tag}.txt"), "w", encoding="utf-8") as handle:
        handle.write(frame.text())
        handle.write("\n")


def capture_menu_frame(capture: VicStreamCapture, ocr: C64FrameOCR, tag: str | None = None) -> FrameText:
    frame = ocr.decode(capture.capture_image())
    if tag is not None:
        save_menu_frame(tag, frame)
    return frame


def wait_for_menu_text(capture: VicStreamCapture, ocr: C64FrameOCR, needles: list[str], timeout: float, tag: str) -> FrameText:
    deadline = time.monotonic() + timeout
    last_frame: FrameText | None = None
    upper_needles = [needle.upper() for needle in needles]
    while time.monotonic() < deadline:
        frame = capture_menu_frame(capture, ocr)
        last_frame = frame
        if all(frame.contains(needle) for needle in upper_needles):
            save_menu_frame(tag, frame)
            return frame
        time.sleep(0.15)
    if last_frame is not None:
        save_menu_frame(f"{tag}_timeout", last_frame)
    raise Failure(f"Timed out waiting for menu text {needles!r}")


def non_space_run_length(frame: FrameText, row: int, start_col: int, width: int = 20) -> int:
    count = 0
    for offset in range(width):
        ch = frame.lines[row][start_col + offset]
        if ch == " ":
            break
        count += 1
    return count


def frame_cell_mask(frame: FrameText, ocr: C64FrameOCR, row: int, col: int) -> list[int]:
    left, top, _, _ = frame.bbox
    cell = frame.image.crop((left + (col * 8), top + (row * 8), left + ((col + 1) * 8), top + ((row + 1) * 8)))
    return ocr._cell_mask(cell)


def first_mask_position(frame: FrameText, ocr: C64FrameOCR, row: int, start_col: int, width: int, expected_mask: list[int]) -> int:
    for col in range(start_col, start_col + width):
        if frame_cell_mask(frame, ocr, row, col) == expected_mask:
            return col
    raise Failure("Expected glyph mask not found in decoded frame.")


def append_screen_tail(screen_tail: str, text: str, limit: int = 200) -> str:
    return (screen_tail + text.upper())[-limit:]


def soak_keyboard_basic_case(session: RestInputSession, screen_tail: str, text: str) -> str:
    session.json_request(
        "POST",
        "/v1/machine:input",
        payload={"events": keyboard_tap_events_for_text(text)},
    )
    screen_tail = append_screen_tail(screen_tail, text)
    wait_for_screen_sequence(session, text_to_screen_codes(screen_tail), timeout=max(4.0, len(text) * 0.8))
    time.sleep(0.3)
    assert_input_state(session, [], [], [])
    session.post_events([{"kind": "release_all"}])
    return screen_tail


def soak_keyboard_hold_case(session: RestInputSession, persistent_inputs: list[str], tap_inputs: list[str]) -> None:
    session.post_events([{"kind": "release_all"}])
    session.post_events(
        [
            {"kind": "keyboard", "inputs": persistent_inputs, "transition": "press"},
            {"kind": "keyboard", "inputs": tap_inputs, "transition": "tap"},
        ]
    )
    time.sleep(0.1)
    state_inputs = session.get_state()["keyboard"]["inputs"]
    for item in persistent_inputs:
        if item not in state_inputs:
            raise Failure(f"Expected persistent keyboard input {item} to remain active, got {state_inputs}")
    for item in tap_inputs:
        if item not in persistent_inputs and item in state_inputs:
            raise Failure(f"Expected tap-only keyboard input {item} to auto-release, got {state_inputs}")
    assert_keyboard_matrix_inputs(session, persistent_inputs)
    session.post_events([{"kind": "release_all"}])
    assert_state_empty(session)


def soak_joystick_case(session: RestInputSession, port: int, pressed_inputs: list[str], release_inputs: list[str]) -> None:
    session.post_events([{"kind": "release_all"}])
    session.post_events([{"kind": "joystick", "port": port, "inputs": pressed_inputs, "transition": "press"}])
    active = [item for item in pressed_inputs if item not in release_inputs]
    if release_inputs:
        session.post_events([{"kind": "joystick", "port": port, "inputs": release_inputs, "transition": "release"}])
    if port == 1:
        port1 = 0x1F
        if "up" in active:
            port1 &= ~0x01
        if "down" in active:
            port1 &= ~0x02
        if "left" in active:
            port1 &= ~0x04
        if "right" in active:
            port1 &= ~0x08
        if "fire" in active:
            port1 &= ~0x10
        assert_joystick_ports(session, port1, 0x1F)
    else:
        port2 = 0x1F
        if "up" in active:
            port2 &= ~0x01
        if "down" in active:
            port2 &= ~0x02
        if "left" in active:
            port2 &= ~0x04
        if "right" in active:
            port2 &= ~0x08
        if "fire" in active:
            port2 &= ~0x10
        assert_joystick_ports(session, 0x1F, port2)
    state = session.get_state()["joysticks"][port - 1]["inputs"]
    if state != active:
        raise Failure(f"Expected joystick port {port} inputs {active}, got {state}")
    session.post_events([{"kind": "release_all"}])
    assert_state_empty(session)


def soak_interleaved_case(
    session: RestInputSession,
    screen_tail: str,
    text: str,
    joystick_port: int,
    joystick_inputs: list[str],
) -> str:
    session.post_events([{"kind": "joystick", "port": joystick_port, "inputs": joystick_inputs, "transition": "press"}])
    session.json_request(
        "POST",
        "/v1/machine:input",
        payload={"events": keyboard_tap_events_for_text(text)},
    )
    screen_tail = append_screen_tail(screen_tail, text)
    wait_for_screen_sequence(session, text_to_screen_codes(screen_tail), timeout=max(4.0, len(text) * 0.8))
    state = session.get_state()
    active_joy = state["joysticks"][joystick_port - 1]["inputs"]
    if active_joy != joystick_inputs:
        raise Failure(f"Expected joystick port {joystick_port} to remain active during keyboard batch, got {active_joy}")
    if joystick_port == 1:
        port1 = 0x1F
        if "up" in joystick_inputs:
            port1 &= ~0x01
        if "down" in joystick_inputs:
            port1 &= ~0x02
        if "left" in joystick_inputs:
            port1 &= ~0x04
        if "right" in joystick_inputs:
            port1 &= ~0x08
        if "fire" in joystick_inputs:
            port1 &= ~0x10
        assert_joystick_ports(session, port1, 0x1F)
    session.post_events([{"kind": "release_all"}])
    assert_state_empty(session)
    return screen_tail


def soak_invalid_atomic_case(session: RestInputSession) -> None:
    session.post_events([{"kind": "release_all"}])
    session.post_events(
        [
            {"kind": "keyboard", "inputs": ["ctrl"], "transition": "press"},
            {"kind": "joystick", "port": 2, "inputs": ["fire"], "transition": "press"},
        ]
    )
    body = session.post_events_expect_error(
        [
            {"kind": "keyboard", "inputs": ["left_shift"], "transition": "press"},
            {"kind": "joystick", "port": 3, "inputs": ["up"], "transition": "press"},
        ]
    )
    assert_error_body_only(body)
    assert_input_state(session, ["ctrl"], [], ["fire"])
    session.post_events([{"kind": "release_all"}])
    assert_state_empty(session)


def soak_invalid_body_case(session: RestInputSession) -> None:
    session.post_events(
        [
            {"kind": "keyboard", "inputs": ["commodore"], "transition": "press"},
            {"kind": "joystick", "port": 1, "inputs": ["fire"], "transition": "press"},
        ]
    )
    body = session.post_without_body_expect_error()
    assert_error_body_only(body)
    assert_input_state(session, ["commodore"], ["fire"], [])

    large_event = b'{"kind":"keyboard","inputs":["a"],"transition":"tap"}'
    oversized = b'{"events":[' + b",".join([large_event] * 120) + b"]}"
    body = session.post_raw_expect_error(oversized, "application/json")
    assert_error_body_only(body)
    assert_input_state(session, ["commodore"], ["fire"], [])

    body = session.post_raw_expect_error(b'{"events":[', "application/json")
    assert_error_body_only(body)
    assert_input_state(session, ["commodore"], ["fire"], [])

    session.post_events([{"kind": "release_all"}])
    assert_state_empty(session)


def menu_keyboard_tap(session: RestInputSession, inputs: list[str], settle: float = MENU_KEY_SETTLE_SECONDS) -> dict[str, Any]:
    response = session.post_events([{"kind": "keyboard", "inputs": inputs, "transition": "tap"}])
    time.sleep(settle)
    return response


def menu_keyboard_transition(session: RestInputSession, inputs: list[str], transition: str, settle: float = MENU_KEY_SETTLE_SECONDS) -> dict[str, Any]:
    response = session.post_events([{"kind": "keyboard", "inputs": inputs, "transition": transition}])
    time.sleep(settle)
    return response


def menu_keyboard_f2_tap(session: RestInputSession, settle: float = MENU_KEY_SETTLE_SECONDS) -> dict[str, Any]:
    # The REST API uses C64 matrix names: F2 is shifted F1.
    return menu_keyboard_tap(session, ["left_shift", "f1"], settle)


def close_menu_keyboard(session: RestInputSession) -> None:
    for _ in range(4):
        menu_keyboard_tap(session, ["run_stop"], 0.35)
    session.post_events([{"kind": "release_all"}])


def read_menu_screen(session: RestInputSession) -> tuple[list[str], list[list[int]]] | None:
    """The open menu as text rows and colour rows, or None when it is closed."""
    body = session.menu_screen()
    if body is None:
        return None
    if len(body) != MENU_SCREEN_BYTES:
        raise Failure(f"menu_screen returned {len(body)} bytes, expected {MENU_SCREEN_BYTES}")
    cells = MENU_SCREEN_ROWS * MENU_SCREEN_COLS
    rows: list[str] = []
    colours: list[list[int]] = []
    for row in range(MENU_SCREEN_ROWS):
        start = row * MENU_SCREEN_COLS
        codes = body[start:start + MENU_SCREEN_COLS]
        rows.append("".join(chr(c & 0x7F) if 0x20 <= (c & 0x7F) <= 0x7E else " " for c in codes))
        colours.append([c & 0x0F for c in body[cells + start:cells + start + MENU_SCREEN_COLS]])
    return rows, colours


# The column a list row's own text starts in is not the same on every screen a
# suite drives. The settings list and the launcher indent by one; the file
# browser does not indent at all and starts a row higher; and a C64 Ultimate
# draws a frame around the whole thing, so its column zero is frame on every
# row and says nothing about the selection. Reading the colour at a fixed
# column therefore found the highlight on some screens and not others: the
# browser's was missed on an Ultimate 64, where the entry is at column zero.
MENU_FRAME_CHARS = "|+-"


def menu_label_column(text: str) -> int | None:
    """The column where a list row's own text begins, past any frame."""
    for column, character in enumerate(text):
        if character != " " and character not in MENU_FRAME_CHARS:
            return column
    return None


def menu_selection(rows: list[str], colours: list[list[int]]) -> tuple[int, str] | None:
    """The highlighted entry as (row, text), or None when nothing is highlighted."""
    for row in range(MENU_LIST_FIRST_ROW, MENU_LIST_LAST_ROW + 1):
        column = menu_label_column(rows[row])
        if column is not None and colours[row][column] == MENU_SELECTED_COLOUR:
            return row, rows[row]
    return None


def menu_row_with(rows: list[str], label: str) -> int | None:
    """The list row holding `label`, or None when it is not on screen."""
    for row in range(MENU_LIST_FIRST_ROW, MENU_LIST_LAST_ROW + 1):
        if label in rows[row]:
            return row
    return None


def wait_for_menu(session: RestInputSession, predicate, description: str,
                  timeout: float = MENU_STATE_TIMEOUT_SECONDS):
    """Poll the menu screen until `predicate(rows, colours)` returns non-None.

    Every menu step below waits for its result to be on the screen instead of
    sleeping long enough that it usually is. The pause a single-device target
    needs is not the pause this path needs: on a cartridge target a key posted
    to machine:input is applied to the keyboard matrix of the computer the
    cartridge is plugged into, and only reaches the menu on the cartridge's
    next scan of that matrix. See tests/lib/targets.py.
    """
    deadline = time.monotonic() + timeout
    while True:
        screen = read_menu_screen(session)
        if screen is not None:
            found = predicate(*screen)
            if found is not None:
                return found
        if time.monotonic() >= deadline:
            shown = "the menu is closed" if screen is None else "\n".join(screen[0])
            raise Failure(f"Timed out after {timeout:g}s waiting for {description}. "
                          f"Menu screen was:\n{shown}")
        time.sleep(pacing.POLL_INTERVAL_SECONDS)


def wait_for_menu_selection(session: RestInputSession, description: str) -> tuple[list[str], tuple[int, str]]:
    """The screen and its highlighted entry, once the menu shows one."""
    def highlighted(rows, colours):
        found = menu_selection(rows, colours)
        return None if found is None else (rows, found)

    return wait_for_menu(session, highlighted, description)


def wait_for_menu_selection_change(session: RestInputSession,
                                   before: tuple[int, str]) -> tuple[int, str] | None:
    """The highlighted entry once it differs from `before`, or None if it never does.

    A cursor key moves the highlight to another row, or, at the edge of a list
    that scrolls, keeps the row and changes its text. Comparing the pair covers
    both. The highlight stops at the ends of a list, where a key legitimately
    changes nothing, so not moving is a result rather than a failure here.
    """
    deadline = time.monotonic() + MENU_STEP_TIMEOUT_SECONDS
    while True:
        screen = read_menu_screen(session)
        if screen is not None:
            current = menu_selection(*screen)
            if current is not None and current != before:
                return current
        if time.monotonic() >= deadline:
            return None
        time.sleep(pacing.POLL_INTERVAL_SECONDS)


def get_config_value(session: RestInputSession, category: str, item: str) -> str:
    body = session.json_request("GET", "/v1/configs/" + urllib.parse.quote(category, safe=""))
    store = body.get(category)
    if not isinstance(store, dict) or item not in store:
        raise Failure(f"Config value {category}/{item} not found in {body}")
    value = store[item]
    if not isinstance(value, str):
        raise Failure(f"Config value {category}/{item} is not a string: {value!r}")
    return value


def set_config_value(session: RestInputSession, category: str, item: str, value: str) -> None:
    response = session.json_request(
        "PUT",
        "/v1/configs/"
        + urllib.parse.quote(category, safe="")
        + "/"
        + urllib.parse.quote(item, safe=""),
        params={"value": value},
    )
    if response.get("errors") != []:
        raise Failure(f"Failed to set config {category}/{item}: {response}")


def open_menu(session: RestInputSession) -> None:
    """Bring the menu up, unless it is already showing.

    menu.toggle_menu is the shared press-then-poll: the press is a toggle, so
    a transport failure is not retried and the state poll answers either way.
    """
    if not menu_lib.toggle_menu(lambda: session.put("menu_button"),
                                session.menu_screen_open, want_open=True):
        raise Failure("the menu did not open")


def soak_special_key_edge_case(session: RestInputSession) -> None:
    for inputs in (
        ["ctrl", "9"],
        ["ctrl", "0"],
        ["left_shift", "cursor_left_right"],
        ["cursor_left_right"],
        ["inst_del"],
        ["right_shift", "inst_del"],
    ):
        response = session.post_events([{"kind": "keyboard", "inputs": inputs, "transition": "tap"}])
        if sorted(response.get("keyboard", {}).get("inputs", [])) != sorted(inputs):
            raise Failure(f"Expected special-key edge snapshot for {inputs}, got {response}")
        time.sleep(0.2)
        assert_state_empty(session)


def soak_rapid_mixed_case(session: RestInputSession, screen_tail: str, text_chunks: list[str], joystick_inputs: list[str]) -> str:
    session.post_events([{"kind": "joystick", "port": 1, "inputs": joystick_inputs, "transition": "press"}])
    for chunk in text_chunks:
        session.json_request(
            "POST",
            "/v1/machine:input",
            payload={"events": keyboard_tap_events_for_text(chunk)},
        )
    expected = "".join(text_chunks)
    screen_tail = append_screen_tail(screen_tail, expected)
    wait_for_screen_sequence(session, text_to_screen_codes(screen_tail), timeout=max(4.0, len(expected) * 0.8))
    assert_joystick_ports(
        session,
        0x1F
        & (~0x01 if "up" in joystick_inputs else 0x1F)
        & (~0x02 if "down" in joystick_inputs else 0x1F)
        & (~0x04 if "left" in joystick_inputs else 0x1F)
        & (~0x08 if "right" in joystick_inputs else 0x1F)
        & (~0x10 if "fire" in joystick_inputs else 0x1F),
        0x1F,
    )
    assert_input_state(session, [], joystick_inputs, [])
    session.post_events([{"kind": "release_all"}])
    assert_state_empty(session)
    return screen_tail


def run_soak_tests(session: RestInputSession, duration_seconds: float) -> int:
    keyboard_text_cases = [
        "aaaaaa",
        "Abab09",
        "C64Z",
        "qwertY",
        "az09ZA",
    ]
    keyboard_hold_cases = [
        (["left_shift"], ["left_shift", "a"]),
        (["right_shift"], ["right_shift", "m"]),
        (["commodore"], ["commodore", "q"]),
        (["ctrl"], ["ctrl", "x"]),
    ]
    joystick_cases = [
        (1, ["up", "fire"], ["fire"]),
        (1, ["left", "right", "fire"], ["right"]),
        (2, ["down", "fire"], ["fire"]),
        (2, ["up", "right", "fire"], ["right"]),
    ]
    interleaved_cases = [
        ("alpha", 1, ["up", "fire"]),
        ("delta", 1, ["left", "fire"]),
        ("omega", 1, ["right", "fire"]),
        ("basic", 1, ["down"]),
    ]
    rapid_mix_cases = [
        (["ab", "C9", "za"], ["up", "fire"]),
        (["Qw", "eR", "12"], ["left"]),
        (["c6", "4Z", "aa"], ["right", "fire"]),
    ]

    wait_for_input_ready(session, timeout=15.0)
    wait_for_basic_ready(session)
    screen_tail = ""

    deadline = time.monotonic() + duration_seconds
    cycles = 0
    while time.monotonic() < deadline:
        text_case = keyboard_text_cases[cycles % len(keyboard_text_cases)]
        hold_case = keyboard_hold_cases[cycles % len(keyboard_hold_cases)]
        joystick_case = joystick_cases[cycles % len(joystick_cases)]
        interleaved_case = interleaved_cases[cycles % len(interleaved_cases)]
        rapid_mix_case = rapid_mix_cases[cycles % len(rapid_mix_cases)]

        detail(f"soak {cycles + 1:03d}: text={text_case} joy{joystick_case[0]}={'+'.join(joystick_case[1])}")
        screen_tail = soak_keyboard_basic_case(session, screen_tail, text_case)
        screen_tail = soak_interleaved_case(session, screen_tail, interleaved_case[0], interleaved_case[1], interleaved_case[2])
        screen_tail = soak_rapid_mixed_case(session, screen_tail, rapid_mix_case[0], rapid_mix_case[1])
        soak_keyboard_hold_case(session, hold_case[0], hold_case[1])
        soak_joystick_case(session, joystick_case[0], joystick_case[1], joystick_case[2])
        soak_special_key_edge_case(session)
        if cycles % 3 == 0:
            soak_invalid_atomic_case(session)
            soak_invalid_body_case(session)
        cycles += 1
    return cycles


def run_contract_tests(session: RestInputSession) -> None:
    with check("input snapshot has stable empty response shape"):
        session.post_events([{"kind": "release_all"}])
        assert_input_state(session, [], [], [])

    with check("POST accepts 64 event batch"):
        session.post_events([{"kind": "release_all"}] * 64)
        assert_state_empty(session)

    with check("bad content-type is rejected without mutation"):
        session.post_events([{"kind": "keyboard", "inputs": ["ctrl"], "transition": "press"}])
        body = session.post_raw_expect_error(
            b'{"events":[{"kind":"release_all"}]}',
            "text/plain",
        )
        assert_error_body_only(body)
        assert_input_state(session, ["ctrl"], [], [])
        session.post_events([{"kind": "release_all"}])

    with check("missing JSON body is rejected without mutation"):
        session.post_events([{"kind": "keyboard", "inputs": ["ctrl"], "transition": "press"}])
        body = session.post_without_body_expect_error()
        assert_error_body_only(body)
        assert_input_state(session, ["ctrl"], [], [])
        session.post_events([{"kind": "release_all"}])

    with check("malformed JSON is rejected without mutation"):
        session.post_events([{"kind": "joystick", "port": 1, "inputs": ["fire"], "transition": "press"}])
        body = session.post_raw_expect_error(b'{"events":[', "application/json")
        assert_error_body_only(body)
        assert_input_state(session, [], ["fire"], [])
        session.post_events([{"kind": "release_all"}])

    with check("unknown root field is rejected without mutation"):
        session.post_events([{"kind": "keyboard", "inputs": ["commodore"], "transition": "press"}])
        body = session.post_payload_expect_error({"events": [{"kind": "release_all"}], "extra": True})
        assert_error_body_only(body)
        assert_input_state(session, ["commodore"], [], [])
        session.post_events([{"kind": "release_all"}])

    with check("late invalid event keeps whole batch atomic"):
        session.post_events(
            [
                {"kind": "keyboard", "inputs": ["ctrl"], "transition": "press"},
                {"kind": "joystick", "port": 1, "inputs": ["fire"], "transition": "press"},
            ]
        )
        body = session.post_events_expect_error(
            [
                {"kind": "release_all"},
                {"kind": "keyboard", "inputs": ["left_shift"], "transition": "press"},
                {"kind": "joystick", "port": 3, "inputs": ["up"], "transition": "press"},
            ]
        )
        assert_error_body_only(body)
        assert_input_state(session, ["ctrl"], ["fire"], [])
        session.post_events([{"kind": "release_all"}])


def run_keyboard_tests(session: RestInputSession) -> None:
    # These assert what the live C64 matrix sees, so the menu must be closed. It
    # must also be closed for safety: the sweep below taps F5, which opens the
    # task menu onto the Assembly 64 form when the UI has focus.
    session.close_menu_from_anywhere()

    with check("keyboard single-tap batch is consumed by BASIC in order"):
        session.json_request(
            "POST",
            "/v1/machine:input",
            payload={"events": keyboard_tap_events_for_text("aaaaaa")},
        )
        wait_for_basic_input_prefix(session, "AAAAAA", timeout=4.0)
        time.sleep(0.3)
        assert_state_empty(session)
        session.post_events([{"kind": "release_all"}])

    echo_offset = prepare_keyboard_echo_program(session)
    with check("keyboard 10 Hz mixed alphabet echo has no missed presses"):
        echo_offset = run_keyboard_echo_stress_case(session, mixed_alphabet_text(ECHO_ALPHABET_LENGTH), 10.0, echo_offset)

    with check("keyboard 20 Hz alternating ab echo has no missed presses"):
        echo_offset = run_keyboard_echo_stress_case(session, alternating_text("a", "b", ECHO_ALTERNATING_LENGTH), 20.0, echo_offset)

    with check("keyboard 5 Hz alternating ab echo has no missed presses"):
        run_keyboard_echo_stress_case(session, alternating_text("a", "b", ECHO_SLOW_ALTERNATING_LENGTH), 5.0, echo_offset)

    with check("keyboard single letter reaches the live C64 matrix"):
        session.post_events([{"kind": "release_all"}])
        session.post_events([{"kind": "keyboard", "inputs": ["l"], "transition": "press"}])
        assert_keyboard_matrix_inputs(session, ["l"])
        session.post_events([{"kind": "release_all"}])
        assert_state_empty(session)

    with check("keyboard shifted pair reaches the live C64 matrix"):
        session.post_events([{"kind": "release_all"}])
        session.post_events([{"kind": "keyboard", "inputs": ["left_shift", "a"], "transition": "press"}])
        assert_keyboard_matrix_inputs(session, ["left_shift", "a"])
        session.post_events([{"kind": "release_all"}])
        assert_state_empty(session)

    with check("keyboard batch applies multiple presses atomically"):
        session.post_events([{"kind": "release_all"}])
        body = session.json_request(
            "POST",
            "/v1/machine:input",
            payload={
                "events": [
                    {"kind": "keyboard", "inputs": ["a"], "transition": "press"},
                    {"kind": "keyboard", "inputs": ["left_shift"], "transition": "press"},
                ]
            },
        )
        if body.get("keyboard", {}).get("inputs") != ["a", "left_shift"]:
            raise Failure(f"Unexpected batch keyboard state: {body}")
        assert_keyboard_matrix_inputs(session, ["a", "left_shift"])
        session.post_events([{"kind": "release_all"}])
        assert_state_empty(session)

    with check("keyboard ordered batch and idempotent release"):
        session.post_events([{"kind": "release_all"}])
        session.post_events(
            [
                {"kind": "keyboard", "inputs": ["left_shift", "ctrl"], "transition": "press"},
                {"kind": "keyboard", "inputs": ["left_shift"], "transition": "press"},
                {"kind": "keyboard", "inputs": ["space"], "transition": "release"},
                {"kind": "keyboard", "inputs": ["left_shift"], "transition": "release"},
            ]
        )
        assert_input_state(session, ["ctrl"], [], [])
        session.post_events([{"kind": "release_all"}])

    with check("keyboard release_all can be followed by press in same batch"):
        session.post_events([{"kind": "release_all"}])
        session.post_events(
            [
                {"kind": "keyboard", "inputs": ["left_shift"], "transition": "press"},
                {"kind": "release_all"},
                {"kind": "keyboard", "inputs": ["commodore"], "transition": "press"},
            ]
        )
        assert_input_state(session, ["commodore"], [], [])
        session.post_events([{"kind": "release_all"}])

    with check("keyboard accepts eight simultaneous inputs"):
        session.post_events([{"kind": "release_all"}])
        inputs = ["a", "s", "d", "f", "j", "k", "l", "space"]
        session.post_events([{"kind": "keyboard", "inputs": inputs, "transition": "press"}])
        assert_input_state(session, ["a", "s", "d", "f", "j", "k", "l", "space"], [], [])
        session.post_events([{"kind": "release_all"}])
        assert_state_empty(session)

    with check("keyboard tap does not release persistent key"):
        session.post_events([{"kind": "release_all"}])
        session.post_events(
            [
                {"kind": "keyboard", "inputs": ["left_shift"], "transition": "press"},
                {"kind": "keyboard", "inputs": ["left_shift", "a"], "transition": "tap"},
            ]
        )
        time.sleep(0.1)
        inputs = session.get_state()["keyboard"]["inputs"]
        if "left_shift" not in inputs or "a" in inputs:
            raise Failure(f"Persistent/tap state mismatch: {inputs}")
        session.post_events([{"kind": "release_all"}])

    with check("keyboard release_all clears state"):
        session.post_events([{"kind": "release_all"}])
        session.post_events([{"kind": "keyboard", "inputs": ["left_shift"], "transition": "press"}])
        session.post_events([{"kind": "release_all"}])
        assert_state_empty(session)

    with check("keyboard restore tap auto releases"):
        session.post_events([{"kind": "release_all"}])
        session.post_events([{"kind": "keyboard", "inputs": ["restore"], "transition": "tap"}])
        time.sleep(0.2)
        assert_state_empty(session)

    with check("keyboard special-key taps snapshot correctly and auto release"):
        for inputs in (["commodore"], ["ctrl"], ["run_stop"], ["restore"], ["f1"], ["f3"], ["f5"], ["f7"], ["left_shift"], ["right_shift"]):
            session.post_events([{"kind": "release_all"}])
            response = session.post_events([{"kind": "keyboard", "inputs": inputs, "transition": "tap"}])
            if sorted(response.get("keyboard", {}).get("inputs", [])) != sorted(inputs):
                raise Failure(f"Expected immediate special-key tap snapshot for {inputs}, got {response}")
            time.sleep(0.2)
            assert_state_empty(session)

    with check("keyboard tap is visible in the live hardware snapshot and auto releases"):
        session.post_events([{"kind": "release_all"}])
        response = session.post_events([{"kind": "keyboard", "inputs": ["a"], "transition": "tap"}])
        if response.get("keyboard", {}).get("inputs") != ["a"]:
            raise Failure(f"Expected immediate tap snapshot for a, got {response}")
        time.sleep(0.2)
        assert_state_empty(session)
        session.post_events([{"kind": "release_all"}])

    with check("keyboard cursor-left tap is visible in the live hardware snapshot and auto releases"):
        session.post_events([{"kind": "release_all"}])
        response = session.post_events([{"kind": "keyboard", "inputs": ["left_shift", "cursor_left_right"], "transition": "tap"}])
        if sorted(response.get("keyboard", {}).get("inputs", [])) != ["cursor_left_right", "left_shift"]:
            raise Failure(f"Expected immediate cursor-left tap snapshot, got {response}")
        time.sleep(0.2)
        assert_state_empty(session)
        session.post_events([{"kind": "release_all"}])

    with check("keyboard tap batch drains through the live matrix path"):
        session.post_events([{"kind": "release_all"}])
        response = session.json_request("POST", "/v1/machine:input", payload={"events": keyboard_tap_events_for_text("ABCDEFGHIJ")})
        if not response.get("keyboard", {}).get("inputs"):
            raise Failure(f"Expected a live tap snapshot while the batch was draining, got {response}")
        wait_state_empty(session, "the ten-tap batch to drain")
        session.post_events([{"kind": "release_all"}])

    with check("keyboard long repeated tap train drains fully without sticky state"):
        session.post_events([{"kind": "release_all"}])
        repeated = [{"kind": "keyboard", "inputs": ["a"], "transition": "tap"} for _ in range(60)]
        response = session.json_request("POST", "/v1/machine:input", payload={"events": repeated})
        if response.get("keyboard", {}).get("inputs") != ["a"]:
            raise Failure(f"Expected repeated tap train to expose the live a snapshot, got {response}")
        wait_state_empty(session, "the sixty-tap train to drain")
        session.post_events([{"kind": "release_all"}])

    with check("invalid keyboard batch does not mutate state"):
        session.post_events([{"kind": "release_all"}])
        body = session.post_events_expect_error(
            [
                {"kind": "keyboard", "inputs": ["left_shift"], "transition": "press"},
                {"kind": "keyboard", "inputs": ["escape"], "transition": "tap"},
            ]
        )
        assert_error_body_only(body)
        assert_state_empty(session)


def run_keyboard_echo_tests(session: RestInputSession, selected: list[str] | None = None) -> None:
    offset = prepare_keyboard_echo_program(session)

    if wants_test(selected, "keyboard-echo-alphabet"):
        with check("keyboard 10 Hz mixed alphabet echo has no missed presses"):
            offset = run_keyboard_echo_stress_case(session, mixed_alphabet_text(ECHO_ALPHABET_LENGTH), 10.0, offset)

    if wants_test(selected, "keyboard-echo-ab-20hz"):
        with check("keyboard 20 Hz alternating ab echo has no missed presses"):
            offset = run_keyboard_echo_stress_case(session, alternating_text("a", "b", ECHO_ALTERNATING_LENGTH), 20.0, offset)

    if wants_test(selected, "keyboard-echo-ab-5hz"):
        with check("keyboard 5 Hz alternating ab echo has no missed presses"):
            run_keyboard_echo_stress_case(session, alternating_text("a", "b", ECHO_SLOW_ALTERNATING_LENGTH), 5.0, offset)


# The rename dialog the menu-editor checks type into. Reached from the file
# browser's context menu on its first entry, which every machine here offers
# Rename on, and abandoned with RUN/STOP so nothing is renamed.
#
# It replaced "Modem Settings" / "Modem Offline Text", which cost a REST config
# write to seed each value, two select_menu_entry scans (the category list and
# then the item list, each reading the highlight back after every key), a
# commit, and a config restore at the end. None of that was about the thing
# under test, which is what the keyboard puts into a UIStringEdit.
RENAME_DIALOG_TITLE = "Give a new name.."
# The context menu a directory entry offers: Enter, Copy to..., Move to...,
# Rename, Delete. The first is what starts selected, and "Rename" is the only
# item beginning with 'r', so one keystroke reaches it. 'r' is not one of the
# letters a machine set to WASD Cursors reads as a cursor key, so it needs no
# respelling; see tests/lib/navigation.py for the ones that do.
# Bound on the launcher descent, which is a loop over "read where the entry
# is, move the highlight one step, look again".
LAUNCHER_DESCENT_STEPS = 24
# Deeper than any directory this suite enters, which is one.
BROWSER_ROOT_STEPS = 8
# A file of this suite's own, in the RAM disk, renamed and then not renamed.
# A drive entry was used first and it did open the dialog, but renaming a drive
# is not what the dialog is for and a drive with no media does not offer it at
# all: on a C64 Ultimate the first entry is an empty SD slot whose context menu
# holds only Enter and Copy to... A file makes the subject unambiguous on every
# machine, and it costs one FTP store.
RENAME_TARGET_DIR = "Temp"
RENAME_TARGET_PATH = "/Temp"
RENAME_TARGET_FILE = "inpfld.prg"
# Two bytes of load address is a valid PRG and nothing here reads it back.
RENAME_TARGET_BYTES = bytes([0x01, 0x08])
RENAME_MENU_ITEM = "Rename"
# Longer than the longest context menu here, which is nine items for a program.
OVERLAY_SELECT_STEPS = 14

# How long to hold a key to see it repeat. The firmware repeats after
# first_delay and then every repeat_speed tick (keyboard_usb.cc), and measured
# on an Ultimate 64 through this field the boundary is sharp: 0.25s and 0.30s
# give one character every time, 0.40s gives two, 0.50s three, 0.60s four, and
# 1.20s ten. 0.6s is comfortably past the first repeat and still leaves the
# marker checks a run they cannot mistake for a single press. It was 1.2s.
MENU_REPEAT_HOLD_SECONDS = float(
    os.environ.get("U64_INPUT_MENU_REPEAT_HOLD", "0.6"))
# The dialog draws its title, a blank line, then the field. A C64 Ultimate
# draws the whole browser inside a frame and this dialog inside that, so the
# field is found from the title rather than from the last framed row.
RENAME_FIELD_ROWS_BELOW_TITLE = 2


def rename_dialog_title_row(rows: list[str]) -> int | None:
    return next((i for i, row in enumerate(rows) if RENAME_DIALOG_TITLE in row), None)


def read_rename_field(session: RestInputSession) -> str:
    """What the rename field holds, read while it is still open.

    A more direct oracle than the old one: it is the editor's own buffer rather
    than the value a commit wrote somewhere else, so nothing between the
    keyboard and the field can hide a defect.
    """
    screen = read_menu_screen(session)
    if screen is None:
        raise Failure("the menu closed while the rename field was being read")
    value = _field_of(screen[0])
    if value is None:
        raise Failure(f"the {RENAME_DIALOG_TITLE!r} dialog is not on screen")
    return value


def overlay_label_colour(rows: list[str], colours: list[list[int]],
                         label: str) -> int | None:
    """The colour the context menu draws `label` in, or None if it is absent.

    Read at the label's own first column, not across the row. The context menu
    is an overlay in the right-hand columns, so on a C64 Ultimate, which draws
    the browser inside a frame, every one of its rows also carries browser text
    in its own colours. Comparing whole-row colour sets therefore compares the
    listing underneath as much as the menu, and the selected item was missed.
    Measured there: the selected item's label is colour 1 and the others are
    colour 12, whatever the row holds.
    """
    row = menu_row_with(rows, label)
    if row is None:
        return None
    return colours[row][rows[row].index(label)]


def select_menu_entry(session: RestInputSession, label: str) -> None:
    """Move the highlight onto the entry whose row holds `label`.

    Reading the highlight back after every key is what makes this work on more
    than one machine. Fixed press counts do not: they encode one machine's
    settings list, and an Ultimate II+L has fourteen settings categories, so
    the nineteen cursor-down presses this replaced stopped on the last of them,
    "Network Settings". The suite then typed into "Time Server 1" while reading
    "Modem Offline Text" back, and reported the empty value it had set itself.
    """
    deadline = time.monotonic() + MENU_SELECT_TIMEOUT_SECONDS
    descending = True
    reversed_once = False
    while True:
        rows, current = wait_for_menu_selection(
            session, f"a highlighted entry while looking for {label!r}")
        if label in current[1]:
            return
        target = menu_row_with(rows, label)
        if target is not None:
            descending = target > current[0]
        # The REST API uses C64 matrix names: cursor-up is shifted cursor-down.
        keys = ["cursor_up_down"] if descending else ["left_shift", "cursor_up_down"]
        session.post_events([{"kind": "keyboard", "inputs": keys, "transition": "tap"}])
        if wait_for_menu_selection_change(session, current) is None:
            if reversed_once or target is not None:
                raise Failure(
                    f"Could not reach {label!r}: the highlight stopped on "
                    f"{current[1].strip()!r}. Menu screen was:\n" + "\n".join(rows))
            # Nothing moved, so this is the end of the list and the entry is on
            # the other side of where the highlight started.
            reversed_once = True
            descending = not descending
        if time.monotonic() >= deadline:
            raise Failure(
                f"Timed out after {MENU_SELECT_TIMEOUT_SECONDS:g}s moving the highlight to "
                f"{label!r}; it is on {current[1].strip()!r}. Menu screen was:\n" + "\n".join(rows))


def in_file_browser(rows: list[str]) -> bool:
    """Whether the file browser is what the menu is showing.

    The browser puts the directory it is showing on the status row and nothing
    else does, so a leading "/" identifies it on every machine.
    """
    return rows[MENU_SCREEN_ROWS - 1].lstrip().startswith("/")


def enter_file_browser(session: RestInputSession) -> None:
    """Leave the menu showing the file browser, whatever it opened on.

    A C64 Ultimate does not put the browser behind the menu button: the button
    opens a launcher whose entries are the browser, the online search and the
    settings screens. The other two machines open the browser directly, and
    there this returns at once.

    The launcher lists hardware actions, so the entry is reached by reading
    which row it is on and moving the highlight there, never by a fixed burst
    that could leave RETURN on whichever entry the cursor stopped at. Same
    reasoning as RestBackend.enter_file_browser in tests/e2e/lib/ui_backend.py.
    """
    entry = session.machine.launcher_browser_entry
    if entry is None:
        return
    for _ in range(LAUNCHER_DESCENT_STEPS):
        rows, _colours = wait_for_menu(
            session, lambda rows, colours: (rows, colours), "the menu screen")
        if in_file_browser(rows):
            return
        row = menu_row_with(rows, entry)
        if row is None:
            menu_keyboard_tap(session, ["arrow_left"], MENU_KEY_SETTLE_SECONDS)
            continue
        rows, cursor = wait_for_menu_selection(
            session, f"a highlighted launcher entry while looking for {entry!r}")
        if cursor[0] != row:
            menu_keyboard_tap(
                session, ["left_shift", "cursor_up_down"] if row < cursor[0]
                else ["cursor_up_down"], MENU_KEY_SETTLE_SECONDS)
            continue
        menu_keyboard_tap(session, ["return"], MENU_KEY_SETTLE_SECONDS)
    raise Failure(f"could not reach the file browser: no {entry!r} entry and no "
                  f"directory on the status row after {LAUNCHER_DESCENT_STEPS} steps")


def make_rename_fixture(host: str) -> None:
    """Put the file the rename dialog is opened on into the RAM disk."""
    with ftp_lib.session(host) as client:
        ftp_lib.store(client, f"{RENAME_TARGET_PATH}/{RENAME_TARGET_FILE}",
                      RENAME_TARGET_BYTES)


def remove_rename_fixture(host: str) -> None:
    with ftp_lib.session(host, directory=RENAME_TARGET_PATH) as client:
        ftp_lib.delete_quietly(client, RENAME_TARGET_FILE)


def go_to_browser_root(session: RestInputSession) -> None:
    """Back the browser out to "/", wherever it was left.

    The precondition gate leaves it there between suites, but this suite
    descends into the RAM disk itself, so a second scenario, a retry, or a run
    started by hand must not depend on where the last one stopped.
    """
    for _ in range(BROWSER_ROOT_STEPS):
        rows, _colours = wait_for_menu(
            session, lambda rows, colours: (rows, colours), "the browser screen")
        if rows[MENU_SCREEN_ROWS - 1].split()[0] == "/":
            return
        menu_keyboard_tap(session, ["left_shift", "cursor_left_right"],
                          MENU_KEY_SETTLE_SECONDS)
    raise Failure(f"the browser did not reach '/' in {BROWSER_ROOT_STEPS} steps")


def select_overlay_item(session: RestInputSession, label: str) -> None:
    """Move the context menu's highlight onto `label`.

    One step at a time, checking after each, rather than by letter. The letter
    is ambiguous on the menu this opens: a program file offers Run, Load, DMA,
    View, Hex View, Copy to..., Move to..., Rename and Delete, and 'r' matches
    Run, which is already selected, so the keypress moved nothing.

    Checking after every step also means the highlight is known to be on the
    wanted item before RETURN is sent, which matters on a menu that also holds
    Delete.
    """
    for _ in range(OVERLAY_SELECT_STEPS):
        rows, colours = wait_for_menu(
            session,
            lambda rows, colours: (
                (rows, colours)
                if overlay_label_colour(rows, colours, label) is not None else None),
            f"the context menu, offering {label!r}")
        if overlay_label_colour(rows, colours, label) == MENU_SELECTED_COLOUR:
            return
        menu_keyboard_tap(session, ["cursor_up_down"], 0.0)
        # Wait for the highlight to have moved rather than for a fixed time.
        # With neither, the read above saw the screen from before the keypress,
        # took it for the result and stepped again, overshooting the item.
        seen = colours
        wait_for_menu(
            session,
            lambda rows, colours: True if colours != seen else None,
            f"the context menu highlight to move while looking for {label!r}")
    raise Failure(f"{label!r} never became the selected context-menu item in "
                  f"{OVERLAY_SELECT_STEPS} steps")


def open_rename_editor(session: RestInputSession) -> None:
    """Open the rename dialog on the browser's first entry, and empty it.

    The context menu is drawn as an overlay over the right-hand columns rather
    than as a full-width list, so select_menu_entry cannot read its highlight:
    that scanner looks for one marked row across the whole width, which here is
    the browser's own selection underneath. The overlay is picked by letter
    instead, which is what the browser offers and what ui_backend does with the
    same menu.

    The landing is confirmed before RETURN is sent, and that matters: this menu
    also holds Delete, so committing on an unverified highlight is not a thing
    to do on a drive entry.
    """
    session.post_events([{"kind": "release_all"}])
    open_menu(session)
    # No F2 here: this wants the file browser, not the settings list. On a C64
    # Ultimate the menu button opens a launcher above the browser, so getting
    # there is a step of its own.
    enter_file_browser(session)
    # Always from the root, and always entering the directory fresh. The
    # browser holds a cached child list per directory, so one that was already
    # sitting in this directory when the fixture was written over FTP can be
    # showing a listing from before it existed. Backing out and descending
    # again is what makes it read the directory rather than its cache.
    go_to_browser_root(session)
    select_menu_entry(session, RENAME_TARGET_DIR)
    # RIGHT descends into the directory; RETURN there would open the drive's
    # own context menu instead.
    menu_keyboard_tap(session, ["cursor_left_right"], MENU_KEY_SETTLE_SECONDS)
    wait_for_menu(
        session,
        lambda rows, colours: (True if rows[MENU_SCREEN_ROWS - 1].lstrip().startswith(
            RENAME_TARGET_PATH) else None),
        f"the browser to enter {RENAME_TARGET_PATH}")
    wait_for_menu(
        session,
        lambda rows, colours: (True if menu_row_with(rows, RENAME_TARGET_FILE)
                               is not None else None),
        f"{RENAME_TARGET_FILE!r} to appear in {RENAME_TARGET_PATH}")
    select_menu_entry(session, RENAME_TARGET_FILE)
    menu_keyboard_tap(session, ["return"], 0.0)
    # "Enter" is the first item and starts selected, so its marking is what a
    # selected row in this overlay looks like on whatever machine this is.
    select_overlay_item(session, RENAME_MENU_ITEM)
    menu_keyboard_tap(session, ["return"], 0.0)
    wait_for_menu(session,
                  lambda rows, colours: (rename_dialog_title_row(rows) is not None) or None,
                  f"the {RENAME_DIALOG_TITLE!r} dialog to open")
    clear_rename_field(session)


def wait_for_rename_field_matching(session: RestInputSession, predicate,
                                   description: str) -> str:
    """Wait until the field satisfies `predicate`, and answer what it holds.

    For the steps whose exact result is not known in advance: a repeat run is
    however many characters the hold produced, but the marker that has to land
    after it is known, so that is what is waited for.
    """
    return wait_for_menu(
        session,
        lambda rows, colours: (
            _field_of(rows)
            if _field_of(rows) is not None and predicate(_field_of(rows))
            else None),
        description)


def wait_for_rename_field(session: RestInputSession, expected: str) -> None:
    """Wait until the field reads `expected`, rather than sleeping a settle.

    Every step below that types something whose result is known waits for that
    result. A fixed settle is both slower, because it always pays its full
    length, and weaker, because a machine slower than it fails the check for a
    reason that has nothing to do with what is being checked.
    """
    wait_for_menu(
        session,
        lambda rows, colours: (True if _field_of(rows) == expected else None),
        f"the rename field to read {expected!r}")


def _field_of(rows: list[str]) -> str | None:
    title = rename_dialog_title_row(rows)
    if title is None:
        return None
    return rows[title + RENAME_FIELD_ROWS_BELOW_TITLE].strip("|").strip()


def type_into_rename_field(session: RestInputSession, text: str, expected: str) -> None:
    """Type `text` and wait for the field to read `expected`."""
    session.post_events(keyboard_tap_events_for_menu_text(text))
    wait_for_rename_field(session, expected)


def clear_rename_field(session: RestInputSession) -> None:
    """Empty the field with one keystroke.

    Shift plus CLR/HOME is KEY_CLEAR, which UIStringEdit answers by emptying
    its whole buffer whatever the length (software/userinterface/ui_elements.cc),
    so this costs one key whatever the name it is replacing.
    """
    session.post_events([{"kind": "keyboard",
                          "inputs": ["left_shift", "clr_home"],
                          "transition": "tap"}])
    wait_for_rename_field(session, "")


def close_rename_editor(session: RestInputSession) -> None:
    """Abandon the rename. Nothing is renamed and nothing needs restoring."""
    menu_keyboard_tap(session, ["run_stop"], 0.0)
    wait_for_menu(session,
                  lambda rows, colours: (rename_dialog_title_row(rows) is None) or None,
                  f"the {RENAME_DIALOG_TITLE!r} dialog to close")
    session.post_events([{"kind": "release_all"}])


def run_menu_keyboard_tests(session: RestInputSession, selected: list[str] | None = None) -> None:
    """What the keyboard puts into a menu string editor.

    Three scenarios sharing one editor session, against the browser's rename
    dialog, abandoned at the end with RUN/STOP. They were seven against a
    config item that had to be seeded over REST, navigated to through two menu
    levels and restored afterwards. What they check is unchanged; where they
    check it, how many editor sessions it takes and how long they wait are not.
    """
    session.post_events([{"kind": "release_all"}])
    assert_state_empty(session)
    make_rename_fixture(session.host)
    opened = False

    def editor() -> None:
        """Open the dialog on first use, and empty it on every use."""
        nonlocal opened
        if not opened:
            open_rename_editor(session)
            opened = True
        else:
            clear_rename_field(session)

    try:
        if wants_test(selected, "menu-shift"):
            # One session, two facts: a letter arrives unshifted, and a shift
            # pressed in its own request is still held when the letter arrives
            # in the next one. Checked separately this opened the editor twice
            # to prove something the pair proves together, and the pair also
            # shows the two compose.
            with check("menu editor takes a letter, and a shift held across requests"):
                editor()
                type_into_rename_field(session, "a", "a")
                # These three keep a settle, unlike everything else here, and
                # the settle is the subject rather than an accident. On a
                # cartridge target every injected event rewrites the whole
                # keyboard matrix of the computer the cartridge is plugged
                # into, so a shift pressed in one request has to have been
                # applied before the next request's letter arrives or the
                # letter's own matrix replaces it. Posted back to back with no
                # gap, an Ultimate II+L read 'aa' where 'aA' was typed.
                menu_keyboard_transition(session, ["left_shift"], "press",
                                         MENU_SHIFT_BATCH_SETTLE_SECONDS)
                menu_keyboard_tap(session, ["a"], MENU_SHIFT_BATCH_SETTLE_SECONDS)
                menu_keyboard_transition(session, ["left_shift"], "release",
                                         MENU_SHIFT_BATCH_SETTLE_SECONDS)
                wait_for_rename_field(session, "aA")

        if wants_test(selected, "menu-repeat-printable"):
            # Repeat starting and repeat stopping are one question asked of one
            # hold: hold the key, release it, wait past where a further repeat
            # would land, then type a marker. A run of 'c' says it repeated and
            # the marker sitting last says it stopped.
            with check("menu editor repeats a held printable key and stops on release"):
                editor()
                menu_keyboard_transition(session, ["c"], "press", 0.0)
                time.sleep(MENU_REPEAT_HOLD_SECONDS)
                menu_keyboard_transition(session, ["c"], "release", 0.0)
                time.sleep(MENU_POST_RELEASE_SETTLE_SECONDS)
                menu_keyboard_tap(session, ["z"], 0.0)
                value = wait_for_rename_field_matching(
                    session, lambda field: field.endswith("z"),
                    "the marker 'z' to land after the repeated run")
                save_menu_note("menu_printable_repeat", value)
                if len(value) < 3 or set(value[:-1]) != {"c"} or not value.endswith("z"):
                    raise Failure(f"Expected a repeated run of 'c' ended by the "
                                  f"marker 'z', got {value!r}.")

        if wants_test(selected, "menu-repeat-cursor"):
            # The same question for a control key, which takes a different path
            # through the decoder. A single tap is the control: the held one has
            # to move the cursor further than it did, and the two markers have
            # to end up adjacent, which says the repeat had stopped before the
            # first of them.
            with check("menu editor repeats a held cursor control and stops on release"):
                editor()
                type_into_rename_field(session, "ABCD", "ABCD")
                menu_keyboard_tap(session, ["left_shift", "cursor_left_right"], 0.0)
                session.post_events(keyboard_tap_events_for_menu_text("Z"))
                tapped = wait_for_rename_field_matching(
                    session, lambda field: "Z" in field,
                    "the marker to land after a single cursor-left")


                clear_rename_field(session)
                type_into_rename_field(session, "ABCD", "ABCD")
                menu_keyboard_transition(session, ["left_shift", "cursor_left_right"],
                                         "press", 0.0)
                time.sleep(MENU_REPEAT_HOLD_SECONDS)
                menu_keyboard_transition(session, ["left_shift", "cursor_left_right"],
                                         "release", 0.0)
                time.sleep(MENU_POST_RELEASE_SETTLE_SECONDS)
                session.post_events(keyboard_tap_events_for_menu_text("ZY"))
                held = wait_for_rename_field_matching(
                    session, lambda field: field.count("Y") == 1,
                    "both markers to land after the repeated cursor run")
                save_menu_note("menu_cursor_repeat", f"tapped={tapped} held={held}")
                if held == tapped:
                    raise Failure(f"A held cursor-left landed where a single tap "
                                  f"did ({held!r}); no repeat was observed.")
                if "ZY" not in held:
                    raise Failure(f"The cursor kept moving between the markers, "
                                  f"so the repeat did not stop: {held!r}.")
    finally:
        # Nothing to restore: the rename is abandoned with RUN/STOP, so no name
        # was changed and no configuration item was written.
        if opened:
            # Two attempts, not one: sharing a try meant that when closing the
            # dialog raised, the browser was never put back and the next suite
            # started inside the RAM disk. browser-long-filename then could not
            # find its fixture directory, whose path it builds from the root,
            # and ftp-client could not find "Remote FTP Servers", which lives
            # there. Restoring the root is what the next suite depends on, so
            # it runs whether or not the dialog closed cleanly.
            try:
                close_rename_editor(session)
            except Failure:
                pass
            try:
                go_to_browser_root(session)
            except Failure:
                pass
        session.close_menu_from_anywhere()
        teardown_step("remove the rename fixture",
                    lambda: remove_rename_fixture(session.host))
        session.post_events([{"kind": "release_all"}])
        assert_state_empty(session)


# The setting that decides which physical port an injected joystick event
# reaches. Every assertion below reads the two CIA ports and names which is
# which, so a machine left on anything but "Normal" fails all of them with a
# message about the ports rather than about the setting. Measured on an
# Ultimate 64 left on "Swapped": port 2 fire read back as port 1.
JOYSTICK_SWAP_STORE = "U64 Specific Settings"
JOYSTICK_SWAP_ITEM = "Joystick Swapper"
JOYSTICK_SWAP_UNSWAPPED = "Normal"


@contextlib.contextmanager
def unswapped_joystick_ports(session: RestInputSession):
    """Run the body with the joystick swapper off, and put it back afterwards.

    The setting belongs to the machine whose ports these are, which is the
    C64-side computer. `session.api` is built from the target, and
    Target.host_for sends everything but the keyboard and the streams to the
    device under test, so on a cartridge target asking it would ask the
    cartridge: an Ultimate II+ has no "U64 Specific Settings" store at all, the
    read would fail, and the guard would quietly do nothing on the one target
    where the ports really are somebody else's.

    A no-op where the computer does not serve the item either.
    """
    computer = UltimateApi(session.target.computer, session.password, session.timeout)
    try:
        was = computer.configs.current(JOYSTICK_SWAP_STORE, JOYSTICK_SWAP_ITEM)
    except Failure:
        was = ""
    if was and was != JOYSTICK_SWAP_UNSWAPPED:
        detail(f"{JOYSTICK_SWAP_ITEM} on {session.target.computer} is {was!r}; "
               f"setting it to {JOYSTICK_SWAP_UNSWAPPED!r} for these checks")
        computer.configs.set(JOYSTICK_SWAP_STORE, JOYSTICK_SWAP_ITEM,
                             JOYSTICK_SWAP_UNSWAPPED)
    try:
        yield
    finally:
        if was and was != JOYSTICK_SWAP_UNSWAPPED:
            try:
                computer.configs.set(JOYSTICK_SWAP_STORE, JOYSTICK_SWAP_ITEM, was)
            except Failure as exc:
                warn(f"could not put {JOYSTICK_SWAP_ITEM} back to {was!r}: {exc}")


def run_joystick_tests(session: RestInputSession) -> None:
    with unswapped_joystick_ports(session):
        run_joystick_checks(session)


def run_joystick_checks(session: RestInputSession) -> None:
    session.post_events([{"kind": "release_all"}])

    with check("joystick port 2 fire keeps Anykey buttons 2 and 3 released"):
        session.post_events([{"kind": "release_all"}])
        session.post_events([{"kind": "joystick", "port": 2, "inputs": ["fire"], "transition": "press"}])
        assert_joystick_ports(session, 0x1F, 0x0F)
        assert_anykey_extra_buttons(session, 2, fire2=False, fire3=False)

    with check("joystick port 2 fire2 lights only Anykey button 2"):
        session.post_events([{"kind": "release_all"}])
        session.post_events([{"kind": "joystick", "port": 2, "inputs": ["fire2"], "transition": "press"}])
        assert_anykey_extra_buttons(session, 2, fire2=True, fire3=False)

    with check("joystick port 2 fire3 lights only Anykey button 3"):
        session.post_events([{"kind": "release_all"}])
        session.post_events([{"kind": "joystick", "port": 2, "inputs": ["fire3"], "transition": "press"}])
        assert_anykey_extra_buttons(session, 2, fire2=False, fire3=True)

    with check("joystick port 1 up press is visible on CIA reads"):
        session.post_events([{"kind": "release_all"}])
        session.post_events([{"kind": "joystick", "port": 1, "inputs": ["up"], "transition": "press"}])
        assert_joystick_ports(session, 0x1E, 0x1F)
        state = session.get_state()["joysticks"]
        if state[0]["inputs"] != ["up"] or state[1]["inputs"] != []:
            raise Failure(f"Joystick state mismatch: {state}")

    with check("joystick port 1 all inputs and idempotent release are visible on CIA reads"):
        session.post_events([{"kind": "release_all"}])
        session.post_events(
            [
                {"kind": "joystick", "port": 1, "inputs": ["up", "down", "left", "right", "fire"], "transition": "press"},
                {"kind": "joystick", "port": 1, "inputs": ["right"], "transition": "release"},
                {"kind": "joystick", "port": 1, "inputs": ["right"], "transition": "release"},
            ]
        )
        assert_joystick_ports(session, 0x08, 0x1F)
        assert_input_state(session, [], ["up", "down", "left", "fire"], [])

    with check("joystick port 2 diagonal and fire are visible on CIA reads"):
        session.post_events([{"kind": "release_all"}])
        session.post_events([{"kind": "joystick", "port": 2, "inputs": ["up", "right", "fire"], "transition": "press"}])
        assert_joystick_ports(session, 0x1F, 0x06)
        inputs = session.get_state()["joysticks"][1]["inputs"]
        if inputs != ["up", "right", "fire"]:
            raise Failure(f"Joystick port 2 state mismatch: {inputs}")

    with check("joystick partial release is visible on CIA reads"):
        session.post_events([{"kind": "release_all"}])
        session.post_events([{"kind": "joystick", "port": 2, "inputs": ["up", "fire"], "transition": "press"}])
        session.post_events([{"kind": "joystick", "port": 2, "inputs": ["fire"], "transition": "release"}])
        assert_joystick_ports(session, 0x1F, 0x1E)

    with check("joystick fire2/fire3 round-trip through REST state"):
        session.post_events([{"kind": "release_all"}])
        session.post_events([{"kind": "joystick", "port": 2, "inputs": ["fire", "fire2", "fire3"], "transition": "press"}])
        assert_joystick_ports(session, 0x1F, 0x0F)
        assert_input_state(session, [], [], ["fire", "fire2", "fire3"])
        session.post_events([{"kind": "joystick", "port": 2, "inputs": ["fire2"], "transition": "release"}])
        assert_joystick_ports(session, 0x1F, 0x0F)
        assert_input_state(session, [], [], ["fire", "fire3"])

    with check("joystick release_all then press in same batch is visible on CIA reads"):
        session.post_events(
            [
                {"kind": "joystick", "port": 1, "inputs": ["up", "fire"], "transition": "press"},
                {"kind": "joystick", "port": 2, "inputs": ["down"], "transition": "press"},
                {"kind": "release_all"},
                {"kind": "joystick", "port": 2, "inputs": ["left"], "transition": "press"},
            ]
        )
        assert_joystick_ports(session, 0x1F, 0x1B)
        assert_input_state(session, [], [], ["left"])

    with check("joystick unusual combination is visible on CIA reads"):
        session.post_events([{"kind": "release_all"}])
        session.post_events([{"kind": "joystick", "port": 1, "inputs": ["up", "down"], "transition": "press"}])
        assert_joystick_ports(session, 0x1C, 0x1F)

    with check("joystick tap does not release persistent input"):
        session.post_events([{"kind": "release_all"}])
        response = session.post_events(
            [
                {"kind": "joystick", "port": 2, "inputs": ["up"], "transition": "press"},
                {"kind": "joystick", "port": 2, "inputs": ["fire"], "transition": "tap"},
            ]
        )
        # A tap is released by the firmware on its own timer, and the response
        # is built after the batch has been applied, so whether the tap is
        # still in it depends on how long the request took. Measured on a C64
        # Ultimate, the same batch came back as ['up'] once in nine runs and as
        # ['up', 'fire'] the rest of the time. What this check is about is that
        # the tap does not take the persistent press down with it, and that is
        # what the reads below assert.
        immediate = response["joysticks"][1]["inputs"]
        if immediate not in (["up", "fire"], ["up"]):
            raise Failure(f"Expected immediate persistent/tap state, got {response}")
        time.sleep(0.2)
        assert_joystick_ports(session, 0x1F, 0x1E)
        assert_input_state(session, [], [], ["up"])

    with check("joystick tap auto releases"):
        session.post_events([{"kind": "release_all"}])
        session.post_events([{"kind": "joystick", "port": 2, "inputs": ["left"], "transition": "tap"}])
        time.sleep(0.2)
        assert_joystick_ports(session, 0x1F, 0x1F)
        if session.get_state()["joysticks"][1]["inputs"] != []:
            raise Failure(f"Expected port 2 state empty, got {session.get_state()}")

    with check("invalid joystick batch does not mutate state"):
        session.post_events([{"kind": "release_all"}])
        session.post_events([{"kind": "joystick", "port": 2, "inputs": ["fire"], "transition": "press"}])
        body = session.post_events_expect_error(
            [
                {"kind": "release_all"},
                {"kind": "joystick", "port": 1, "inputs": ["jump"], "transition": "press"},
            ]
        )
        assert_error_body_only(body)
        assert_input_state(session, [], [], ["fire"])
        assert_joystick_ports(session, 0x1F, 0x0F)
        session.post_events([{"kind": "release_all"}])

    with check("joystick release_all clears both ports"):
        session.post_events(
            [
                {"kind": "joystick", "port": 1, "inputs": ["up"], "transition": "press"},
                {"kind": "joystick", "port": 2, "inputs": ["fire"], "transition": "press"},
                {"kind": "release_all"},
            ]
        )
        assert_joystick_ports(session, 0x1F, 0x1F)
        assert_state_empty(session)

    with check("machine reset clears keyboard and joystick REST state"):
        session.post_events(
            [
                {"kind": "keyboard", "inputs": ["ctrl"], "transition": "press"},
                {"kind": "joystick", "port": 1, "inputs": ["up"], "transition": "press"},
            ]
        )
        reset_to_basic(session)
        assert_state_empty(session)


def run_tests(session: RestInputSession, soak_duration_seconds: float | None = None, selected: list[str] | None = None) -> int:
    wait_for_input_ready(session, timeout=15.0)
    reset_to_basic(session)
    if soak_duration_seconds is not None:
        # The soak asserts the same CIA ports the joystick checks do.
        with unswapped_joystick_ports(session):
            return run_soak_tests(session, soak_duration_seconds)
    if wants_test(selected, "contract"):
        run_contract_tests(session)
    if wants_test(selected, "joystick"):
        run_joystick_tests(session)
    if wants_test(selected, "keyboard"):
        run_keyboard_tests(session)
    if wants_keyboard_echo_tests(selected):
        run_keyboard_echo_tests(session, selected=selected)
    if wants_menu_tests(selected):
        menu_selected = selected if selected and "menu" not in selected and "all" not in selected else None
        run_menu_keyboard_tests(session, selected=menu_selected)
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Validate U64 keyboard and joystick REST input injection",
        epilog="Use --soak to continue with expanded long-run REST input coverage after the standard checks.",
    )
    cli.add_device_arguments(parser, password=None, timeout=5.0, colour=False)
    parser.add_argument("-r", "--rest-host", default=os.environ.get("U64_REST_HOST"))
    parser.add_argument("-s", "--soak", action="store_true", help="run the expanded soak suite after the standard checks")
    parser.add_argument(
        "--test",
        action="append",
        choices=TEST_CHOICES,
        help="run one suite or menu subtest; repeat for multiple selections",
    )
    parser.add_argument(
        "-d",
        "--soak-duration",
        default="5m",
        help="how long the soak suite should run (default: %(default)s)",
    )
    args = parser.parse_args()

    rest_host = args.rest_host or args.host
    session = RestInputSession(rest_host, args.password, args.timeout)
    selected_tests = None if not args.test else args.test
    soak_duration_seconds = cli.parse_duration(args.soak_duration) if args.soak else None
    if args.soak and selected_tests is not None:
        suite_fail("input_test", "--test cannot be combined with --soak")
        return 2
    try:
        soak_cycles = run_tests(session, soak_duration_seconds=soak_duration_seconds, selected=selected_tests)
    except Failure as exc:
        suite_fail("input_test", str(exc))
        return 1
    except (OSError, TimeoutError, urllib.error.URLError) as exc:
        if rest_lib.looks_unreachable(exc):
            suite_fail("input_test", f"connection failure: {format_exception(exc)}")
        else:
            suite_fail("input_test", f"REST failure: {format_exception(exc)}")
        return 1

    if soak_duration_seconds is not None:
        suite_ok("input_test", f"{check_count()} checks, {soak_cycles} soak cycles")
    else:
        suite_ok("input_test")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
