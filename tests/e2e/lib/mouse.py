"""A mouse on control port 1, and a C64 program that reports what it saw.

`tools/c64/mouse-listener.asm` runs on the C64 and follows port 1 the way a
1351 driver with Micromys wheel support does, and port 2 and the keyboard as a
joystick routine and a keyboard scan do. It keeps the position, the smallest
and largest change of each POT between frames, the buttons, the wheel pulses,
the port 2 lines and a press count for every key in a fixed RAM block, which
`MouseListener` reads over REST, and shows them on screen.

The mouse itself comes from a backend with one interface: `move`, `press`,
`release` and `wheel`. `PicoMouse` drives the USB mouse of the Pico 2 W fixture
(`tests/lib/pico_hid.py`), so a check runs through the firmware's USB HID
driver exactly as a real mouse does.

    listener = MouseListener(api)
    listener.start()
    mouse = PicoMouse(pico)
    listener.reset()
    mouse.move(20, 10)
    state = listener.wait_until(lambda s: s.x == 20)

Positions are in POT counts. With Mouse Sensitivity 8 and acceleration off,
one HID count moves the position by one POT count. Y grows downwards.
"""

from __future__ import annotations

import time
from collections.abc import Callable
from dataclasses import dataclass
from pathlib import Path

from assembler import assemble
from pico_hid import MOUSE_BUTTONS as MOUSE_BUTTON_MASKS
from report import Failure

LISTENER_SOURCE = Path(__file__).resolve().parents[3] / "tools" / "c64" / "mouse-listener.asm"

# Result block of mouse-listener.asm.
BLOCK = 0xC000
BLOCK_LENGTH = 0x68
READY_VALUE = 0xA5
RESET = 0xC001

BUTTON_BITS = {"left": 0x01, "middle": 0x02, "right": 0x04}
PORT2_LINES = ("up", "down", "left", "right", "fire")

# Keyboard matrix positions (column = bit of $DC00, row = bit of $DC01) of the
# keys the suites press.
KEYS = {
    "return": (0, 1), "cursor_left_right": (0, 2), "cursor_up_down": (0, 7),
    "a": (1, 2), "left_shift": (1, 7), "b": (3, 4), "right_shift": (6, 4), "space": (7, 4),
}

START_TIMEOUT_SECONDS = 10.0
STABLE_READ_ATTEMPTS = 10
RESET_TIMEOUT_SECONDS = 3.0

def _signed16(low: int, high: int) -> int:
    value = low | (high << 8)
    return value - 0x10000 if value & 0x8000 else value


def _stable_part(block: bytes) -> bytes:
    """X and Y: the only counters of more than one byte a read can split."""
    return block[0x02:0x06]


def _counters(state: MouseState) -> tuple:
    return (state.x, state.y, tuple(state.presses.values()), state.wheel_up, state.wheel_down,
            tuple(state.port2_presses.values()), tuple(state.cursor.values()), state.key_counts)


def _signed8(value: int) -> int:
    return value - 0x100 if value & 0x80 else value


@dataclass(frozen=True)
class MouseState:
    x: int
    y: int
    held: frozenset[str]
    presses: dict[str, int]
    wheel_up: int
    wheel_down: int
    frames: int
    potx: int
    poty: int
    lines: int
    # Smallest and largest change of the position between two frames, in the
    # position's own directions.
    step_x: tuple[int, int]
    step_y: tuple[int, int]
    port2: frozenset[str]
    port2_presses: dict[str, int]
    cursor: dict[str, int]
    keys_held: int
    key_counts: bytes
    matrix: bytes

    def key_presses(self, name: str) -> int:
        column, row = KEYS[name]
        return self.key_counts[column * 8 + row]

    def key_down(self, name: str) -> bool:
        column, row = KEYS[name]
        return bool(self.matrix[column] & (1 << row))

    def __str__(self) -> str:
        held = ",".join(sorted(self.held)) or "none"
        presses = " ".join(f"{name}={count}" for name, count in self.presses.items())
        port2 = ",".join(sorted(self.port2)) or "none"
        port2_presses = " ".join(f"{name}={count}" for name, count in self.port2_presses.items() if count)
        cursor = " ".join(f"{name}={count}" for name, count in self.cursor.items() if count)
        keys = " ".join(f"{index // 8}.{index % 8}={count}" for index, count in enumerate(self.key_counts) if count)
        return (f"x={self.x} y={self.y} step x{self.step_x} y{self.step_y} held={held} presses[{presses}] "
                f"wheel up={self.wheel_up} down={self.wheel_down} "
                f"port2={port2} [{port2_presses}] cursor[{cursor}] keys held={self.keys_held} [{keys}] "
                f"pot=${self.potx:02X}/${self.poty:02X} lines=${self.lines:02X} frames={self.frames}")


class MouseListener:
    """The C64 side: start the listener, reset it and read its RAM block."""

    def __init__(self, api) -> None:
        self.api = api

    def start(self) -> None:
        program = assemble(LISTENER_SOURCE)
        self.api.machine.writemem(BLOCK, bytes(2))
        status, _, body = self.api.runners.upload("run_prg", program)
        if status != 200:
            raise Failure(f"runners:run_prg returned HTTP {status}: {body[:160]!r}")
        deadline = time.monotonic() + START_TIMEOUT_SECONDS
        while self.api.machine.readmem(BLOCK, 1)[0] != READY_VALUE:
            if time.monotonic() > deadline:
                raise Failure("the mouse listener did not start")
            time.sleep(0.1)

    def reset(self) -> MouseState:
        """Zero position and counters; the current position becomes the origin."""
        self.api.machine.writemem(RESET, b"\x01")
        deadline = time.monotonic() + RESET_TIMEOUT_SECONDS
        while self.api.machine.readmem(RESET, 1)[0] != 0:
            if time.monotonic() > deadline:
                raise Failure("the mouse listener did not reset")
            time.sleep(0.05)
        return self.state()

    def state(self) -> MouseState:
        """One consistent view of the result block.

        A read is a DMA transfer that stops the 6510 wherever it is, which can
        be between the two bytes of the X or Y total. A view is taken only when
        two reads in a row agree on X and Y; every other counter is one byte
        that the listener writes in one go, so typing or pulses going on while
        the block is read cannot keep a view from being taken.
        """
        previous = self.api.machine.readmem(BLOCK, BLOCK_LENGTH)
        for _ in range(STABLE_READ_ATTEMPTS):
            current = self.api.machine.readmem(BLOCK, BLOCK_LENGTH)
            if _stable_part(current) == _stable_part(previous):
                return self._decode(current)
            previous = current
        raise Failure("the mouse listener's result block kept changing between reads")

    def _decode(self, b: bytes) -> MouseState:
        if b[0] != READY_VALUE:
            raise Failure("the mouse listener is not running")
        return MouseState(
            x=_signed16(b[2], b[3]), y=_signed16(b[4], b[5]),
            held=frozenset(name for name, bit in BUTTON_BITS.items() if b[6] & bit),
            presses={"left": b[7], "middle": b[8], "right": b[9]},
            wheel_up=b[10], wheel_down=b[11], frames=b[12] | (b[13] << 8),
            potx=b[14], poty=b[15], lines=b[16],
            step_x=(_signed8(b[0x11]), _signed8(b[0x12])),
            # Y moves opposite to POTY.
            step_y=(-_signed8(b[0x14]), -_signed8(b[0x13])),
            port2=frozenset(name for bit, name in enumerate(PORT2_LINES) if b[0x15] & (1 << bit)),
            port2_presses={name: b[0x16 + bit] for bit, name in enumerate(PORT2_LINES)},
            cursor={"up": b[0x1B], "down": b[0x1C], "left": b[0x1D], "right": b[0x1E]},
            keys_held=b[0x1F], key_counts=bytes(b[0x20:0x60]), matrix=bytes(b[0x60:0x68]))

    def quiet(self, timeout: float = 15.0, hold: float = 0.3) -> MouseState:
        """Wait until nothing is held or pending and no counter changes for `hold` seconds.

        Wheel bursts and queued cursor keys finish on the Ultimate's own
        timers, after the call that caused them has returned, so a check reads
        its result only once they are over.
        """
        deadline = time.monotonic() + timeout
        last = self.state()
        since = time.monotonic()
        while True:
            time.sleep(0.05)
            state = self.state()
            idle = not (state.held or state.lines or state.port2 or state.keys_held)
            if not idle or _counters(state) != _counters(last):
                last, since = state, time.monotonic()
            elif time.monotonic() - since >= hold:
                return state
            if time.monotonic() > deadline:
                raise Failure(f"the mouse listener did not go quiet within {timeout}s: {state}")

    def wait_until(self, condition: Callable[[MouseState], bool], timeout: float = 3.0) -> MouseState:
        """Poll until `condition` holds; raise with the last state if it never does."""
        deadline = time.monotonic() + timeout
        while True:
            state = self.state()
            if condition(state):
                return state
            if time.monotonic() > deadline:
                raise Failure(f"the mouse listener never reached the expected state: {state}")
            time.sleep(0.05)


class PicoMouse:
    """Mouse input from the USB mouse of the Pico 2 W fixture.

    The fixture answers once a report is queued on its USB endpoint, which is
    before the Ultimate has read it. Every call therefore waits
    `REPORT_SECONDS` after its last report, so the next step, such as a
    listener reset, cannot start while that report is still on its way.
    """

    # The firmware polls the mouse endpoint every 20ms.
    REPORT_SECONDS = 0.06

    def __init__(self, pico) -> None:
        self.pico = pico
        self.held: set[str] = set()

    def move(self, dx: int, dy: int) -> None:
        """Move by dx, dy HID counts, split into reports of at most 63 counts.

        A report of more than 63 counts could move the position by 64 or more
        between two frames, which a 1351 driver cannot tell from a move the
        other way.
        """
        while dx or dy:
            step_x = max(-63, min(63, dx))
            step_y = max(-63, min(63, dy))
            self.pico.mouse_move(step_x, step_y)
            dx -= step_x
            dy -= step_y
            time.sleep(self.REPORT_SECONDS)

    def press(self, button: str) -> None:
        self.held.add(button)
        self.pico.mouse_buttons(*sorted(self.held))
        time.sleep(self.REPORT_SECONDS)

    def release(self, button: str) -> None:
        self.held.discard(button)
        self.pico.mouse_buttons(*sorted(self.held))
        time.sleep(self.REPORT_SECONDS)

    def release_all(self) -> None:
        self.held.clear()
        self.pico.mouse_buttons()
        time.sleep(self.REPORT_SECONDS)

    def buttons(self, *pressed: str) -> None:
        """One report with exactly these buttons held."""
        self.held = set(pressed)
        self.pico.mouse_buttons(*sorted(self.held))
        time.sleep(self.REPORT_SECONDS)

    def stream(self, dx: int = 0, dy: int = 0, count: int = 1, interval_ms: int = 0, wheel: int = 0,
               pan: int = 0, buttons: tuple[str, ...] | None = None, key: str | None = None) -> dict:
        """`count` identical reports; see `Pico.mouse_stream`. Returns the fixture's timing."""
        mask = None
        if buttons is not None:
            self.held = set(buttons)
            mask = 0
            for name in buttons:
                mask |= MOUSE_BUTTON_MASKS[name]
        status = self.pico.mouse_stream(dx=dx, dy=dy, count=count, interval_ms=interval_ms,
                                        wheel=wheel, pan=pan, buttons=mask, key=key)
        time.sleep(self.REPORT_SECONDS)
        stream = status.get("last_stream", {})
        if stream.get("sent") != count:
            raise Failure(f"the fixture sent {stream.get('sent')} of {count} reports")
        return stream

    def wheel(self, vertical: int = 0, horizontal: int = 0, pulses_per_detent: int = 1,
              gap_ms: int | None = None) -> None:
        """Turn the wheels by whole detents, vertical first.

        Each Micromys pulse is 50ms active and 50ms released, so by default the
        gap after each detent lets its whole burst finish before the next one
        starts. A smaller `gap_ms`, down to 0 for as fast as the host polls,
        turns the wheel faster than the pulses can follow.
        """
        if gap_ms is None:
            gap_ms = 100 * pulses_per_detent + 150
        self.pico.mouse_wheel(vertical=vertical, horizontal=horizontal, gap_ms=gap_ms)
        time.sleep(self.REPORT_SECONDS)
