"""Host test for the order of operations in recover.py::prog_fpga (no hardware, no pyftdi needed).

Run: python3 -m unittest recovery/u64ii/test_recover_order.py   (from the repository root)
The CPU reset must be asserted right after the FPGA load and once more after the one second wait.
"""
import importlib.util
import logging
import pathlib
import sys
import types
import unittest
from unittest import mock

HERE = pathlib.Path(__file__).resolve().parent


def load_recover():
    stubs = {}
    for name in ("pyftdi", "pyftdi.jtag", "pyftdi.ftdi"):
        stubs[name] = types.ModuleType(name)
    stubs["pyftdi.jtag"].JtagEngine = object
    stubs["pyftdi.jtag"].JtagTool = object
    stubs["pyftdi.jtag"].__dict__["__all__"] = ["JtagEngine", "JtagTool"]
    stubs["pyftdi.ftdi"].Ftdi = object
    stubs["pyftdi.ftdi"].__dict__["__all__"] = ["Ftdi"]
    with mock.patch.dict(sys.modules, stubs):
        spec = importlib.util.spec_from_file_location("recover_under_test", HERE / "recover.py")
        mod = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(mod)
    return mod


class FakeJtag:
    def __init__(self, events):
        self.events = events

    def xilinx_load_fpga(self, name):
        self.events.append(("load_fpga",))

    def user_set_outputs(self, value):
        self.events.append(("outputs", value))


class ProgFpgaOrder(unittest.TestCase):
    def test_reset_after_load_and_again_after_the_wait(self):
        recover = load_recover()
        events = []
        with mock.patch.object(recover.time, "sleep", lambda s: events.append(("sleep", s))):
            recover.prog_fpga(logging.getLogger("t"), FakeJtag(events))
        self.assertEqual(events, [("load_fpga",), ("outputs", 0x80), ("sleep", 1), ("outputs", 0x80)])


if __name__ == "__main__":
    unittest.main()
