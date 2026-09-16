"""A mouse on control port 1, and a C64 program that reports what it saw.

`tools/c64/mouse-listener.asm` runs on the C64 and follows port 1 the way a
1351 driver with Micromys wheel support does, and port 2 and the keyboard as a
joystick routine and a keyboard scan do. It keeps the position, the smallest
and largest change of each POT between frames, the buttons, the wheel pulses,
the port 2 lines and a press count for every key in a fixed RAM block, which
`MouseListener` reads over REST, and shows them on screen.

The mouse itself comes from a backend with one interface: `move`, `press`,
`release`, `buttons`, `wheel` and `stream`. `RestMouse` sends `mouse` events to
`POST /v1/machine:input`, which the firmware feeds through the same report
handling as a USB mouse, and needs no hardware. `PicoMouse` drives the USB
mouse of the Pico 2 W fixture (`tests/lib/pico_hid.py`), so a check runs
through the firmware's USB driver exactly as a real mouse does.

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
from collections.abc import Callable, Sequence
from dataclasses import dataclass
from pathlib import Path

from assembler import assemble
from pico_hid import MOUSE_BUTTONS as MOUSE_BUTTON_MASKS
from report import Failure

LISTENER_SOURCE = Path(__file__).resolve().parents[3] / "tools" / "c64" / "mouse-listener.asm"

# Result block of mouse-listener.asm.
BLOCK = 0xC000
BLOCK_LENGTH = 0x6A
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
    # Frames whose position changed: with `frames`, how smooth the movement was.
    moved: int

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
                f"pot=${self.potx:02X}/${self.poty:02X} lines=${self.lines:02X} "
                f"frames={self.frames} moved={self.moved}")


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
            keys_held=b[0x1F], key_counts=bytes(b[0x20:0x60]), matrix=bytes(b[0x60:0x68]),
            moved=b[0x68] | (b[0x69] << 8))

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

    def path(self, steps: list[tuple[int, int]], reports_per_step: int = 2) -> None:
        """Move through `steps`, each as `reports_per_step` reports at the host's poll rate.

        The steps follow each other as fast as the fixture takes commands, so a
        list whose steps grow and shrink moves the mouse the way a hand speeds
        up and slows down.
        """
        for dx, dy in steps:
            status = self.pico.mouse_stream(dx=dx, dy=dy, count=reports_per_step)
            sent = status.get("last_stream", {}).get("sent")
            if sent != reports_per_step:
                raise Failure(f"the fixture sent {sent} of {reports_per_step} reports")
        time.sleep(self.REPORT_SECONDS)

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


class RestMouse:
    """Mouse input over REST: `mouse` events on `POST /v1/machine:input`.

    Every call waits until the firmware has sent all of its reports
    (`mouse.pending` is 0), so a check reads the result of the call and not of
    part of it.
    """

    REPORT_CLAMP = 63
    PATH_STEPS = 256
    MIN_INTERVAL_MS = 20
    PENDING_TIMEOUT_SECONDS = 120.0

    def __init__(self, api) -> None:
        self.api = api
        self.held: set[str] = set()

    def _send(self, events: Sequence[dict]) -> None:
        for start in range(0, len(events), 64):
            self.api.machine.send_input(list(events[start:start + 64]))

    def pending(self) -> int:
        return int(self.api.machine.input_state().get("mouse", {}).get("pending", 0))

    def wait_sent(self) -> None:
        deadline = time.monotonic() + self.PENDING_TIMEOUT_SECONDS
        while self.pending():
            if time.monotonic() > deadline:
                raise Failure("the REST mouse still had reports pending")
            time.sleep(0.05)
        # The last report is handled by the firmware a moment after it leaves
        # the queue.
        time.sleep(0.03)

    def _path(self, steps: list[tuple[int, int]], interval_ms: int) -> None:
        interval_ms = max(self.MIN_INTERVAL_MS, interval_ms)
        events = [{"kind": "mouse", "path": [list(step) for step in steps[start:start + self.PATH_STEPS]],
                   "interval_ms": interval_ms}
                  for start in range(0, len(steps), self.PATH_STEPS)]
        for event in events:
            self._send([event])
            self.wait_sent()

    def path(self, steps: list[tuple[int, int]], reports_per_step: int = 2) -> None:
        """Move through `steps`, each as `reports_per_step` reports 20ms apart; see `PicoMouse.path`."""
        self._path([step for step in steps for _ in range(reports_per_step)], self.MIN_INTERVAL_MS)

    def move(self, dx: int, dy: int) -> None:
        """Move by dx, dy HID counts, split into steps of at most 63 counts."""
        steps = []
        while dx or dy:
            step_x = max(-self.REPORT_CLAMP, min(self.REPORT_CLAMP, dx))
            step_y = max(-self.REPORT_CLAMP, min(self.REPORT_CLAMP, dy))
            steps.append((step_x, step_y))
            dx -= step_x
            dy -= step_y
        if len(steps) == 1:
            self._send([{"kind": "mouse", "move": {"x": steps[0][0], "y": steps[0][1]}}])
            self.wait_sent()
        elif steps:
            self._path(steps, self.MIN_INTERVAL_MS)

    def press(self, button: str) -> None:
        self.buttons(*(self.held | {button}))

    def release(self, button: str) -> None:
        self.buttons(*(self.held - {button}))

    def release_all(self) -> None:
        self.buttons()

    def tap(self, *buttons: str) -> None:
        """Press and release these buttons in one event; held buttons stay held."""
        self._send([{"kind": "mouse", "inputs": sorted(buttons), "transition": "tap"}])
        self.wait_sent()

    def buttons(self, *pressed: str) -> None:
        """Exactly these buttons held."""
        wanted = set(pressed)
        events = []
        if wanted - self.held:
            events.append({"kind": "mouse", "inputs": sorted(wanted - self.held), "transition": "press"})
        if self.held - wanted:
            events.append({"kind": "mouse", "inputs": sorted(self.held - wanted), "transition": "release"})
        if events:
            self._send(events)
            self.wait_sent()
        self.held = wanted

    def wheel(self, vertical: int = 0, horizontal: int = 0, pulses_per_detent: int = 1,
              gap_ms: int | None = None) -> None:
        """Turn the wheels by whole detents, vertical first; see `PicoMouse.wheel`."""
        if gap_ms is None:
            gap_ms = 100 * pulses_per_detent + 150
        if gap_ms <= self.MIN_INTERVAL_MS:
            if vertical or horizontal:
                self._send([{"kind": "mouse", "wheel": {"vertical": vertical, "horizontal": horizontal}}])
                self.wait_sent()
            return
        detents = ([{"vertical": 1 if vertical > 0 else -1}] * abs(vertical) +
                   [{"horizontal": 1 if horizontal > 0 else -1}] * abs(horizontal))
        for detent in detents:
            self._send([{"kind": "mouse", "wheel": detent}])
            self.wait_sent()
            time.sleep(gap_ms / 1000.0)

    def stream(self, dx: int = 0, dy: int = 0, count: int = 1, interval_ms: int = 0, wheel: int = 0,
               pan: int = 0, buttons: tuple[str, ...] | None = None, key: str | None = None) -> dict:
        """`count` identical reports, at least 20ms apart; see `PicoMouse.stream`.

        `key` is held on the keyboard over REST for the whole stream.
        """
        started = time.monotonic()
        if buttons is not None:
            self.buttons(*buttons)
        if key is not None:
            self.api.machine.send_input([{"kind": "keyboard", "inputs": [key], "transition": "press"}])
        try:
            if wheel or pan:
                for _ in range(count):
                    events = []
                    if dx or dy:
                        events.append({"kind": "mouse", "move": {"x": dx, "y": dy}})
                    events.append({"kind": "mouse", "wheel": {"vertical": wheel, "horizontal": pan}})
                    self._send(events)
                    self.wait_sent()
                    time.sleep(max(interval_ms, self.MIN_INTERVAL_MS) / 1000.0)
            else:
                self._path([(dx, dy)] * count, interval_ms)
        finally:
            if key is not None:
                self.api.machine.send_input([{"kind": "keyboard", "inputs": [key], "transition": "release"}])
        return {"sent": count, "elapsed_ms": int((time.monotonic() - started) * 1000)}
