from __future__ import annotations

from collections import deque
from pathlib import Path
import sys


TOOLS_API = Path(__file__).resolve().parent
if str(TOOLS_API) not in sys.path:
    sys.path.insert(0, str(TOOLS_API))

import input_tool  # noqa: E402


class FakeSession:
    def __init__(self) -> None:
        self.host = "u64"
        self.calls: list[list[dict[str, object]]] = []
        self.payloads: list[dict[str, object]] = []

    def post_events(self, events: list[dict[str, object]]) -> None:
        self.calls.append(events)

    def json_request(self, method: str, path: str, payload: dict[str, object]) -> dict[str, object]:
        assert method == "POST"
        assert path == "/v1/machine:input"
        self.payloads.append(payload)
        self.calls.append(payload["events"])  # type: ignore[arg-type]
        return {"errors": []}


def scripted_reader(*sequences: str):
    queue = deque(sequences)

    def read() -> str:
        if not queue:
            raise AssertionError("interactive loop read past scripted input")
        return queue.popleft()

    return read


def scripted_ready_reader(*sequences: str):
    queue = deque(sequences)

    def read():
        if not queue:
            return None
        return queue.popleft()

    return read


def test_translate_sequence_uses_keyboard_taps_for_text_input() -> None:
    assert input_tool.translate_sequence("a", 2) == {
        "kind": "keyboard",
        "inputs": ["a"],
        "transition": "tap",
    }
    assert input_tool.translate_sequence("A", 2) == {
        "kind": "keyboard",
        "inputs": ["left_shift", "a"],
        "transition": "tap",
    }


def test_run_interactive_loop_posts_single_tap_for_keyboard_input() -> None:
    session = FakeSession()

    input_tool.run_interactive_loop(
        session,
        2,
        scripted_reader("a", "\x03"),
        scripted_ready_reader(),
    )

    assert session.calls == [
        [{"kind": "keyboard", "inputs": ["a"], "transition": "tap"}],
        [{"kind": "release_all"}],
    ]
    assert session.payloads == [
        {
            "events": [{"kind": "keyboard", "inputs": ["a"], "transition": "tap"}],
        },
    ]


def test_run_interactive_loop_batches_ready_keys_with_pacing() -> None:
    session = FakeSession()

    input_tool.run_interactive_loop(
        session,
        2,
        scripted_reader("a", "\x03"),
        scripted_ready_reader("b", "C"),
    )

    assert session.calls == [
        [
            {"kind": "keyboard", "inputs": ["a"], "transition": "tap"},
            {"kind": "keyboard", "inputs": ["b"], "transition": "tap"},
            {"kind": "keyboard", "inputs": ["left_shift", "c"], "transition": "tap"},
        ],
        [{"kind": "release_all"}],
    ]
    assert session.payloads == [
        {
            "events": [
                {"kind": "keyboard", "inputs": ["a"], "transition": "tap"},
                {"kind": "keyboard", "inputs": ["b"], "transition": "tap"},
                {"kind": "keyboard", "inputs": ["left_shift", "c"], "transition": "tap"},
            ],
            "pace_ms": input_tool.BATCH_PACE_MS,
        },
    ]


def test_run_interactive_loop_preserves_release_all_and_joystick_events() -> None:
    session = FakeSession()

    input_tool.run_interactive_loop(
        session,
        2,
        scripted_reader("\x1b[1;5A", "\x03"),
        scripted_ready_reader("\x1b"),
    )

    assert session.calls == [
        [{"kind": "joystick", "port": 2, "inputs": ["up"], "transition": "tap"}],
        [{"kind": "release_all"}],
        [{"kind": "release_all"}],
    ]
