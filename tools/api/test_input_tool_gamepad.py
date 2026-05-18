from __future__ import annotations

from pathlib import Path
import sys
from unittest.mock import patch


TOOLS_API = Path(__file__).resolve().parent
if str(TOOLS_API) not in sys.path:
    sys.path.insert(0, str(TOOLS_API))

import input_tool  # noqa: E402


def test_gamepad_state_combines_left_right_sticks_and_dpad_into_one_joystick() -> None:
    state = input_tool.GamepadState(port=2, axis_threshold=10000)

    press_left = state.apply_input_event(input_tool.EV_ABS, input_tool.ABS_X, -20000, now=0.0)
    press_right = state.apply_input_event(input_tool.EV_ABS, input_tool.ABS_RX, 20000, now=0.0)
    press_dpad = state.apply_input_event(input_tool.EV_ABS, input_tool.ABS_HAT0Y, -1, now=0.0)
    release_left = state.apply_input_event(input_tool.EV_ABS, input_tool.ABS_X, 0, now=0.0)

    assert press_left == [
        {"kind": "joystick", "port": 2, "inputs": ["left"], "transition": "press"}
    ]
    assert press_right == [
        {"kind": "joystick", "port": 2, "inputs": ["right"], "transition": "press"}
    ]
    assert press_dpad == [
        {"kind": "joystick", "port": 2, "inputs": ["up"], "transition": "press"}
    ]
    assert release_left == [
        {"kind": "joystick", "port": 2, "inputs": ["left"], "transition": "release"}
    ]


def test_gamepad_a_maps_to_fire_and_b_repeats_fire() -> None:
    state = input_tool.GamepadState(port=1, axis_threshold=10000, fire_repeat_hz=10.0)

    fire_press = state.apply_input_event(input_tool.EV_KEY, input_tool.BTN_SOUTH, 1, now=0.0)
    fire_release = state.apply_input_event(input_tool.EV_KEY, input_tool.BTN_SOUTH, 0, now=0.0)
    b_initial = state.apply_input_event(input_tool.EV_KEY, input_tool.BTN_EAST, 1, now=1.0)
    b_repeat = state.poll_repeat_fire(now=1.11)
    b_release = state.apply_input_event(input_tool.EV_KEY, input_tool.BTN_EAST, 0, now=1.11)
    b_repeat_after_release = state.poll_repeat_fire(now=1.25)

    assert fire_press == [
        {"kind": "joystick", "port": 1, "inputs": ["fire"], "transition": "press"}
    ]
    assert fire_release == [
        {"kind": "joystick", "port": 1, "inputs": ["fire"], "transition": "release"}
    ]
    assert b_initial == [{"kind": "joystick", "port": 1, "inputs": ["fire"], "transition": "tap"}]
    assert b_repeat == {"kind": "joystick", "port": 1, "inputs": ["fire"], "transition": "tap"}
    assert b_release == []
    assert b_repeat_after_release is None


def test_find_default_gamepad_device_prefers_real_controller_over_system_control() -> None:
    candidates = [
        "/dev/input/by-id/usb-Keychron_Keychron_C3_Pro-if02-event-joystick",
        "/dev/input/by-id/usb-Microsoft_Controller-event-joystick",
    ]
    names = {
        "/dev/input/by-id/usb-Keychron_Keychron_C3_Pro-if02-event-joystick": "Keychron Keychron C3 Pro System Control",
        "/dev/input/by-id/usb-Microsoft_Controller-event-joystick": "Microsoft X-Box 360 pad",
    }

    with patch.object(input_tool.glob, "glob", side_effect=[candidates]), patch.object(
        input_tool, "input_event_name", side_effect=lambda path: names[path]
    ):
        assert input_tool.find_default_gamepad_device() == candidates[1]
