#!/usr/bin/env python3
import argparse
from collections import deque
import glob
import os
import select
import struct
import sys
import termios
import time
import tty
import urllib.error
from contextlib import contextmanager
from typing import Callable, Dict, List, Optional, Set, Tuple

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
MAX_BATCH_EVENTS = 64
BATCH_IDLE_SECONDS = float(os.environ.get("U64_INPUT_BATCH_IDLE", "0.02"))
BATCH_PACE_MS = int(os.environ.get("U64_INPUT_PACE_MS", "20"))
GAMEPAD_AXIS_THRESHOLD = int(os.environ.get("U64_INPUT_GAMEPAD_AXIS_THRESHOLD", "12000"))
GAMEPAD_FIRE_REPEAT_HZ = float(os.environ.get("U64_INPUT_GAMEPAD_FIRE_REPEAT_HZ", "10.0"))
QUIT_SEQUENCES = ("\x03", "\x04")
JOYSTICK_INPUT_ORDER = ["up", "down", "left", "right", "fire"]

INPUT_EVENT_STRUCT = struct.Struct("llHHi")
EV_KEY = 0x01
EV_ABS = 0x03
ABS_X = 0x00
ABS_Y = 0x01
ABS_RX = 0x03
ABS_RY = 0x04
ABS_HAT0X = 0x10
ABS_HAT0Y = 0x11
BTN_SOUTH = 0x130
BTN_EAST = 0x131
GAMEPAD_NAME_KEYWORDS = ("xbox", "x-box", "controller", "gamepad", "joypad", "pad")


@contextmanager
def raw_terminal():
    fd = sys.stdin.fileno()
    old = termios.tcgetattr(fd)
    try:
        tty.setraw(fd)
        yield
    finally:
        termios.tcsetattr(fd, termios.TCSADRAIN, old)


def read_key_sequence(timeout: float = 0.05) -> str:
    ch = sys.stdin.read(1)
    if ch != "\x1b":
        return ch
    seq = ch
    while select.select([sys.stdin], [], [], timeout)[0]:
        seq += sys.stdin.read(1)
        if seq.endswith("~") or (len(seq) >= 3 and seq[-1].isalpha()):
            break
    return seq


def read_ready_key_sequence(timeout: float = BATCH_IDLE_SECONDS) -> Optional[str]:
    if not select.select([sys.stdin], [], [], timeout)[0]:
        return None
    return read_key_sequence()


def keyboard_event(inputs: List[str]) -> Dict[str, object]:
    return {"kind": "keyboard", "inputs": inputs, "transition": "tap"}


def joystick_event(port: int, inputs: List[str]) -> Dict[str, object]:
    return {"kind": "joystick", "port": port, "inputs": inputs, "transition": "tap"}


def release_all_event() -> Dict[str, str]:
    return {"kind": "release_all"}


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
        self.b_pressed = False
        self.repeat_fire_at: Optional[float] = None
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

        if code == BTN_SOUTH:
            self.a_pressed = value != 0
            return self._recompute_logical_inputs()

        if code == BTN_EAST:
            pressed = value != 0
            if pressed and not self.b_pressed:
                self.b_pressed = True
                self.repeat_fire_at = now + self.fire_repeat_interval
                return [joystick_event(self.port, ["fire"])]
            if not pressed:
                self.b_pressed = False
                self.repeat_fire_at = None
            return []

        return []

    def poll_repeat_fire(self, now: float) -> Optional[Dict[str, object]]:
        if not self.b_pressed or self.repeat_fire_at is None or now < self.repeat_fire_at:
            return None
        while self.repeat_fire_at is not None and now >= self.repeat_fire_at:
            self.repeat_fire_at += self.fire_repeat_interval
        return joystick_event(self.port, ["fire"])

    def repeat_timeout(self, now: float) -> Optional[float]:
        if not self.b_pressed or self.repeat_fire_at is None:
            return None
        return max(0.0, self.repeat_fire_at - now)


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


class BufferedSequenceReader:
    def __init__(
        self,
        read_sequence: Callable[[], str],
        read_ready_sequence: Callable[[], Optional[str]],
    ) -> None:
        self.read_sequence = read_sequence
        self.read_ready_sequence = read_ready_sequence
        self.buffer: deque[str] = deque()

    def get(self) -> str:
        if self.buffer:
            return self.buffer.popleft()
        return self.read_sequence()

    def get_ready(self) -> Optional[str]:
        if self.buffer:
            return self.buffer.popleft()
        return self.read_ready_sequence()

    def push(self, seq: str) -> None:
        self.buffer.appendleft(seq)


def translate_sequence(seq: str, joystick_port: int) -> Optional[Dict[str, object]]:
    if seq == "\x1b":
        return release_all_event()
    if seq == "\n":
        return joystick_event(joystick_port, ["fire"])
    if seq.startswith("\x1b["):
        if seq in ("\x1b[13;5u", "\x1b[27;5;13~"):
            return joystick_event(joystick_port, ["fire"])
        if seq[-1:] in ARROW_KEYS and (";5" in seq or seq.startswith("\x1b[5")):
            return joystick_event(joystick_port, [ARROW_KEYS[seq[-1]]])
        return None
    if len(seq) != 1:
        return None
    if "a" <= seq <= "z" or "0" <= seq <= "9":
        return keyboard_event([seq])
    if "A" <= seq <= "Z":
        return keyboard_event(["left_shift", seq.lower()])
    mapped = KEY_MAP.get(seq)
    if mapped:
        return keyboard_event([mapped])
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
    path = device_path or find_default_gamepad_device()
    if not path:
        return None
    try:
        return GamepadDevice(path, joystick_port)
    except OSError:
        return None


def print_mapping_overview(host: str, joystick_port: int, gamepad: Optional[GamepadDevice]) -> None:
    print(f"REST input tool -> {host}")
    print("keys: text=C64 keys; Ctrl+arrows=joy; Ctrl+Enter/Ctrl+J=fire; Esc=release_all; Ctrl+C/Ctrl+D=quit")
    print(f"paced batch mode: {BATCH_PACE_MS} ms between queued events")
    print(f"joystick port: {joystick_port}")
    if gamepad:
        print(f"gamepad: {gamepad.path} (left stick, right stick, d-pad -> movement; A=fire; B=repeat fire)")
    else:
        print("gamepad: not detected")


def post_interactive_events(session: RestInputSession, events: List[Dict[str, object]]) -> None:
    payload: Dict[str, object] = {"events": events}
    if len(events) > 1 and BATCH_PACE_MS > 0:
        payload["pace_ms"] = BATCH_PACE_MS
    session.json_request("POST", "/v1/machine:input", payload=payload)


def classify_sequence(seq: str, joystick_port: int) -> Tuple[str, Optional[Dict[str, object]]]:
    if seq in QUIT_SEQUENCES:
        return "quit", None
    event = translate_sequence(seq, joystick_port)
    if not event:
        return "ignore", None
    if event.get("kind") == "release_all":
        return "release_all", event
    return "event", event


def collect_event_batch(
    first_event: Dict[str, object],
    joystick_port: int,
    reader: BufferedSequenceReader,
) -> Tuple[List[Dict[str, object]], Optional[str]]:
    events = [first_event]
    first_signature = (
        first_event.get("kind"),
        tuple(first_event.get("inputs", [])),  # type: ignore[arg-type]
        first_event.get("transition"),
    )
    while len(events) < MAX_BATCH_EVENTS:
        seq = reader.get_ready()
        if seq is None:
            break
        action, event = classify_sequence(seq, joystick_port)
        if action == "ignore":
            continue
        if action == "event" and event is not None:
            event_signature = (
                event.get("kind"),
                tuple(event.get("inputs", [])),  # type: ignore[arg-type]
                event.get("transition"),
            )
            if event_signature == first_signature and event.get("kind") == "keyboard":
                reader.push(seq)
                break
            events.append(event)
            continue
        reader.push(seq)
        return events, None
    return events, None


def handle_interactive_sequence(
    session: RestInputSession,
    joystick_port: int,
    seq: str,
    reader: BufferedSequenceReader,
) -> bool:
    action, event = classify_sequence(seq, joystick_port)
    if action == "ignore":
        return False
    if action == "quit":
        session.post_events([release_all_event()])
        return True
    if action == "release_all":
        session.post_events([release_all_event()])
        return False
    if event is None:
        return False
    events, pending_action = collect_event_batch(event, joystick_port, reader)
    post_interactive_events(session, events)
    if pending_action == "release_all":
        session.post_events([release_all_event()])
    elif pending_action == "quit":
        session.post_events([release_all_event()])
        return True
    return False


def run_interactive_loop(
    session: RestInputSession,
    joystick_port: int,
    read_sequence: Callable[[], str],
    read_ready_sequence: Callable[[], Optional[str]],
) -> None:
    reader = BufferedSequenceReader(read_sequence, read_ready_sequence)
    while True:
        if handle_interactive_sequence(session, joystick_port, reader.get(), reader):
            break


def run_interactive_with_gamepad(
    session: RestInputSession,
    joystick_port: int,
    gamepad: GamepadDevice,
    read_sequence: Callable[[], str],
    read_ready_sequence: Callable[[], Optional[str]],
) -> None:
    stdin_fd = sys.stdin.fileno()
    gamepad_fd = gamepad.fileno()
    reader = BufferedSequenceReader(read_sequence, read_ready_sequence)

    while True:
        now = time.monotonic()
        timeout = gamepad.state.repeat_timeout(now)
        ready, _, _ = select.select([stdin_fd, gamepad_fd], [], [], timeout)
        now = time.monotonic()
        if not ready:
            repeat_event = gamepad.state.poll_repeat_fire(now)
            if repeat_event:
                session.post_events([repeat_event])
            continue
        if gamepad_fd in ready:
            events = gamepad.read_events(now)
            repeat_event = gamepad.state.poll_repeat_fire(now)
            if repeat_event:
                events.append(repeat_event)
            if events:
                session.post_events(events)
        if stdin_fd in ready:
            if handle_interactive_sequence(session, joystick_port, reader.get(), reader):
                break


def run_interactive(session: RestInputSession, joystick_port: int, gamepad: Optional[GamepadDevice]) -> None:
    print_mapping_overview(session.host, joystick_port, gamepad)
    wait_for_input_ready(session, timeout=15.0)
    session.post_events([release_all_event()])
    with raw_terminal():
        if gamepad:
            run_interactive_with_gamepad(session, joystick_port, gamepad, read_key_sequence, read_ready_key_sequence)
        else:
            run_interactive_loop(session, joystick_port, read_key_sequence, read_ready_key_sequence)


def assert_input_state(session: RestInputSession, keyboard: List[str], joystick1: List[str], joystick2: List[str]) -> None:
    state = session.get_state()
    if state.get("keyboard", {}).get("inputs") != keyboard:
        raise Failure(f"Keyboard state mismatch: {state}")
    if state.get("joysticks") != [{"port": 1, "inputs": joystick1}, {"port": 2, "inputs": joystick2}]:
        raise Failure(f"Joystick state mismatch: {state}")


def run_self_test(session: RestInputSession, joystick_port: int, gamepad: Optional[GamepadDevice]) -> None:
    print_mapping_overview(session.host, joystick_port, gamepad)
    wait_for_input_ready(session, timeout=15.0)

    print("[1] keyboard input reaches the live C64 matrix ... ", end="", flush=True)
    reset_to_basic(session)
    session.post_events([{"kind": "keyboard", "inputs": ["x"], "transition": "press"}])
    assert_keyboard_matrix_inputs(session, ["x"])
    session.post_events([release_all_event()])
    print("OK")

    print("[2] keyboard persistent state round-trips ... ", end="", flush=True)
    session.post_events([{"kind": "keyboard", "inputs": ["ctrl"], "transition": "press"}])
    assert_input_state(session, ["ctrl"], [], [])
    session.post_events([release_all_event()])
    assert_input_state(session, [], [], [])
    print("OK")

    print("[3] joystick movement reaches the live CIA joystick lines ... ", end="", flush=True)
    direction = translate_sequence("\x1b[1;5A", joystick_port)
    fire = translate_sequence("\n", joystick_port)
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
    if direction is None or fire is None:
        raise Failure("internal joystick key translation failed")
    session.post_events([release_all_event()])
    assert_input_state(session, [], [], [])
    print("OK")

    print("input_tool self-test: OK")


def main() -> int:
    parser = argparse.ArgumentParser(description="Send local keyboard input to U64 REST input injection")
    parser.add_argument("--host", default=os.environ.get("U64_INPUT_HOST", "u64"))
    parser.add_argument("--rest-host", default=os.environ.get("U64_INPUT_REST_HOST"))
    parser.add_argument("--password", default=os.environ.get("U64_INPUT_PASSWORD", os.environ.get("C64U_PASSWORD")))
    parser.add_argument("--timeout", type=float, default=float(os.environ.get("U64_INPUT_TIMEOUT", "5.0")))
    parser.add_argument("--self-test", action="store_true", help="run key/joystick verification and exit")
    parser.add_argument("--joystick-port", type=int, choices=(1, 2), default=2)
    parser.add_argument("--gamepad-device", help="Linux /dev/input event device for the USB gamepad")
    parser.add_argument("--no-gamepad", action="store_true", help="disable automatic USB gamepad support")
    args = parser.parse_args()

    rest_host = args.rest_host or args.host
    session = RestInputSession(rest_host, args.password, args.timeout)
    gamepad = None if args.no_gamepad else open_gamepad(args.gamepad_device, args.joystick_port)
    try:
        if args.self_test:
            run_self_test(session, args.joystick_port, gamepad)
        else:
            run_interactive(session, args.joystick_port, gamepad)
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
        if gamepad:
            gamepad.close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
