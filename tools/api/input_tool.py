#!/usr/bin/env python3
import argparse
import glob
import http.client
import json
import math
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
BATCH_PACE_MS = int(os.environ.get("U64_INPUT_PACE_MS", "10"))
KEY_REPEAT_HZ = float(os.environ.get("U64_INPUT_REPEAT_HZ", "50.0"))
KEY_REPEAT_INTERVAL = 1.0 / KEY_REPEAT_HZ if KEY_REPEAT_HZ > 0 else 0.05
KEY_REPEAT_PACE_MS = int(os.environ.get("U64_INPUT_REPEAT_PACE_MS", "20"))
KEY_REPEAT_MAX_BURST = max(1, int(os.environ.get("U64_INPUT_REPEAT_MAX_BURST", "6")))
KEY_REPEAT_STALE_SECONDS = float(os.environ.get("U64_INPUT_REPEAT_STALE", "0.25"))
KEY_REPEAT_CONFIRM_COUNT = max(2, int(os.environ.get("U64_INPUT_REPEAT_CONFIRM", "3")))
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


class InteractiveRestClient:
    def __init__(self, session: RestInputSession) -> None:
        self.host = session.host
        self.password = session.password
        self.timeout = session.timeout
        self._connection: Optional[http.client.HTTPConnection] = None
        self.last_request_duration = max(KEY_REPEAT_INTERVAL, 0.08)

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
            started_at = time.monotonic()
            try:
                connection.request("POST", "/v1/machine:input", body=body, headers=headers)
                response = connection.getresponse()
                data = response.read()
                self.last_request_duration = max(time.monotonic() - started_at, KEY_REPEAT_INTERVAL)
                if response.status >= 400:
                    self.close()
                    text = data.decode("utf-8", errors="replace").strip()
                    raise Failure(f"REST input POST failed with HTTP {response.status}: {text}")
                if not data:
                    return {}
                return json.loads(data.decode("utf-8"))
            except (OSError, http.client.HTTPException, json.JSONDecodeError) as exc:
                self.close()
                if attempt == 0:
                    continue
                raise Failure(f"Interactive REST request failed: {format_exception(exc)}") from exc
        raise Failure("Interactive REST request failed.")


class RepeatState:
    def __init__(self) -> None:
        self.sequence: Optional[str] = None
        self.signature: Optional[Tuple[object, object, Tuple[object, ...], object]] = None
        self.event: Optional[Dict[str, object]] = None
        self.count = 0
        self.confirmed = False
        self.last_seen_at = 0.0
        self.next_emit_at = 0.0

    def clear(self) -> None:
        self.sequence = None
        self.signature = None
        self.event = None
        self.count = 0
        self.confirmed = False
        self.last_seen_at = 0.0
        self.next_emit_at = 0.0

    def _active(self) -> bool:
        return self.event is not None and self.sequence is not None and self.signature is not None

    def timeout(self, now: float) -> Optional[float]:
        if not self._active():
            return None
        stale_deadline = self.last_seen_at + KEY_REPEAT_STALE_SECONDS
        if self.confirmed:
            deadline = min(stale_deadline, self.next_emit_at)
        else:
            deadline = stale_deadline
        return max(0.0, deadline - now)

    def observe(self, sequence: str, event: Dict[str, object], now: float) -> bool:
        repeatable = event.get("transition") == "tap" and event.get("kind") in ("keyboard", "joystick")
        if not repeatable:
            self.clear()
            return False

        signature = event_signature(event)
        if self.sequence == sequence and self.signature == signature:
            self.count += 1
            self.last_seen_at = now
            if self.confirmed:
                return True
            if self.count >= KEY_REPEAT_CONFIRM_COUNT:
                self.confirmed = True
                self.next_emit_at = now + KEY_REPEAT_INTERVAL
                return True
            return False

        self.sequence = sequence
        self.signature = signature
        self.event = event
        self.count = 1
        self.confirmed = False
        self.last_seen_at = now
        self.next_emit_at = 0.0
        return False

    def poll(self, now: float) -> Optional[Dict[str, object]]:
        if not self._active():
            return None
        if (now - self.last_seen_at) > KEY_REPEAT_STALE_SECONDS:
            self.clear()
            return None
        if not self.confirmed or now < self.next_emit_at or self.event is None:
            return None
        while self.next_emit_at <= now:
            self.next_emit_at += KEY_REPEAT_INTERVAL
        return self.event


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


def flush_event_batch(client: InteractiveRestClient, events: List[Dict[str, object]]) -> None:
    while events:
        chunk = events[:MAX_BATCH_EVENTS]
        client.post_events(chunk)
        del events[:MAX_BATCH_EVENTS]


def flush_repeat_event(client: InteractiveRestClient, event: Dict[str, object]) -> None:
    if event.get("kind") != "keyboard" or event.get("transition") != "tap":
        client.post_events([event])
        return

    burst = max(1, min(KEY_REPEAT_MAX_BURST, int(math.ceil(client.last_request_duration * KEY_REPEAT_HZ))))
    if burst == 1:
        client.post_events([event])
        return

    client.post_events([event] * burst, pace_ms=KEY_REPEAT_PACE_MS)


def translate_sequence(seq: str, joystick_port: int) -> Optional[Dict[str, object]]:
    if seq == "\x1b":
        return release_all_event()
    if seq == "\n":
        return joystick_event(joystick_port, ["fire"])
    if seq == "\x1b[3~":
        return keyboard_event(["inst_del"])
    if seq.startswith("\x1b["):
        if seq in ("\x1b[13;5u", "\x1b[27;5;13~"):
            return joystick_event(joystick_port, ["fire"])
    if (seq.startswith("\x1b[") or seq.startswith("\x1bO")) and seq[-1:] in ARROW_KEYS:
        direction = ARROW_KEYS[seq[-1]]
        if ";5" in seq:
            return joystick_event(joystick_port, [direction])
        return cursor_keyboard_event(direction)
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
    print("keys: text=C64 keys; arrows=C64 cursor; Ctrl+arrows=joy; Ctrl+Enter/Ctrl+J=fire; Esc=release_all; Ctrl+C/Ctrl+D=quit")
    print(f"interactive repeat: {KEY_REPEAT_HZ:.0f}/s without client backlog")
    print(f"paced multi-key batches: {BATCH_PACE_MS} ms between batched events")
    print(f"joystick port: {joystick_port}")
    if gamepad:
        print(f"gamepad: {gamepad.path} (left stick, right stick, d-pad -> movement; A=fire; B=repeat fire)")
    else:
        print("gamepad: not detected")


def classify_sequence(seq: str, joystick_port: int) -> Tuple[str, Optional[Dict[str, object]]]:
    if seq in QUIT_SEQUENCES:
        return "quit", None
    event = translate_sequence(seq, joystick_port)
    if not event:
        return "ignore", None
    if event.get("kind") == "release_all":
        return "release_all", event
    return "event", event


def handle_interactive_sequence(
    joystick_port: int,
    seq: str,
    repeat_state: RepeatState,
    now: float,
) -> Tuple[Optional[str], List[Dict[str, object]]]:
    action, event = classify_sequence(seq, joystick_port)
    if action == "ignore":
        return None, []
    if action == "quit":
        repeat_state.clear()
        return "quit", []
    if action == "release_all":
        repeat_state.clear()
        return "release_all", [release_all_event()]
    if event is None:
        return None, []
    if repeat_state.observe(seq, event, now):
        return None, []
    return None, [event]


def run_interactive_loop(
    session: RestInputSession,
    joystick_port: int,
) -> None:
    stdin_fd = sys.stdin.fileno()
    client = InteractiveRestClient(session)
    repeat_state = RepeatState()
    try:
        while True:
            now = time.monotonic()
            ready, _, _ = select.select([stdin_fd], [], [], repeat_state.timeout(now))
            now = time.monotonic()
            pending_events: List[Dict[str, object]] = []
            if ready:
                for seq in drain_stdin_sequences(stdin_fd):
                    control, events = handle_interactive_sequence(joystick_port, seq, repeat_state, time.monotonic())
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
            repeat_event = repeat_state.poll(time.monotonic())
            if pending_events:
                flush_event_batch(client, pending_events)
            if repeat_event:
                flush_repeat_event(client, repeat_event)
    finally:
        client.close()


def run_interactive_with_gamepad(
    session: RestInputSession,
    joystick_port: int,
    gamepad: GamepadDevice,
) -> None:
    stdin_fd = sys.stdin.fileno()
    gamepad_fd = gamepad.fileno()
    client = InteractiveRestClient(session)
    repeat_state = RepeatState()

    try:
        while True:
            now = time.monotonic()
            timeout = combine_timeout(repeat_state.timeout(now), gamepad.state.repeat_timeout(now))
            ready, _, _ = select.select([stdin_fd, gamepad_fd], [], [], timeout)
            now = time.monotonic()
            pending_events: List[Dict[str, object]] = []
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
                        control, events = handle_interactive_sequence(joystick_port, seq, repeat_state, time.monotonic())
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
            repeat_keyboard_event = repeat_state.poll(time.monotonic())
            if pending_events:
                flush_event_batch(client, pending_events)
            if repeat_keyboard_event:
                flush_repeat_event(client, repeat_keyboard_event)
    finally:
        client.close()


def run_interactive(session: RestInputSession, joystick_port: int, gamepad: Optional[GamepadDevice]) -> None:
    print_mapping_overview(session.host, joystick_port, gamepad)
    wait_for_input_ready(session, timeout=15.0)
    session.post_events([release_all_event()])
    with raw_terminal():
        if gamepad:
            run_interactive_with_gamepad(session, joystick_port, gamepad)
        else:
            run_interactive_loop(session, joystick_port)


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
