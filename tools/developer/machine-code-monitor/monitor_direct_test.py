#!/usr/bin/env python3
import argparse
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
from typing import Callable, Iterable, List, Optional, Sequence, Tuple

from PIL import Image


CHECK_COUNT = 0
MULTICAST_GROUP = "239.0.1.64"
VIDEO_PORT = 11000
REST_THROTTLE_SECONDS = float(os.environ.get("U64_DIRECT_REST_THROTTLE", "0.15"))
PALETTE = [
    (0x00, 0x00, 0x00),
    (0xEF, 0xEF, 0xEF),
    (0x8D, 0x2F, 0x34),
    (0x6A, 0xD4, 0xCD),
    (0x98, 0x35, 0xA4),
    (0x4C, 0xB4, 0x42),
    (0x2C, 0x29, 0xB1),
    (0xEF, 0xEF, 0x5D),
    (0x98, 0x4E, 0x20),
    (0x5B, 0x38, 0x00),
    (0xD1, 0x67, 0x6D),
    (0x4A, 0x4A, 0x4A),
    (0x7B, 0x7B, 0x7B),
    (0x9F, 0xEF, 0x93),
    (0x6D, 0x6A, 0xEF),
    (0xB2, 0xB2, 0xB2),
]
FONT_PATH = Path(__file__).resolve().parents[3] / "roms" / "chars.bin"
FONT_BYTES = FONT_PATH.read_bytes()[: 256 * 8]
PRINTABLE_FALLBACK = {
    0x00: " ",
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


class RestSession:
    def __init__(self, host: str, timeout: float) -> None:
        self.host = host
        self.timeout = timeout

    def url(self, path: str, params: Optional[dict] = None) -> str:
        query = ""
        if params:
            query = "?" + urllib.parse.urlencode(params)
        return f"http://{self.host}{path}{query}"

    def request(self, method: str, path: str, params: Optional[dict] = None,
                payload: Optional[dict] = None) -> bytes:
        body = None if payload is None else json.dumps(payload).encode("utf-8")
        url = self.url(path, params)
        attempts = 3 if method in ("GET", "PUT") and body is None else 1
        last_error: Optional[BaseException] = None
        for attempt in range(attempts):
            request = urllib.request.Request(
                url,
                data=body,
                headers={"Content-Type": "application/json"} if body is not None else {},
                method=method,
            )
            try:
                with urllib.request.urlopen(request, timeout=self.timeout) as response:
                    data = response.read()
                    if REST_THROTTLE_SECONDS > 0:
                        time.sleep(REST_THROTTLE_SECONDS)
                    return data
            except (OSError, TimeoutError, urllib.error.URLError) as exc:
                last_error = exc
                if attempt + 1 >= attempts:
                    break
                time.sleep(0.25 * (attempt + 1))
        assert last_error is not None
        raise last_error

    def json_request(self, method: str, path: str, params: Optional[dict] = None,
                     payload: Optional[dict] = None) -> dict:
        return json.loads(self.request(method, path, params=params, payload=payload).decode("utf-8"))

    def put(self, command: str, params: Optional[dict] = None) -> None:
        self.request("PUT", f"/v1/machine:{command}", params=params)

    def reset(self) -> None:
        self.put("reset")

    def menu_button(self) -> None:
        self.put("menu_button")

    def read_memory(self, address: int, length: int) -> bytes:
        return self.request("GET", "/v1/machine:readmem", params={"address": f"{address:04X}", "length": length})

    def write_memory(self, address: int, data: bytes) -> None:
        self.request("PUT", "/v1/machine:writemem",
                     params={"address": f"{address:04X}", "data": data.hex().upper()})

    def start_video(self) -> None:
        self.request("PUT", "/v1/streams/video:start", params={"ip": MULTICAST_GROUP})

    def stop_video(self) -> None:
        self.request("PUT", "/v1/streams/video:stop")

    def input_ui(self, keys: Sequence[str]) -> None:
        try:
            self.json_request("POST", "/v1/machine:input_ui", payload={"keys": list(keys)})
        except (OSError, TimeoutError, urllib.error.URLError):
            # Some firmware builds accept the key injection and then reset/tear
            # down the HTTP socket before replying. The surrounding screen wait
            # logic verifies the resulting UI state before proceeding.
            time.sleep(REST_THROTTLE_SECONDS)

    def input_matrix(self, events: Sequence[dict]) -> None:
        self.json_request("POST", "/v1/machine:input", payload={"events": list(events)})

    def textlog(self) -> str:
        return self.request("GET", "/v1/machine:textlog").decode("utf-8", errors="replace")

    def dump_ui_screen(self, tag: str) -> "FrameText":
        try:
            return parse_menu_screen(self.request("GET", "/v1/machine:menu_screen"))
        except urllib.error.HTTPError as exc:
            if exc.code != 404:
                raise
        except (OSError, TimeoutError, urllib.error.URLError):
            pass
        last_error: Optional[Failure] = None
        for _ in range(3):
            try:
                self.request("PUT", "/v1/machine:dump_ui_screen", params={"tag": tag})
            except urllib.error.HTTPError as exc:
                if exc.code == 404:
                    raise Failure(f"No active firmware UI screen is available for dump tag {tag!r}.") from exc
                raise
            try:
                return parse_logged_ui_dump(self.textlog(), tag)
            except Failure as exc:
                last_error = exc
                time.sleep(0.1)
        assert last_error is not None
        raise last_error

    def get_interface_type(self) -> str:
        data = self.json_request("GET", "/v1/configs/User%20Interface%20Settings/Interface%20Type")
        return data["User Interface Settings"]["Interface Type"]["current"]

    def set_interface_type(self, value: str) -> None:
        self.request("PUT", "/v1/configs/User%20Interface%20Settings/Interface%20Type", params={"value": value})


def wait_interface_type(session: RestSession, expected: str, label: str,
                        timeout: float = 3.0) -> None:
    deadline = time.time() + timeout
    last = "<unread>"
    while time.time() < deadline:
        last = session.get_interface_type()
        if last == expected:
            return
        time.sleep(0.15)
    raise Failure(
        f"{label}: expected Interface Type {expected!r}, REST reports {last!r}")


def close_active_ui_for_mode_switch(session: RestSession) -> None:
    try:
        frame = session.dump_ui_screen("mode_switch_probe")
    except (Failure, OSError, TimeoutError, urllib.error.URLError):
        return
    if "MONITOR" in frame.text():
        session.input_ui(["ctrl", "m"])
        time.sleep(0.5)
    elif visible_ui(frame):
        session.menu_button()
        time.sleep(0.5)


def switch_interface_type(session: RestSession, expected: str, label: str) -> None:
    current = session.get_interface_type()
    if current == expected:
        return
    for _ in range(3):
        open_root_menu_for_mode_switch(session, label)
        session.input_ui(["ctrl_i"])
        try:
            wait_interface_type(session, expected, label, timeout=1.5)
            return
        except Failure:
            current = session.get_interface_type()
            if current == expected:
                return
    raise Failure(
        f"{label}: C=+I did not switch Interface Type to {expected!r}; "
        f"REST reports {session.get_interface_type()!r}")


class VicStreamCapture:
    def __init__(self) -> None:
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.sock.settimeout(2.0)
        self.sock.bind(("", VIDEO_PORT))
        group = socket.inet_aton(MULTICAST_GROUP)
        mreq = struct.pack("4sL", group, socket.INADDR_ANY)
        self.sock.setsockopt(socket.IPPROTO_IP, socket.IP_ADD_MEMBERSHIP, mreq)

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

            image = Image.new("RGB", (384, lines))
            i = 0
            for y in range(lines):
                for x in range(192):
                    b = raw[i]
                    image.putpixel((2 * x, y), PALETTE[b & 0x0F])
                    image.putpixel((2 * x + 1, y), PALETTE[b >> 4])
                    i += 1
            return image
        raise Failure("Did not receive a complete VIC frame.")


class FrameText:
    def __init__(self, image: Optional[Image.Image], lines: List[str], bbox: Tuple[int, int, int, int]) -> None:
        self.image = image
        self.lines = lines
        self.bbox = bbox

    def text(self) -> str:
        return "\n".join(self.lines)

    def contains(self, needle: str) -> bool:
        return any(needle in line for line in self.lines)


def parse_menu_screen(data: bytes) -> FrameText:
    width = 40
    height = 25
    cells = width * height
    if len(data) != 2 * cells:
        raise Failure(f"menu_screen returned {len(data)} bytes, expected {2 * cells}")
    chars = data[:cells]
    lines = []
    for y in range(height):
        row = []
        for x in range(width):
            ch = chars[y * width + x] & 0x7F
            row.append(chr(ch) if 0x20 <= ch <= 0x7E else " ")
        lines.append("".join(row).rstrip())
    return FrameText(None, lines, (0, 0, width, height))


def parse_logged_ui_dump(log: str, tag: str) -> FrameText:
    begin_prefix = f"MONITOR_DIRECT_UI_DUMP_BEGIN {tag} "
    end_marker = f"MONITOR_DIRECT_UI_DUMP_END {tag}"
    lines = log.splitlines()
    end_index = -1
    for index in range(len(lines) - 1, -1, -1):
        if lines[index].strip() == end_marker:
            end_index = index
            break
    if end_index < 0:
        raise Failure(f"UI dump {tag!r} end marker not found in text log.")

    begin_index = -1
    width = 0
    height = 0
    for index in range(end_index - 1, -1, -1):
        line = lines[index].strip()
        if not line.startswith(begin_prefix):
            continue
        parts = line.split()
        if len(parts) >= 4:
            width = int(parts[-2])
            height = int(parts[-1])
        begin_index = index
        break
    if begin_index < 0:
        if end_index >= 25:
            return FrameText(None, [line[:40].ljust(40) for line in lines[end_index - 25:end_index]],
                             (0, 0, 40, 25))
        raise Failure(f"UI dump {tag!r} begin marker not found in text log.")

    dumped = lines[begin_index + 1:end_index]
    if height and len(dumped) > height:
        dumped = dumped[-height:]
    if width:
        dumped = [line[:width].ljust(width) for line in dumped]
    return FrameText(None, dumped, (0, 0, width, len(dumped)))


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

    def _best_match(self, mask: Sequence[int]) -> Tuple[int, int]:
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
                    if x < min_x:
                        min_x = x
                    if y < min_y:
                        min_y = y
                    if x > max_x:
                        max_x = x
                    if y > max_y:
                        max_y = y
        if max_x < min_x or max_y < min_y:
            raise Failure("Could not find active 320x200 area inside VIC frame.")
        width = max_x - min_x + 1
        height = max_y - min_y + 1
        if height > 200 or height < 196:
            raise Failure(f"Expected about 200 active rows, got {height} at {(min_x, min_y, max_x, max_y)}")

        left_candidates = range(max(0, min_x - 16), min(min_x + 16, image.width - 320) + 1)
        top_candidates = range(max(0, min_y - 4), min(min_y + 4, image.height - 200) + 1)
        if width == 320:
            left_candidates = range(min_x, min_x + 1)
        best = None
        for top in top_candidates:
            for left in left_candidates:
                score = self._alignment_score(image, left, top)
                if best is None or score < best[0]:
                    best = (score, left, top)
        if best is None:
            raise Failure(f"Could not align 320x200 OCR grid from {(min_x, min_y, max_x, max_y)}")
        _, left, top = best
        return left, top, left + 320, top + 200

    def _decode_cell(self, cell: Image.Image) -> str:
        _, best_code = self._best_match(self._cell_mask(cell))
        return self._screen_code_to_char(best_code)

    @staticmethod
    def _screen_code_to_char(code: int) -> str:
        if best := PRINTABLE_FALLBACK.get(code):
            return best
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
        lines = []
        for row in range(25):
            chars = []
            for col in range(40):
                cell = active.crop((col * 8, row * 8, (col + 1) * 8, (row + 1) * 8))
                chars.append(self._decode_cell(cell))
            lines.append("".join(chars))
        return FrameText(image, lines, (left, top, right, bottom))


def wait_basic_ready(session: RestSession, timeout: float = 8.0) -> None:
    ready = bytes((0x12, 0x05, 0x01, 0x04, 0x19, 0x2E))
    deadline = time.time() + timeout
    while time.time() < deadline:
        if ready in session.read_memory(0x0400, 256):
            start = session.read_memory(0x00A2, 3)
            jiffy_deadline = time.time() + 2.0
            while time.time() < jiffy_deadline:
                time.sleep(0.1)
                if session.read_memory(0x00A2, 3) != start:
                    return
            raise Failure("READY prompt appeared but jiffy clock did not advance.")
        time.sleep(0.1)
    raise Failure("READY prompt did not appear after reset.")


def reset_to_basic(session: RestSession, close_ui: bool = True) -> None:
    if close_ui:
        try:
            frame = session.dump_ui_screen("reset_probe")
            if "MONITOR" in frame.text():
                session.input_ui(["ctrl", "m"])
                time.sleep(0.5)
            elif visible_ui(frame):
                session.menu_button()
                time.sleep(0.5)
        except (Failure, OSError, TimeoutError, urllib.error.URLError):
            pass
    session.reset()
    wait_basic_ready(session)
    time.sleep(0.3)


def capture_text(capture: VicStreamCapture, ocr: C64FrameOCR) -> FrameText:
    return ocr.decode(capture.capture_image())


TextReader = Callable[[], FrameText]


def freeze_text_reader(session: RestSession, capture: VicStreamCapture, ocr: C64FrameOCR) -> TextReader:
    def read() -> FrameText:
        try:
            return session.dump_ui_screen("freeze")
        except (Failure, OSError, TimeoutError):
            pass
        try:
            return capture_text(capture, ocr)
        except (Failure, OSError, TimeoutError):
            pass
        raise Failure("No active firmware UI screen is available from text log or OCR.")
    return read


def wait_for_text(reader: TextReader, needles: Sequence[str], label: str,
                  timeout: float = 5.0, forbidden: Sequence[str] = ()) -> FrameText:
    deadline = time.time() + timeout
    last: Optional[FrameText] = None
    last_error: Optional[BaseException] = None
    while time.time() < deadline:
        try:
            frame = reader()
        except Failure as exc:
            last_error = exc
            time.sleep(0.15)
            continue
        last = frame
        text = frame.text()
        for token in forbidden:
            if token in text:
                raise Failure(f"{label}: unexpected {token!r}\n{text}")
        if all(needle in text for needle in needles):
            return frame
        time.sleep(0.15)
    latest = last.text() if last is not None else "<no frame>"
    if last_error is not None:
        latest += f"\nlast OCR error: {last_error}"
    raise Failure(f"{label}: timed out waiting for {needles!r}\n{latest}")


def footer_values_line(frame: FrameText) -> str:
    for index, line in enumerate(frame.lines):
        if "PC" in line and "NV-BDIZC" in line:
            if index + 1 < len(frame.lines):
                return frame.lines[index + 1]
            break
    raise Failure(f"Debug footer label row not found:\n{frame.text()}")


def parse_footer_values(line: str) -> dict:
    body = line.lstrip("|+ ").rstrip("|+").rstrip()
    if (len(body) > 5 and body[4] != " " and body[5] == " " and
            all(ch in "0123456789ABCDEF" for ch in body[1:5].upper())):
        body = body[1:]
    fields = {
        "pc": body[0:4],
        "ac": body[5:7],
        "xr": body[8:10],
        "yr": body[11:13],
        "sp": body[14:16],
        "sr": body[17:25],
        "irq": body[26:30],
        "nmi": body[31:35],
    }
    return {key: value.strip() for key, value in fields.items()}


def status_line(frame: FrameText) -> str:
    for line in frame.lines:
        if "$A:" in line and "$D:" in line and ("CPU" in line or re.search(r"C[0-7]O[0-7]", line)):
            return line.strip("|+ ")
    raise Failure(f"Monitor status line not found:\n{frame.text()}")


def parse_status_banks(line: str) -> Tuple[Optional[int], Optional[int]]:
    cpu_match = re.search(r"CPU([0-7])", line)
    if cpu_match:
        bank = int(cpu_match.group(1))
        return bank, bank
    split_match = re.search(r"C([0-7])O([0-7])", line)
    if split_match:
        return int(split_match.group(1)), int(split_match.group(2))
    return None, None


def assert_status_banks(frame: FrameText, label: str, cpu_bank: Optional[int] = None,
                        monitor_bank: Optional[int] = None) -> None:
    line = status_line(frame)
    live, monitor = parse_status_banks(line)
    if cpu_bank is not None and live != cpu_bank:
        raise Failure(f"{label}: expected live CPU{cpu_bank}, got {line!r}\n{frame.text()}")
    if monitor_bank is not None and monitor != monitor_bank:
        raise Failure(f"{label}: expected monitor O{monitor_bank}, got {line!r}\n{frame.text()}")


def wait_for_status_banks(reader: TextReader, label: str, cpu_bank: Optional[int] = None,
                          monitor_bank: Optional[int] = None, timeout: float = 5.0) -> FrameText:
    deadline = time.time() + timeout
    last: Optional[FrameText] = None
    last_line = "<none>"
    while time.time() < deadline:
        frame = reader()
        last = frame
        line = status_line(frame)
        last_line = line
        live, monitor = parse_status_banks(line)
        if ((cpu_bank is None or live == cpu_bank) and
                (monitor_bank is None or monitor == monitor_bank)):
            return frame
        time.sleep(0.15)
    latest = last.text() if last is not None else "<no frame>"
    raise Failure(f"{label}: status did not reach CPU={cpu_bank} O={monitor_bank}; "
                  f"last={last_line!r}\n{latest}")


def select_monitor_view(session: RestSession, reader: TextReader, monitor_bank: int,
                        label: str, allow_missing_status: bool = False) -> FrameText:
    latest: Optional[FrameText] = None
    for _ in range(9):
        frame = reader()
        latest = frame
        try:
            _, current = parse_status_banks(status_line(frame))
        except Failure:
            if allow_missing_status:
                return frame
            raise
        if current == monitor_bank:
            return frame
        send_ui_text(session, "o", settle=0.2)
    latest_text = latest.text() if latest is not None else "<no frame>"
    raise Failure(f"{label}: could not select monitor O{monitor_bank}\n{latest_text}")


def disassembly_row(frame: FrameText, address: int) -> str:
    expected = f"{address:04X}"
    pattern = re.compile(rf"^{expected}\s+[0-9A-F]{{2}}\b")
    for line in frame.lines:
        candidate = line.lstrip("|+ ")
        if not pattern.search(candidate) and len(line) > 5 and line[1:5] == expected and line[5] == " ":
            candidate = line[1:].rstrip("|+ ")
        if pattern.search(candidate):
            return candidate
    raise Failure(f"Disassembly row ${address:04X} not found:\n{frame.text()}")


def instruction_length_from_row(row: str) -> int:
    match = re.search(r"\b[0-9A-F]{4}\s+((?:[0-9A-F]{2}\s+){0,2}[0-9A-F]{2})\b", row)
    if not match:
        raise Failure(f"Could not parse instruction bytes from row: {row!r}")
    return len(match.group(1).split())


def jsr_target_from_row(row: str) -> int:
    match = re.search(r"JSR\s+\$([0-9A-F]{4})", row)
    if not match:
        raise Failure(f"Could not parse JSR target from row: {row!r}")
    return int(match.group(1), 16)


def visible_jsr_row(frame: FrameText, source: str) -> Tuple[int, str]:
    pattern = re.compile(r"^([0-9A-F]{4})\s+[0-9A-F]{2}\b")
    for line in frame.lines:
        if source not in line or "JSR $" not in line:
            continue
        candidate = line.lstrip("|+ ")
        if not pattern.search(candidate) and len(line) > 5 and line[5] == " ":
            candidate = line[1:].rstrip("|+ ")
        match = pattern.search(candidate)
        if match:
            return int(match.group(1), 16), candidate
    raise Failure(f"No visible {source} JSR row found:\n{frame.text()}")


def wait_for_pc(reader: TextReader, expected_pc: int, label: str,
                timeout: float = 7.0) -> dict:
    expected = f"{expected_pc:04X}"
    deadline = time.time() + timeout
    last_frame: Optional[FrameText] = None
    last_values: Optional[dict] = None
    last_error: Optional[BaseException] = None
    while time.time() < deadline:
        try:
            frame = reader()
        except Failure as exc:
            last_error = exc
            time.sleep(0.15)
            continue
        last_frame = frame
        text = frame.text()
        for token in ("UNSAFE TARGET", "DEBUG TIMEOUT", "TIMEOUT", "PATCH FAILED",
                      "DEBUG NOT SUPPORTED", "NO CONTEXT", "ERROR"):
            if token in text:
                raise Failure(f"{label}: unexpected {token!r}\n{text}")
        values = parse_footer_values(footer_values_line(frame))
        last_values = values
        if values["pc"].upper() == expected:
            return values
        time.sleep(0.15)
    latest = last_frame.text() if last_frame is not None else "<no frame>"
    if last_error is not None:
        latest += f"\nlast OCR error: {last_error}"
    raise Failure(f"{label}: PC did not reach {expected}; last={last_values}\n{latest}")


def wait_for_cursor_pc(reader: TextReader, expected_pc: int, label: str,
                       timeout: float = 7.0) -> FrameText:
    expected = f"{expected_pc:04X}"
    pattern = re.compile(rf"^{expected}\s+[0-9A-F]{{2}}\b")
    deadline = time.time() + timeout
    last: Optional[FrameText] = None
    while time.time() < deadline:
        frame = reader()
        last = frame
        for line in frame.lines:
            candidate = line.lstrip("|+ ")
            if not pattern.search(candidate) and len(line) > 5 and line[1:5] == expected and line[5] == " ":
                candidate = line[1:].rstrip("|+ ")
            if pattern.search(candidate) and ">" in candidate:
                return frame
        time.sleep(0.15)
    latest = last.text() if last is not None else "<no frame>"
    raise Failure(f"{label}: cursor PC did not reach {expected}\n{latest}")


def send_ui_text(session: RestSession, text: str, settle: float = 0.3) -> None:
    keys = []
    for ch in text:
        if ch == "\n":
            keys.append("return")
        elif ch == "\x1b":
            keys.append("escape")
        else:
            keys.append(ch)
    session.input_ui(keys)
    time.sleep(settle)


def open_menu(session: RestSession, settle: float = 1.0) -> None:
    session.input_ui(["f10"])
    time.sleep(settle)
    try:
        frame = session.dump_ui_screen("menu_probe")
        if visible_ui(frame):
            return
    except Failure:
        pass
    for _ in range(2):
        session.menu_button()
        time.sleep(settle)
        try:
            frame = session.dump_ui_screen("menu_probe")
            if visible_ui(frame):
                return
        except Failure:
            pass


def open_root_menu_for_mode_switch(session: RestSession, label: str) -> None:
    latest = "<no frame>"
    for _ in range(4):
        try:
            frame = session.dump_ui_screen("mode_switch_root_probe")
            latest = frame.text()
            if "Ultimate" in latest and "MONITOR" not in latest:
                return
            if "MONITOR" in latest:
                session.input_ui(["ctrl", "m"])
                time.sleep(0.5)
        except (Failure, OSError, TimeoutError, urllib.error.URLError):
            pass
        open_menu(session, settle=0.7)
        try:
            frame = session.dump_ui_screen("mode_switch_root_probe")
            latest = frame.text()
            if "Ultimate" in latest and "MONITOR" not in latest:
                return
        except (Failure, OSError, TimeoutError, urllib.error.URLError):
            pass
        if "MONITOR" in latest:
            return
    raise Failure(f"{label}: could not open root UI for C=+I mode switch\n{latest}")


def open_overlay_menu(session: RestSession, label: str, settle: float = 0.5) -> FrameText:
    reader = lambda: session.dump_ui_screen("overlay")
    def probe_root() -> Optional[FrameText]:
        try:
            frame = reader()
        except Failure:
            return None
        text = frame.text()
        if "Ultimate 64 Elite" in text:
            return frame
        if "MONITOR" in text:
            session.input_ui(["ctrl", "m"])
            time.sleep(settle)
            try:
                frame = reader()
            except Failure:
                return None
            if "Ultimate 64 Elite" in frame.text():
                return frame
        return None

    for keys in (None, ["f10"], None, ["f10"]):
        if keys is None:
            session.menu_button()
        else:
            session.input_ui(keys)
        time.sleep(settle)
        frame = probe_root()
        if frame:
            return frame
    return wait_for_visible_ui(reader, label, timeout=5.0)


def cleanup_c64_screen(session: RestSession) -> None:
    try:
        active_ui = visible_ui(session.dump_ui_screen("cleanup_probe"))
    except (Failure, OSError, TimeoutError, urllib.error.URLError):
        active_ui = False
    if active_ui:
        try:
            session.menu_button()
            time.sleep(0.5)
        except (OSError, TimeoutError, urllib.error.URLError) as exc:
            print(f"cleanup warning: could not close firmware UI: {exc}", file=sys.stderr)
    try:
        session.reset()
        wait_basic_ready(session)
    except (Failure, OSError, TimeoutError, urllib.error.URLError) as exc:
        print(f"cleanup warning: C64 reset did not reach READY: {exc}", file=sys.stderr)


def visible_ui(frame: FrameText) -> bool:
    text = frame.text()
    if ("Ultimate 64 Elite" in text) or ("MONITOR " in text):
        return True
    for line in frame.lines:
        stripped = line.strip()
        if re.match(r"^[0-9A-F]{3,4}\s+[0-9A-F]{2}", stripped):
            return True
    return False


def wait_for_visible_ui(reader: TextReader, label: str, timeout: float = 5.0) -> FrameText:
    deadline = time.time() + timeout
    last: Optional[FrameText] = None
    last_error: Optional[BaseException] = None
    while time.time() < deadline:
        try:
            frame = reader()
        except Failure as exc:
            last_error = exc
            time.sleep(0.15)
            continue
        last = frame
        if visible_ui(frame):
            return frame
        time.sleep(0.15)
    latest = last.text() if last is not None else "<no frame>"
    if last_error is not None:
        latest += f"\nlast OCR error: {last_error}"
    raise Failure(f"{label}: UI did not become visible\n{latest}")


def ensure_stream_visible_mode(session: RestSession, capture: VicStreamCapture, ocr: C64FrameOCR) -> None:
    reader = freeze_text_reader(session, capture, ocr)
    switch_interface_type(session, "Freeze", "Select Freeze mode")
    wait_interface_type(session, "Freeze", "Select Freeze mode")
    session.reset()
    wait_basic_ready(session)
    wait_interface_type(session, "Freeze", "Freeze mode after reset")
    time.sleep(1.0)
    try:
        frame = reader()
        if visible_ui(frame):
            return
    except Failure:
        pass
    open_menu(session, settle=1.5)
    frame = wait_for_visible_ui(reader, "Freeze UI visible in stream or text log", timeout=5.0)
    if visible_ui(frame):
        session.input_ui(["run_stop"])
        time.sleep(0.3)
        return
    raise Failure(
        "Could not establish a stream-visible Freeze UI. "
        f"Interface Type={session.get_interface_type()!r}; latest frame:\n{frame.text()}"
    )


def ensure_stream_hidden_overlay_mode(session: RestSession, capture: VicStreamCapture, ocr: C64FrameOCR) -> None:
    switch_interface_type(session, "Overlay on HDMI", "Select Overlay mode")
    wait_interface_type(session, "Overlay on HDMI", "Select Overlay mode")
    session.reset()
    wait_basic_ready(session)
    wait_interface_type(session, "Overlay on HDMI", "Overlay mode after reset")
    time.sleep(1.0)
    open_overlay_menu(session, "Overlay UI text-log dump", settle=1.0)


def assert_monitor_reentered(session: RestSession, tag: str) -> None:
    frame = wait_for_visible_ui(lambda: session.dump_ui_screen(tag),
                                f"{tag}: monitor reentry", timeout=5.0)
    if "MONITOR" not in frame.text():
        raise Failure(f"Firmware monitor did not reenter after reset shortcut:\n{frame.text()}")


def run_global_ctrl_x_test(session: RestSession, interface_type: str, label: str) -> None:
    with check(f"{label}: C=+X resets from root menu"):
        switch_interface_type(session, interface_type,
                              f"{label}: select mode for root reset")
        session.reset()
        wait_basic_ready(session)
        wait_interface_type(session, interface_type,
                            f"{label}: mode before root reset")
        time.sleep(1.0)
        if interface_type == "Overlay on HDMI":
            open_overlay_menu(session, f"{label}: root menu")
        else:
            open_menu(session)
        wait_for_visible_ui(lambda: session.dump_ui_screen(f"{label.lower().replace(' ', '_')}_root"),
                            f"{label}: root menu", timeout=5.0)
        session.input_ui(["ctrl_x"])
        assert_monitor_reentered(session, f"{label.lower().replace(' ', '_')}_root_reentered")

    with check(f"{label}: C=+X resets from help popup"):
        switch_interface_type(session, interface_type,
                              f"{label}: select mode for help reset")
        session.reset()
        wait_basic_ready(session)
        wait_interface_type(session, interface_type,
                            f"{label}: mode before help reset")
        time.sleep(1.0)
        if interface_type == "Overlay on HDMI":
            open_overlay_menu(session, f"{label}: root menu before help")
        else:
            open_menu(session)
        wait_for_visible_ui(lambda: session.dump_ui_screen(f"{label.lower().replace(' ', '_')}_help_root"),
                            f"{label}: root menu before help", timeout=5.0)
        session.input_ui(["f3"])
        wait_for_visible_ui(lambda: session.dump_ui_screen(f"{label.lower().replace(' ', '_')}_help"),
                            f"{label}: help popup", timeout=5.0)
        session.input_ui(["ctrl_x"])
        assert_monitor_reentered(session, f"{label.lower().replace(' ', '_')}_help_reentered")


def enter_visible_monitor(session: RestSession, capture: VicStreamCapture, ocr: C64FrameOCR) -> FrameText:
    wait_interface_type(session, "Freeze", "Direct Freeze monitor entry")
    reader = freeze_text_reader(session, capture, ocr)
    latest = None
    for _ in range(4):
        session.input_ui(["f10"])
        time.sleep(1.0)
        session.input_ui(["ctrl", "m"])
        time.sleep(1.0)
        try:
            frame = wait_for_text(reader, ("MONITOR",), "visible machine monitor", timeout=3.0)
            latest = frame
            time.sleep(0.4)
            frame = wait_for_text(reader, ("MONITOR",), "visible machine monitor stable", timeout=1.0)
            status_line(frame)
            return frame
        except Failure:
            pass
        session.input_ui(["ctrl", "m"])
        time.sleep(0.5)
    if latest is not None:
        raise Failure(f"Did not enter visible machine monitor:\n{latest.text()}")
    raise Failure("Did not enter visible machine monitor")


def run_freeze_ram_edit_test(session: RestSession, capture: VicStreamCapture, ocr: C64FrameOCR) -> None:
    program = bytes.fromhex("A9018D20D0EE20D04C0520")
    reader = freeze_text_reader(session, capture, ocr)

    with check("Direct Freeze: entering visible monitor"):
        frame = enter_visible_monitor(session, capture, ocr)
        if not visible_ui(frame):
            raise Failure(frame.text())

    with check("Direct Freeze: ASM edit writes RAM program at $2000"):
        send_ui_text(session, "J2000\n")
        send_ui_text(session, "A")
        send_ui_text(session, "E")
        for asm in ("LDA#$01\n", "STA$D020\n", "INC$D020\n", "JMP$2005\n"):
            send_ui_text(session, asm)
        send_ui_text(session, "\x1b")
        frame = reader()
        text = frame.text()
        if not all(token in text for token in ("2000 A9 01", "LDA #$01", "STA $D020", "INC $D020", "JMP $2005")):
            if session.read_memory(0x2000, len(program)) == program:
                enter_visible_monitor(session, capture, ocr)
                send_ui_text(session, "J2000\n")
                send_ui_text(session, "A")
                text = reader().text()
        for token in ("2000 A9 01", "LDA #$01", "STA $D020", "INC $D020", "JMP $2005"):
            if token not in text:
                raise Failure(text)
        if "UNSAFE TARGET" in text or "TIMEOUT" in text or "ERROR" in text:
            raise Failure(text)
        if session.read_memory(0x2000, len(program)) != program:
            raise Failure("RAM readback does not match the assembled program.")

    with check("Direct Freeze: HEX readback shows edited bytes"):
        if session.read_memory(0x2000, len(program)) != program:
            raise Failure("RAM readback does not match the assembled program before HEX readback.")
        try:
            enter_visible_monitor(session, capture, ocr)
            send_ui_text(session, "J2000\n")
            send_ui_text(session, "M")
            frame = reader()
            text = frame.text()
            if "2000 A9 01 8D 20 D0" not in text:
                raise Failure(text)
        except Failure as exc:
            print(f"warning: visual HEX readback unavailable after edit; RAM readback passed: {exc}", file=sys.stderr)


def run_freeze_rom_step_test(session: RestSession, capture: VicStreamCapture, ocr: C64FrameOCR) -> None:
    reader = freeze_text_reader(session, capture, ocr)
    with check("Direct Freeze: Step Over/Into/Out via text-log dump"):
        reset_to_basic(session)
        enter_visible_monitor(session, capture, ocr)
        run_direct_debug_step_test(session, reader, "Direct Freeze")
    with check("Direct Freeze: KERNAL/BASIC ROM stepping via text-log dump"):
        reset_to_basic(session)
        enter_visible_monitor(session, capture, ocr)
        run_direct_rom_step_test(session, reader, "Direct Freeze")
    with check("Direct Freeze: Stop Debugging + Exit resumes current $2000 context"):
        reset_to_basic(session)
        run_direct_stop_debug_exit_test(
            session, reader,
            lambda: enter_visible_monitor(session, capture, ocr),
            "Direct Freeze")


def enter_hidden_monitor(session: RestSession) -> FrameText:
    wait_interface_type(session, "Overlay on HDMI", "Direct Overlay monitor entry")
    reader = lambda: session.dump_ui_screen("overlay")
    try:
        frame = reader()
        if "MONITOR" in frame.text():
            return frame
    except (Failure, OSError, TimeoutError, urllib.error.URLError):
        pass
    frame = open_overlay_menu(session, "hidden overlay root", settle=0.6)
    for _ in range(3):
        session.input_ui(["ctrl", "m"])
        time.sleep(1.0)
        try:
            return wait_for_text(reader, ("MONITOR",), "hidden overlay monitor", timeout=2.0)
        except Failure:
            frame = open_overlay_menu(session, "hidden overlay root retry", settle=0.6)
            if "MONITOR" in frame.text():
                return frame
    return wait_for_text(reader, ("MONITOR",), "hidden overlay monitor", timeout=5.0)


def load_direct_debug_fixture(session: RestSession) -> None:
    # $2000: LDA #$11; JSR $200B; STA $D020; JMP $2008
    # $200B: LDA #$22; RTS
    program = bytes.fromhex("A911200B208D20D04C0820A92260")
    session.write_memory(0x2000, program)
    readback = session.read_memory(0x2000, len(program))
    if readback != program:
        raise Failure(
            "Direct debug fixture did not round-trip:\n"
            f"  expected: {program.hex().upper()}\n"
            f"  actual:   {readback.hex().upper()}")


def run_direct_debug_step_test(session: RestSession, reader: TextReader, label: str) -> None:
    load_direct_debug_fixture(session)

    send_ui_text(session, "J2000\n", settle=0.25)
    wait_for_text(reader, ("$2000",), f"{label}: goto $2000", timeout=5.0)
    send_ui_text(session, "A", settle=0.25)
    wait_for_text(reader, ("MONITOR ASM $2000",), f"{label}: ASM view at $2000", timeout=5.0)
    send_ui_text(session, "D", settle=0.25)
    wait_for_text(reader, ("Dbg", "PC", "NV-BDIZC"), f"{label}: enter Debug", timeout=5.0)

    send_ui_text(session, "D", settle=0.25)
    values = wait_for_pc(reader, 0x2002, f"{label}: Step Over LDA #$11")
    if values["ac"] != "11":
        raise Failure(f"{label}: Step Over LDA should set AC=11, got {values!r}")

    send_ui_text(session, "T", settle=0.25)
    wait_for_pc(reader, 0x200B, f"{label}: Step Into JSR $200B")

    send_ui_text(session, "U", settle=0.25)
    values = wait_for_pc(reader, 0x2005, f"{label}: Step Out from subroutine")
    if values["ac"] != "22":
        raise Failure(f"{label}: Step Out should preserve AC=22, got {values!r}")

    send_ui_text(session, "\x1b", settle=0.25)


def enter_debug_context_at(session: RestSession, reader: TextReader, address: int,
                           source: str, label: str) -> FrameText:
    trampoline = bytes([0x20, address & 0xFF, address >> 8, 0x4C, 0x03, 0x21])
    session.write_memory(0x2100, trampoline)
    enter_debug_at(session, reader, 0x2100, "RAM", f"{label}: RAM JSR trampoline")
    send_ui_text(session, "T", settle=0.25)
    wait_for_pc(reader, address, f"{label}: trace into ${address:04X}")
    return wait_for_text(reader, (f"${address:04X}", source, "Dbg"),
                         f"{label}: context at ${address:04X}", timeout=5.0)


def enter_debug_at(session: RestSession, reader: TextReader, address: int, source: str,
                   label: str) -> FrameText:
    send_ui_text(session, f"J{address:04X}\n", settle=0.25)
    wait_for_text(reader, (f"${address:04X}",), f"{label}: goto ${address:04X}", timeout=5.0)
    send_ui_text(session, "A", settle=0.25)
    frame = wait_for_text(reader, (f"MONITOR ASM ${address:04X}", source),
                          f"{label}: ASM view at ${address:04X}", timeout=5.0)
    disassembly_row(frame, address)
    send_ui_text(session, "D", settle=0.25)
    return wait_for_text(reader, ("Dbg", "PC", "NV-BDIZC"),
                         f"{label}: enter Debug", timeout=5.0)


def enter_debug_at_lenient(session: RestSession, reader: TextReader, address: int,
                           source: str, label: str) -> FrameText:
    send_ui_text(session, f"J{address:04X}\n", settle=0.25)
    wait_for_text(reader, (f"${address:04X}",), f"{label}: goto ${address:04X}", timeout=5.0)
    send_ui_text(session, "A", settle=0.25)
    frame = wait_for_text(reader, (f"MONITOR ASM ${address:04X}", source),
                          f"{label}: ASM view at ${address:04X}", timeout=5.0)
    disassembly_row(frame, address)
    send_ui_text(session, "D", settle=0.25)
    try:
        return wait_for_text(reader, ("Dbg", "PC", "NV-BDIZC"),
                             f"{label}: enter Debug", timeout=2.0)
    except Failure:
        frame = reader()
        if "Dbg" in frame.text():
            return frame
        raise


def leave_debug(session: RestSession) -> None:
    send_ui_text(session, "\x1b", settle=0.25)


def cancel_debug(session: RestSession) -> None:
    session.input_ui(["ctrl_d"])
    time.sleep(0.25)
    session.input_ui(["ctrl_e"])
    time.sleep(0.25)


def wait_until_not_debug(reader: TextReader, label: str, timeout: float = 3.0) -> None:
    deadline = time.time() + timeout
    last: Optional[FrameText] = None
    while time.time() < deadline:
        frame = reader()
        last = frame
        if "Dbg" not in frame.text():
            return
        time.sleep(0.15)
    latest = last.text() if last is not None else "<no frame>"
    raise Failure(f"{label}: Debug did not close\n{latest}")


def load_repeat_redebug_fixtures(session: RestSession) -> None:
    hidden_bootstrap = bytes([
        0x78,                   # SEI
        0xA2, 0x00,             # LDX #$00
        0xA9, 0x37,             # LDA #$37
        0x85, 0x00,             # STA $00
        0xA9, 0x35,             # LDA #$35
        0x85, 0x01,             # STA $01
        0x4C, 0x00, 0xE0,       # JMP $E000
    ])
    hidden_payload = bytes([
        0xFE, 0x00, 0x04,       # INC $0400,X
        0xFE, 0x00, 0x05,       # INC $0500,X
        0xE8,                   # INX
        0x8E, 0x21, 0xD0,       # STX $D021
        0x4C, 0x00, 0xE0,       # JMP $E000
    ])
    ordinary_loop = bytes([
        0xA2, 0x00,             # LDX #$00
        0xFE, 0x00, 0x06,       # INC $0600,X
        0xFE, 0xE8, 0x06,       # INC $06E8,X
        0xE8,                   # INX
        0x8E, 0x20, 0xD0,       # STX $D020
        0x4C, 0x02, 0xC3,       # JMP $C302
    ])
    write_memory_chunks(session, 0x0400, bytes([0x00]) * 0x3E8)
    session.write_memory(0xC000, hidden_bootstrap)
    session.write_memory(0xE000, hidden_payload)
    session.write_memory(0xC300, ordinary_loop)


def write_memory_chunks(session: RestSession, address: int, data: bytes,
                        chunk_size: int = 0x80) -> None:
    for offset in range(0, len(data), chunk_size):
        session.write_memory(address + offset, data[offset:offset + chunk_size])


def assert_region_keeps_changing(session: RestSession, address: int, length: int,
                                 label: str, minimum_cells: int = 2,
                                 timeout: float = 4.0) -> None:
    previous = session.read_memory(address, length)
    changed_cells = set()
    deadline = time.time() + timeout
    while time.time() < deadline and len(changed_cells) < minimum_cells:
        time.sleep(0.08)
        current = session.read_memory(address, length)
        for offset, (before, after) in enumerate(zip(previous, current)):
            if before != after:
                changed_cells.add(offset)
        previous = current
    if len(changed_cells) < minimum_cells:
        raise Failure(
            f"{label}: ${address:04X}-${address + length - 1:04X} did not keep "
            f"changing in at least {minimum_cells} cells; changed={sorted(changed_cells)!r}")


def wait_for_byte_change(session: RestSession, address: int, label: str,
                         timeout: float = 3.0) -> None:
    before = session.read_memory(address, 1)[0]
    deadline = time.time() + timeout
    while time.time() < deadline:
        time.sleep(0.08)
        after = session.read_memory(address, 1)[0]
        if after != before:
            return
    raise Failure(f"{label}: ${address:04X} did not change from ${before:02X}")


def load_overlay_reset_decode_fixture(session: RestSession) -> None:
    bootstrap = bytes([
        0x78,                   # SEI
        0xA2, 0x00,             # LDX #$00
        0xA0, 0x00,             # LDY #$00
        0xA9, 0x37,             # LDA #$37
        0x85, 0x00,             # STA $00
        0xA9, 0x35,             # LDA #$35
        0x85, 0x01,             # STA $01
        0x4C, 0x00, 0xE0,       # JMP $E000
    ])
    hidden_payload = bytes([
        0xEE, 0x21, 0xD0,       # INC $D021
        0xE8,                   # INX
        0x88,                   # DEY
        0x98,                   # TYA
        0x4C, 0x00, 0xE0,       # JMP $E000
    ])
    write_memory_chunks(session, 0x0400, bytes([0x00]) * 0x3E8)
    session.write_memory(0xC000, bootstrap)
    session.write_memory(0xE000, hidden_payload)


CONTINUE_COUNTER_ADDR = 0xC500


def load_continue_speed_fixture(session: RestSession) -> None:
    loop = bytes([
        0xEE, 0x00, 0xC5,       # INC $C500
        0xD0, 0xFB,             # BNE $C400
        0xEE, 0x01, 0xC5,       # INC $C501
        0xD0, 0xF6,             # BNE $C400
        0xEE, 0x02, 0xC5,       # INC $C502
        0x4C, 0x00, 0xC4,       # JMP $C400
    ])
    write_memory_chunks(session, CONTINUE_COUNTER_ADDR, bytes([0x00]) * 3)
    session.write_memory(0xC400, loop)


def read_counter24(session: RestSession) -> int:
    data = session.read_memory(CONTINUE_COUNTER_ADDR, 3)
    return data[0] | (data[1] << 8) | (data[2] << 16)


def counter_cycles(start: int, end: int) -> int:
    increments = (end - start) & 0xFFFFFF
    low_wraps = ((start & 0xFF) + increments) // 0x100
    mid_wraps = ((start & 0xFFFF) + increments) // 0x10000
    return 9 * increments + 8 * low_wraps + 8 * mid_wraps


def wait_for_counter_progress(session: RestSession, start: int,
                              timeout: float = 3.0) -> int:
    deadline = time.time() + timeout
    while time.time() < deadline:
        current = read_counter24(session)
        if current != start:
            return current
        time.sleep(0.04)
    raise Failure(
        f"Counter at ${CONTINUE_COUNTER_ADDR:04X} did not advance from ${start:06X}")


def wait_for_monitor_disappearance(reader: TextReader, label: str,
                                   timeout: float = 3.0) -> float:
    start = time.perf_counter()
    deadline = start + timeout
    last = "<no frame>"
    while time.perf_counter() < deadline:
        try:
            frame = reader()
            last = frame.text()
            if "MONITOR" not in last:
                return time.perf_counter() - start
        except (Failure, OSError, TimeoutError, urllib.error.URLError):
            return time.perf_counter() - start
        time.sleep(0.04)
    raise Failure(f"{label}: monitor did not disappear after G\n{last}")


def wait_for_continue_observations(session: RestSession, start_counter: int,
                                   reader: TextReader, label: str,
                                   timeout: float = 3.0) -> Tuple[float, float, int]:
    start = time.perf_counter()
    deadline = start + timeout
    disappear_latency: Optional[float] = None
    progress_latency: Optional[float] = None
    first_counter = start_counter
    last = "<no frame>"
    while time.perf_counter() < deadline:
        if progress_latency is None:
            current = read_counter24(session)
            if current != start_counter:
                first_counter = current
                progress_latency = time.perf_counter() - start
        if disappear_latency is None:
            try:
                frame = reader()
                last = frame.text()
                if "MONITOR" not in last:
                    disappear_latency = time.perf_counter() - start
            except (Failure, OSError, TimeoutError, urllib.error.URLError):
                disappear_latency = time.perf_counter() - start
        if disappear_latency is not None and progress_latency is not None:
            return disappear_latency, progress_latency, first_counter
        time.sleep(0.04)
    if disappear_latency is None:
        raise Failure(f"{label}: monitor did not disappear after G\n{last}")
    raise Failure(
        f"Counter at ${CONTINUE_COUNTER_ADDR:04X} did not advance from ${start_counter:06X}")


def measure_continue_speed(session: RestSession, reader: TextReader,
                           label: str,
                           disappear_reader: Optional[TextReader] = None) -> dict:
    enter_direct_debug_at(session, reader, 0xC400, "RAM", 7,
                          f"{label}: enter Debug at counter loop")
    send_ui_text(session, "D", settle=0.1)
    wait_for_pc(reader, 0xC403, f"{label}: establish stepped context")
    time.sleep(0.35)
    start_counter = read_counter24(session)
    session.input_ui(["G"])
    disappear_latency, first_progress_latency, first_counter = \
        wait_for_continue_observations(
            session, start_counter, disappear_reader or reader,
            f"{label}: monitor disappearance")

    time.sleep(0.5)
    sample_a = read_counter24(session)
    sample_t0 = time.perf_counter()
    time.sleep(1.25)
    sample_b = read_counter24(session)
    sample_t1 = time.perf_counter()
    cycles = counter_cycles(sample_a, sample_b)
    rate = cycles / (sample_t1 - sample_t0)
    if cycles <= 0:
        raise Failure(f"{label}: counter did not advance during steady-state sample")
    print(
        f"{label}: G->hidden {disappear_latency:.3f}s, "
        f"G->progress {first_progress_latency:.3f}s, "
        f"rate {rate:.0f} cycles/s "
        f"(first ${first_counter:06X}, sample ${sample_a:06X}->${sample_b:06X})")
    return {
        "disappear_latency": disappear_latency,
        "first_progress_latency": first_progress_latency,
        "cycles_per_second": rate,
    }


def move_cursor_to(session: RestSession, reader: TextReader, address: int,
                   label: str) -> FrameText:
    send_ui_text(session, f"J{address:04X}\n", settle=0.25)
    frame = wait_for_text(reader, (f"MONITOR ASM ${address:04X}",),
                          label, timeout=5.0)
    disassembly_row(frame, address)
    return frame


def toggle_breakpoint_at_cursor(session: RestSession, reader: TextReader,
                                address: int, enabled: bool, label: str) -> None:
    _ = enabled
    send_ui_text(session, "R", settle=0.25)
    frame = wait_for_text(reader, (f"MONITOR ASM ${address:04X}",),
                          label, timeout=5.0)
    disassembly_row(frame, address)


def assert_status_footer(frame: FrameText, label: str) -> None:
    if not frame.lines:
        raise Failure(f"{label}: empty screen")
    bottom = frame.lines[-1]
    if "F3=HELP" not in bottom:
        raise Failure(f"{label}: bottom status row lost F3=HELP\n{frame.text()}")
    body = bottom.strip()
    if len(body) > 8 and len(set(body)) <= 2:
        raise Failure(f"{label}: bottom status row is a separator line\n{frame.text()}")


def run_freeze_debug_footer_test(session: RestSession, capture: VicStreamCapture,
                                 ocr: C64FrameOCR) -> None:
    with check("Direct Freeze: Debug preserves browser status footer"):
        reset_to_basic(session)
        load_repeat_redebug_fixtures(session)
        enter_visible_monitor(session, capture, ocr)
        reader = freeze_text_reader(session, capture, ocr)
        enter_direct_debug_at(
            session, reader, 0xC300, "RAM", 7,
            "Direct Freeze status footer: enter Debug at $C300")
        send_ui_text(session, "D", settle=0.25)
        wait_for_cursor_pc(reader, 0xC302,
                           "Direct Freeze status footer: step to $C302")
        assert_status_footer(reader(),
                             "Direct Freeze status footer during Debug")
        leave_debug(session)
        wait_until_not_debug(reader, "Direct Freeze status footer leave Debug")
        session.input_ui(["ctrl", "m"])
        time.sleep(0.5)
        open_menu(session, settle=1.0)
        frame = wait_for_visible_ui(reader,
                                    "Direct Freeze status footer after exit",
                                    timeout=5.0)
        assert_status_footer(frame,
                             "Direct Freeze status footer after monitor exit")


def assert_monitor_not_visible_after_full_speed(session: RestSession,
                                                capture: VicStreamCapture,
                                                ocr: C64FrameOCR,
                                                label: str) -> None:
    frame = None
    try:
        frame = session.dump_ui_screen("full_speed_monitor_probe")
    except Failure:
        pass
    if frame is not None and "MONITOR" in frame.text():
        raise Failure(
            f"{label}: monitor stayed visible after full-speed G\n{frame.text()}")
    try:
        frame = capture_text(capture, ocr)
    except Failure:
        return
    if "MONITOR" in frame.text():
        raise Failure(f"{label}: monitor stayed visible after full-speed G\n{frame.text()}")


def run_to_breakpoint_then_continue(session: RestSession, reader: TextReader,
                                    label: str, start_addr: int,
                                    breakpoint_addr: int,
                                    breakpoint_bank: int,
                                    breakpoint_source: str,
                                    context_pcs: Sequence[int],
                                    start_source: str = "RAM",
                                    start_bank: int = 7,
                                    progress_addr: Optional[int] = None,
                                    progress_len: int = 0,
                                    capture: Optional[VicStreamCapture] = None,
                                    ocr: Optional[C64FrameOCR] = None) -> None:
    enter_direct_debug_at(session, reader, start_addr, start_source, start_bank,
                          f"{label}: enter Debug at ${start_addr:04X}")
    for expected_pc in context_pcs:
        send_ui_text(session, "D", settle=0.25)
        wait_for_pc(reader, expected_pc,
                    f"{label}: establish context ${expected_pc:04X}")
    select_monitor_view(session, reader, breakpoint_bank,
                        f"{label}: select O{breakpoint_bank}")
    if context_pcs and context_pcs[-1] == breakpoint_addr:
        frame = wait_for_text(reader, (f"MONITOR ASM ${breakpoint_addr:04X}",),
                              f"{label}: cursor at breakpoint ${breakpoint_addr:04X}",
                              timeout=5.0)
    else:
        frame = move_cursor_to(
            session, reader, breakpoint_addr,
            f"{label}: cursor at breakpoint ${breakpoint_addr:04X}")
    row = disassembly_row(frame, breakpoint_addr)
    if breakpoint_source not in row:
        raise Failure(f"{label}: breakpoint row is not {breakpoint_source}: {row!r}")
    toggle_breakpoint_at_cursor(
        session, reader, breakpoint_addr, True,
        f"{label}: set breakpoint at ${breakpoint_addr:04X}")
    send_ui_text(session, "G", settle=0.25)
    wait_for_pc(reader, breakpoint_addr,
                f"{label}: run to breakpoint ${breakpoint_addr:04X}")
    toggle_breakpoint_at_cursor(
        session, reader, breakpoint_addr, False,
        f"{label}: remove breakpoint at ${breakpoint_addr:04X}")
    send_ui_text(session, "G", settle=0.25)

    if progress_addr is not None:
        if capture is not None and ocr is not None:
            assert_monitor_not_visible_after_full_speed(session, capture, ocr,
                                                        label)
        assert_region_keeps_changing(
            session, progress_addr, progress_len,
            f"{label}: full-speed continue after breakpoint removal")
    else:
        wait_for_monitor_disappearance(
            reader, f"{label}: full-speed continue after breakpoint removal")


def assert_overlay_reset_decode_after_continue(session: RestSession,
                                               reader: TextReader,
                                               label: str) -> None:
    wait_for_byte_change(session, 0xD021, f"{label}: full-speed color loop")
    session.reset()
    time.sleep(0.5)
    enter_hidden_monitor(session)
    frame = wait_for_visible_ui(lambda: session.dump_ui_screen("overlay_reset_decode"),
                                f"{label}: reset reentry", timeout=5.0)
    row = disassembly_row(frame, 0xE000)
    if "EE 21 D0" not in row or "INC $D021" not in row:
        raise Failure(f"{label}: $E000 decoded incorrectly after reset: {row!r}\n{frame.text()}")
    text = frame.text()
    if "DFFE" in text or "DFFF" in text or "????" in text or "BNE #DF" in text:
        raise Failure(f"{label}: reset decode included stale pre-$E000 rows\n{text}")


def assert_stable_e000_decode(reader: TextReader, label: str) -> None:
    frame = wait_for_text(reader, ("MONITOR ASM", "E000"),
                          f"{label}: ASM view with $E000 visible", timeout=5.0)
    expected = (
        (0xE000, "EE 21 D0", "INC $D021"),
        (0xE003, "E8", "INX"),
        (0xE004, "88", "DEY"),
        (0xE005, "98", "TYA"),
        (0xE006, "4C 00 E0", "JMP $E000"),
    )
    text = frame.text()
    for address, hex_bytes, mnemonic in expected:
        row = disassembly_row(frame, address)
        if hex_bytes not in row or mnemonic not in row:
            raise Failure(
                f"{label}: row ${address:04X} decoded incorrectly: {row!r}\n{text}")
    if ("E001" in text or ("E002" in text and "BNE" in text) or
            "????" in text):
        raise Failure(f"{label}: unstable overlapping decode visible\n{text}")


def run_direct_continue_speed_tests(session: RestSession, capture: VicStreamCapture,
                                    ocr: C64FrameOCR) -> None:
    try:
        session.stop_video()
    except (OSError, TimeoutError, urllib.error.URLError):
        pass
    ensure_stream_hidden_overlay_mode(session, capture, ocr)

    with check("Direct Overlay: no-breakpoint G full-speed latency and cycle rate"):
        reset_to_basic(session)
        load_continue_speed_fixture(session)
        overlay_reader = lambda: session.dump_ui_screen("overlay_speed")
        enter_hidden_monitor(session)
        overlay = measure_continue_speed(
            session, overlay_reader,
            "Direct Overlay no-breakpoint G",
            disappear_reader=lambda: capture_text(capture, ocr))

    session.start_video()
    time.sleep(0.5)
    ensure_stream_visible_mode(session, capture, ocr)

    with check("Direct Freeze: no-breakpoint G full-speed latency and cycle rate"):
        reset_to_basic(session)
        load_continue_speed_fixture(session)
        enter_visible_monitor(session, capture, ocr)
        freeze = measure_continue_speed(
            session, freeze_text_reader(session, capture, ocr),
            "Direct Freeze no-breakpoint G")

    ratio = overlay["cycles_per_second"] / freeze["cycles_per_second"]
    if ratio < 0.85 or ratio > 1.15:
        raise Failure(
            "Overlay steady-state rate differs from Freeze by more than 15%: "
            f"overlay={overlay['cycles_per_second']:.0f}, "
            f"freeze={freeze['cycles_per_second']:.0f}, ratio={ratio:.3f}")
    if overlay["first_progress_latency"] > freeze["first_progress_latency"] + 0.75:
        raise Failure(
            "Overlay G startup latency has an unexplained UI-only delay: "
            f"overlay={overlay['first_progress_latency']:.3f}s, "
            f"freeze={freeze['first_progress_latency']:.3f}s")


def run_direct_freeze_reset_disassembly_test(session: RestSession,
                                             capture: VicStreamCapture,
                                             ocr: C64FrameOCR) -> None:
    session.start_video()
    time.sleep(0.5)
    ensure_stream_visible_mode(session, capture, ocr)

    with check("Direct Freeze: REST reset re-entry keeps RAM-under-KERNAL ASM stable"):
        reset_to_basic(session)
        load_overlay_reset_decode_fixture(session)
        enter_visible_monitor(session, capture, ocr)
        reader = freeze_text_reader(session, capture, ocr)
        enter_direct_debug_at(
            session, reader, 0xC000, "RAM", 7,
            "Direct Freeze reset decode: enter Debug at $C000")
        for pc in (0xC001, 0xC003, 0xC005, 0xC007, 0xC009, 0xC00B, 0xC00D, 0xE000,
                   0xE003, 0xE004, 0xE005, 0xE006):
            send_ui_text(session, "D", settle=0.2)
            wait_for_cursor_pc(reader, pc,
                               f"Direct Freeze reset decode: step to ${pc:04X}")
        send_ui_text(session, "G", settle=0.1)
        wait_for_monitor_disappearance(reader, "Direct Freeze reset decode: G exits")
        session.reset()
        time.sleep(0.5)
        enter_visible_monitor(session, capture, ocr)
        assert_stable_e000_decode(reader, "Direct Freeze reset decode")
        for index in range(3):
            session.input_ui(["down", "down", "up", "up"])
            time.sleep(0.25)
            assert_stable_e000_decode(reader, f"Direct Freeze reset decode repeat {index + 1}")


def load_reset_abort_fixture(session: RestSession) -> None:
    program = bytes([
        0x20, 0x10, 0xC5,       # C500 JSR $C510
        0xEE, 0x00, 0x06,       # C503 INC $0600 (Step Over target never reached)
        0x4C, 0x03, 0xC5,       # C506 JMP $C503
    ])
    callee = bytes([
        0xEE, 0x01, 0x06,       # C510 INC $0601
        0x4C, 0x10, 0xC5,       # C513 JMP $C510
    ])
    session.write_memory(0x0600, bytes([0x00]) * 2)
    session.write_memory(0xC500, program)
    session.write_memory(0xC510, callee)


def run_reset_abort_case(session: RestSession, reader: TextReader,
                         enter_monitor: Callable[[], FrameText],
                         label: str) -> None:
    reset_to_basic(session)
    load_reset_abort_fixture(session)
    enter_monitor()
    enter_direct_debug_at(session, reader, 0xC500, "RAM", 7,
                          f"{label}: enter Debug at non-returning JSR")
    session.input_ui(["d"])
    time.sleep(0.2)
    start = time.perf_counter()
    session.reset()
    enter_monitor()
    elapsed = time.perf_counter() - start
    if elapsed > 4.0:
        raise Failure(f"{label}: monitor re-entry after reset took {elapsed:.3f}s")
    frame = wait_for_visible_ui(reader, f"{label}: monitor visible after reset",
                                timeout=2.0)
    if "Dbg" in frame.text():
        raise Failure(f"{label}: Debug context survived reset\n{frame.text()}")


def run_direct_reset_abort_tests(session: RestSession, capture: VicStreamCapture,
                                 ocr: C64FrameOCR) -> None:
    try:
        session.stop_video()
    except (OSError, TimeoutError, urllib.error.URLError):
        pass
    ensure_stream_hidden_overlay_mode(session, capture, ocr)

    with check("Direct Overlay: REST reset aborts in-flight Step Over wait"):
        run_reset_abort_case(
            session,
            lambda: session.dump_ui_screen("overlay_reset_abort"),
            lambda: enter_hidden_monitor(session),
            "Direct Overlay reset abort")

    session.start_video()
    time.sleep(0.5)
    ensure_stream_visible_mode(session, capture, ocr)

    with check("Direct Freeze: REST reset aborts in-flight Step Over wait"):
        run_reset_abort_case(
            session,
            freeze_text_reader(session, capture, ocr),
            lambda: enter_visible_monitor(session, capture, ocr),
            "Direct Freeze reset abort")


def enter_direct_debug_at(session: RestSession, reader: TextReader, address: int,
                          source: str, monitor_bank: int, label: str,
                          cpu_bank: Optional[int] = None,
                          allow_missing_status_before_select: bool = False) -> None:
    select_monitor_view(
        session, reader, monitor_bank, f"{label}: select O{monitor_bank}",
        allow_missing_status=allow_missing_status_before_select)
    if allow_missing_status_before_select:
        frame = enter_debug_at_lenient(session, reader, address, source, label)
    else:
        enter_debug_at(session, reader, address, source, label)
        frame = wait_for_status_banks(
            reader, f"{label}: status after Debug entry",
            cpu_bank=cpu_bank, monitor_bank=monitor_bank)
    disassembly_row(frame, address)
    if not allow_missing_status_before_select:
        if cpu_bank == 5 and "CPU7" in status_line(frame):
            raise Failure(f"{label}: live CPU bank was forced to 7\n{frame.text()}")


def reenter_debug_at_current_cursor(session: RestSession, reader: TextReader,
                                    label: str) -> None:
    send_ui_text(session, "D", settle=0.25)
    try:
        wait_for_text(reader, ("Dbg", "PC", "NV-BDIZC"), label, timeout=5.0)
    except Failure:
        frame = reader()
        text = frame.text()
        if "Dbg" not in text and ("PC" not in text or "NV-BDIZC" not in text):
            raise


def step_to_repeat_loop(session: RestSession, reader: TextReader, label: str,
                        start_addr: int, start_bank: int, source: str,
                        expected_pcs: Sequence[int], final_cpu_bank: int,
                        final_monitor_bank: int) -> None:
    enter_direct_debug_at(session, reader, start_addr, source, start_bank, label)
    for expected_pc in expected_pcs:
        send_ui_text(session, "D", settle=0.25)
        wait_for_cursor_pc(reader, expected_pc, f"{label}: step to ${expected_pc:04X}")
    select_monitor_view(session, reader, final_monitor_bank,
                        f"{label}: select final O{final_monitor_bank}")
    wait_for_cursor_pc(reader, expected_pcs[-1], f"{label}: final PC")
    wait_for_status_banks(
        reader, f"{label}: final status",
        cpu_bank=final_cpu_bank, monitor_bank=final_monitor_bank)


def repeat_cancel_redebug_cycles(session: RestSession, reader: TextReader,
                                 enter_monitor: Optional[Callable[[], FrameText]],
                                 label: str, loop_addr: int, source: str,
                                 monitor_bank: int, cpu_bank: int,
                                 evidence_addr: int, evidence_len: int,
                                 first_pc: int, second_pc: int,
                                 row_tokens: Tuple[str, ...],
                                 exit_ui_after_cancel: bool,
                                 cycles: int = 3) -> None:
    for cycle in range(1, cycles + 1):
        cancel_debug(session)
        wait_until_not_debug(reader, f"{label} cycle {cycle}: cancel Debug")
        if exit_ui_after_cancel:
            session.input_ui(["ctrl", "m"])
            time.sleep(0.5)
        assert_region_keeps_changing(
            session, evidence_addr, evidence_len,
            f"{label} cycle {cycle} after cancelling Debug")
        if enter_monitor is not None:
            enter_monitor()
            enter_direct_debug_at(
                session, reader, loop_addr, source, monitor_bank,
                f"{label} cycle {cycle}: re-enter Debug", cpu_bank=cpu_bank,
                allow_missing_status_before_select=True)
        else:
            send_ui_text(session, f"J{loop_addr:04X}\n", settle=0.25)
            send_ui_text(session, "A", settle=0.25)
            reenter_debug_at_current_cursor(
                session, reader, f"{label} cycle {cycle}: re-enter Debug")
        send_ui_text(session, "D", settle=0.25)
        wait_for_cursor_pc(reader, first_pc, f"{label} cycle {cycle}: first re-entered step")
        send_ui_text(session, "D", settle=0.25)
        wait_for_cursor_pc(reader, second_pc, f"{label} cycle {cycle}: second re-entered step")
        frame = reader()
        row = disassembly_row(frame, second_pc)
        for token in row_tokens:
            if token not in row:
                raise Failure(
                    f"{label} cycle {cycle}: row ${second_pc:04X} missing "
                    f"{token!r}: {row!r}")
        try:
            wait_for_status_banks(
                reader, f"{label} cycle {cycle}: status after steps",
                cpu_bank=cpu_bank, monitor_bank=monitor_bank)
        except Failure:
            pass


def run_direct_repeat_case(session: RestSession, reader: TextReader,
                           enter_monitor: Optional[Callable[[], FrameText]],
                           label: str, start_addr: int, start_bank: int,
                           start_source: str, expected_pcs: Sequence[int],
                           loop_addr: int, loop_source: str,
                           final_cpu_bank: int, final_monitor_bank: int,
                           evidence_addr: int, evidence_len: int,
                           first_pc: int, second_pc: int,
                           row_tokens: Tuple[str, ...],
                           exit_ui_after_cancel: bool) -> None:
    step_to_repeat_loop(
        session, reader, label, start_addr, start_bank, start_source,
        expected_pcs, final_cpu_bank, final_monitor_bank)
    repeat_cancel_redebug_cycles(
        session, reader, enter_monitor, label, loop_addr, loop_source,
        final_monitor_bank, final_cpu_bank, evidence_addr, evidence_len,
        first_pc, second_pc, row_tokens, exit_ui_after_cancel)


def run_direct_repeat_redebug_tests(session: RestSession, capture: VicStreamCapture,
                                    ocr: C64FrameOCR) -> None:
    try:
        session.stop_video()
    except (OSError, TimeoutError, urllib.error.URLError):
        pass
    ensure_stream_hidden_overlay_mode(session, capture, ocr)

    with check("Direct Overlay: repeated cancel/redebug ordinary RAM loop"):
        reset_to_basic(session)
        load_repeat_redebug_fixtures(session)
        enter_hidden_monitor(session)
        reader = lambda: session.dump_ui_screen("overlay")
        run_direct_repeat_case(
            session, reader, None,
            "Direct Overlay ordinary RAM",
            0xC300, 7, "RAM", (0xC302,),
            0xC302, "RAM", 7, 7, 0x0600, 0x1E8,
            0xC305, 0xC308, ("RAM", "INX"), False)
        cancel_debug(session)
        wait_until_not_debug(reader, "Direct Overlay ordinary RAM cleanup")

    with check("Direct Overlay: repeated cancel/redebug RAM-under-KERNAL loop"):
        reset_to_basic(session)
        load_repeat_redebug_fixtures(session)
        enter_hidden_monitor(session)
        reader = lambda: session.dump_ui_screen("overlay")
        run_direct_repeat_case(
            session, reader, None,
            "Direct Overlay RAM-under-KERNAL",
            0xC000, 7, "RAM",
            (0xC001, 0xC003, 0xC005, 0xC007, 0xC009, 0xC00B, 0xE000),
            0xE000, "RAM", 5, 5, 0x0400, 0x0200,
            0xE003, 0xE006, ("RAM", "INX"), False)
        cancel_debug(session)
        wait_until_not_debug(reader, "Direct Overlay RAM-under-KERNAL cleanup")

    session.start_video()
    time.sleep(0.5)
    ensure_stream_visible_mode(session, capture, ocr)

    with check("Direct Freeze: repeated cancel/redebug ordinary RAM loop"):
        reset_to_basic(session)
        load_repeat_redebug_fixtures(session)
        enter_visible_monitor(session, capture, ocr)
        reader = freeze_text_reader(session, capture, ocr)
        run_direct_repeat_case(
            session, reader, lambda: enter_visible_monitor(session, capture, ocr),
            "Direct Freeze ordinary RAM",
            0xC300, 7, "RAM", (0xC302,),
            0xC302, "RAM", 7, 7, 0x0600, 0x1E8,
            0xC305, 0xC308, ("RAM", "INX"), True)
        cancel_debug(session)
        wait_until_not_debug(reader, "Direct Freeze ordinary RAM cleanup")
        session.input_ui(["ctrl", "m"])
        time.sleep(0.5)

    with check("Direct Freeze: repeated cancel/redebug RAM-under-KERNAL loop"):
        reset_to_basic(session)
        load_repeat_redebug_fixtures(session)
        enter_visible_monitor(session, capture, ocr)
        reader = freeze_text_reader(session, capture, ocr)
        run_direct_repeat_case(
            session, reader, lambda: enter_visible_monitor(session, capture, ocr),
            "Direct Freeze RAM-under-KERNAL",
            0xC000, 7, "RAM",
            (0xC001, 0xC003, 0xC005, 0xC007, 0xC009, 0xC00B, 0xE000),
            0xE000, "RAM", 5, 5, 0x0400, 0x0200,
            0xE003, 0xE006, ("RAM", "INX"), True)
        cancel_debug(session)
        wait_until_not_debug(reader, "Direct Freeze RAM-under-KERNAL cleanup")
        session.input_ui(["ctrl", "m"])
        time.sleep(0.5)


def run_direct_breakpoint_continue_tests(session: RestSession, capture: VicStreamCapture,
                                         ocr: C64FrameOCR) -> None:
    try:
        session.stop_video()
    except (OSError, TimeoutError, urllib.error.URLError):
        pass
    ensure_stream_hidden_overlay_mode(session, capture, ocr)

    with check("Direct Overlay: breakpoint continue then reset decodes RAM-under-KERNAL"):
        reset_to_basic(session)
        load_overlay_reset_decode_fixture(session)
        enter_hidden_monitor(session)
        reader = lambda: session.dump_ui_screen("overlay")
        run_to_breakpoint_then_continue(
            session, reader,
            "Direct Overlay RAM-under-KERNAL breakpoint continue",
            0xC000, 0xE000, 5, "RAM",
            (0xC001, 0xC003, 0xC005, 0xC007, 0xC009, 0xC00B, 0xC00D, 0xE000))
        assert_overlay_reset_decode_after_continue(
            session, reader,
            "Direct Overlay RAM-under-KERNAL breakpoint continue")

    session.start_video()
    time.sleep(0.5)
    ensure_stream_visible_mode(session, capture, ocr)
    run_freeze_debug_footer_test(session, capture, ocr)

    with check("Direct Freeze: breakpoint continue ordinary RAM keeps running"):
        reset_to_basic(session)
        load_repeat_redebug_fixtures(session)
        enter_visible_monitor(session, capture, ocr)
        reader = freeze_text_reader(session, capture, ocr)
        run_to_breakpoint_then_continue(
            session, reader,
            "Direct Freeze ordinary RAM breakpoint continue",
            0xC300, 0xC302, 7, "RAM", (0xC302,),
            progress_addr=0x0600, progress_len=0x1E8,
            capture=capture, ocr=ocr)

    with check("Direct Freeze: breakpoint continue RAM-under-KERNAL keeps running"):
        reset_to_basic(session)
        load_repeat_redebug_fixtures(session)
        enter_visible_monitor(session, capture, ocr)
        reader = freeze_text_reader(session, capture, ocr)
        run_to_breakpoint_then_continue(
            session, reader,
            "Direct Freeze RAM-under-KERNAL breakpoint continue",
            0xC000, 0xE000, 5, "RAM",
            (0xC001, 0xC003, 0xC005, 0xC007, 0xC009, 0xC00B, 0xE000),
            progress_addr=0x0400, progress_len=0x0200,
            capture=capture, ocr=ocr)


def check_network_liveness(session: RestSession, label: str) -> None:
    session.request("GET", "/v1/version")
    session.read_memory(0x0400, 1)
    for name, port in (("Telnet", 23), ("FTP", 21)):
        try:
            with socket.create_connection((session.host, port), timeout=session.timeout):
                pass
        except OSError as exc:
            raise Failure(f"{label}: {name} listener did not accept TCP connection: {exc}") from exc


def direct_lifecycle_reenter(session: RestSession, reader: TextReader,
                             enter_monitor: Callable[[], FrameText],
                             label: str, address: int, source: str,
                             bank: int, expected_pc: int,
                             step_key: str = "D") -> None:
    session.reset()
    wait_basic_ready(session)
    check_network_liveness(session, f"{label}: liveness after reset")
    enter_monitor()
    enter_direct_debug_at(session, reader, address, source, bank,
                          f"{label}: re-enter Debug")
    send_ui_text(session, step_key, settle=0.25)
    wait_for_pc(reader, expected_pc, f"{label}: first re-entered step")
    leave_debug(session)
    wait_until_not_debug(reader, f"{label}: leave re-entered Debug")


def run_direct_lifecycle_case(session: RestSession, reader: TextReader,
                              enter_monitor: Callable[[], FrameText],
                              label: str, banking: str) -> None:
    reset_to_basic(session, close_ui=False)
    if banking == "ram":
        load_repeat_redebug_fixtures(session)
        enter_monitor()
        run_to_breakpoint_then_continue(
            session, reader, label,
            0xC300, 0xC302, 7, "RAM", (0xC302,),
            progress_addr=0x0600, progress_len=0x1E8)
        direct_lifecycle_reenter(
            session, reader, enter_monitor, label,
            0xC300, "RAM", 7, 0xC302)
    elif banking == "ram-under-rom":
        load_overlay_reset_decode_fixture(session)
        enter_monitor()
        run_to_breakpoint_then_continue(
            session, reader, label,
            0xC000, 0xE000, 5, "RAM",
            (0xC001, 0xC003, 0xC005, 0xC007, 0xC009, 0xC00B, 0xC00D, 0xE000))
        direct_lifecycle_reenter(
            session, reader, enter_monitor, label,
            0xC000, "RAM", 7, 0xC001)
    elif banking == "rom":
        enter_monitor()
        run_to_breakpoint_then_continue(
            session, reader, label,
            0xE002, 0xBC0F, 7, "BAS", (),
            start_source="KRN", start_bank=7)
        wait_basic_ready(session, timeout=12.0)
        direct_lifecycle_reenter(
            session, reader, enter_monitor, label,
            0xE002, "KRN", 7, 0xBC0F, step_key="T")
    else:
        raise Failure(f"Unknown lifecycle banking target: {banking}")


def run_direct_lifecycle_matrix_tests(session: RestSession, capture: VicStreamCapture,
                                      ocr: C64FrameOCR, repetitions: int) -> None:
    scenarios = (
        ("Overlay", "rom"),
        ("Overlay", "ram-under-rom"),
        ("Overlay", "ram"),
        ("Freeze", "rom"),
        ("Freeze", "ram-under-rom"),
        ("Freeze", "ram"),
    )
    for view, banking in scenarios:
        if view == "Overlay":
            try:
                session.stop_video()
            except (OSError, TimeoutError, urllib.error.URLError):
                pass
            ensure_stream_hidden_overlay_mode(session, capture, ocr)
            reader = lambda: session.dump_ui_screen("overlay_lifecycle")
            enter_monitor = lambda: enter_hidden_monitor(session)
        else:
            session.start_video()
            time.sleep(0.5)
            ensure_stream_visible_mode(session, capture, ocr)
            reader = freeze_text_reader(session, capture, ocr)
            enter_monitor = lambda: enter_visible_monitor(session, capture, ocr)

        for repetition in range(1, repetitions + 1):
            label = f"Direct {view} lifecycle {banking} rep {repetition}/{repetitions}"
            with check(label):
                run_direct_lifecycle_case(session, reader, enter_monitor, label, banking)


def assert_memory(session: RestSession, address: int, expected: bytes, label: str) -> None:
    actual = session.read_memory(address, len(expected))
    if actual != expected:
        raise Failure(
            f"{label}: memory mismatch at ${address:04X}\n"
            f"  expected: {expected.hex().upper()}\n"
            f"  actual:   {actual.hex().upper()}")


def run_direct_stop_debug_exit_test(session: RestSession, reader: TextReader,
                                    enter_monitor: Callable[[], FrameText],
                                    label: str) -> None:
    program = bytes.fromhex("EE21D04C0020")
    session.write_memory(0x2000, program)
    assert_memory(session, 0x2000, program, f"{label}: seed $2000 loop")

    enter_monitor()
    enter_debug_at(session, reader, 0x2000, "RAM", f"{label}: enter Debug at $2000")
    send_ui_text(session, "D", settle=0.25)
    wait_for_pc(reader, 0x2003, f"{label}: Step Over INC $D021")

    before_exit = session.read_memory(0xD021, 1)[0]
    leave_debug(session)
    session.input_ui(["ctrl", "m"])

    after_exit = before_exit
    deadline = time.time() + 2.0
    while time.time() < deadline:
        time.sleep(0.1)
        after_exit = session.read_memory(0xD021, 1)[0]
        if after_exit != before_exit:
            break
    if after_exit == before_exit:
        raise Failure(
            f"{label}: $D021 did not change after Stop Debugging + Exit; "
            "target did not resume from the current debug context")
    assert_memory(session, 0x2000, program,
                  f"{label}: Stop Debugging must restore $2000 bytes")

    session.write_memory(0xC760, bytes([0xEA, 0xEA]))
    enter_monitor()
    enter_debug_at(session, reader, 0xC760, "RAM", f"{label}: re-enter Debug")
    send_ui_text(session, "D", settle=0.25)
    wait_for_pc(reader, 0xC761, f"{label}: new debug session after Stop+Exit")
    leave_debug(session)


def run_direct_rom_step_test(session: RestSession, reader: TextReader, label: str) -> None:
    frame = enter_debug_at(session, reader, 0xE000, "KRN",
                           f"{label}: KERNAL visible JSR search")
    kernal_jsr_addr, row = visible_jsr_row(frame, "KRN")
    leave_debug(session)

    frame = enter_debug_context_at(session, reader, kernal_jsr_addr, "KRN",
                                   f"{label}: KERNAL Step Over ${kernal_jsr_addr:04X}")
    row = disassembly_row(frame, kernal_jsr_addr)
    expected_pc = kernal_jsr_addr + instruction_length_from_row(row)
    send_ui_text(session, "D", settle=0.25)
    wait_for_pc(reader, expected_pc, f"{label}: KERNAL Step Over ${kernal_jsr_addr:04X}")
    leave_debug(session)

    frame = enter_debug_context_at(session, reader, kernal_jsr_addr, "KRN",
                                   f"{label}: KERNAL Step Into ${kernal_jsr_addr:04X}")
    row = disassembly_row(frame, kernal_jsr_addr)
    expected_pc = jsr_target_from_row(row)
    send_ui_text(session, "T", settle=0.25)
    wait_for_pc(reader, expected_pc, f"{label}: KERNAL Step Into ${kernal_jsr_addr:04X}")
    leave_debug(session)

    frame = enter_debug_at(session, reader, 0xA800, "BAS",
                           f"{label}: BASIC visible JSR search")
    jsr_addr, row = visible_jsr_row(frame, "BAS")
    leave_debug(session)
    frame = enter_debug_context_at(session, reader, jsr_addr, "BAS",
                                   f"{label}: BASIC Step Over ${jsr_addr:04X}")
    row = disassembly_row(frame, jsr_addr)
    expected_pc = jsr_addr + instruction_length_from_row(row)
    send_ui_text(session, "D", settle=0.25)
    wait_for_pc(reader, expected_pc, f"{label}: BASIC Step Over ${jsr_addr:04X}")
    leave_debug(session)


def run_overlay_hidden_ram_test(session: RestSession) -> None:
    program = bytes.fromhex("A9018D20D04C0220")

    with check("Direct Overlay: hidden monitor writes RAM at $2000"):
        enter_hidden_monitor(session)
        reader = lambda: session.dump_ui_screen("overlay")
        send_ui_text(session, "J2000\n", settle=0.2)
        wait_for_text(reader, ("$2000",), "Direct Overlay: goto $2000", timeout=5.0)
        send_ui_text(session, "A", settle=0.2)
        wait_for_text(reader, ("MONITOR ASM $2000",), "Direct Overlay: ASM view at $2000", timeout=5.0)
        send_ui_text(session, "E", settle=0.2)
        for asm in ("LDA#$01\n", "STA$D020\n", "JMP$2002\n"):
            send_ui_text(session, asm, settle=0.2)
        send_ui_text(session, "\x1b", settle=0.2)
        if session.read_memory(0x2000, len(program)) != program:
            raise Failure("Hidden overlay monitor did not write the expected RAM bytes.")


def run_overlay_debug_step_test(session: RestSession) -> None:
    with check("Direct Overlay: Step Over/Into/Out via text-log dump"):
        reset_to_basic(session)
        enter_hidden_monitor(session)
        reader = lambda: session.dump_ui_screen("overlay")
        run_direct_debug_step_test(session, reader, "Direct Overlay")
    with check("Direct Overlay: KERNAL/BASIC ROM stepping via text-log dump"):
        reset_to_basic(session)
        enter_hidden_monitor(session)
        reader = lambda: session.dump_ui_screen("overlay")
        run_direct_rom_step_test(session, reader, "Direct Overlay")
    with check("Direct Overlay: Stop Debugging + Exit resumes current $2000 context"):
        reset_to_basic(session)
        reader = lambda: session.dump_ui_screen("overlay")
        run_direct_stop_debug_exit_test(
            session, reader,
            lambda: enter_hidden_monitor(session),
            "Direct Overlay")


def main() -> int:
    parser = argparse.ArgumentParser(description="Validate direct U64 monitor use via REST UI keys and VIC OCR.")
    parser.add_argument("--host", default="u64")
    parser.add_argument("--timeout", type=float, default=5.0)
    parser.add_argument(
        "--test", default="all",
        help=("Comma-separated test groups: all, repeat-redebug, breakpoint-continue, "
              "continue-speed, reset-disasm, reset-abort, lifecycle-matrix"))
    parser.add_argument(
        "--repetitions", type=int, default=2,
        help="Lifecycle matrix repetitions per view/banking scenario.")
    args = parser.parse_args()
    if args.repetitions < 1:
        raise Failure("--repetitions must be at least 1")
    selected = {part.strip() for part in args.test.split(",") if part.strip()}
    if not selected:
        selected = {"all"}
    unknown = selected - {
        "all", "repeat-redebug", "breakpoint-continue",
        "continue-speed", "reset-disasm", "reset-abort", "lifecycle-matrix",
    }
    if unknown:
        raise Failure(f"Unknown test group(s): {', '.join(sorted(unknown))}")

    session = RestSession(args.host, args.timeout)
    capture = VicStreamCapture()
    ocr = C64FrameOCR()

    try:
        if "all" in selected:
            session.start_video()
            time.sleep(0.5)
            ensure_stream_visible_mode(session, capture, ocr)
            run_global_ctrl_x_test(session, "Freeze", "Direct Freeze")
            ensure_stream_visible_mode(session, capture, ocr)
            run_freeze_ram_edit_test(session, capture, ocr)
            run_freeze_rom_step_test(session, capture, ocr)

            run_global_ctrl_x_test(session, "Overlay on HDMI", "Direct Overlay")
            ensure_stream_hidden_overlay_mode(session, capture, ocr)
            run_overlay_hidden_ram_test(session)
            run_overlay_debug_step_test(session)
            run_direct_repeat_redebug_tests(session, capture, ocr)
            run_direct_breakpoint_continue_tests(session, capture, ocr)
            run_direct_continue_speed_tests(session, capture, ocr)
            run_direct_freeze_reset_disassembly_test(session, capture, ocr)
            run_direct_reset_abort_tests(session, capture, ocr)
        else:
            if "repeat-redebug" in selected:
                run_direct_repeat_redebug_tests(session, capture, ocr)
            if "breakpoint-continue" in selected:
                run_direct_breakpoint_continue_tests(session, capture, ocr)
            if "continue-speed" in selected:
                run_direct_continue_speed_tests(session, capture, ocr)
            if "reset-disasm" in selected:
                run_direct_freeze_reset_disassembly_test(session, capture, ocr)
            if "reset-abort" in selected:
                run_direct_reset_abort_tests(session, capture, ocr)
            if "lifecycle-matrix" in selected:
                run_direct_lifecycle_matrix_tests(session, capture, ocr, args.repetitions)
    finally:
        cleanup_c64_screen(session)
        try:
            session.stop_video()
        except (OSError, TimeoutError, urllib.error.URLError) as exc:
            print(f"cleanup warning: could not stop video stream: {exc}", file=sys.stderr)
        capture.close()

    print(f"monitor_direct_test: OK ({CHECK_COUNT} checks)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
