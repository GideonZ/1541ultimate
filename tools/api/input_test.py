#!/usr/bin/env python3
import argparse
import http.client
import json
import os
import re
import socket
import struct
import sys
import time
import urllib.error
import urllib.parse
import urllib.request
from collections import Counter
from contextlib import contextmanager
from pathlib import Path
from typing import Any, Dict, List, Optional, Tuple

from PIL import Image

CHECK_COUNT = 0
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
BASIC_INPUT_SETTLE_SECONDS = float(os.environ.get("U64_INPUT_BASIC_SETTLE", "3.0"))
MENU_KEY_SETTLE_SECONDS = float(os.environ.get("U64_INPUT_MENU_KEY_SETTLE", "0.30"))
MENU_HOLD_SECONDS = float(os.environ.get("U64_INPUT_MENU_HOLD_SECONDS", "1.2"))
MENU_POST_RELEASE_SETTLE_SECONDS = float(os.environ.get("U64_INPUT_MENU_POST_RELEASE_SETTLE", "0.35"))
MENU_NAV_PREPARE_SETTLE_SECONDS = float(os.environ.get("U64_INPUT_MENU_NAV_PREPARE_SETTLE", "0.15"))
MENU_NAV_OPEN_SETTLE_SECONDS = float(os.environ.get("U64_INPUT_MENU_NAV_OPEN_SETTLE", "0.75"))
MENU_NAV_STEP_SETTLE_SECONDS = float(os.environ.get("U64_INPUT_MENU_NAV_STEP_SETTLE", "0.08"))
MENU_NAV_SELECT_SETTLE_SECONDS = float(os.environ.get("U64_INPUT_MENU_NAV_SELECT_SETTLE", "0.45"))
MENU_EDITOR_OPEN_SETTLE_SECONDS = float(os.environ.get("U64_INPUT_MENU_EDITOR_OPEN_SETTLE", "0.70"))
MENU_EDITOR_SAVE_SETTLE_SECONDS = float(os.environ.get("U64_INPUT_MENU_EDITOR_SAVE_SETTLE", "0.25"))
MENU_EXIT_SETTLE_SECONDS = float(os.environ.get("U64_INPUT_MENU_EXIT_SETTLE", "0.25"))
MENU_RESET_SETTLE_SECONDS = float(os.environ.get("U64_INPUT_MENU_RESET_SETTLE", "0.75"))
MENU_CONFIG_SETTLE_SECONDS = float(os.environ.get("U64_INPUT_MENU_CONFIG_SETTLE", "0.20"))
MENU_SHIFT_BATCH_SETTLE_SECONDS = float(os.environ.get("U64_INPUT_MENU_SHIFT_BATCH_SETTLE", "0.30"))
MENU_TYPE_SETTLE_SECONDS = float(os.environ.get("U64_INPUT_MENU_TYPE_SETTLE", "0.25"))
MENU_VIDEO_TIMEOUT_SECONDS = float(os.environ.get("U64_INPUT_MENU_VIDEO_TIMEOUT", "6.0"))
MULTICAST_GROUP = "239.0.1.64"
VIDEO_PORT = 11000
MENU_EVIDENCE_DIR = os.environ.get("U64_INPUT_MENU_EVIDENCE_DIR")
MODEM_SETTINGS_CATEGORY = "Modem Settings"
MODEM_OFFLINE_TEXT_ITEM = "Modem Offline Text"
DEFAULT_MODEM_OFFLINE_TEXT = "/flash/offline.txt"
FONT_PATH = Path(__file__).resolve().parents[2] / "roms" / "chars.bin"
FONT_BYTES = FONT_PATH.read_bytes()[: 256 * 8]
PRINTABLE_FALLBACK = {
    0x00: " ",
}
KEYBOARD_MATRIX: Dict[str, Tuple[int, int]] = {
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


class Failure(RuntimeError):
    pass


@contextmanager
def check(label: str):
    global CHECK_COUNT
    CHECK_COUNT += 1
    print(f"[{CHECK_COUNT:02d}] {label} ... ", end="", flush=True)
    try:
        yield
    except Exception:
        print("FAIL", flush=True)
        raise
    print("OK", flush=True)


def format_exception(exc: BaseException) -> str:
    if isinstance(exc, urllib.error.URLError) and getattr(exc, "reason", None) is not None:
        return f"{exc} ({exc.reason})"
    return str(exc)


def wants_test(selected: Optional[List[str]], name: str) -> bool:
    return selected is None or "all" in selected or name in selected


def wants_menu_tests(selected: Optional[List[str]]) -> bool:
    return wants_test(selected, "menu") or any(item.startswith("menu-") for item in selected or [])


def wants_keyboard_echo_tests(selected: Optional[List[str]]) -> bool:
    return any(item.startswith("keyboard-echo-") for item in selected or [])


def device_unavailable(exc: BaseException) -> bool:
    text = format_exception(exc).lower()
    markers = (
        "no route to host",
        "network is unreachable",
        "connection refused",
        "timed out",
        "temporary failure in name resolution",
        "not found",
    )
    return any(marker in text for marker in markers)


class RestInputSession:
    def __init__(self, host: str, password: Optional[str], timeout: float) -> None:
        self.host = host
        self.password = password
        self.timeout = timeout

    def url(self, path: str, params: Optional[Dict[str, Any]] = None) -> str:
        query = ""
        if params:
            query = "?" + urllib.parse.urlencode(params)
        return f"http://{self.host}{path}{query}"

    def request(
        self,
        method: str,
        path: str,
        params: Optional[Dict[str, Any]] = None,
        body: Optional[bytes] = None,
        content_type: Optional[str] = "application/json",
    ) -> bytes:
        headers = {}
        if self.password:
            headers["X-Password"] = self.password
        if body is not None and content_type is not None:
            headers["Content-Type"] = content_type
        request = urllib.request.Request(self.url(path, params), data=body, headers=headers, method=method)
        with urllib.request.urlopen(request, timeout=self.timeout) as response:
            return response.read()

    def json_request(
        self,
        method: str,
        path: str,
        params: Optional[Dict[str, Any]] = None,
        payload: Optional[Dict[str, Any]] = None,
    ) -> Dict[str, Any]:
        body = None if payload is None else json.dumps(payload).encode("utf-8")
        data = self.request(method, path, params=params, body=body)
        return json.loads(data.decode("utf-8"))

    def get_state(self) -> Dict[str, Any]:
        return self.json_request("GET", "/v1/machine:input")

    def post_events(self, events: List[Dict[str, Any]]) -> Dict[str, Any]:
        return self.json_request("POST", "/v1/machine:input", payload={"events": events})

    def post_payload_expect_error(self, payload: Dict[str, Any]) -> Dict[str, Any]:
        try:
            self.json_request("POST", "/v1/machine:input", payload=payload)
        except urllib.error.HTTPError as exc:
            if exc.code != 400:
                raise
            return json.loads(exc.read().decode("utf-8"))
        raise Failure("Expected HTTP 400, but request succeeded")

    def post_events_expect_error(self, events: List[Dict[str, Any]]) -> Dict[str, Any]:
        return self.post_payload_expect_error({"events": events})

    def post_raw_expect_error(self, body: bytes, content_type: Optional[str]) -> Dict[str, Any]:
        try:
            self.request("POST", "/v1/machine:input", body=body, content_type=content_type)
        except urllib.error.HTTPError as exc:
            if exc.code != 400:
                raise
            return json.loads(exc.read().decode("utf-8"))
        raise Failure("Expected HTTP 400, but request succeeded")

    def post_without_body_expect_error(self, expected_code: int = 412) -> Dict[str, Any]:
        headers: Dict[str, str] = {"Content-Type": "application/json"}
        if self.password:
            headers["X-Password"] = self.password
        connection = None
        try:
            connection = http.client.HTTPConnection(self.host, timeout=self.timeout)
            connection.request("POST", "/v1/machine:input", body=b"", headers=headers)
            response = connection.getresponse()
            body = response.read()
            if response.status != expected_code:
                raise Failure(f"Expected HTTP {expected_code}, got HTTP {response.status}")
            return json.loads(body.decode("utf-8"))
        except http.client.HTTPException as exc:
            raise Failure(f"HTTP client failure: {exc}") from exc
        finally:
            try:
                if connection is not None:
                    connection.close()
            except Exception:
                pass

    def put(self, command: str) -> None:
        self.request("PUT", f"/v1/machine:{command}")

    def start_video_stream(self, ip: str = MULTICAST_GROUP, port: int = VIDEO_PORT) -> None:
        self.request("PUT", "/v1/streams/video:start", params={"ip": f"{ip}:{port}"})

    def stop_video_stream(self) -> None:
        self.request("PUT", "/v1/streams/video:stop")

    def reset(self) -> None:
        self.put("reset")

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
    def __init__(self, image: Image.Image, lines: List[str], codes: List[List[int]], bbox: Tuple[int, int, int, int]) -> None:
        self.image = image
        self.lines = lines
        self.codes = codes
        self.bbox = bbox

    def text(self) -> str:
        return "\n".join(self.lines)

    def contains(self, needle: str) -> bool:
        needle_upper = needle.upper()
        return any(needle_upper in line for line in self.lines)


class VicStreamCapture:
    def __init__(self) -> None:
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.sock.settimeout(2.0)
        self.sock.bind(("", VIDEO_PORT))
        group = socket.inet_aton(MULTICAST_GROUP)
        membership = struct.pack("4sL", group, socket.INADDR_ANY)
        self.sock.setsockopt(socket.IPPROTO_IP, socket.IP_ADD_MEMBERSHIP, membership)

    def close(self) -> None:
        try:
            self.sock.close()
        except OSError:
            pass

    def _drain_partial_frame(self) -> None:
        while True:
            data, _ = self.sock.recvfrom(1024)
            _, _, line, _ = struct.unpack("<HHHH", data[0:8])
            if line & 0x8000:
                return

    def capture_image(self) -> Image.Image:
        for _ in range(8):
            self._drain_partial_frame()
            raw = bytearray()
            while True:
                data, _ = self.sock.recvfrom(1024)
                _, _, line, _ = struct.unpack("<HHHH", data[0:8])
                raw.extend(data[12:])
                if line & 0x8000:
                    break

            lines = len(raw) // 192
            if lines < 180:
                continue

            image = Image.new("P", (384, lines))
            i = 0
            for y in range(lines):
                for x in range(192):
                    value = raw[i]
                    image.putpixel((2 * x, y), value & 0x0F)
                    image.putpixel((2 * x + 1, y), value >> 4)
                    i += 1
            return image
        raise Failure("Did not receive a complete VIC frame.")


class C64FrameOCR:
    def __init__(self) -> None:
        self.font = FONT_BYTES

    def _cell_mask(self, cell: Image.Image) -> List[int]:
        bg = Counter(cell.getdata()).most_common(1)[0][0]
        mask = []
        for y in range(8):
            row = 0
            for x in range(8):
                if cell.getpixel((x, y)) != bg:
                    row |= (1 << (7 - x))
            mask.append(row)
        return mask

    def _best_match(self, mask: List[int]) -> Tuple[int, int]:
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

    def _active_bbox(self, image: Image.Image) -> Tuple[int, int, int, int]:
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
        lines: List[str] = []
        codes: List[List[int]] = []
        for row in range(25):
            chars: List[str] = []
            row_codes: List[int] = []
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
    last_error: Optional[BaseException] = None
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
    session.post_events([{"kind": "release_all"}])
    session.post_events([{"kind": "keyboard", "inputs": ["run_stop"], "transition": "tap"}])
    time.sleep(0.3)
    session.reset()
    wait_for_basic_ready(session)
    session.post_events([{"kind": "release_all"}])


def reset_to_basic_for_keyboard_input(session: RestInputSession) -> None:
    reset_to_basic(session)
    time.sleep(BASIC_INPUT_SETTLE_SECONDS)


def reset_to_basic_for_menu_navigation(session: RestInputSession) -> None:
    reset_to_basic(session)
    time.sleep(MENU_RESET_SETTLE_SECONDS)


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


def find_cursor_row(session: RestInputSession) -> Optional[int]:
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


def assert_state_empty(session: RestInputSession) -> None:
    state = session.get_state()
    if state.get("keyboard", {}).get("inputs") != []:
        raise Failure(f"Expected empty keyboard state, got {state}")
    if state.get("joysticks") != [{"port": 1, "inputs": []}, {"port": 2, "inputs": []}]:
        raise Failure(f"Expected empty joystick state, got {state}")


def assert_error_body_only(body: Dict[str, Any]) -> None:
    if not body.get("errors"):
        raise Failure(f"Expected validation errors, got {body}")
    if "keyboard" in body or "joysticks" in body:
        raise Failure(f"Error response must not include state snapshots, got {body}")


def assert_input_state(
    session: RestInputSession,
    keyboard: List[str],
    joystick1: List[str],
    joystick2: List[str],
) -> None:
    state = session.get_state()
    if state.get("errors") != []:
        raise Failure(f"Expected no errors in state snapshot, got {state}")
    if state.get("keyboard", {}).get("inputs") != keyboard:
        raise Failure(f"Keyboard state mismatch; expected {keyboard}, got {state}")
    if state.get("joysticks") != [{"port": 1, "inputs": joystick1}, {"port": 2, "inputs": joystick2}]:
        raise Failure(f"Joystick state mismatch; expected {joystick1}/{joystick2}, got {state}")


def read_joystick_cia(session: RestInputSession) -> Tuple[int, int]:
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


def read_joystick_pots(session: RestInputSession, port: int) -> Tuple[int, int]:
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


def assert_keyboard_matrix_inputs(session: RestInputSession, inputs: List[str]) -> None:
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


def keyboard_tap_events_for_text(text: str) -> List[Dict[str, Any]]:
    events: List[Dict[str, Any]] = []
    for ch in text:
        if "a" <= ch <= "z" or "0" <= ch <= "9":
            events.append({"kind": "keyboard", "inputs": [ch], "transition": "tap"})
        elif "A" <= ch <= "Z":
            events.append({"kind": "keyboard", "inputs": ["left_shift", ch.lower()], "transition": "tap"})
        else:
            raise Failure(f"Unsupported keyboard tap text {text!r}")
    return events


def keyboard_press_inputs_for_char(ch: str) -> List[str]:
    if "a" <= ch <= "z" or "0" <= ch <= "9":
        return [ch]
    if "A" <= ch <= "Z":
        return ["left_shift", ch.lower()]
    raise Failure(f"Unsupported keyboard press text {ch!r}")


def keyboard_release_inputs_for_char(ch: str) -> List[str]:
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
    for ch in text:
        now = time.monotonic()
        if now < next_send:
            time.sleep(next_send - now)
        session.post_events(keyboard_tap_events_for_text(ch))
        next_send += interval
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


def run_keyboard_echo_stress_case(session: RestInputSession, text: str, hz: float) -> None:
    try:
        reset_to_basic_for_keyboard_input(session)
        offset = start_keyboard_echo_program(session)
        post_keyboard_text_at_rate(session, text, hz)
        wait_for_keyboard_echo_sequence(session, text, offset, timeout=max(6.0, len(text) * 0.25))
        time.sleep(0.25)
        assert_state_empty(session)
    finally:
        try:
            session.post_events([{"kind": "release_all"}])
        except Exception:
            pass


def keyboard_tap_events_for_menu_text(text: str) -> List[Dict[str, Any]]:
    punctuation = {
        "/": "slash",
        ".": "period",
        "-": "minus",
        ":": "colon",
    }
    events: List[Dict[str, Any]] = []
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


def ensure_menu_evidence_dir() -> Optional[str]:
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


def capture_menu_frame(capture: VicStreamCapture, ocr: C64FrameOCR, tag: Optional[str] = None) -> FrameText:
    frame = ocr.decode(capture.capture_image())
    if tag is not None:
        save_menu_frame(tag, frame)
    return frame


def wait_for_menu_text(capture: VicStreamCapture, ocr: C64FrameOCR, needles: List[str], timeout: float, tag: str) -> FrameText:
    deadline = time.monotonic() + timeout
    last_frame: Optional[FrameText] = None
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


def frame_cell_mask(frame: FrameText, ocr: C64FrameOCR, row: int, col: int) -> List[int]:
    left, top, _, _ = frame.bbox
    cell = frame.image.crop((left + (col * 8), top + (row * 8), left + ((col + 1) * 8), top + ((row + 1) * 8)))
    return ocr._cell_mask(cell)


def first_mask_position(frame: FrameText, ocr: C64FrameOCR, row: int, start_col: int, width: int, expected_mask: List[int]) -> int:
    for col in range(start_col, start_col + width):
        if frame_cell_mask(frame, ocr, row, col) == expected_mask:
            return col
    raise Failure("Expected glyph mask not found in decoded frame.")


def parse_duration_seconds(text: str) -> float:
    match = re.fullmatch(r"\s*(\d+(?:\.\d+)?)\s*([smhSMH]?)\s*", text)
    if not match:
        raise Failure(f"Unsupported duration {text!r}; use values like 30, 45s, 5m, or 1.5h")
    value = float(match.group(1))
    unit = match.group(2).lower()
    scale = {"": 1.0, "s": 1.0, "m": 60.0, "h": 3600.0}[unit]
    return value * scale


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


def soak_keyboard_hold_case(session: RestInputSession, persistent_inputs: List[str], tap_inputs: List[str]) -> None:
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


def soak_joystick_case(session: RestInputSession, port: int, pressed_inputs: List[str], release_inputs: List[str]) -> None:
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
    joystick_inputs: List[str],
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


def menu_keyboard_tap(session: RestInputSession, inputs: List[str], settle: float = MENU_KEY_SETTLE_SECONDS) -> Dict[str, Any]:
    response = session.post_events([{"kind": "keyboard", "inputs": inputs, "transition": "tap"}])
    time.sleep(settle)
    return response


def menu_keyboard_transition(session: RestInputSession, inputs: List[str], transition: str, settle: float = MENU_KEY_SETTLE_SECONDS) -> Dict[str, Any]:
    response = session.post_events([{"kind": "keyboard", "inputs": inputs, "transition": transition}])
    time.sleep(settle)
    return response


def menu_keyboard_f2_tap(session: RestInputSession, settle: float = MENU_KEY_SETTLE_SECONDS) -> Dict[str, Any]:
    # The REST API uses C64 matrix names: F2 is shifted F1.
    return menu_keyboard_tap(session, ["left_shift", "f1"], settle)


def close_menu_keyboard(session: RestInputSession) -> None:
    for _ in range(4):
        menu_keyboard_tap(session, ["run_stop"], 0.35)
    session.post_events([{"kind": "release_all"}])


def menu_move_to_top(session: RestInputSession, settle: float = 0.05, count: int = 32) -> None:
    for _ in range(count):
        menu_keyboard_tap(session, ["left_shift", "cursor_up_down"], settle)


def menu_move_down(session: RestInputSession, count: int, settle: float = 0.05) -> None:
    for _ in range(count):
        menu_keyboard_tap(session, ["cursor_up_down"], settle)


def clear_menu_editor_field(session: RestInputSession, settle: float = 0.8) -> None:
    delete_events = [{"kind": "keyboard", "inputs": ["inst_del"], "transition": "tap"} for _ in range(32)]
    session.post_events(delete_events)
    time.sleep(settle)


def type_menu_editor_text(session: RestInputSession, text: str, settle: float = 0.6) -> None:
    session.post_events(keyboard_tap_events_for_menu_text(text))
    time.sleep(settle)


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


def read_offline_text_field(session: RestInputSession) -> str:
    return get_config_value(session, MODEM_SETTINGS_CATEGORY, MODEM_OFFLINE_TEXT_ITEM)


def save_offline_text_evidence(tag: str, value: str) -> None:
    save_menu_note(tag, f"{MODEM_OFFLINE_TEXT_ITEM}: {value}")


def navigate_to_modem_offline_text_editor(session: RestInputSession) -> None:
    session.post_events([{"kind": "release_all"}])
    time.sleep(MENU_NAV_PREPARE_SETTLE_SECONDS)
    session.put("menu_button")
    time.sleep(MENU_NAV_OPEN_SETTLE_SECONDS)
    menu_keyboard_f2_tap(session, MENU_NAV_SELECT_SETTLE_SECONDS)
    menu_move_to_top(session, settle=MENU_NAV_STEP_SETTLE_SECONDS, count=20)
    menu_move_down(session, 19, settle=MENU_NAV_STEP_SETTLE_SECONDS)
    menu_keyboard_tap(session, ["return"], MENU_NAV_SELECT_SETTLE_SECONDS)
    menu_move_to_top(session, settle=MENU_NAV_STEP_SETTLE_SECONDS, count=20)
    menu_move_down(session, 11, settle=MENU_NAV_STEP_SETTLE_SECONDS)
    menu_keyboard_tap(session, ["return"], MENU_EDITOR_OPEN_SETTLE_SECONDS)


def read_menu_editor_window(frame: FrameText, row: int, col: int, width: int = 20) -> str:
    return frame.lines[row][col:col + width]


def save_menu_editor_and_read(session: RestInputSession, tag: str) -> str:
    menu_keyboard_tap(session, ["return"], MENU_EDITOR_SAVE_SETTLE_SECONDS)
    menu_keyboard_tap(session, ["run_stop"], MENU_EXIT_SETTLE_SECONDS)
    menu_keyboard_tap(session, ["run_stop"], MENU_EXIT_SETTLE_SECONDS)
    menu_keyboard_tap(session, ["return"], 0.40)
    session.post_events([{"kind": "release_all"}])
    time.sleep(MENU_EXIT_SETTLE_SECONDS)
    value = read_offline_text_field(session)
    save_offline_text_evidence(tag, value)
    assert_state_empty(session)
    return value


def restore_offline_text_field(session: RestInputSession) -> None:
    set_config_value(session, MODEM_SETTINGS_CATEGORY, MODEM_OFFLINE_TEXT_ITEM, DEFAULT_MODEM_OFFLINE_TEXT)
    save_offline_text_evidence("menu_editor_restored", DEFAULT_MODEM_OFFLINE_TEXT)


def open_modem_offline_text_editor(session: RestInputSession, initial_value: str) -> None:
    reset_to_basic_for_menu_navigation(session)
    assert_state_empty(session)
    set_config_value(session, MODEM_SETTINGS_CATEGORY, MODEM_OFFLINE_TEXT_ITEM, initial_value)
    time.sleep(MENU_CONFIG_SETTLE_SECONDS)
    navigate_to_modem_offline_text_editor(session)


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


def soak_rapid_mixed_case(session: RestInputSession, screen_tail: str, text_chunks: List[str], joystick_inputs: List[str]) -> str:
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

        print(f"[soak {cycles + 1:03d}] text={text_case} joy{joystick_case[0]}={'+'.join(joystick_case[1])}", flush=True)
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
    with check("keyboard single letter reaches the live C64 matrix"):
        reset_to_basic_for_keyboard_input(session)
        session.post_events([{"kind": "keyboard", "inputs": ["l"], "transition": "press"}])
        assert_keyboard_matrix_inputs(session, ["l"])
        session.post_events([{"kind": "release_all"}])
        assert_state_empty(session)

    with check("keyboard shifted pair reaches the live C64 matrix"):
        reset_to_basic_for_keyboard_input(session)
        session.post_events([{"kind": "keyboard", "inputs": ["left_shift", "a"], "transition": "press"}])
        assert_keyboard_matrix_inputs(session, ["left_shift", "a"])
        session.post_events([{"kind": "release_all"}])
        assert_state_empty(session)

    with check("keyboard batch applies multiple presses atomically"):
        reset_to_basic_for_keyboard_input(session)
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
        reset_to_basic_for_keyboard_input(session)
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
        reset_to_basic_for_keyboard_input(session)
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
        reset_to_basic_for_keyboard_input(session)
        inputs = ["a", "s", "d", "f", "j", "k", "l", "space"]
        session.post_events([{"kind": "keyboard", "inputs": inputs, "transition": "press"}])
        assert_input_state(session, ["a", "s", "d", "f", "j", "k", "l", "space"], [], [])
        session.post_events([{"kind": "release_all"}])
        assert_state_empty(session)

    with check("keyboard tap does not release persistent key"):
        reset_to_basic_for_keyboard_input(session)
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
        reset_to_basic_for_keyboard_input(session)
        session.post_events([{"kind": "keyboard", "inputs": ["left_shift"], "transition": "press"}])
        session.post_events([{"kind": "release_all"}])
        assert_state_empty(session)

    with check("keyboard restore tap auto releases"):
        reset_to_basic_for_keyboard_input(session)
        session.post_events([{"kind": "keyboard", "inputs": ["restore"], "transition": "tap"}])
        time.sleep(0.2)
        assert_state_empty(session)

    with check("keyboard special-key taps snapshot correctly and auto release"):
        for inputs in (["commodore"], ["ctrl"], ["run_stop"], ["restore"], ["f1"], ["f3"], ["f5"], ["f7"], ["left_shift"], ["right_shift"]):
            reset_to_basic_for_keyboard_input(session)
            response = session.post_events([{"kind": "keyboard", "inputs": inputs, "transition": "tap"}])
            if sorted(response.get("keyboard", {}).get("inputs", [])) != sorted(inputs):
                raise Failure(f"Expected immediate special-key tap snapshot for {inputs}, got {response}")
            time.sleep(0.2)
            assert_state_empty(session)

    with check("keyboard tap is visible in the live hardware snapshot and auto releases"):
        reset_to_basic_for_keyboard_input(session)
        response = session.post_events([{"kind": "keyboard", "inputs": ["a"], "transition": "tap"}])
        if response.get("keyboard", {}).get("inputs") != ["a"]:
            raise Failure(f"Expected immediate tap snapshot for a, got {response}")
        time.sleep(0.2)
        assert_state_empty(session)
        session.post_events([{"kind": "release_all"}])

    with check("keyboard single-tap batch is consumed by BASIC in order"):
        reset_to_basic_for_keyboard_input(session)
        session.json_request(
            "POST",
            "/v1/machine:input",
            payload={"events": keyboard_tap_events_for_text("aaaaaa")},
        )
        wait_for_basic_input_prefix(session, "AAAAAA", timeout=4.0)
        time.sleep(0.3)
        assert_state_empty(session)
        session.post_events([{"kind": "release_all"}])

    with check("keyboard cursor-left tap is visible in the live hardware snapshot and auto releases"):
        reset_to_basic_for_keyboard_input(session)
        response = session.post_events([{"kind": "keyboard", "inputs": ["left_shift", "cursor_left_right"], "transition": "tap"}])
        if sorted(response.get("keyboard", {}).get("inputs", [])) != ["cursor_left_right", "left_shift"]:
            raise Failure(f"Expected immediate cursor-left tap snapshot, got {response}")
        time.sleep(0.2)
        assert_state_empty(session)
        session.post_events([{"kind": "release_all"}])

    with check("keyboard tap batch drains through the live matrix path"):
        reset_to_basic_for_keyboard_input(session)
        response = session.json_request("POST", "/v1/machine:input", payload={"events": keyboard_tap_events_for_text("ABCDEFGHIJ")})
        if not response.get("keyboard", {}).get("inputs"):
            raise Failure(f"Expected a live tap snapshot while the batch was draining, got {response}")
        time.sleep(1.2)
        assert_state_empty(session)
        session.post_events([{"kind": "release_all"}])

    with check("keyboard long repeated tap train drains fully without sticky state"):
        reset_to_basic_for_keyboard_input(session)
        repeated = [{"kind": "keyboard", "inputs": ["a"], "transition": "tap"} for _ in range(60)]
        response = session.json_request("POST", "/v1/machine:input", payload={"events": repeated})
        if response.get("keyboard", {}).get("inputs") != ["a"]:
            raise Failure(f"Expected repeated tap train to expose the live a snapshot, got {response}")
        time.sleep(6.0)
        assert_state_empty(session)
        session.post_events([{"kind": "release_all"}])

    with check("keyboard 10 Hz mixed alphabet echo has no missed presses"):
        run_keyboard_echo_stress_case(session, mixed_alphabet_text(200), 10.0)

    with check("keyboard 20 Hz alternating ab echo has no missed presses"):
        run_keyboard_echo_stress_case(session, alternating_text("a", "b", 100), 20.0)

    with check("keyboard 5 Hz alternating ab echo has no missed presses"):
        run_keyboard_echo_stress_case(session, alternating_text("a", "b", 20), 5.0)

    with check("invalid keyboard batch does not mutate state"):
        reset_to_basic(session)
        body = session.post_events_expect_error(
            [
                {"kind": "keyboard", "inputs": ["left_shift"], "transition": "press"},
                {"kind": "keyboard", "inputs": ["escape"], "transition": "tap"},
            ]
        )
        assert_error_body_only(body)
        assert_state_empty(session)


def run_keyboard_echo_tests(session: RestInputSession, selected: Optional[List[str]] = None) -> None:
    if wants_test(selected, "keyboard-echo-alphabet"):
        with check("keyboard 10 Hz mixed alphabet echo has no missed presses"):
            run_keyboard_echo_stress_case(session, mixed_alphabet_text(200), 10.0)

    if wants_test(selected, "keyboard-echo-ab-20hz"):
        with check("keyboard 20 Hz alternating ab echo has no missed presses"):
            run_keyboard_echo_stress_case(session, alternating_text("a", "b", 100), 20.0)

    if wants_test(selected, "keyboard-echo-ab-5hz"):
        with check("keyboard 5 Hz alternating ab echo has no missed presses"):
            run_keyboard_echo_stress_case(session, alternating_text("a", "b", 20), 5.0)


def run_menu_keyboard_tests(session: RestInputSession, selected: Optional[List[str]] = None) -> None:
    reset_to_basic_for_menu_navigation(session)
    session.post_events([{"kind": "release_all"}])
    assert_state_empty(session)
    original_offline_text = read_offline_text_field(session)
    save_offline_text_evidence("menu_original_value", original_offline_text)
    try:
        if wants_test(selected, "menu-shift"):
            with check("menu editor keeps separate-batch shift active across POSTs"):
                open_modem_offline_text_editor(session, "")
                menu_keyboard_tap(session, ["a"], MENU_TYPE_SETTLE_SECONDS)
                lower_value = save_menu_editor_and_read(session, "menu_lowercase_control")
                if lower_value != "a":
                    raise Failure(f"Lowercase control write produced {lower_value!r} instead of 'a'.")

                open_modem_offline_text_editor(session, "")
                menu_keyboard_transition(session, ["left_shift"], "press", MENU_SHIFT_BATCH_SETTLE_SECONDS)
                menu_keyboard_tap(session, ["a"], MENU_SHIFT_BATCH_SETTLE_SECONDS)
                menu_keyboard_transition(session, ["left_shift"], "release", MENU_SHIFT_BATCH_SETTLE_SECONDS)
                separate_value = save_menu_editor_and_read(session, "menu_shift_separate_batches")
                if separate_value != "A":
                    raise Failure(f"Separate-batch shift write produced {separate_value!r} instead of 'A'.")

        if wants_test(selected, "menu-repeat-printable"):
            with check("menu editor repeats held printable keys and stops after release"):
                open_modem_offline_text_editor(session, "")
                menu_keyboard_transition(session, ["c"], "press", 0.1)
                time.sleep(MENU_HOLD_SECONDS)
                menu_keyboard_transition(session, ["c"], "release", 0.1)
                time.sleep(MENU_POST_RELEASE_SETTLE_SECONDS)
                repeated_value = save_menu_editor_and_read(session, "menu_printable_repeat")
                if len(repeated_value) < 2 or set(repeated_value) != {"c"}:
                    raise Failure(f"Held printable key produced {repeated_value!r} instead of a repeated run of 'c'.")

                open_modem_offline_text_editor(session, "")
                menu_keyboard_transition(session, ["c"], "press", 0.1)
                time.sleep(MENU_HOLD_SECONDS)
                menu_keyboard_transition(session, ["c"], "release", 0.1)
                time.sleep(0.60)
                stopped_value = save_menu_editor_and_read(session, "menu_printable_repeat_stopped")
                if stopped_value != repeated_value:
                    raise Failure("Printable repeat continued after release.")

        if wants_test(selected, "menu-repeat-cursor"):
            with check("menu editor repeats held cursor keys and stops after release"):
                open_modem_offline_text_editor(session, "ABCD")
                menu_keyboard_tap(session, ["left_shift", "cursor_left_right"], 0.2)
                time.sleep(MENU_POST_RELEASE_SETTLE_SECONDS)
                type_menu_editor_text(session, "Z", 0.4)
                control_value = save_menu_editor_and_read(session, "menu_cursor_single_tap")

                open_modem_offline_text_editor(session, "ABCD")
                menu_keyboard_transition(session, ["left_shift", "cursor_left_right"], "press", 0.1)
                time.sleep(MENU_HOLD_SECONDS)
                menu_keyboard_transition(session, ["left_shift", "cursor_left_right"], "release", 0.1)
                time.sleep(0.10)
                type_menu_editor_text(session, "Z", 0.4)
                short_value = save_menu_editor_and_read(session, "menu_cursor_repeat_short_wait")
                if short_value == control_value:
                    raise Failure("Held cursor-left matched the single-tap result; no repeated menu navigation was observed.")

                open_modem_offline_text_editor(session, "ABCD")
                menu_keyboard_transition(session, ["left_shift", "cursor_left_right"], "press", 0.1)
                time.sleep(MENU_HOLD_SECONDS)
                menu_keyboard_transition(session, ["left_shift", "cursor_left_right"], "release", 0.1)
                time.sleep(0.60)
                type_menu_editor_text(session, "Z", 0.4)
                long_value = save_menu_editor_and_read(session, "menu_cursor_repeat_long_wait")
                if long_value != short_value:
                    raise Failure("Cursor repeat continued to move after release.")
    finally:
        try:
            restore_offline_text_field(session)
        except Exception:
            pass
        reset_to_basic_for_keyboard_input(session)
        assert_state_empty(session)


def run_joystick_tests(session: RestInputSession) -> None:
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
        if response["joysticks"][1]["inputs"] != ["up", "fire"]:
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


def run_tests(session: RestInputSession, soak_duration_seconds: Optional[float] = None, selected: Optional[List[str]] = None) -> int:
    wait_for_input_ready(session, timeout=15.0)
    if soak_duration_seconds is not None:
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
    parser.add_argument("-H", "--host", default=os.environ.get("U64_INPUT_HOST", "u64"))
    parser.add_argument("-r", "--rest-host", default=os.environ.get("U64_INPUT_REST_HOST"))
    parser.add_argument("-p", "--password", default=os.environ.get("U64_INPUT_PASSWORD", os.environ.get("C64U_PASSWORD")))
    parser.add_argument("-t", "--timeout", type=float, default=float(os.environ.get("U64_INPUT_TIMEOUT", "5.0")))
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
    soak_duration_seconds = parse_duration_seconds(args.soak_duration) if args.soak else None
    if args.soak and selected_tests is not None:
        print("--test cannot be combined with --soak", file=sys.stderr)
        return 2
    try:
        soak_cycles = run_tests(session, soak_duration_seconds=soak_duration_seconds, selected=selected_tests)
    except Failure as exc:
        print(exc, file=sys.stderr)
        return 1
    except (OSError, TimeoutError, urllib.error.URLError) as exc:
        if device_unavailable(exc):
            print(f"Connection failure: {format_exception(exc)}", file=sys.stderr)
        else:
            print(f"REST failure: {format_exception(exc)}", file=sys.stderr)
        return 1

    if soak_duration_seconds is not None:
        print(f"input_test: OK ({CHECK_COUNT} checks, {soak_cycles} soak cycles)")
    else:
        print(f"input_test: OK ({CHECK_COUNT} checks)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
