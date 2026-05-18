#!/usr/bin/env python3
import argparse
import http.client
import json
import os
import sys
import time
import urllib.error
import urllib.parse
import urllib.request
from contextlib import contextmanager
from typing import Any, Dict, List, Optional, Tuple

CHECK_COUNT = 0
READY_SCREEN_CODES = bytes((0x12, 0x05, 0x01, 0x04, 0x19, 0x2E))
KEY_HOLD_SECONDS = 0.12
KEY_GAP_SECONDS = 0.03
JOYSTICK_PROBE_ADDR = 0xC000
JOYSTICK_PROBE_SCREEN_ADDR = 0x0400
JOYSTICK_PROBE_CODE = bytes(
    (
        0xAD,
        0x00,
        0xDC,
        0x8D,
        0x00,
        0x04,
        0xAD,
        0x01,
        0xDC,
        0x8D,
        0x01,
        0x04,
        0x4C,
        0x00,
        0xC0,
    )
)
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


def screen_code(text: str) -> bytes:
    result = bytearray()
    for ch in text:
        if "A" <= ch <= "Z":
            result.append(ord(ch) - 64)
        else:
            result.append(ord(ch))
    return bytes(result)


def cursor_address(session: RestInputSession) -> int:
    pointer = session.read_memory(0x00D1, 3)
    return pointer[0] + (pointer[1] << 8) + pointer[2]


def reset_to_basic(session: RestInputSession) -> None:
    session.reset()
    wait_for_basic_ready(session)
    session.post_events([{"kind": "release_all"}])


def text_to_key_inputs(text: str) -> List[List[str]]:
    result: List[List[str]] = []
    for ch in text:
        if "a" <= ch <= "z":
            result.append([ch])
        elif "A" <= ch <= "Z":
            result.append([ch.lower()])
        elif "0" <= ch <= "9":
            result.append([ch])
        elif ch == " ":
            result.append(["space"])
        elif ch == ",":
            result.append(["comma"])
        elif ch == ":":
            result.append(["colon"])
        elif ch == "(":
            result.append(["left_shift", "8"])
        elif ch == ")":
            result.append(["left_shift", "9"])
        elif ch in ("\r", "\n"):
            result.append(["return"])
        else:
            raise Failure(f"No C64 key mapping for {ch!r}")
    return result


def send_c64_keys(session: RestInputSession, sequence: List[List[str]]) -> None:
    for inputs in sequence:
        session.post_events([{"kind": "keyboard", "inputs": inputs, "transition": "press"}])
        time.sleep(KEY_HOLD_SECONDS)
        session.post_events([{"kind": "keyboard", "inputs": inputs, "transition": "release"}])
        time.sleep(KEY_GAP_SECONDS)


def type_c64_text(session: RestInputSession, text: str) -> None:
    send_c64_keys(session, text_to_key_inputs(text))


def basic_input_line_start(screen: bytes) -> int:
    ready = screen.find(READY_SCREEN_CODES)
    if ready < 0:
        raise Failure("BASIC READY prompt not visible.")
    return ((ready // 40) + 1) * 40


def wait_for_screen_bytes(session: RestInputSession, address: int, expected: bytes, timeout: float = 5.0) -> None:
    deadline = time.time() + timeout
    actual = b""
    while time.time() < deadline:
        actual = session.read_memory(address, len(expected))
        if actual == expected:
            return
        time.sleep(0.1)
    raise Failure(f"Expected {expected.hex().upper()} at ${address:04X}, got {actual.hex().upper()}")


def assert_c64_typed_text(session: RestInputSession, text: str) -> None:
    screen = session.read_memory(0x0400, 1000)
    address = 0x0400 + basic_input_line_start(screen)
    type_c64_text(session, text)
    wait_for_screen_bytes(session, address, screen_code(text.upper()))


def start_joystick_probe(session: RestInputSession) -> None:
    reset_to_basic(session)
    session.write_memory(JOYSTICK_PROBE_ADDR, JOYSTICK_PROBE_CODE)
    type_c64_text(session, "SYS49152\n")
    wait_for_screen_bytes(session, JOYSTICK_PROBE_SCREEN_ADDR, b"\xFF\xFF")


def assert_c64_joystick_probe(session: RestInputSession, port1: int, port2: int) -> None:
    wait_for_screen_bytes(session, JOYSTICK_PROBE_SCREEN_ADDR, bytes(((port2 & 0x1F) | 0xE0, (port1 & 0x1F) | 0xE0)))


def assert_joystick_ports(session: RestInputSession, port1: int, port2: int) -> None:
    actual_port1, actual_port2 = read_joystick_cia(session)
    if (actual_port1 & 0x1F) != port1 or (actual_port2 & 0x1F) != port2:
        raise Failure(
            f"Expected joy1=${port1:02X} joy2=${port2:02X}, "
            f"got joy1=${actual_port1 & 0x1F:02X} joy2=${actual_port2 & 0x1F:02X}"
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
        reset_to_basic(session)
        session.post_events([{"kind": "keyboard", "inputs": ["l"], "transition": "press"}])
        assert_keyboard_matrix_inputs(session, ["l"])
        session.post_events([{"kind": "release_all"}])
        assert_state_empty(session)

    with check("keyboard shifted pair reaches the live C64 matrix"):
        reset_to_basic(session)
        session.post_events([{"kind": "keyboard", "inputs": ["left_shift", "a"], "transition": "press"}])
        assert_keyboard_matrix_inputs(session, ["left_shift", "a"])
        session.post_events([{"kind": "release_all"}])
        assert_state_empty(session)

    with check("paced keyboard tap batch types consecutive characters"):
        reset_to_basic(session)
        screen = session.read_memory(0x0400, 1000)
        address = 0x0400 + basic_input_line_start(screen)
        session.json_request(
            "POST",
            "/v1/machine:input",
            payload={
                "events": [
                    {"kind": "keyboard", "inputs": ["a"], "transition": "tap"},
                    {"kind": "keyboard", "inputs": ["b"], "transition": "tap"},
                ],
                "pace_ms": 25,
            },
        )
        wait_for_screen_bytes(session, address, screen_code("AB"))
        session.post_events([{"kind": "release_all"}])
        assert_state_empty(session)

    with check("keyboard ordered batch and idempotent release"):
        reset_to_basic(session)
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
        reset_to_basic(session)
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
        reset_to_basic(session)
        inputs = ["a", "s", "d", "f", "j", "k", "l", "space"]
        session.post_events([{"kind": "keyboard", "inputs": inputs, "transition": "press"}])
        assert_input_state(session, ["a", "s", "d", "f", "j", "k", "l", "space"], [], [])
        session.post_events([{"kind": "release_all"}])
        assert_state_empty(session)

    with check("keyboard tap does not release persistent key"):
        reset_to_basic(session)
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
        reset_to_basic(session)
        session.post_events([{"kind": "keyboard", "inputs": ["left_shift"], "transition": "press"}])
        session.post_events([{"kind": "release_all"}])
        assert_state_empty(session)

    with check("keyboard restore tap auto releases"):
        reset_to_basic(session)
        session.post_events([{"kind": "keyboard", "inputs": ["restore"], "transition": "tap"}])
        time.sleep(0.2)
        assert_state_empty(session)

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


def run_joystick_tests(session: RestInputSession) -> None:
    reset_to_basic(session)

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


def run_tests(session: RestInputSession) -> None:
    wait_for_input_ready(session, timeout=15.0)
    run_contract_tests(session)
    run_keyboard_tests(session)
    run_joystick_tests(session)


def main() -> int:
    parser = argparse.ArgumentParser(description="Validate U64 keyboard and joystick REST input injection")
    parser.add_argument("--host", default=os.environ.get("U64_INPUT_HOST", "u64"))
    parser.add_argument("--rest-host", default=os.environ.get("U64_INPUT_REST_HOST"))
    parser.add_argument("--password", default=os.environ.get("U64_INPUT_PASSWORD", os.environ.get("C64U_PASSWORD")))
    parser.add_argument("--timeout", type=float, default=float(os.environ.get("U64_INPUT_TIMEOUT", "5.0")))
    args = parser.parse_args()

    rest_host = args.rest_host or args.host
    session = RestInputSession(rest_host, args.password, args.timeout)
    try:
        run_tests(session)
    except Failure as exc:
        print(exc, file=sys.stderr)
        return 1
    except (OSError, TimeoutError, urllib.error.URLError) as exc:
        if device_unavailable(exc):
            print(f"Connection failure: {format_exception(exc)}", file=sys.stderr)
        else:
            print(f"REST failure: {format_exception(exc)}", file=sys.stderr)
        return 1

    print(f"input_test: OK ({CHECK_COUNT} checks)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
