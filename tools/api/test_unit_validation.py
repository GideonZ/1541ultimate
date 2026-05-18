"""Unit-level contract tests for `/v1/machine:input`."""

from __future__ import annotations

from copy import deepcopy
from typing import Any

import pytest

KEYBOARD_INPUTS = [
    "inst_del",
    "return",
    "cursor_left_right",
    "f7",
    "f1",
    "f3",
    "f5",
    "cursor_up_down",
    "3",
    "w",
    "a",
    "4",
    "z",
    "s",
    "e",
    "left_shift",
    "5",
    "r",
    "d",
    "6",
    "c",
    "f",
    "t",
    "x",
    "7",
    "y",
    "g",
    "8",
    "b",
    "h",
    "u",
    "v",
    "9",
    "i",
    "j",
    "0",
    "m",
    "k",
    "o",
    "n",
    "plus",
    "p",
    "l",
    "minus",
    "period",
    "colon",
    "at",
    "comma",
    "pound",
    "star",
    "semicolon",
    "clr_home",
    "right_shift",
    "equals",
    "arrow_up",
    "slash",
    "1",
    "arrow_left",
    "ctrl",
    "2",
    "space",
    "commodore",
    "q",
    "run_stop",
    "restore",
]
JOYSTICK_INPUTS = ["up", "down", "left", "right", "fire"]
TRANSITIONS = {"press", "release", "tap"}
RESTORE = "restore"
UNSUPPORTED_MESSAGE = "Keyboard and joystick injection require Ultimate 64-class hardware."


class InputApiModel:
    def __init__(self, supported: bool = True, port2_supported: bool = True) -> None:
        self.supported = supported
        self.port2_supported = port2_supported
        self.keyboard_pressed: set[str] = set()
        self.keyboard_overlay: set[str] = set()
        self.joystick_pressed = {1: set[str](), 2: set[str]()}
        self.joystick_overlay = {1: set[str](), 2: set[str]()}

    def get(self) -> tuple[int, dict[str, Any]]:
        if not self.supported:
            return self._unsupported()
        return 200, self._snapshot()

    def post(self, payload: Any) -> tuple[int, dict[str, Any]]:
        if not self.supported:
            return self._unsupported()
        errors = self._validate_payload(payload)
        if errors:
            return 400, {"errors": errors}
        for event in payload["events"]:
            self._apply(event)
        return 200, self._snapshot()

    def post_raw(
        self, body: Any, content_type: str | None = "application/json"
    ) -> tuple[int, dict[str, Any]]:
        if content_type != "application/json":
            return 400, {"errors": ["Unsupported Content-Type; expected application/json."]}
        if body is None:
            return 412, {"errors": ["Expected Body, but got none."]}
        return self.post(body)

    def tick(self) -> None:
        self.keyboard_overlay.clear()
        self.joystick_overlay[1].clear()
        self.joystick_overlay[2].clear()

    def _unsupported(self) -> tuple[int, dict[str, Any]]:
        return 501, {"errors": [UNSUPPORTED_MESSAGE]}

    def _snapshot(self) -> dict[str, Any]:
        return {
            "errors": [],
            "keyboard": {"inputs": self._ordered(KEYBOARD_INPUTS, self.keyboard_pressed | self.keyboard_overlay)},
            "joysticks": [
                {"port": 1, "inputs": self._ordered(JOYSTICK_INPUTS, self.joystick_pressed[1] | self.joystick_overlay[1])},
                {"port": 2, "inputs": self._ordered(JOYSTICK_INPUTS, self.joystick_pressed[2] | self.joystick_overlay[2])},
            ],
        }

    @staticmethod
    def _ordered(order: list[str], values: set[str]) -> list[str]:
        return [item for item in order if item in values]

    def _validate_payload(self, payload: Any) -> list[str]:
        if not isinstance(payload, dict):
            return ["Root must be an object."]
        if set(payload) - {"events", "pace_ms"}:
            return ["Root must contain only `events`."]
        events = payload.get("events")
        if not isinstance(events, list):
            return ["`events` must be an array."]
        pace_ms = payload.get("pace_ms")
        if pace_ms is not None:
            if type(pace_ms) is not int:
                return ["`pace_ms` must be an integer."]
            if not 0 <= pace_ms <= 1000:
                return ["`pace_ms` must be between 0 and 1000."]
        if not 1 <= len(events) <= 64:
            return ["`events` must contain 1..64 entries."]
        for event in events:
            error = self._validate_event(event)
            if error:
                return [error]
        return []

    def _validate_event(self, event: Any) -> str | None:
        if not isinstance(event, dict):
            return "Each event must be an object."
        kind = event.get("kind")
        if not isinstance(kind, str) or kind not in {"keyboard", "joystick", "release_all"}:
            return "`kind` must be one of `keyboard`, `joystick`, or `release_all`."
        if kind == "keyboard":
            return self._validate_keyboard(event)
        if kind == "joystick":
            return self._validate_joystick(event)
        return self._validate_release_all(event)

    def _validate_transition(self, event: dict[str, Any]) -> str | None:
        transition = event.get("transition")
        if not isinstance(transition, str) or transition not in TRANSITIONS:
            return "`transition` must be one of `press`, `release`, or `tap`."
        return None

    def _validate_inputs(self, event: dict[str, Any], allowed: list[str], limit: int, label: str) -> str | None:
        inputs = event.get("inputs")
        if not isinstance(inputs, list):
            return "`inputs` must be an array."
        if not 1 <= len(inputs) <= limit:
            return f"`inputs` must contain 1..{limit} entries."
        seen: set[str] = set()
        for input_name in inputs:
            if not isinstance(input_name, str):
                return f"{label} inputs must be strings."
            if input_name not in allowed:
                return f"`{input_name}` is not a valid {label.lower()} input."
            if input_name in seen:
                return f"`{input_name}` appears more than once in `inputs`."
            seen.add(input_name)
        return None

    def _validate_keyboard(self, event: dict[str, Any]) -> str | None:
        if set(event) - {"kind", "inputs", "transition"}:
            return "Unknown property in keyboard event."
        error = self._validate_transition(event)
        if error:
            return error
        error = self._validate_inputs(event, KEYBOARD_INPUTS, 8, "Keyboard")
        if error:
            return error
        inputs = event["inputs"]
        if RESTORE in inputs and (inputs != [RESTORE] or event["transition"] != "tap"):
            return "`restore` must appear alone in `inputs` and only with transition `tap`."
        return None

    def _validate_joystick(self, event: dict[str, Any]) -> str | None:
        if set(event) - {"kind", "port", "inputs", "transition"}:
            return "Unknown property in joystick event."
        error = self._validate_transition(event)
        if error:
            return error
        port = event.get("port")
        if type(port) is not int:
            return "`port` must be an integer."
        if port not in {1, 2}:
            return "`port` must be 1 or 2."
        if port == 2 and not self.port2_supported:
            return "Joystick port 2 is not supported by this device."
        return self._validate_inputs(event, JOYSTICK_INPUTS, 5, "Joystick")

    @staticmethod
    def _validate_release_all(event: dict[str, Any]) -> str | None:
        if set(event) != {"kind"}:
            return "`release_all` events may only contain `kind`."
        return None

    def _apply(self, event: dict[str, Any]) -> None:
        kind = event["kind"]
        if kind == "release_all":
            self.keyboard_pressed.clear()
            self.keyboard_overlay.clear()
            self.joystick_pressed[1].clear()
            self.joystick_pressed[2].clear()
            self.joystick_overlay[1].clear()
            self.joystick_overlay[2].clear()
            return
        if kind == "keyboard":
            target = self.keyboard_pressed
            overlay = self.keyboard_overlay
        else:
            target = self.joystick_pressed[event["port"]]
            overlay = self.joystick_overlay[event["port"]]
        inputs = set(event["inputs"])
        transition = event["transition"]
        if transition == "press":
            target.update(inputs)
        elif transition == "release":
            target.difference_update(inputs)
        else:
            overlay.update(inputs)


def post_ok(api: InputApiModel, events: list[dict[str, Any]]) -> dict[str, Any]:
    status, body = api.post({"events": events})
    assert status == 200
    assert body["errors"] == []
    return body


def post_bad(api: InputApiModel, payload: Any) -> dict[str, Any]:
    before = deepcopy(api._snapshot())
    status, body = api.post(payload)
    assert status == 400
    assert body["errors"]
    assert "keyboard" not in body
    assert "joysticks" not in body
    assert api._snapshot() == before
    return body


def test_get_success_response_shape_and_ordering() -> None:
    api = InputApiModel()
    body = post_ok(
        api,
        [
            {"kind": "joystick", "port": 2, "inputs": ["fire", "up"], "transition": "press"},
            {"kind": "keyboard", "inputs": ["a", "left_shift"], "transition": "press"},
        ],
    )

    assert body == {
        "errors": [],
        "keyboard": {"inputs": ["a", "left_shift"]},
        "joysticks": [{"port": 1, "inputs": []}, {"port": 2, "inputs": ["up", "fire"]}],
    }
    assert api.get() == (200, body)


def test_content_type_is_required_for_post_body() -> None:
    api = InputApiModel()
    valid_body = {"events": [{"kind": "release_all"}]}

    assert api.post_raw(valid_body, "application/json")[0] == 200
    for content_type in (None, "", "text/plain", "application/json; charset=utf-8"):
        status, body = api.post_raw(valid_body, content_type)
        assert status == 400
        assert body["errors"]


def test_missing_json_body_is_rejected_without_mutation() -> None:
    api = InputApiModel()
    post_ok(api, [{"kind": "keyboard", "inputs": ["ctrl"], "transition": "press"}])

    status, body = api.post_raw(None, "application/json")

    assert status == 412
    assert body["errors"]
    assert api.get()[1]["keyboard"]["inputs"] == ["ctrl"]


@pytest.mark.parametrize("payload", [None, [], "{}", 1])
def test_root_payload_must_be_object(payload: Any) -> None:
    post_bad(InputApiModel(), payload)


@pytest.mark.parametrize("payload", [{}, {"events": [], "extra": True}, {"extra": []}])
def test_root_must_contain_only_events(payload: Any) -> None:
    post_bad(InputApiModel(), payload)


def test_root_accepts_optional_pace_ms() -> None:
    api = InputApiModel()

    status, body = api.post({"events": [{"kind": "release_all"}], "pace_ms": 20})

    assert status == 200
    assert body["errors"] == []


@pytest.mark.parametrize("pace_ms", ["20", True, -1, 1001])
def test_pace_ms_must_be_integer_in_range_when_present(pace_ms: Any) -> None:
    post_bad(InputApiModel(), {"events": [{"kind": "release_all"}], "pace_ms": pace_ms})


@pytest.mark.parametrize("events", [None, {}, "event", 1])
def test_events_must_be_array(events: Any) -> None:
    post_bad(InputApiModel(), {"events": events})


def test_events_accepts_one_to_64_entries() -> None:
    api = InputApiModel()
    event = {"kind": "release_all"}

    assert api.post({"events": [event]})[0] == 200
    assert api.post({"events": [event] * 64})[0] == 200
    post_bad(api, {"events": []})
    post_bad(api, {"events": [event] * 65})


@pytest.mark.parametrize("event", [None, [], "event", 1])
def test_each_event_must_be_object(event: Any) -> None:
    post_bad(InputApiModel(), {"events": [event]})


@pytest.mark.parametrize("kind", [None, "", "mouse", 1, True])
def test_kind_enum_is_closed_and_string_typed(kind: Any) -> None:
    post_bad(InputApiModel(), {"events": [{"kind": kind}]})


@pytest.mark.parametrize("input_name", KEYBOARD_INPUTS)
def test_keyboard_input_enum_accepts_every_documented_value(input_name: str) -> None:
    transition = "tap" if input_name == RESTORE else "press"
    assert InputApiModel().post({"events": [{"kind": "keyboard", "inputs": [input_name], "transition": transition}]})[0] == 200


@pytest.mark.parametrize("transition", ["press", "release", "tap"])
def test_keyboard_transition_enum_accepts_all_values(transition: str) -> None:
    assert InputApiModel().post({"events": [{"kind": "keyboard", "inputs": ["a"], "transition": transition}]})[0] == 200


@pytest.mark.parametrize(
    "event",
    [
        {"kind": "keyboard", "transition": "tap"},
        {"kind": "keyboard", "inputs": None, "transition": "tap"},
        {"kind": "keyboard", "inputs": "a", "transition": "tap"},
        {"kind": "keyboard", "inputs": [], "transition": "tap"},
        {"kind": "keyboard", "inputs": ["a"] * 9, "transition": "tap"},
        {"kind": "keyboard", "inputs": ["a", "a"], "transition": "tap"},
        {"kind": "keyboard", "inputs": ["escape"], "transition": "tap"},
        {"kind": "keyboard", "inputs": [1], "transition": "tap"},
        {"kind": "keyboard", "inputs": ["a"], "transition": None},
        {"kind": "keyboard", "inputs": ["a"], "transition": "hold"},
        {"kind": "keyboard", "inputs": ["a"], "transition": 1},
        {"kind": "keyboard", "port": 1, "inputs": ["a"], "transition": "tap"},
        {"kind": "keyboard", "inputs": ["a"], "transition": "tap", "duration_ms": 20},
    ],
)
def test_keyboard_validation_rejects_invalid_shape_values_and_unknown_fields(event: dict[str, Any]) -> None:
    post_bad(InputApiModel(), {"events": [event]})


@pytest.mark.parametrize(
    "event",
    [
        {"kind": "keyboard", "inputs": [RESTORE], "transition": "press"},
        {"kind": "keyboard", "inputs": [RESTORE], "transition": "release"},
        {"kind": "keyboard", "inputs": [RESTORE, "run_stop"], "transition": "tap"},
        {"kind": "keyboard", "inputs": [RESTORE, "a"], "transition": "tap"},
    ],
)
def test_restore_is_tap_only_and_alone(event: dict[str, Any]) -> None:
    body = post_bad(InputApiModel(), {"events": [event]})
    assert "restore" in body["errors"][0]


@pytest.mark.parametrize("input_name", JOYSTICK_INPUTS)
def test_joystick_input_enum_accepts_every_documented_value(input_name: str) -> None:
    assert InputApiModel().post(
        {"events": [{"kind": "joystick", "port": 1, "inputs": [input_name], "transition": "press"}]}
    )[0] == 200


@pytest.mark.parametrize("transition", ["press", "release", "tap"])
def test_joystick_transition_enum_accepts_all_values(transition: str) -> None:
    assert InputApiModel().post(
        {"events": [{"kind": "joystick", "port": 1, "inputs": ["fire"], "transition": transition}]}
    )[0] == 200


@pytest.mark.parametrize(
    "inputs",
    [
        ["up", "right", "fire"],
        ["up", "down"],
        ["left", "right"],
        ["up", "down", "left", "right", "fire"],
    ],
)
def test_joystick_accepts_diagonals_and_unusual_combinations(inputs: list[str]) -> None:
    assert InputApiModel().post(
        {"events": [{"kind": "joystick", "port": 2, "inputs": inputs, "transition": "press"}]}
    )[0] == 200


@pytest.mark.parametrize(
    "event",
    [
        {"kind": "joystick", "inputs": ["up"], "transition": "tap"},
        {"kind": "joystick", "port": None, "inputs": ["up"], "transition": "tap"},
        {"kind": "joystick", "port": "1", "inputs": ["up"], "transition": "tap"},
        {"kind": "joystick", "port": True, "inputs": ["up"], "transition": "tap"},
        {"kind": "joystick", "port": 0, "inputs": ["up"], "transition": "tap"},
        {"kind": "joystick", "port": 3, "inputs": ["up"], "transition": "tap"},
        {"kind": "joystick", "port": 1, "transition": "tap"},
        {"kind": "joystick", "port": 1, "inputs": [], "transition": "tap"},
        {"kind": "joystick", "port": 1, "inputs": ["up"] * 6, "transition": "tap"},
        {"kind": "joystick", "port": 1, "inputs": ["jump"], "transition": "tap"},
        {"kind": "joystick", "port": 1, "inputs": ["up", "up"], "transition": "tap"},
        {"kind": "joystick", "port": 1, "inputs": [1], "transition": "tap"},
        {"kind": "joystick", "port": 1, "inputs": ["up"], "transition": "hold"},
        {"kind": "joystick", "port": 1, "inputs": ["up"], "transition": 1},
        {"kind": "joystick", "port": 1, "inputs": ["up"], "transition": "tap", "duration_ms": 20},
    ],
)
def test_joystick_validation_rejects_invalid_shape_values_and_unknown_fields(event: dict[str, Any]) -> None:
    post_bad(InputApiModel(), {"events": [event]})


def test_optional_port2_capability_gate_rejects_port2_before_mutation() -> None:
    api = InputApiModel(port2_supported=False)

    post_ok(api, [{"kind": "joystick", "port": 1, "inputs": ["fire"], "transition": "press"}])
    body = post_bad(api, {"events": [{"kind": "joystick", "port": 2, "inputs": ["up"], "transition": "press"}]})

    assert "port 2" in body["errors"][0]
    assert api.get()[1]["joysticks"] == [{"port": 1, "inputs": ["fire"]}, {"port": 2, "inputs": []}]


@pytest.mark.parametrize(
    "event",
    [
        {"kind": "release_all", "inputs": []},
        {"kind": "release_all", "port": 1},
        {"kind": "release_all", "transition": "tap"},
        {"kind": "release_all", "extra": True},
    ],
)
def test_release_all_rejects_extra_fields(event: dict[str, Any]) -> None:
    post_bad(InputApiModel(), {"events": [event]})


def test_capability_failure_returns_501_without_snapshot_fields() -> None:
    api = InputApiModel(supported=False)

    for method in (api.get, lambda: api.post({"events": [{"kind": "release_all"}]})):
        status, body = method()
        assert status == 501
        assert body == {"errors": [UNSUPPORTED_MESSAGE]}


def test_invalid_batch_is_atomic_even_when_error_is_late() -> None:
    api = InputApiModel()
    post_ok(
        api,
        [
            {"kind": "keyboard", "inputs": ["ctrl"], "transition": "press"},
            {"kind": "joystick", "port": 1, "inputs": ["fire"], "transition": "press"},
        ],
    )

    post_bad(
        api,
        {
            "events": [
                {"kind": "release_all"},
                {"kind": "keyboard", "inputs": ["left_shift"], "transition": "press"},
                {"kind": "joystick", "port": 3, "inputs": ["up"], "transition": "press"},
            ]
        },
    )

    assert api.get()[1] == {
        "errors": [],
        "keyboard": {"inputs": ["ctrl"]},
        "joysticks": [{"port": 1, "inputs": ["fire"]}, {"port": 2, "inputs": []}],
    }


def test_batch_events_are_applied_in_order_and_can_recover_after_release_all() -> None:
    api = InputApiModel()
    body = post_ok(
        api,
        [
            {"kind": "keyboard", "inputs": ["left_shift"], "transition": "press"},
            {"kind": "joystick", "port": 1, "inputs": ["up", "fire"], "transition": "press"},
            {"kind": "release_all"},
            {"kind": "keyboard", "inputs": ["commodore", "ctrl"], "transition": "press"},
            {"kind": "keyboard", "inputs": ["commodore"], "transition": "release"},
            {"kind": "joystick", "port": 2, "inputs": ["down"], "transition": "press"},
        ],
    )

    assert body["keyboard"]["inputs"] == ["ctrl"]
    assert body["joysticks"] == [{"port": 1, "inputs": []}, {"port": 2, "inputs": ["down"]}]


def test_press_release_and_release_all_are_idempotent() -> None:
    api = InputApiModel()

    body = post_ok(
        api,
        [
            {"kind": "keyboard", "inputs": ["a"], "transition": "press"},
            {"kind": "keyboard", "inputs": ["a"], "transition": "press"},
            {"kind": "keyboard", "inputs": ["space"], "transition": "release"},
            {"kind": "joystick", "port": 1, "inputs": ["fire"], "transition": "release"},
            {"kind": "joystick", "port": 1, "inputs": ["fire"], "transition": "press"},
            {"kind": "joystick", "port": 1, "inputs": ["fire"], "transition": "press"},
        ],
    )

    assert body["keyboard"]["inputs"] == ["a"]
    assert body["joysticks"][0]["inputs"] == ["fire"]
    assert post_ok(api, [{"kind": "release_all"}, {"kind": "release_all"}]) == {
        "errors": [],
        "keyboard": {"inputs": []},
        "joysticks": [{"port": 1, "inputs": []}, {"port": 2, "inputs": []}],
    }


def test_keyboard_tap_overlay_auto_releases_without_clearing_persistent_key() -> None:
    api = InputApiModel()

    body = post_ok(
        api,
        [
            {"kind": "keyboard", "inputs": ["left_shift"], "transition": "press"},
            {"kind": "keyboard", "inputs": ["a"], "transition": "tap"},
        ],
    )
    assert body["keyboard"]["inputs"] == ["a", "left_shift"]

    api.tick()
    assert api.get()[1]["keyboard"]["inputs"] == ["left_shift"]


def test_release_does_not_cancel_active_keyboard_or_joystick_tap_overlay() -> None:
    api = InputApiModel()

    body = post_ok(
        api,
        [
            {"kind": "keyboard", "inputs": ["a"], "transition": "tap"},
            {"kind": "keyboard", "inputs": ["a"], "transition": "release"},
            {"kind": "joystick", "port": 1, "inputs": ["fire"], "transition": "tap"},
            {"kind": "joystick", "port": 1, "inputs": ["fire"], "transition": "release"},
        ],
    )
    assert body["keyboard"]["inputs"] == ["a"]
    assert body["joysticks"][0]["inputs"] == ["fire"]

    api.tick()
    assert api.get()[1]["keyboard"]["inputs"] == []
    assert api.get()[1]["joysticks"][0]["inputs"] == []


def test_joystick_tap_overlay_auto_releases_without_clearing_persistent_inputs() -> None:
    api = InputApiModel()

    body = post_ok(
        api,
        [
            {"kind": "joystick", "port": 2, "inputs": ["up"], "transition": "press"},
            {"kind": "joystick", "port": 2, "inputs": ["fire"], "transition": "tap"},
        ],
    )
    assert body["joysticks"][1]["inputs"] == ["up", "fire"]

    api.tick()
    assert api.get()[1]["joysticks"][1]["inputs"] == ["up"]


def test_restore_tap_is_reported_temporarily_but_not_persisted() -> None:
    api = InputApiModel()

    assert post_ok(api, [{"kind": "keyboard", "inputs": [RESTORE], "transition": "tap"}])["keyboard"]["inputs"] == [
        RESTORE
    ]
    api.tick()
    assert api.get()[1]["keyboard"]["inputs"] == []
