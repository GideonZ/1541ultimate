#!/usr/bin/env python3
import argparse
import errno
import fcntl
import glob
import http.client
import json
import os
import select
import shutil
import struct
import sys
import termios
import time
import tty
import urllib.error
from contextlib import contextmanager
from datetime import datetime
from typing import Any, Callable, Dict, List, Optional, Set, Tuple

from input_test import (
    Failure,
    RestInputSession,
    assert_joystick_ports,
    assert_keyboard_matrix_inputs,
    device_unavailable,
    format_exception,
    reset_to_basic,
    wait_for_input_ready,
)

KEY_MAP: Dict[str, str] = {
    " ": "space",
    "\r": "return",
    "\n": "return",
    "\t": "ctrl",
    "\x7f": "inst_del",
    "\b": "inst_del",
    ":": "colon",
    ";": "semicolon",
    ",": "comma",
    ".": "period",
    "/": "slash",
    "+": "plus",
    "-": "minus",
    "=": "equals",
    "@": "at",
    "*": "star",
    "^": "arrow_up",
    "_": "arrow_left",
    "£": "pound",
}

ARROW_KEYS = {
    "A": "up",
    "B": "down",
    "C": "right",
    "D": "left",
}
TERMINAL_SPECIAL_KEY_HELP_LINES = (
    "fallback: text/arrows/F1-F8/Home/Ins/Del/PgUp/PgDn, Tab=CTRL, Esc=release_all",
)
LOW_LEVEL_SPECIAL_KEY_HELP_LINES = (
    "keys: direct Linux mapping; Ctrl=C=, Tab=CTRL, Esc=release_all, Ctrl+Esc=quit",
)
DIRECT_KEY_SEQUENCE_MAP: Dict[str, List[str]] = {
    "\x1bOP": ["f1"],
    "\x1b[11~": ["f1"],
    "\x1b[[A": ["f1"],
    "\x1bOQ": ["left_shift", "f1"],
    "\x1b[12~": ["left_shift", "f1"],
    "\x1b[[B": ["left_shift", "f1"],
    "\x1bOR": ["f3"],
    "\x1b[13~": ["f3"],
    "\x1b[[C": ["f3"],
    "\x1bOS": ["left_shift", "f3"],
    "\x1b[14~": ["left_shift", "f3"],
    "\x1b[[D": ["left_shift", "f3"],
    "\x1b[15~": ["f5"],
    "\x1b[17~": ["left_shift", "f5"],
    "\x1b[18~": ["f7"],
    "\x1b[19~": ["left_shift", "f7"],
    "\x1b[H": ["clr_home"],
    "\x1bOH": ["clr_home"],
    "\x1b[1~": ["clr_home"],
    "\x1b[7~": ["clr_home"],
    "\x1b[2~": ["inst_del"],
    "\x1b[3~": ["inst_del"],
    "\x1b[F": ["run_stop"],
    "\x1bOF": ["run_stop"],
    "\x1b[4~": ["run_stop"],
    "\x1b[6~": ["run_stop"],
    "\x1b[8~": ["run_stop"],
    "\x1b[5~": ["restore"],
    "\x1b[24~": ["restore"],
}
ESCAPE_SEQUENCE_TIMEOUT = float(os.environ.get("U64_INPUT_ESCAPE_TIMEOUT", "0.10"))
MAX_BATCH_EVENTS = 64
KEYBOARD_TAP_BATCH_EVENTS = max(1, int(os.environ.get("U64_INPUT_KEYBOARD_TAP_BATCH_EVENTS", "10")))
BATCH_PACE_MS = int(os.environ.get("U64_INPUT_PACE_MS", "10"))
REPEAT_BATCH_EVENTS = max(1, int(os.environ.get("U64_INPUT_REPEAT_BATCH_EVENTS", "6")))
REPEAT_BATCH_TARGET_HZ = float(os.environ.get("U64_INPUT_REPEAT_HZ", "50.0"))
REPEAT_BATCH_INTERVAL = (float(REPEAT_BATCH_EVENTS) / REPEAT_BATCH_TARGET_HZ) if REPEAT_BATCH_TARGET_HZ > 0 else 0.06
REPEATED_KEYBOARD_TAP_PACE_MS = max(0, int(os.environ.get("U64_INPUT_REPEAT_PACE_MS", "80")))
KEY_REPEAT_STALE_SECONDS = float(os.environ.get("U64_INPUT_REPEAT_STALE", "0.25"))
KEY_REPEAT_TRIGGER_SECONDS = float(os.environ.get("U64_INPUT_REPEAT_TRIGGER", "0.50"))
KEY_REPEAT_CONFIRM_COUNT = max(2, int(os.environ.get("U64_INPUT_REPEAT_CONFIRM", "4")))
KEY_REPEAT_CADENCE_SECONDS = float(os.environ.get("U64_INPUT_REPEAT_CADENCE", "0.10"))
KEY_REPEAT_CADENCE_CONFIRM_COUNT = 2
GAMEPAD_AXIS_THRESHOLD = int(os.environ.get("U64_INPUT_GAMEPAD_AXIS_THRESHOLD", "12000"))
GAMEPAD_FIRE_REPEAT_HZ = float(os.environ.get("U64_INPUT_GAMEPAD_FIRE_REPEAT_HZ", "10.0"))
QUIT_SEQUENCES = ("\x03", "\x04")
JOYSTICK_INPUT_ORDER = ["up", "down", "left", "right", "fire", "fire2", "fire3"]

INPUT_EVENT_STRUCT = struct.Struct("llHHi")
EV_KEY = 0x01
EV_ABS = 0x03
EVIOCGRAB = 0x40044590
ABS_X = 0x00
ABS_Y = 0x01
ABS_RX = 0x03
ABS_RY = 0x04
ABS_HAT0X = 0x10
ABS_HAT0Y = 0x11
BTN_SOUTH = 0x130
BTN_EAST = 0x131
BTN_C = 0x132
BTN_NORTH = 0x133
BTN_WEST = 0x134
BTN_TRIGGER = 0x120
BTN_THUMB = 0x121
BTN_THUMB2 = 0x122
BTN_TOP = 0x123
GAMEPAD_NAME_KEYWORDS = ("xbox", "x-box", "controller", "gamepad", "joypad", "pad")
KEYBOARD_NAME_EXCLUSIONS = ("system control", "consumer control", "power button", "sleep button", "video bus")
KEY_ESC = 1
KEY_1 = 2
KEY_2 = 3
KEY_3 = 4
KEY_4 = 5
KEY_5 = 6
KEY_6 = 7
KEY_7 = 8
KEY_8 = 9
KEY_9 = 10
KEY_0 = 11
KEY_MINUS = 12
KEY_EQUAL = 13
KEY_BACKSPACE = 14
KEY_TAB = 15
KEY_Q = 16
KEY_W = 17
KEY_E = 18
KEY_R = 19
KEY_T = 20
KEY_Y = 21
KEY_U = 22
KEY_I = 23
KEY_O = 24
KEY_P = 25
KEY_LEFTBRACE = 26
KEY_RIGHTBRACE = 27
KEY_ENTER = 28
KEY_LEFTCTRL = 29
KEY_A = 30
KEY_S = 31
KEY_D = 32
KEY_F = 33
KEY_G = 34
KEY_H = 35
KEY_J = 36
KEY_K = 37
KEY_L = 38
KEY_SEMICOLON = 39
KEY_APOSTROPHE = 40
KEY_GRAVE = 41
KEY_LEFTSHIFT = 42
KEY_BACKSLASH = 43
KEY_Z = 44
KEY_X = 45
KEY_C = 46
KEY_V = 47
KEY_B = 48
KEY_N = 49
KEY_M = 50
KEY_COMMA = 51
KEY_DOT = 52
KEY_SLASH = 53
KEY_RIGHTSHIFT = 54
KEY_LEFTALT = 56
KEY_SPACE = 57
KEY_F1 = 59
KEY_F2 = 60
KEY_F3 = 61
KEY_F4 = 62
KEY_F5 = 63
KEY_F6 = 64
KEY_F7 = 65
KEY_F8 = 66
KEY_F9 = 67
KEY_F10 = 68
KEY_F11 = 87
KEY_F12 = 88
KEY_RIGHTCTRL = 97
KEY_RIGHTALT = 100
KEY_HOME = 102
KEY_UP = 103
KEY_PAGEUP = 104
KEY_LEFT = 105
KEY_RIGHT = 106
KEY_END = 107
KEY_DOWN = 108
KEY_PAGEDOWN = 109
KEY_INSERT = 110
KEY_DELETE = 111
LETTER_KEYCODES: Dict[int, str] = {
    KEY_A: "a", KEY_B: "b", KEY_C: "c", KEY_D: "d", KEY_E: "e", KEY_F: "f", KEY_G: "g", KEY_H: "h",
    KEY_I: "i", KEY_J: "j", KEY_K: "k", KEY_L: "l", KEY_M: "m", KEY_N: "n", KEY_O: "o", KEY_P: "p",
    KEY_Q: "q", KEY_R: "r", KEY_S: "s", KEY_T: "t", KEY_U: "u", KEY_V: "v", KEY_W: "w", KEY_X: "x",
    KEY_Y: "y", KEY_Z: "z",
}
DIGIT_KEYCODES: Dict[int, str] = {
    KEY_0: "0", KEY_1: "1", KEY_2: "2", KEY_3: "3", KEY_4: "4",
    KEY_5: "5", KEY_6: "6", KEY_7: "7", KEY_8: "8", KEY_9: "9",
}
KEYCODE_CHAR_MAP: Dict[int, Tuple[str, str]] = {
    KEY_1: ("1", "!"),
    KEY_2: ("2", "@"),
    KEY_3: ("3", "#"),
    KEY_4: ("4", "$"),
    KEY_5: ("5", "%"),
    KEY_6: ("6", "^"),
    KEY_7: ("7", "&"),
    KEY_8: ("8", "*"),
    KEY_9: ("9", "("),
    KEY_0: ("0", ")"),
    KEY_MINUS: ("-", "_"),
    KEY_EQUAL: ("=", "+"),
    KEY_LEFTBRACE: ("[", "{"),
    KEY_RIGHTBRACE: ("]", "}"),
    KEY_BACKSLASH: ("\\", "|"),
    KEY_SEMICOLON: (";", ":"),
    KEY_APOSTROPHE: ("'", "\""),
    KEY_GRAVE: ("`", "~"),
    KEY_COMMA: (",", "<"),
    KEY_DOT: (".", ">"),
    KEY_SLASH: ("/", "?"),
    KEY_SPACE: (" ", " "),
}
KEYCODE_BASE_INPUT_MAP: Dict[int, str] = {
    **LETTER_KEYCODES,
    **DIGIT_KEYCODES,
    KEY_MINUS: "minus",
    KEY_EQUAL: "equals",
    KEY_SEMICOLON: "semicolon",
    KEY_COMMA: "comma",
    KEY_DOT: "period",
    KEY_SLASH: "slash",
}
ARROW_KEYCODE_DIRECTION = {
    KEY_UP: "up",
    KEY_DOWN: "down",
    KEY_LEFT: "left",
    KEY_RIGHT: "right",
}
LOW_LEVEL_DIRECT_KEY_INPUTS: Dict[int, str] = {
    **LETTER_KEYCODES,
    **DIGIT_KEYCODES,
    KEY_MINUS: "minus",
    KEY_EQUAL: "equals",
    KEY_SEMICOLON: "semicolon",
    KEY_COMMA: "comma",
    KEY_DOT: "period",
    KEY_SLASH: "slash",
    KEY_SPACE: "space",
    KEY_ENTER: "return",
    KEY_BACKSPACE: "inst_del",
    KEY_DELETE: "inst_del",
    KEY_INSERT: "inst_del",
    KEY_HOME: "clr_home",
    KEY_END: "run_stop",
    KEY_PAGEDOWN: "run_stop",
    KEY_PAGEUP: "restore",
    KEY_RIGHT: "cursor_left_right",
    KEY_DOWN: "cursor_up_down",
    KEY_F1: "f1",
    KEY_F3: "f3",
    KEY_F5: "f5",
    KEY_F7: "f7",
    KEY_F12: "restore",
}
LOW_LEVEL_SHIFTED_KEY_INPUTS: Dict[int, str] = {
    KEY_LEFT: "cursor_left_right",
    KEY_UP: "cursor_up_down",
    KEY_F2: "f1",
    KEY_F4: "f3",
    KEY_F6: "f5",
    KEY_F8: "f7",
}


@contextmanager
def raw_terminal():
    fd = sys.stdin.fileno()
    old = termios.tcgetattr(fd)
    try:
        tty.setraw(fd)
        yield
    finally:
        termios.tcsetattr(fd, termios.TCSADRAIN, old)


def read_key_sequence(timeout: float = ESCAPE_SEQUENCE_TIMEOUT) -> str:
    ch = sys.stdin.read(1)
    if ch != "\x1b":
        return ch
    seq = ch
    while select.select([sys.stdin], [], [], timeout)[0]:
        seq += sys.stdin.read(1)
        if (
            seq.endswith("~")
            or (len(seq) >= 3 and seq[-1].isalpha())
            or (len(seq) >= 2 and not (seq.startswith("\x1b[") or seq.startswith("\x1bO")))
        ):
            break
    return seq


def keyboard_event(inputs: List[str]) -> Dict[str, object]:
    return {"kind": "keyboard", "inputs": inputs, "transition": "tap"}


def joystick_event(port: int, inputs: List[str]) -> Dict[str, object]:
    return {"kind": "joystick", "port": port, "inputs": inputs, "transition": "tap"}


def release_all_event() -> Dict[str, str]:
    return {"kind": "release_all"}


def event_signature(event: Dict[str, object]) -> Tuple[object, object, Tuple[object, ...], object]:
    return (
        event.get("kind"),
        event.get("port"),
        tuple(event.get("inputs", [])),  # type: ignore[arg-type]
        event.get("transition"),
    )


def cursor_keyboard_event(direction: str) -> Dict[str, object]:
    if direction == "up":
        return keyboard_event(["left_shift", "cursor_up_down"])
    if direction == "down":
        return keyboard_event(["cursor_up_down"])
    if direction == "left":
        return keyboard_event(["left_shift", "cursor_left_right"])
    return keyboard_event(["cursor_left_right"])


def ordered_keyboard_inputs(inputs: List[str]) -> List[str]:
    order = {"ctrl": 0, "commodore": 1, "left_shift": 2, "right_shift": 3}
    return sorted(inputs, key=lambda item: (order.get(item, 10), item))


def print_special_key_help(low_level_keyboard: bool = False) -> None:
    lines = LOW_LEVEL_SPECIAL_KEY_HELP_LINES if low_level_keyboard else TERMINAL_SPECIAL_KEY_HELP_LINES
    for line in lines:
        print(line)


def keyboard_inputs_for_char(ch: str) -> Optional[List[str]]:
    if len(ch) != 1:
        return None
    if "a" <= ch <= "z" or "0" <= ch <= "9":
        return [ch]
    if "A" <= ch <= "Z":
        return ["left_shift", ch.lower()]
    mapped = KEY_MAP.get(ch)
    if mapped:
        return [mapped]
    return None


def incomplete_escape_sequence(seq: str) -> bool:
    if not seq.startswith("\x1b"):
        return False
    if seq in ("\x1b", "\x1b[", "\x1bO"):
        return True
    if seq.startswith("\x1b["):
        return not (seq.endswith("~") or (len(seq) >= 3 and seq[-1].isalpha()))
    if seq.startswith("\x1bO"):
        return not (len(seq) >= 3 and seq[-1].isalpha())
    return False


class SequenceDecoder:
    def __init__(self) -> None:
        self.escape_pending: Optional[str] = None
        self.escape_pending_deadline = 0.0
        self.suppressed_literal: Optional[str] = None
        self.suppressed_literal_deadline = 0.0

    def clear(self) -> None:
        self.escape_pending = None
        self.escape_pending_deadline = 0.0
        self.suppressed_literal = None
        self.suppressed_literal_deadline = 0.0

    def timeout(self, now: float) -> Optional[float]:
        if self.suppressed_literal is not None and now >= self.suppressed_literal_deadline:
            self.suppressed_literal = None
            self.suppressed_literal_deadline = 0.0
        if self.escape_pending is None:
            return None
        return max(0.0, self.escape_pending_deadline - now)

    def poll(self, now: float) -> Tuple[Optional[str], List[Dict[str, object]]]:
        if self.escape_pending is None or now < self.escape_pending_deadline:
            return None, []
        pending = self.escape_pending
        self.escape_pending = None
        self.escape_pending_deadline = 0.0
        if pending == "\x1b":
            return "release_all", [release_all_event()]
        return None, []

    def translate(self, seq: str, joystick_port: int, now: float) -> Tuple[str, Optional[Dict[str, object]], Optional[str]]:
        if self.suppressed_literal is not None:
            if now < self.suppressed_literal_deadline and seq == self.suppressed_literal:
                self.suppressed_literal = None
                self.suppressed_literal_deadline = 0.0
                return "ignore", None, None
            if now >= self.suppressed_literal_deadline:
                self.suppressed_literal = None
                self.suppressed_literal_deadline = 0.0
        if self.escape_pending is not None:
            seq = self.escape_pending + seq
            self.escape_pending = None
            self.escape_pending_deadline = 0.0

        if incomplete_escape_sequence(seq):
            self.escape_pending = seq
            self.escape_pending_deadline = now + ESCAPE_SEQUENCE_TIMEOUT
            return "prefix", None, None
        if seq in QUIT_SEQUENCES:
            return "quit", None, None
        event = translate_sequence(seq, joystick_port)
        if not event:
            return "ignore", None, None
        if seq.startswith("\x1b") and seq[-1:] in ARROW_KEYS:
            self.suppressed_literal = seq[-1]
            self.suppressed_literal_deadline = now + 0.05
        if event.get("kind") == "release_all":
            return "release_all", event, None
        return "event", event, seq


def summarize_input_list(inputs: List[object]) -> str:
    return "+".join(str(item) for item in inputs) if inputs else "-"


def summarize_event(event: Dict[str, object]) -> str:
    kind = event.get("kind")
    if kind == "release_all":
        return "release_all"
    inputs = summarize_input_list(list(event.get("inputs", [])))  # type: ignore[arg-type]
    transition = str(event.get("transition", "?"))
    if kind == "keyboard":
        return f"kbd:{transition}:{inputs}"
    if kind == "joystick":
        return f"joy{event.get('port', '?')}:{transition}:{inputs}"
    return str(kind)


def summarize_events(events: List[Dict[str, object]], max_items: int = 5) -> str:
    shown = [summarize_event(event) for event in events[:max_items]]
    if len(events) > max_items:
        shown.append(f"... +{len(events) - max_items}")
    return ", ".join(shown) if shown else "-"


def summarize_json_response(response: Dict[str, Any]) -> str:
    if not response:
        return "empty"
    if "keyboard" in response or "joysticks" in response:
        keyboard = summarize_input_list(response.get("keyboard", {}).get("inputs", []))
        joysticks = response.get("joysticks", [{}, {}])
        joy1 = summarize_input_list(joysticks[0].get("inputs", [])) if len(joysticks) > 0 else "-"
        joy2 = summarize_input_list(joysticks[1].get("inputs", [])) if len(joysticks) > 1 else "-"
        return f"kb={keyboard} j1={joy1} j2={joy2}"
    if "errors" in response:
        return f"errors={len(response.get('errors', []))}"
    keys = ",".join(sorted(response.keys()))
    return keys or "json"


class RestCallLogger:
    def __init__(self, level: int) -> None:
        self.level = level

    def _emit(self, line: str) -> None:
        if self.level <= 0:
            return
        timestamp = datetime.now().strftime("%H:%M:%S.%f")[:-3]
        sys.stdout.write(f"\r{timestamp} {line}\x1b[K\r\n")
        sys.stdout.flush()

    def _serialize_verbatim(self, payload: Optional[Any], content_type: Optional[str]) -> str:
        if payload is None:
            return "-"
        if isinstance(payload, (bytes, bytearray)):
            if content_type == "application/json":
                try:
                    return bytes(payload).decode("utf-8")
                except UnicodeDecodeError:
                    return bytes(payload).hex()
            return bytes(payload).hex()
        if isinstance(payload, str):
            return payload
        if isinstance(payload, (dict, list)):
            return json.dumps(payload, separators=(",", ":"), ensure_ascii=False)
        return str(payload)

    def _format_request(self, method: str, path: str, payload: Optional[Any], content_type: Optional[str]) -> str:
        if path == "/v1/machine:input" and isinstance(payload, dict):
            events = payload.get("events", [])
            if isinstance(events, list):
                pace_ms = payload.get("pace_ms")
                suffix = f" pace={pace_ms}" if pace_ms is not None else ""
                max_items = len(events) if self.level >= 2 else 5
                return f"{method} {path} events={len(events)}{suffix} [{summarize_events(events, max_items=max_items)}]"
        if isinstance(payload, dict):
            keys = ",".join(sorted(payload.keys()))
            return f"{method} {path} json={keys or '-'}"
        if isinstance(payload, (bytes, bytearray)):
            return f"{method} {path} body={len(payload)}B type={content_type or '-'}"
        return f"{method} {path}"

    def log_success(
        self,
        method: str,
        path: str,
        payload: Optional[Any],
        response: Optional[Any],
        elapsed_ms: float,
        content_type: Optional[str] = None,
        status: Optional[int] = None,
    ) -> None:
        if self.level <= 0:
            return
        request = self._format_request(method, path, payload, content_type)
        response_text = ""
        raw_response = ""
        if isinstance(response, dict):
            response_text = summarize_json_response(response)
            raw_response = self._serialize_verbatim(response, content_type)
        elif isinstance(response, (bytes, bytearray)):
            stripped = bytes(response).strip()
            if stripped.startswith(b"{") and stripped.endswith(b"}"):
                try:
                    decoded_json = json.loads(stripped.decode("utf-8"))
                    response_text = summarize_json_response(decoded_json)
                    raw_response = stripped.decode("utf-8")
                except Exception:
                    response_text = f"{len(response)}B"
                    raw_response = bytes(response).hex()
            else:
                response_text = f"{len(response)}B"
                raw_response = bytes(response).hex()
        elif response is not None:
            response_text = str(response)
            raw_response = str(response)
        status_text = f" -> {status}" if status is not None else ""
        tail = f" {response_text}" if response_text else ""
        self._emit(f"[rest] {request}{status_text}{tail} ({elapsed_ms:.1f} ms)")
        if self.level >= 2:
            self._emit(f"[rest] >>> {self._serialize_verbatim(payload, content_type)}")
            if raw_response:
                self._emit(f"[rest] <<< {status if status is not None else '-'} {raw_response}")

    def log_error(
        self,
        method: str,
        path: str,
        payload: Optional[Any],
        error: BaseException,
        elapsed_ms: float,
        content_type: Optional[str] = None,
        status: Optional[int] = None,
    ) -> None:
        if self.level <= 0:
            return
        request = self._format_request(method, path, payload, content_type)
        status_text = f" -> {status}" if status is not None else ""
        self._emit(f"[rest] {request}{status_text} ERROR {format_exception(error)} ({elapsed_ms:.1f} ms)")
        if self.level >= 2:
            self._emit(f"[rest] >>> {self._serialize_verbatim(payload, content_type)}")


class InputDeviceOpenError(OSError):
    def __init__(self, device_kind: str, path: str, error: OSError) -> None:
        self.device_kind = device_kind
        self.path = path
        self.original_error = error
        super().__init__(error.errno, error.strerror, error.filename)

    @property
    def permission_denied(self) -> bool:
        return self.errno in (errno.EACCES, errno.EPERM)


class GamepadState:
    def __init__(
        self,
        port: int,
        axis_threshold: int = GAMEPAD_AXIS_THRESHOLD,
        fire_repeat_hz: float = GAMEPAD_FIRE_REPEAT_HZ,
    ) -> None:
        self.port = port
        self.axis_threshold = axis_threshold
        self.fire_repeat_interval = 1.0 / fire_repeat_hz if fire_repeat_hz > 0 else 0.1
        self.left_x = 0
        self.left_y = 0
        self.right_x = 0
        self.right_y = 0
        self.hat_x = 0
        self.hat_y = 0
        self.a_pressed = False
        self.fire2_pressed = False
        self.fire3_pressed = False
        self.logical_inputs: Set[str] = set()

    def _ordered(self, inputs: Set[str]) -> List[str]:
        return [name for name in JOYSTICK_INPUT_ORDER if name in inputs]

    def _axis_inputs(self, x: int, y: int, threshold: int) -> Set[str]:
        inputs: Set[str] = set()
        if x <= -threshold:
            inputs.add("left")
        elif x >= threshold:
            inputs.add("right")
        if y <= -threshold:
            inputs.add("up")
        elif y >= threshold:
            inputs.add("down")
        return inputs

    def _recompute_logical_inputs(self) -> List[Dict[str, object]]:
        new_inputs = (
            self._axis_inputs(self.left_x, self.left_y, self.axis_threshold)
            | self._axis_inputs(self.right_x, self.right_y, self.axis_threshold)
            | self._axis_inputs(self.hat_x, self.hat_y, 1)
        )
        if self.a_pressed:
            new_inputs.add("fire")
        if self.fire2_pressed:
            new_inputs.add("fire2")
        if self.fire3_pressed:
            new_inputs.add("fire3")

        released = self.logical_inputs - new_inputs
        pressed = new_inputs - self.logical_inputs
        self.logical_inputs = new_inputs

        events: List[Dict[str, object]] = []
        if released:
            events.append(
                {
                    "kind": "joystick",
                    "port": self.port,
                    "inputs": self._ordered(released),
                    "transition": "release",
                }
            )
        if pressed:
            events.append(
                {
                    "kind": "joystick",
                    "port": self.port,
                    "inputs": self._ordered(pressed),
                    "transition": "press",
                }
            )
        return events

    def apply_input_event(self, event_type: int, code: int, value: int, now: float) -> List[Dict[str, object]]:
        if event_type == EV_ABS:
            if code == ABS_X:
                self.left_x = value
            elif code == ABS_Y:
                self.left_y = value
            elif code == ABS_RX:
                self.right_x = value
            elif code == ABS_RY:
                self.right_y = value
            elif code == ABS_HAT0X:
                self.hat_x = value
            elif code == ABS_HAT0Y:
                self.hat_y = value
            else:
                return []
            return self._recompute_logical_inputs()

        if event_type != EV_KEY:
            return []

        if code in (BTN_SOUTH, BTN_NORTH, BTN_TRIGGER, BTN_THUMB2):
            self.a_pressed = value != 0
            return self._recompute_logical_inputs()

        if code in (BTN_EAST, BTN_THUMB):
            self.fire2_pressed = value != 0
            return self._recompute_logical_inputs()

        if code in (BTN_WEST, BTN_TOP, BTN_C):
            self.fire3_pressed = value != 0
            return self._recompute_logical_inputs()

        return []

    def poll_repeat_fire(self, now: float) -> Optional[Dict[str, object]]:
        return None

    def repeat_timeout(self, now: float) -> Optional[float]:
        return None


class GamepadDevice:
    def __init__(self, path: str, port: int) -> None:
        self.path = path
        self.fd = os.open(path, os.O_RDONLY | os.O_NONBLOCK)
        self.state = GamepadState(port)

    def fileno(self) -> int:
        return self.fd

    def close(self) -> None:
        os.close(self.fd)

    def read_events(self, now: float) -> List[Dict[str, object]]:
        events: List[Dict[str, object]] = []
        while True:
            try:
                data = os.read(self.fd, INPUT_EVENT_STRUCT.size * 32)
            except BlockingIOError:
                break
            if not data:
                break
            limit = len(data) - (len(data) % INPUT_EVENT_STRUCT.size)
            for offset in range(0, limit, INPUT_EVENT_STRUCT.size):
                _, _, event_type, code, value = INPUT_EVENT_STRUCT.unpack_from(data, offset)
                events.extend(self.state.apply_input_event(event_type, code, value, now))
            if len(data) < INPUT_EVENT_STRUCT.size * 32:
                break
        return events


class InteractiveRestClient:
    def __init__(self, session: RestInputSession, logger: Optional[RestCallLogger] = None) -> None:
        self.host = session.host
        self.password = session.password
        self.timeout = session.timeout
        self._connection: Optional[http.client.HTTPConnection] = None
        self.logger = logger

    def close(self) -> None:
        if self._connection is None:
            return
        try:
            self._connection.close()
        except Exception:
            pass
        self._connection = None

    def _connect(self) -> http.client.HTTPConnection:
        if self._connection is None:
            self._connection = http.client.HTTPConnection(self.host, timeout=self.timeout)
        return self._connection

    def post_events(self, events: List[Dict[str, object]], pace_ms: Optional[int] = None) -> Dict[str, object]:
        payload: Dict[str, object] = {"events": events}
        effective_pace_ms = pace_ms
        if effective_pace_ms is None and len(events) > 1 and BATCH_PACE_MS > 0:
            effective_pace_ms = BATCH_PACE_MS
        if effective_pace_ms is not None:
            payload["pace_ms"] = effective_pace_ms
        body = json.dumps(payload).encode("utf-8")
        headers: Dict[str, str] = {"Content-Type": "application/json"}
        if self.password:
            headers["X-Password"] = self.password
        for attempt in range(2):
            connection = self._connect()
            started = time.monotonic()
            try:
                connection.request("POST", "/v1/machine:input", body=body, headers=headers)
                response = connection.getresponse()
                data = response.read()
                elapsed_ms = (time.monotonic() - started) * 1000.0
                if response.status >= 400:
                    if self.logger:
                        self.logger.log_error(
                            "POST",
                            "/v1/machine:input",
                            payload,
                            Failure(f"HTTP {response.status}"),
                            elapsed_ms,
                            content_type="application/json",
                            status=response.status,
                        )
                    self.close()
                    text = data.decode("utf-8", errors="replace").strip()
                    raise Failure(f"REST input POST failed with HTTP {response.status}: {text}")
                if not data:
                    if self.logger:
                        self.logger.log_success(
                            "POST",
                            "/v1/machine:input",
                            payload,
                            {},
                            elapsed_ms,
                            content_type="application/json",
                            status=response.status,
                        )
                    return {}
                decoded = json.loads(data.decode("utf-8"))
                if self.logger:
                    self.logger.log_success(
                        "POST",
                        "/v1/machine:input",
                        payload,
                        decoded,
                        elapsed_ms,
                        content_type="application/json",
                        status=response.status,
                    )
                return decoded
            except urllib.error.HTTPError as exc:
                if self.logger:
                    self.logger.log_error(
                        "POST",
                        "/v1/machine:input",
                        payload,
                        exc,
                        (time.monotonic() - started) * 1000.0,
                        content_type="application/json",
                        status=exc.code,
                    )
                self.close()
                if attempt == 0:
                    continue
                raise
            except (OSError, http.client.HTTPException, json.JSONDecodeError) as exc:
                if self.logger:
                    self.logger.log_error(
                        "POST",
                        "/v1/machine:input",
                        payload,
                        exc,
                        (time.monotonic() - started) * 1000.0,
                        content_type="application/json",
                    )
                self.close()
                if attempt == 0:
                    continue
                raise Failure(f"Interactive REST request failed: {format_exception(exc)}") from exc
        raise Failure("Interactive REST request failed.")


class VerboseRestSession(RestInputSession):
    def __init__(self, host: str, password: Optional[str], timeout: float, logger: RestCallLogger) -> None:
        super().__init__(host, password, timeout)
        self.logger = logger

    def request(
        self,
        method: str,
        path: str,
        params: Optional[Dict[str, Any]] = None,
        body: Optional[bytes] = None,
        content_type: Optional[str] = "application/json",
    ) -> bytes:
        started = time.monotonic()
        payload: Optional[Any] = None
        if content_type == "application/json" and body:
            try:
                payload = json.loads(body.decode("utf-8"))
            except Exception:
                payload = body
        elif body is not None:
            payload = body
        full_path = path
        if params:
            full_path = self.url(path, params).replace(f"http://{self.host}", "", 1)
        try:
            data = super().request(method, path, params=params, body=body, content_type=content_type)
            self.logger.log_success(method, full_path, payload, data, (time.monotonic() - started) * 1000.0, content_type=content_type)
            return data
        except urllib.error.HTTPError as exc:
            self.logger.log_error(method, full_path, payload, exc, (time.monotonic() - started) * 1000.0, content_type=content_type, status=exc.code)
            raise
        except (OSError, TimeoutError, urllib.error.URLError, http.client.HTTPException) as exc:
            self.logger.log_error(method, full_path, payload, exc, (time.monotonic() - started) * 1000.0, content_type=content_type)
            raise


class RepeatState:
    def __init__(self) -> None:
        self.sequence: Optional[str] = None
        self.signature: Optional[Tuple[object, object, Tuple[object, ...], object]] = None
        self.event: Optional[Dict[str, object]] = None
        self.count = 0
        self.confirmed = False
        self.first_seen_at = 0.0
        self.last_seen_at = 0.0
        self.next_batch_at = 0.0
        self.fast_cadence_count = 0

    def clear(self) -> None:
        self.sequence = None
        self.signature = None
        self.event = None
        self.count = 0
        self.confirmed = False
        self.first_seen_at = 0.0
        self.last_seen_at = 0.0
        self.next_batch_at = 0.0
        self.fast_cadence_count = 0

    def _active(self) -> bool:
        return self.event is not None and self.sequence is not None and self.signature is not None

    def timeout(self, now: float) -> Optional[float]:
        if not self._active():
            return None
        stale_deadline = self.last_seen_at + KEY_REPEAT_STALE_SECONDS
        if not self.confirmed and self.count >= KEY_REPEAT_CONFIRM_COUNT:
            return max(0.0, min(stale_deadline, self.first_seen_at + KEY_REPEAT_TRIGGER_SECONDS) - now)
        if self.confirmed:
            return max(0.0, min(stale_deadline, self.next_batch_at) - now)
        return max(0.0, stale_deadline - now)

    def _repeatable(self, event: Dict[str, object]) -> bool:
        return (
            event.get("kind") == "keyboard"
            and event.get("transition") == "tap"
            and "restore" not in event.get("inputs", [])
        )

    def stop(self) -> None:
        self.clear()

    def _start(
        self,
        sequence: str,
        signature: Tuple[object, object, Tuple[object, ...], object],
        event: Dict[str, object],
        now: float,
    ) -> None:
        self.sequence = sequence
        self.signature = signature
        self.event = event
        self.count = 1
        self.confirmed = False
        self.first_seen_at = now
        self.last_seen_at = now
        self.next_batch_at = 0.0
        self.fast_cadence_count = 0

    def observe(self, sequence: str, event: Dict[str, object], now: float) -> Tuple[bool, List[Dict[str, object]]]:
        if not self._repeatable(event):
            self.clear()
            return False, []

        signature = event_signature(event)
        if self.sequence == sequence and self.signature == signature:
            interval = now - self.last_seen_at
            if not self.confirmed and interval > KEY_REPEAT_CADENCE_SECONDS:
                self.clear()
                self._start(sequence, signature, event, now)
                return False, []
            self.count += 1
            self.last_seen_at = now
            if interval <= KEY_REPEAT_CADENCE_SECONDS:
                self.fast_cadence_count += 1
            else:
                self.fast_cadence_count = 0
            if self.confirmed:
                return True, []
            if (
                self.count >= KEY_REPEAT_CONFIRM_COUNT
                and self.fast_cadence_count >= KEY_REPEAT_CADENCE_CONFIRM_COUNT
                and (now - self.first_seen_at) >= KEY_REPEAT_TRIGGER_SECONDS
            ):
                self.confirmed = True
                self.next_batch_at = now + REPEAT_BATCH_INTERVAL
                return True, [self.event] * REPEAT_BATCH_EVENTS if self.event is not None else []
            return True, []

        self.clear()
        self._start(sequence, signature, event, now)
        return False, []

    def poll(self, now: float) -> List[Dict[str, object]]:
        if not self._active():
            return []
        if (now - self.last_seen_at) > KEY_REPEAT_STALE_SECONDS:
            self.clear()
            return []
        if not self.confirmed:
            if (
                self.event is None
                or self.count < KEY_REPEAT_CONFIRM_COUNT
                or self.fast_cadence_count < KEY_REPEAT_CADENCE_CONFIRM_COUNT
                or now < (self.first_seen_at + KEY_REPEAT_TRIGGER_SECONDS)
            ):
                return []
            self.confirmed = True
            self.next_batch_at = now + REPEAT_BATCH_INTERVAL
            return [self.event] * REPEAT_BATCH_EVENTS
        if self.event is None or now < self.next_batch_at:
            return []
        while self.next_batch_at <= now:
            self.next_batch_at += REPEAT_BATCH_INTERVAL
        return [self.event] * REPEAT_BATCH_EVENTS


def combine_timeout(*timeouts: Optional[float]) -> Optional[float]:
    values = [timeout for timeout in timeouts if timeout is not None]
    if not values:
        return None
    return min(values)


def drain_stdin_sequences(stdin_fd: int) -> List[str]:
    sequences = [read_key_sequence()]
    while select.select([stdin_fd], [], [], 0)[0]:
        sequences.append(read_key_sequence())
    return sequences


def keyboard_batch_pace_ms(events: List[Dict[str, object]]) -> int:
    if not events:
        return 0
    if not all(event.get("kind") == "keyboard" and event.get("transition") == "tap" for event in events):
        return 0
    if len(events) <= 1:
        return 0
    first_signature = event_signature(events[0])
    for event in events[1:]:
        if event_signature(event) != first_signature:
            return 0
    return REPEATED_KEYBOARD_TAP_PACE_MS


def flush_event_batch(client: InteractiveRestClient, events: List[Dict[str, object]]) -> None:
    while events:
        keyboard_taps_only = all(event.get("kind") == "keyboard" and event.get("transition") == "tap" for event in events)
        chunk_size = KEYBOARD_TAP_BATCH_EVENTS if keyboard_taps_only else MAX_BATCH_EVENTS
        chunk = events[:chunk_size]
        pace_ms = keyboard_batch_pace_ms(chunk) if keyboard_taps_only else None
        client.post_events(chunk, pace_ms=pace_ms)
        del events[:chunk_size]


def translate_sequence(seq: str, joystick_port: int) -> Optional[Dict[str, object]]:
    inputs = DIRECT_KEY_SEQUENCE_MAP.get(seq)
    if inputs:
        return keyboard_event(inputs)
    if seq == "\x1b":
        return release_all_event()
    if (seq.startswith("\x1b[") or seq.startswith("\x1bO")) and ";" not in seq and seq[-1:] in ARROW_KEYS:
        direction = ARROW_KEYS[seq[-1]]
        return cursor_keyboard_event(direction)
    inputs = keyboard_inputs_for_char(seq)
    if inputs:
        return keyboard_event(inputs)
    return None


def input_event_name(path: str) -> str:
    event_name = os.path.basename(os.path.realpath(path))
    if not event_name.startswith("event"):
        return ""
    sysfs_name = f"/sys/class/input/{event_name}/device/name"
    try:
        with open(sysfs_name, "r", encoding="utf-8") as fh:
            return fh.read().strip()
    except OSError:
        return ""


def input_event_path(path: str) -> str:
    try:
        return os.path.realpath(path)
    except OSError:
        return path


def keyboard_device_rank(path: str) -> Tuple[int, str, str]:
    name = input_event_name(path).lower()
    basename = os.path.basename(path).lower()
    score = 0
    if basename.endswith("-event-kbd"):
        score += 200
    if name:
        score += 20
    if "keyboard" in name or "kbd" in name:
        score += 100
    if any(keyword in name for keyword in KEYBOARD_NAME_EXCLUSIONS):
        score -= 200
    if "mouse" in name or "joystick" in name or "gamepad" in name:
        score -= 200
    return (-score, name, path)


def find_default_keyboard_device() -> Optional[str]:
    candidates = list(glob.glob("/dev/input/by-id/*-event-kbd"))
    candidates.extend(glob.glob("/dev/input/by-path/*-event-kbd"))
    deduped: List[str] = []
    seen: Set[str] = set()
    for path in candidates:
        resolved = input_event_path(path)
        if resolved in seen:
            continue
        seen.add(resolved)
        deduped.append(path)
    if deduped:
        return sorted(deduped, key=keyboard_device_rank)[0]
    fallback = sorted(glob.glob("/dev/input/event*"))
    filtered = []
    for path in fallback:
        name = input_event_name(path).lower()
        if not name:
            continue
        if any(keyword in name for keyword in KEYBOARD_NAME_EXCLUSIONS):
            continue
        if "keyboard" in name or "kbd" in name:
            filtered.append(path)
    if filtered:
        return sorted(filtered, key=keyboard_device_rank)[0]
    return None


def keyboard_state_event(inputs: List[str], transition: str) -> Dict[str, object]:
    return {"kind": "keyboard", "inputs": inputs, "transition": transition}


class HeldLogicalKey:
    def __init__(self, input_name: str) -> None:
        self.input_name = input_name
        self.sources: Set[object] = set()

    def active(self) -> bool:
        return bool(self.sources)

    def press(self, source: object) -> Optional[Dict[str, object]]:
        if source in self.sources:
            return None
        was_active = self.active()
        self.sources.add(source)
        if not was_active:
            return keyboard_state_event([self.input_name], "press")
        return None

    def release(self, source: object) -> Optional[Dict[str, object]]:
        if source not in self.sources:
            return None
        self.sources.remove(source)
        if not self.active():
            return keyboard_state_event([self.input_name], "release")
        return None


class LowLevelKeyboardState:
    def __init__(self, joystick_port: int) -> None:
        self.joystick_port = joystick_port
        self.left_shift = HeldLogicalKey("left_shift")
        self.right_shift = HeldLogicalKey("right_shift")
        self.ctrl = HeldLogicalKey("ctrl")
        self.commodore = HeldLogicalKey("commodore")

    def _shift_active(self) -> bool:
        return self.left_shift.active() or self.right_shift.active()

    def _press_modifier(self, code: int) -> Optional[Dict[str, object]]:
        if code == KEY_LEFTSHIFT:
            return self.left_shift.press(("phys", code))
        if code == KEY_RIGHTSHIFT:
            return self.right_shift.press(("phys", code))
        if code == KEY_LEFTCTRL:
            return self.commodore.press(("phys", code))
        if code == KEY_RIGHTCTRL:
            return self.commodore.press(("phys", code))
        if code == KEY_TAB:
            return self.ctrl.press(("phys", code))
        return None

    def _release_modifier(self, code: int) -> Optional[Dict[str, object]]:
        if code == KEY_LEFTSHIFT:
            return self.left_shift.release(("phys", code))
        if code == KEY_RIGHTSHIFT:
            return self.right_shift.release(("phys", code))
        if code == KEY_LEFTCTRL:
            return self.commodore.release(("phys", code))
        if code == KEY_RIGHTCTRL:
            return self.commodore.release(("phys", code))
        if code == KEY_TAB:
            return self.ctrl.release(("phys", code))
        return None

    def _press_mapped_key(self, code: int) -> List[Dict[str, object]]:
        direct_input = LOW_LEVEL_DIRECT_KEY_INPUTS.get(code)
        if direct_input is not None:
            return [keyboard_state_event([direct_input], "press")]

        shifted_input = LOW_LEVEL_SHIFTED_KEY_INPUTS.get(code)
        if shifted_input is not None:
            events: List[Dict[str, object]] = []
            if not self._shift_active():
                event = self.left_shift.press(("synthetic", code))
                if event is not None:
                    events.append(event)
            events.append(keyboard_state_event([shifted_input], "press"))
            return events
        return []

    def _release_mapped_key(self, code: int) -> List[Dict[str, object]]:
        direct_input = LOW_LEVEL_DIRECT_KEY_INPUTS.get(code)
        if direct_input is not None:
            return [keyboard_state_event([direct_input], "release")]

        shifted_input = LOW_LEVEL_SHIFTED_KEY_INPUTS.get(code)
        if shifted_input is not None:
            events: List[Dict[str, object]] = [keyboard_state_event([shifted_input], "release")]
            event = self.left_shift.release(("synthetic", code))
            if event is not None:
                events.append(event)
            return events
        return []

    def apply_input_event(self, code: int, value: int) -> Tuple[Optional[str], List[Dict[str, object]]]:
        if value == 0:
            event = self._release_modifier(code)
            if event is not None:
                return None, [event]
            return None, self._release_mapped_key(code)
        if value != 1:
            return None, []
        modifier_event = self._press_modifier(code)
        if modifier_event is not None:
            return None, [modifier_event]
        if code in (KEY_LEFTSHIFT, KEY_RIGHTSHIFT, KEY_LEFTCTRL, KEY_RIGHTCTRL, KEY_LEFTALT, KEY_RIGHTALT, KEY_TAB):
            return None, []

        if code == KEY_ESC and value == 1:
            if self.commodore.active():
                return "quit", []
            return "release_all", [release_all_event()]
        return None, self._press_mapped_key(code)


class KeyboardDevice:
    def __init__(self, path: str, joystick_port: int) -> None:
        self.path = path
        self.fd = os.open(path, os.O_RDONLY | os.O_NONBLOCK)
        self.state = LowLevelKeyboardState(joystick_port)
        self.grabbed = False
        try:
            fcntl.ioctl(self.fd, EVIOCGRAB, 1)
            self.grabbed = True
        except OSError:
            self.grabbed = False

    def fileno(self) -> int:
        return self.fd

    def close(self) -> None:
        try:
            if self.grabbed:
                fcntl.ioctl(self.fd, EVIOCGRAB, 0)
        finally:
            os.close(self.fd)

    def read_updates(self) -> List[Tuple[Optional[str], List[Dict[str, object]]]]:
        updates: List[Tuple[Optional[str], List[Dict[str, object]]]] = []
        while True:
            try:
                data = os.read(self.fd, INPUT_EVENT_STRUCT.size * 64)
            except BlockingIOError:
                break
            if not data:
                break
            limit = len(data) - (len(data) % INPUT_EVENT_STRUCT.size)
            for offset in range(0, limit, INPUT_EVENT_STRUCT.size):
                _, _, event_type, code, value = INPUT_EVENT_STRUCT.unpack_from(data, offset)
                if event_type != EV_KEY:
                    continue
                control, events = self.state.apply_input_event(code, value)
                if control is not None or events:
                    updates.append((control, events))
            if len(data) < INPUT_EVENT_STRUCT.size * 64:
                break
        return updates


def gamepad_device_rank(path: str) -> Tuple[int, str, str]:
    name = input_event_name(path).lower()
    basename = os.path.basename(path).lower()
    score = 0
    if name:
        score += 10
    if any(keyword in name for keyword in GAMEPAD_NAME_KEYWORDS):
        score += 100
    if "system control" in name:
        score -= 100
    if "keyboard" in name:
        score -= 100
    if "mouse" in name:
        score -= 100
    if any(keyword in basename for keyword in ("xbox", "controller", "gamepad", "joypad")):
        score += 20
    return (-score, name, path)


def find_default_gamepad_device() -> Optional[str]:
    candidates = sorted(glob.glob("/dev/input/by-id/*-event-joystick"))
    if candidates:
        return sorted(candidates, key=gamepad_device_rank)[0]
    fallback = sorted(glob.glob("/dev/input/event*"))
    filtered = [path for path in fallback if any(keyword in input_event_name(path).lower() for keyword in GAMEPAD_NAME_KEYWORDS)]
    if filtered:
        return sorted(filtered, key=gamepad_device_rank)[0]
    return None


def open_gamepad(device_path: Optional[str], joystick_port: int) -> Optional[GamepadDevice]:
    return open_gamepad_checked(device_path, joystick_port, tolerate_error=True)


def open_gamepad_checked(device_path: Optional[str], joystick_port: int, tolerate_error: bool = False) -> Optional[GamepadDevice]:
    path = device_path or find_default_gamepad_device()
    if not path:
        return None
    try:
        return GamepadDevice(path, joystick_port)
    except OSError as exc:
        if not tolerate_error:
            raise InputDeviceOpenError("gamepad", path, exc) from exc
        return None


def open_keyboard(device_path: Optional[str], joystick_port: int) -> Optional[KeyboardDevice]:
    return open_keyboard_checked(device_path, joystick_port, tolerate_error=True)


def open_keyboard_checked(device_path: Optional[str], joystick_port: int, tolerate_error: bool = False) -> Optional[KeyboardDevice]:
    path = device_path or find_default_keyboard_device()
    if not path:
        return None
    try:
        return KeyboardDevice(path, joystick_port)
    except OSError as exc:
        if not tolerate_error:
            raise InputDeviceOpenError("keyboard", path, exc) from exc
        return None


def preserved_input_tool_environment() -> List[str]:
    names = sorted(name for name in os.environ if name.startswith("U64_INPUT_"))
    if "C64U_PASSWORD" in os.environ:
        names.append("C64U_PASSWORD")
    return sorted(set(names))


def build_sudo_reexec_command() -> List[str]:
    command = ["sudo"]
    preserved = preserved_input_tool_environment()
    if preserved:
        command.append("--preserve-env=" + ",".join(preserved))
    command.append(sys.executable)
    command.append(os.path.abspath(__file__))
    command.extend(sys.argv[1:])
    return command


def can_prompt_for_sudo() -> bool:
    return (
        os.geteuid() != 0
        and os.environ.get("U64_INPUT_TOOL_SUDO_ESCALATED") != "1"
        and sys.stdin.isatty()
        and sys.stdout.isatty()
        and shutil.which("sudo") is not None
    )


def request_input_access_or_warn(error: InputDeviceOpenError) -> bool:
    capability = "low-level keyboard capture" if error.device_kind == "keyboard" else "gamepad input"
    if error.permission_denied and can_prompt_for_sudo():
        print(f"{error.device_kind} access denied: {error.path}")
        print(f"{capability} needs read access to /dev/input event devices.")
        answer = input("Re-run with sudo now to grant that access? [Y/n] ").strip().lower()
        if answer in ("", "y", "yes"):
            env = os.environ.copy()
            env["U64_INPUT_TOOL_SUDO_ESCALATED"] = "1"
            command = build_sudo_reexec_command()
            try:
                os.execvpe("sudo", command, env)
            except OSError as exc:
                raise Failure(f"Unable to re-run with sudo: {format_exception(exc)}") from exc
        return False
    if error.permission_denied:
        print(
            f"warning: {error.device_kind} access denied for {error.path}; "
            f"continuing without {capability}. Re-run with sudo to enable it.",
            file=sys.stderr,
        )
        return False
    raise Failure(f"Unable to open {error.device_kind} device {error.path}: {format_exception(error.original_error)}")


def print_mapping_overview(
    host: str,
    joystick_port: int,
    keyboard: Optional[KeyboardDevice],
    gamepad: Optional[GamepadDevice],
    verbosity: int,
) -> None:
    print(f"REST input tool -> {host}")
    if keyboard:
        print("keys: low-level Linux mapping; Ctrl=C=, Tab=CTRL, arrows/F1-F8/Home/etc map directly; Esc=release_all; Ctrl+Esc=quit")
    else:
        print("keys: terminal fallback; direct text/arrows/F1-F8/Home/Tab only; Esc=release_all")
    print_special_key_help(low_level_keyboard=keyboard is not None)
    if keyboard:
        print("keyboard repeats: Linux low-level key-repeat events")
    else:
        if REPEATED_KEYBOARD_TAP_PACE_MS > 0:
            print(
                f"interactive hold queue: repeated same-key taps are paced at {REPEATED_KEYBOARD_TAP_PACE_MS} ms/event "
                f"in batches up to {REPEAT_BATCH_EVENTS} events"
            )
        else:
            print(f"interactive hold queue: repeated same-key taps are sent in batches up to {REPEAT_BATCH_EVENTS} events")
    print(f"rapid mixed-key batches: up to {KEYBOARD_TAP_BATCH_EVENTS} taps/request at pace_ms=0")
    print(f"paced multi-key batches: {BATCH_PACE_MS} ms between batched events")
    print(f"joystick port: {joystick_port}")
    if verbosity >= 2:
        verbose_text = "full"
    elif verbosity == 1:
        verbose_text = "concise"
    else:
        verbose_text = "off"
    print(f"verbose REST logging: {verbose_text}")
    if keyboard:
        print(f"keyboard: {keyboard.path} (low-level Linux input, exclusive grab enabled)")
    else:
        print("keyboard: stdin raw-terminal fallback")
    if gamepad:
        print(f"gamepad: {gamepad.path} (left stick, right stick, d-pad -> movement; A/X=fire; B=fire2; Y=fire3)")
    else:
        print("gamepad: not detected")


def handle_interactive_sequence(
    joystick_port: int,
    seq: str,
    repeat_state: RepeatState,
    decoder: SequenceDecoder,
    now: float,
) -> Tuple[Optional[str], List[Dict[str, object]]]:
    action, event, repeat_sequence = decoder.translate(seq, joystick_port, now)
    if action in ("ignore", "prefix"):
        return None, repeat_state.poll(now)
    if action == "help":
        return "help", repeat_state.poll(now)
    if action == "quit":
        repeat_state.stop()
        decoder.clear()
        return "quit", []
    if action == "release_all":
        repeat_state.stop()
        decoder.clear()
        return "release_all", [release_all_event()]
    if event is None:
        return None, repeat_state.poll(now)
    suppress_original, extra_events = repeat_state.observe(repeat_sequence or seq, event, now)
    if suppress_original:
        return None, extra_events
    return None, extra_events + [event]


def run_interactive_loop(
    session: RestInputSession,
    joystick_port: int,
    logger: Optional[RestCallLogger],
) -> None:
    stdin_fd = sys.stdin.fileno()
    client = InteractiveRestClient(session, logger=logger)
    repeat_state = RepeatState()
    decoder = SequenceDecoder()
    try:
        while True:
            now = time.monotonic()
            ready, _, _ = select.select([stdin_fd], [], [], combine_timeout(repeat_state.timeout(now), decoder.timeout(now)))
            now = time.monotonic()
            pending_events: List[Dict[str, object]] = []
            control, events = decoder.poll(now)
            if control == "release_all":
                repeat_state.stop()
                pending_events.extend(events)
            if ready:
                for seq in drain_stdin_sequences(stdin_fd):
                    control, events = handle_interactive_sequence(joystick_port, seq, repeat_state, decoder, time.monotonic())
                    if events:
                        pending_events.extend(events)
                    if control == "release_all":
                        if pending_events:
                            flush_event_batch(client, pending_events)
                            pending_events = []
                    elif control == "help":
                        print_special_key_help()
                    elif control == "quit":
                        if pending_events:
                            flush_event_batch(client, pending_events)
                        client.post_events([release_all_event()])
                        return
            pending_events.extend(repeat_state.poll(time.monotonic()))
            if pending_events:
                flush_event_batch(client, pending_events)
    finally:
        client.close()


def run_interactive_with_gamepad(
    session: RestInputSession,
    joystick_port: int,
    gamepad: GamepadDevice,
    logger: Optional[RestCallLogger],
) -> None:
    stdin_fd = sys.stdin.fileno()
    gamepad_fd = gamepad.fileno()
    client = InteractiveRestClient(session, logger=logger)
    repeat_state = RepeatState()
    decoder = SequenceDecoder()

    try:
        while True:
            now = time.monotonic()
            timeout = combine_timeout(repeat_state.timeout(now), gamepad.state.repeat_timeout(now), decoder.timeout(now))
            ready, _, _ = select.select([stdin_fd, gamepad_fd], [], [], timeout)
            now = time.monotonic()
            pending_events: List[Dict[str, object]] = []
            control, events = decoder.poll(now)
            if control == "release_all":
                repeat_state.stop()
                pending_events.extend(events)
            if not ready:
                repeat_event = gamepad.state.poll_repeat_fire(now)
                if repeat_event:
                    pending_events.append(repeat_event)
            else:
                if gamepad_fd in ready:
                    pending_events.extend(gamepad.read_events(now))
                    repeat_event = gamepad.state.poll_repeat_fire(now)
                    if repeat_event:
                        pending_events.append(repeat_event)
                if stdin_fd in ready:
                    for seq in drain_stdin_sequences(stdin_fd):
                        control, events = handle_interactive_sequence(joystick_port, seq, repeat_state, decoder, time.monotonic())
                        if events:
                            pending_events.extend(events)
                        if control == "release_all":
                            if pending_events:
                                flush_event_batch(client, pending_events)
                                pending_events = []
                        elif control == "help":
                            print_special_key_help()
                        elif control == "quit":
                            if pending_events:
                                flush_event_batch(client, pending_events)
                            client.post_events([release_all_event()])
                            return
            pending_events.extend(repeat_state.poll(time.monotonic()))
            if pending_events:
                flush_event_batch(client, pending_events)
    finally:
        client.close()


def run_interactive_low_level(
    session: RestInputSession,
    keyboard: KeyboardDevice,
    gamepad: Optional[GamepadDevice],
    logger: Optional[RestCallLogger],
) -> None:
    keyboard_fd = keyboard.fileno()
    gamepad_fd = gamepad.fileno() if gamepad is not None else None
    client = InteractiveRestClient(session, logger=logger)

    try:
        while True:
            now = time.monotonic()
            timeout = gamepad.state.repeat_timeout(now) if gamepad is not None else None
            fds = [keyboard_fd]
            if gamepad_fd is not None:
                fds.append(gamepad_fd)
            ready, _, _ = select.select(fds, [], [], timeout)
            now = time.monotonic()
            pending_events: List[Dict[str, object]] = []

            if not ready and gamepad is not None:
                repeat_event = gamepad.state.poll_repeat_fire(now)
                if repeat_event:
                    pending_events.append(repeat_event)
            else:
                if keyboard_fd in ready:
                    for control, events in keyboard.read_updates():
                        if events:
                            pending_events.extend(events)
                        if control == "release_all":
                            if pending_events:
                                flush_event_batch(client, pending_events)
                                pending_events = []
                        elif control == "quit":
                            if pending_events:
                                flush_event_batch(client, pending_events)
                            client.post_events([release_all_event()])
                            return
                if gamepad is not None and gamepad_fd is not None and gamepad_fd in ready:
                    pending_events.extend(gamepad.read_events(now))
                    repeat_event = gamepad.state.poll_repeat_fire(now)
                    if repeat_event:
                        pending_events.append(repeat_event)

            if pending_events:
                flush_event_batch(client, pending_events)
    finally:
        client.close()


def run_interactive(
    session: RestInputSession,
    joystick_port: int,
    keyboard: Optional[KeyboardDevice],
    gamepad: Optional[GamepadDevice],
    logger: Optional[RestCallLogger],
    verbosity: int,
) -> None:
    print_mapping_overview(session.host, joystick_port, keyboard, gamepad, verbosity)
    wait_for_input_ready(session, timeout=15.0)
    startup_client = InteractiveRestClient(session, logger=logger)
    try:
        startup_client.post_events([release_all_event()])
    finally:
        startup_client.close()
    if keyboard:
        run_interactive_low_level(session, keyboard, gamepad, logger)
    else:
        with raw_terminal():
            if gamepad:
                run_interactive_with_gamepad(session, joystick_port, gamepad, logger)
            else:
                run_interactive_loop(session, joystick_port, logger)


def assert_input_state(session: RestInputSession, keyboard: List[str], joystick1: List[str], joystick2: List[str]) -> None:
    state = session.get_state()
    if state.get("keyboard", {}).get("inputs") != keyboard:
        raise Failure(f"Keyboard state mismatch: {state}")
    if state.get("joysticks") != [{"port": 1, "inputs": joystick1}, {"port": 2, "inputs": joystick2}]:
        raise Failure(f"Joystick state mismatch: {state}")


def collect_interactive_events(sequence_times: List[Tuple[float, str]], joystick_port: int) -> List[Tuple[float, Dict[str, object]]]:
    repeat_state = RepeatState()
    decoder = SequenceDecoder()
    emitted: List[Tuple[float, Dict[str, object]]] = []
    for now, seq in sequence_times:
        control, events = decoder.poll(now)
        if control == "release_all":
            repeat_state.stop()
        for event in events:
            emitted.append((now, event))
        for event in repeat_state.poll(now):
            emitted.append((now, event))
        control, events = handle_interactive_sequence(joystick_port, seq, repeat_state, decoder, now)
        if control == "release_all":
            repeat_state.stop()
        if control == "quit":
            break
        for event in events:
            emitted.append((now, event))
    final_time = sequence_times[-1][0] + KEY_REPEAT_STALE_SECONDS + 0.05 if sequence_times else 0.0
    control, events = decoder.poll(final_time)
    if control == "release_all":
        repeat_state.stop()
    for event in events:
        emitted.append((final_time, event))
    for event in repeat_state.poll(final_time):
        emitted.append((final_time, event))
    return emitted


def collect_low_level_keyboard_events(key_events: List[Tuple[int, int]], joystick_port: int) -> List[Tuple[Optional[str], List[Dict[str, object]]]]:
    state = LowLevelKeyboardState(joystick_port)
    return [state.apply_input_event(code, value) for code, value in key_events]


def run_local_input_tool_regressions(joystick_port: int) -> None:
    decoder = SequenceDecoder()
    for seq, expected in (
        ("\x1b[A", ["left_shift", "cursor_up_down"]),
        ("\x1b[B", ["cursor_up_down"]),
        ("\x1b[C", ["cursor_left_right"]),
        ("\x1b[D", ["left_shift", "cursor_left_right"]),
        ("\x1bOA", ["left_shift", "cursor_up_down"]),
        ("\x1bOB", ["cursor_up_down"]),
        ("\x1bOC", ["cursor_left_right"]),
        ("\x1bOD", ["left_shift", "cursor_left_right"]),
    ):
        action, event, _ = decoder.translate(seq, joystick_port, 0.0)
        if action != "event" or event != keyboard_event(expected):
            raise Failure(f"Cursor translation mismatch for {seq!r}: {event}")

    fragmented_cursor_events = collect_interactive_events([(0.00, "\x1b["), (0.01, "D"), (0.30, "\x1bO"), (0.31, "A")], joystick_port)
    fragmented_expected = [
        keyboard_event(["left_shift", "cursor_left_right"]),
        keyboard_event(["left_shift", "cursor_up_down"]),
    ]
    if [event for _, event in fragmented_cursor_events] != fragmented_expected:
        raise Failure(f"Fragmented cursor translation mismatch: {fragmented_cursor_events}")

    duplicate_cursor_suffix_events = collect_interactive_events([(0.00, "\x1b[D"), (0.01, "D")], joystick_port)
    if [event for _, event in duplicate_cursor_suffix_events] != [keyboard_event(["left_shift", "cursor_left_right"])]:
        raise Failure(f"Duplicate cursor suffix should be suppressed: {duplicate_cursor_suffix_events}")

    esc_release_events = collect_interactive_events([(0.00, "\x1b")], joystick_port)
    if [event for _, event in esc_release_events] != [release_all_event()]:
        raise Failure(f"Standalone Esc should release all, got {esc_release_events}")

    pretrigger_hold_events = collect_interactive_events([(0.00, "a"), (0.18, "a"), (0.34, "a"), (0.49, "a")], joystick_port)
    if [event for _, event in pretrigger_hold_events] != [keyboard_event(["a"])] * 4:
        raise Failure(f"Slow same-key taps should stay distinct before hold-repeat cadence: {pretrigger_hold_events}")

    quick_repeat_taps = collect_interactive_events([(0.00, "a"), (0.20, "a"), (0.40, "a"), (0.60, "a")], joystick_port)
    if [event for _, event in quick_repeat_taps] != [keyboard_event(["a"])] * 4:
        raise Failure(f"Distinct same-key taps should not trigger hold-repeat: {quick_repeat_taps}")

    left_shift_events = collect_low_level_keyboard_events([(KEY_LEFTSHIFT, 1), (KEY_LEFTSHIFT, 0)], joystick_port)
    if left_shift_events != [
        (None, [keyboard_state_event(["left_shift"], "press")]),
        (None, [keyboard_state_event(["left_shift"], "release")]),
    ]:
        raise Failure(f"Low-level left-shift mapping mismatch: {left_shift_events}")

    right_shift_events = collect_low_level_keyboard_events([(KEY_RIGHTSHIFT, 1), (KEY_RIGHTSHIFT, 0)], joystick_port)
    if right_shift_events != [
        (None, [keyboard_state_event(["right_shift"], "press")]),
        (None, [keyboard_state_event(["right_shift"], "release")]),
    ]:
        raise Failure(f"Low-level right-shift mapping mismatch: {right_shift_events}")

    shifted_letter_events = collect_low_level_keyboard_events(
        [(KEY_RIGHTSHIFT, 1), (KEY_A, 1), (KEY_A, 0), (KEY_RIGHTSHIFT, 0)],
        joystick_port,
    )
    if shifted_letter_events != [
        (None, [keyboard_state_event(["right_shift"], "press")]),
        (None, [keyboard_state_event(["a"], "press")]),
        (None, [keyboard_state_event(["a"], "release")]),
        (None, [keyboard_state_event(["right_shift"], "release")]),
    ]:
        raise Failure(f"Low-level shifted-letter mapping mismatch: {shifted_letter_events}")

    commodore_events = collect_low_level_keyboard_events([(KEY_LEFTCTRL, 1), (KEY_LEFTCTRL, 0)], joystick_port)
    if commodore_events != [
        (None, [keyboard_state_event(["commodore"], "press")]),
        (None, [keyboard_state_event(["commodore"], "release")]),
    ]:
        raise Failure(f"Low-level commodore mapping mismatch: {commodore_events}")

    ctrl_events = collect_low_level_keyboard_events([(KEY_TAB, 1), (KEY_TAB, 0)], joystick_port)
    if ctrl_events != [
        (None, [keyboard_state_event(["ctrl"], "press")]),
        (None, [keyboard_state_event(["ctrl"], "release")]),
    ]:
        raise Failure(f"Low-level ctrl mapping mismatch: {ctrl_events}")

    quit_events = collect_low_level_keyboard_events([(KEY_LEFTCTRL, 1), (KEY_ESC, 1)], joystick_port)
    if quit_events != [
        (None, [keyboard_state_event(["commodore"], "press")]),
        ("quit", []),
    ]:
        raise Failure(f"Low-level Ctrl+Esc quit mapping mismatch: {quit_events}")

    fkey_events = {
        KEY_F1: [
            (None, [keyboard_state_event(["f1"], "press")]),
            (None, [keyboard_state_event(["f1"], "release")]),
        ],
        KEY_F2: [
            (None, [keyboard_state_event(["left_shift"], "press"), keyboard_state_event(["f1"], "press")]),
            (None, [keyboard_state_event(["f1"], "release"), keyboard_state_event(["left_shift"], "release")]),
        ],
        KEY_F3: [
            (None, [keyboard_state_event(["f3"], "press")]),
            (None, [keyboard_state_event(["f3"], "release")]),
        ],
        KEY_F4: [
            (None, [keyboard_state_event(["left_shift"], "press"), keyboard_state_event(["f3"], "press")]),
            (None, [keyboard_state_event(["f3"], "release"), keyboard_state_event(["left_shift"], "release")]),
        ],
        KEY_F5: [
            (None, [keyboard_state_event(["f5"], "press")]),
            (None, [keyboard_state_event(["f5"], "release")]),
        ],
        KEY_F6: [
            (None, [keyboard_state_event(["left_shift"], "press"), keyboard_state_event(["f5"], "press")]),
            (None, [keyboard_state_event(["f5"], "release"), keyboard_state_event(["left_shift"], "release")]),
        ],
        KEY_F7: [
            (None, [keyboard_state_event(["f7"], "press")]),
            (None, [keyboard_state_event(["f7"], "release")]),
        ],
        KEY_F8: [
            (None, [keyboard_state_event(["left_shift"], "press"), keyboard_state_event(["f7"], "press")]),
            (None, [keyboard_state_event(["f7"], "release"), keyboard_state_event(["left_shift"], "release")]),
        ],
    }
    for keycode, expected in fkey_events.items():
        events = collect_low_level_keyboard_events([(keycode, 1), (keycode, 0)], joystick_port)
        if events != expected:
            raise Failure(f"Low-level F-key mapping mismatch for {keycode}: {events}")

    rapid_low_level_events = collect_low_level_keyboard_events(
        [
            (KEY_A, 1), (KEY_A, 0),
            (KEY_S, 1), (KEY_S, 0),
            (KEY_D, 1), (KEY_D, 0),
            (KEY_F, 1), (KEY_F, 0),
        ],
        joystick_port,
    )
    if [events for _, events in rapid_low_level_events if events] != [
        [keyboard_state_event(["a"], "press")],
        [keyboard_state_event(["a"], "release")],
        [keyboard_state_event(["s"], "press")],
        [keyboard_state_event(["s"], "release")],
        [keyboard_state_event(["d"], "press")],
        [keyboard_state_event(["d"], "release")],
        [keyboard_state_event(["f"], "press")],
        [keyboard_state_event(["f"], "release")],
    ]:
        raise Failure(f"Low-level random typing injected unexpected modifiers: {rapid_low_level_events}")

    left_arrow_events = collect_low_level_keyboard_events(
        [(KEY_LEFT, 1), (KEY_LEFT, 0)],
        joystick_port,
    )
    if left_arrow_events != [
        (None, [keyboard_state_event(["left_shift"], "press"), keyboard_state_event(["cursor_left_right"], "press")]),
        (None, [keyboard_state_event(["cursor_left_right"], "release"), keyboard_state_event(["left_shift"], "release")]),
    ]:
        raise Failure(f"Low-level left-arrow mapping mismatch: {left_arrow_events}")

    gamepad = GamepadState(joystick_port)
    fire23_events = [
        gamepad.apply_input_event(EV_KEY, BTN_SOUTH, 1, 0.0),
        gamepad.apply_input_event(EV_KEY, BTN_SOUTH, 0, 0.0),
        gamepad.apply_input_event(EV_KEY, BTN_NORTH, 1, 0.0),
        gamepad.apply_input_event(EV_KEY, BTN_NORTH, 0, 0.0),
        gamepad.apply_input_event(EV_KEY, BTN_EAST, 1, 0.0),
        gamepad.apply_input_event(EV_KEY, BTN_WEST, 1, 0.0),
        gamepad.apply_input_event(EV_KEY, BTN_EAST, 0, 0.0),
        gamepad.apply_input_event(EV_KEY, BTN_WEST, 0, 0.0),
    ]
    if fire23_events != [
        [{"kind": "joystick", "port": joystick_port, "inputs": ["fire"], "transition": "press"}],
        [{"kind": "joystick", "port": joystick_port, "inputs": ["fire"], "transition": "release"}],
        [{"kind": "joystick", "port": joystick_port, "inputs": ["fire"], "transition": "press"}],
        [{"kind": "joystick", "port": joystick_port, "inputs": ["fire"], "transition": "release"}],
        [{"kind": "joystick", "port": joystick_port, "inputs": ["fire2"], "transition": "press"}],
        [{"kind": "joystick", "port": joystick_port, "inputs": ["fire3"], "transition": "press"}],
        [{"kind": "joystick", "port": joystick_port, "inputs": ["fire2"], "transition": "release"}],
        [{"kind": "joystick", "port": joystick_port, "inputs": ["fire3"], "transition": "release"}],
    ]:
        raise Failure(f"Gamepad face-button mapping mismatch: {fire23_events}")

    generic_button_events = [
        gamepad.apply_input_event(EV_KEY, BTN_TRIGGER, 1, 0.0),
        gamepad.apply_input_event(EV_KEY, BTN_TRIGGER, 0, 0.0),
        gamepad.apply_input_event(EV_KEY, BTN_THUMB, 1, 0.0),
        gamepad.apply_input_event(EV_KEY, BTN_THUMB2, 1, 0.0),
        gamepad.apply_input_event(EV_KEY, BTN_TOP, 1, 0.0),
        gamepad.apply_input_event(EV_KEY, BTN_THUMB, 0, 0.0),
        gamepad.apply_input_event(EV_KEY, BTN_THUMB2, 0, 0.0),
        gamepad.apply_input_event(EV_KEY, BTN_TOP, 0, 0.0),
    ]
    if generic_button_events != [
        [{"kind": "joystick", "port": joystick_port, "inputs": ["fire"], "transition": "press"}],
        [{"kind": "joystick", "port": joystick_port, "inputs": ["fire"], "transition": "release"}],
        [{"kind": "joystick", "port": joystick_port, "inputs": ["fire2"], "transition": "press"}],
        [{"kind": "joystick", "port": joystick_port, "inputs": ["fire"], "transition": "press"}],
        [{"kind": "joystick", "port": joystick_port, "inputs": ["fire3"], "transition": "press"}],
        [{"kind": "joystick", "port": joystick_port, "inputs": ["fire2"], "transition": "release"}],
        [{"kind": "joystick", "port": joystick_port, "inputs": ["fire"], "transition": "release"}],
        [{"kind": "joystick", "port": joystick_port, "inputs": ["fire3"], "transition": "release"}],
    ]:
        raise Failure(f"Generic gamepad button mapping mismatch: {generic_button_events}")

    for seq, expected in (
        ("\t", ["ctrl"]),
        ("\x1bOP", ["f1"]),
        ("\x1bOQ", ["left_shift", "f1"]),
        ("\x1bOR", ["f3"]),
        ("\x1bOS", ["left_shift", "f3"]),
        ("\x1b[15~", ["f5"]),
        ("\x1b[17~", ["left_shift", "f5"]),
        ("\x1b[18~", ["f7"]),
        ("\x1b[19~", ["left_shift", "f7"]),
        ("\x1b[H", ["clr_home"]),
        ("\x1b[3~", ["inst_del"]),
        ("\b", ["inst_del"]),
        ("\x1b[F", ["run_stop"]),
        ("\x1b[5~", ["restore"]),
        ("\n", ["return"]),
    ):
        decoder.clear()
        action, event, repeat_sequence = decoder.translate(seq, joystick_port, 0.0)
        if action != "event" or event != keyboard_event(expected) or repeat_sequence != seq:
            raise Failure(f"Special mapping mismatch for {seq!r}: {event}")

    for seq in ("\x1b[20~", "\x1b[Z", "\x1b[21~", "\x1b[23~", "\x1ba", "\x1bA", "\x1bo", "\x01", "\x18", "\x1b[1;5A", "\x1b[13;5u"):
        decoder.clear()
        action, event, repeat_sequence = decoder.translate(seq, joystick_port, 0.0)
        if action != "ignore" or event is not None or repeat_sequence is not None:
            raise Failure(f"Legacy terminal workaround should be ignored for {seq!r}: {(action, event, repeat_sequence)}")

    rapid_text = "ABCDEFGHIJKLMNOPQRSTUVWXYZ" * 2
    rapid_events = collect_interactive_events([(index * 0.01, ch) for index, ch in enumerate(rapid_text)], joystick_port)
    expected_rapid = [keyboard_event(["left_shift", ch.lower()]) for ch in rapid_text]
    if [event for _, event in rapid_events] != expected_rapid:
        raise Failure("Rapid mixed-key stream dropped or reordered keys")

    hold_sequences = [(0.0, "a")] + [(0.60 + index * 0.033, "a") for index in range(36)]
    hold_events = collect_interactive_events(hold_sequences, joystick_port)
    hold_times = [timestamp for timestamp, event in hold_events if event == keyboard_event(["a"])]
    if not hold_times:
        raise Failure("Held-key regression produced no emitted events")
    if hold_times[0] != 0.0:
        raise Failure(f"Held-key regression lost the initial tap: {hold_times[:4]}")
    if len(hold_times) < 38:
        raise Failure(f"Held-key regression emitted too few repeated taps: {len(hold_times)}")
    if any(timestamp < KEY_REPEAT_TRIGGER_SECONDS for timestamp in hold_times[1:]):
        raise Failure(f"Held-key regression emitted repeats too early: {hold_times[:6]}")
    if hold_times[-1] < 1.4:
        raise Failure(f"Held-key regression stopped too early: {hold_times[-4:]}")


def run_self_test(session: RestInputSession, joystick_port: int, gamepad: Optional[GamepadDevice]) -> None:
    verbosity = session.logger.level if isinstance(session, VerboseRestSession) else 0
    print_mapping_overview(session.host, joystick_port, None, gamepad, verbosity)
    wait_for_input_ready(session, timeout=15.0)

    print("[1] local repeat, cursor, and special-key regressions ... ", end="", flush=True)
    run_local_input_tool_regressions(joystick_port)
    print("OK")

    print("[2] keyboard input reaches the live C64 matrix ... ", end="", flush=True)
    reset_to_basic(session)
    session.post_events([{"kind": "keyboard", "inputs": ["x"], "transition": "press"}])
    assert_keyboard_matrix_inputs(session, ["x"])
    session.post_events([release_all_event()])
    print("OK")

    print("[3] cursor keys map to C64 cursor movement ... ", end="", flush=True)
    for seq, expected in (
        ("\x1b[A", ["left_shift", "cursor_up_down"]),
        ("\x1b[B", ["cursor_up_down"]),
        ("\x1b[C", ["cursor_left_right"]),
        ("\x1b[D", ["left_shift", "cursor_left_right"]),
    ):
        translated = translate_sequence(seq, joystick_port)
        if translated != keyboard_event(expected):
            raise Failure(f"Cursor translation mismatch for {seq!r}: {translated}")
    reset_to_basic(session)
    response = session.post_events([{"kind": "keyboard", "inputs": ["left_shift", "cursor_left_right"], "transition": "tap"}])
    if sorted(response.get("keyboard", {}).get("inputs", [])) != ["cursor_left_right", "left_shift"]:
        raise Failure(f"Cursor tap snapshot mismatch: {response}")
    time.sleep(0.2)
    assert_input_state(session, [], [], [])
    session.post_events([release_all_event()])
    print("OK")

    print("[4] special-key mappings reach the REST keyboard snapshot ... ", end="", flush=True)
    for inputs in (["commodore"], ["ctrl"], ["run_stop"], ["restore"], ["f1"], ["f7"], ["left_shift"], ["right_shift"]):
        response = session.post_events([{"kind": "keyboard", "inputs": inputs, "transition": "tap"}])
        if sorted(response.get("keyboard", {}).get("inputs", [])) != sorted(inputs):
            raise Failure(f"Special-key snapshot mismatch for {inputs}: {response}")
        time.sleep(0.2)
        assert_input_state(session, [], [], [])
    session.post_events([release_all_event()])
    print("OK")

    print("[5] keyboard persistent state round-trips ... ", end="", flush=True)
    session.post_events([{"kind": "keyboard", "inputs": ["ctrl"], "transition": "press"}])
    assert_input_state(session, ["ctrl"], [], [])
    session.post_events([release_all_event()])
    assert_input_state(session, [], [], [])
    print("OK")

    print("[6] extended joystick fire buttons round-trip through REST state ... ", end="", flush=True)
    session.post_events([{"kind": "joystick", "port": joystick_port, "inputs": ["fire2", "fire3"], "transition": "press"}])
    if joystick_port == 1:
        assert_input_state(session, [], ["fire2", "fire3"], [])
    else:
        assert_input_state(session, [], [], ["fire2", "fire3"])
    session.post_events([release_all_event()])
    assert_input_state(session, [], [], [])
    print("OK")

    print("[7] joystick movement reaches the live CIA joystick lines ... ", end="", flush=True)
    reset_to_basic(session)
    session.post_events(
        [
            {"kind": "joystick", "port": joystick_port, "inputs": ["up"], "transition": "press"},
            {"kind": "joystick", "port": joystick_port, "inputs": ["fire"], "transition": "press"},
        ]
    )
    if joystick_port == 1:
        assert_joystick_ports(session, 0x0E, 0x1F)
    else:
        assert_joystick_ports(session, 0x1F, 0x0E)
    session.post_events([release_all_event()])
    assert_input_state(session, [], [], [])
    print("OK")

    print("input_tool self-test: OK")


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Inject local keyboard and gamepad input via the Ultimate 64 REST API.",
        epilog=(
            "Environment: U64_INPUT_HOST, U64_INPUT_REST_HOST, "
            "U64_INPUT_PASSWORD or C64U_PASSWORD, U64_INPUT_TIMEOUT.\n"
            "Use -v for concise REST logging, -vv or -V for full request/response details."
        ),
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )

    connection = parser.add_argument_group("connection options")
    connection.add_argument(
        "-H",
        "--host",
        metavar="HOST",
        default=os.environ.get("U64_INPUT_HOST", "u64"),
        help="target Ultimate 64 host name (default: %(default)s)",
    )
    connection.add_argument(
        "-r",
        "--rest-host",
        metavar="HOST",
        default=os.environ.get("U64_INPUT_REST_HOST"),
        help="REST API host name; defaults to --host",
    )
    connection.add_argument(
        "-p",
        "--password",
        metavar="PASSWORD",
        default=os.environ.get("U64_INPUT_PASSWORD", os.environ.get("C64U_PASSWORD")),
        help="REST password; defaults to $U64_INPUT_PASSWORD or $C64U_PASSWORD",
    )
    connection.add_argument(
        "-t",
        "--timeout",
        metavar="SECONDS",
        type=float,
        default=float(os.environ.get("U64_INPUT_TIMEOUT", "5.0")),
        help="per-request timeout in seconds (default: %(default)s)",
    )

    action = parser.add_argument_group("action options")
    action.add_argument(
        "-s",
        "--self-test",
        action="store_true",
        help="run keyboard and joystick verification, then exit",
    )

    input_group = parser.add_argument_group("input options")
    input_group.add_argument(
        "-j",
        "--joystick-port",
        metavar="PORT",
        type=int,
        choices=(1, 2),
        default=2,
        help="joystick port driven by keyboard shortcuts and gamepad input (default: %(default)s)",
    )
    input_group.add_argument(
        "-g",
        "--gamepad-device",
        metavar="DEVICE",
        help="Linux /dev/input event device to use as the USB gamepad",
    )
    input_group.add_argument(
        "-k",
        "--keyboard-device",
        metavar="DEVICE",
        help="Linux /dev/input event device to use as the keyboard (default: auto-detect *-event-kbd)",
    )
    input_group.add_argument(
        "-G",
        "--no-gamepad",
        action="store_true",
        help="disable automatic USB gamepad detection",
    )
    input_group.add_argument(
        "-T",
        "--terminal-keyboard",
        action="store_true",
        help="use the legacy stdin raw-terminal decoder instead of low-level /dev/input keyboard capture",
    )

    logging = parser.add_argument_group("logging options")
    logging.add_argument(
        "-v",
        "--verbose",
        action="count",
        default=0,
        help="log REST calls; repeat for more detail",
    )
    logging.add_argument(
        "-V",
        "--verboser",
        action="store_true",
        help="show full REST request and response payloads (same as -vv)",
    )
    args = parser.parse_args()

    rest_host = args.rest_host or args.host
    verbosity = max(args.verbose, 2 if args.verboser else 0)
    logger = RestCallLogger(verbosity)
    session: RestInputSession
    if verbosity > 0:
        session = VerboseRestSession(rest_host, args.password, args.timeout, logger)
    else:
        session = RestInputSession(rest_host, args.password, args.timeout)
    keyboard = None
    if not args.terminal_keyboard:
        try:
            keyboard = open_keyboard_checked(args.keyboard_device, args.joystick_port)
        except InputDeviceOpenError as exc:
            if request_input_access_or_warn(exc):
                return 0
            keyboard = None
    if args.keyboard_device and keyboard is None and not args.terminal_keyboard:
        raise Failure(f"Unable to open keyboard device {args.keyboard_device}")

    gamepad = None
    if not args.no_gamepad:
        try:
            gamepad = open_gamepad_checked(args.gamepad_device, args.joystick_port)
        except InputDeviceOpenError as exc:
            if request_input_access_or_warn(exc):
                return 0
            gamepad = None
    if args.gamepad_device and gamepad is None and not args.no_gamepad:
        raise Failure(f"Unable to open gamepad device {args.gamepad_device}")
    try:
        if args.self_test:
            run_self_test(session, args.joystick_port, gamepad)
        else:
            run_interactive(session, args.joystick_port, keyboard, gamepad, logger if verbosity > 0 else None, verbosity)
    except KeyboardInterrupt:
        try:
            session.post_events([release_all_event()])
        except Exception:
            pass
        print()
        return 0
    except Failure as exc:
        print(exc, file=sys.stderr)
        return 1
    except (OSError, TimeoutError, urllib.error.URLError) as exc:
        if device_unavailable(exc):
            print(f"Connection failure: {format_exception(exc)}", file=sys.stderr)
        else:
            print(f"REST failure: {format_exception(exc)}", file=sys.stderr)
        return 1
    finally:
        if keyboard:
            keyboard.close()
        if gamepad:
            gamepad.close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
