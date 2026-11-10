"""Host test for the order of operations in recover.py::prog_fpga (no hardware, no pyftdi needed).

Run: python3 -m unittest recovery/u64ii/test_recover_order.py   (from the repository root)
The CPU reset must be asserted right after the FPGA load, confirmed by reading the output register
back (a repeated write if it was lost), with no wait between the load and the upload.
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
    stubs["pyftdi.jtag"].BitSequence = object  # used in annotations, which older Pythons evaluate at once
    stubs["pyftdi.jtag"].__dict__["__all__"] = ["JtagEngine", "JtagTool", "BitSequence"]
    stubs["pyftdi.ftdi"].Ftdi = object
    stubs["pyftdi.ftdi"].__dict__["__all__"] = ["Ftdi"]
    with mock.patch.dict(sys.modules, stubs):
        spec = importlib.util.spec_from_file_location("recover_under_test", HERE / "recover.py")
        mod = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(mod)
    return mod


class FakeJtag:
    """The output register: a write returns the value the register held before it (the TDO capture).
    drop_writes: how many of the first writes are lost (the user chain is not answering yet)."""

    def __init__(self, events, drop_writes=0):
        self.events = events
        self.register = 0
        self.drop = drop_writes

    def xilinx_load_fpga(self, name):
        self.events.append(("load_fpga",))

    def user_set_outputs(self, value):
        self.events.append(("outputs", value))
        before = self.register
        if self.drop > 0:
            self.drop -= 1
        else:
            self.register = value
        return before


class ProgFpgaOrder(unittest.TestCase):
    def go(self, jtag):
        recover = load_recover()
        with mock.patch.object(recover.time, "sleep", lambda s: jtag.events.append(("sleep", s))):
            recover.prog_fpga(logging.getLogger("t"), jtag)
        return recover

    def test_reset_right_after_load_and_no_wait(self):
        events = []
        self.go(FakeJtag(events))
        # the second write only reads the first one back: no sleep anywhere
        self.assertEqual(events, [("load_fpga",), ("outputs", 0x80), ("outputs", 0x80)])

    def test_lost_reset_write_is_repeated_until_confirmed(self):
        events = []
        jtag = FakeJtag(events, drop_writes=2)
        self.go(jtag)
        self.assertEqual(jtag.register, 0x80)
        self.assertEqual(events[0], ("load_fpga",))
        self.assertEqual([e for e in events if e[0] == "sleep"], [])
        self.assertEqual(events[-1], ("outputs", 0x80))
        self.assertEqual(len(events), 1 + 4)  # two lost writes, one that lands, one that reads it back

    def test_reset_that_never_lands_is_an_error(self):
        recover = load_recover()
        with self.assertRaises(recover.JtagClientException):
            recover.prog_fpga(logging.getLogger("t"), FakeJtag([], drop_writes=1000))


if __name__ == "__main__":
    unittest.main()
