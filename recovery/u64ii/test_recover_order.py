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


class FakeBitSequence:
    """What recover.py needs of pyftdi's BitSequence: built from (value, msb, length), read back with int()."""

    def __init__(self, value=0, msb=False, length=None, bytes_=None):
        self.value = value
        self.length = length

    def __int__(self):
        return self.value


def load_recover():
    stubs = {}
    for name in ("pyftdi", "pyftdi.jtag", "pyftdi.ftdi"):
        stubs[name] = types.ModuleType(name)
    stubs["pyftdi.jtag"].JtagEngine = object
    stubs["pyftdi.jtag"].JtagTool = object
    stubs["pyftdi.jtag"].BitSequence = FakeBitSequence  # also used in annotations, which older Pythons evaluate at once
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
        # round 1: both writes lost; a short pause; round 2: the write lands and the next one reads it back
        self.assertEqual(events, [("load_fpga",), ("outputs", 0x80), ("outputs", 0x80), ("sleep", 0.05),
                                  ("outputs", 0x80), ("outputs", 0x80)])

    def test_reset_that_never_lands_is_an_error(self):
        recover = load_recover()
        events = []
        with mock.patch.object(recover.time, "sleep", lambda s: events.append(("sleep", s))):
            with self.assertRaises(recover.JtagClientException):
                recover.prog_fpga(logging.getLogger("t"), FakeJtag(events, drop_writes=1000))
        self.assertEqual(len([e for e in events if e[0] == "outputs"]), 80)  # 40 rounds, as recovery.c polls


class FakeEngine:
    """Stands in for pyftdi's JtagEngine on the user chain: remembers which user register was selected
    (the 5 bits recover.py shifts in set_user_ir) and keeps the output register (user register 2). An update
    of it returns the value the register held before, as the TDO capture of the real chain does."""

    def __init__(self, drop_updates=0):
        self.selected = None
        self.register = 0
        self.drop = drop_updates

    def write_ir(self, seq):
        pass

    def write_dr(self, seq):
        self.selected = int(seq) >> 1

    def change_state(self, state):
        pass

    def shift_register(self, seq):
        return FakeBitSequence(0, length=seq.length)

    def shift_and_update_register(self, seq):
        before = FakeBitSequence(self.register, length=8)
        if self.selected == 2:
            if self.drop > 0:
                self.drop -= 1
            else:
                self.register = int(seq)
        return before

    def go_idle(self):
        pass


class RealUserSetOutputs(unittest.TestCase):
    """Runs the real JtagClient.user_set_outputs() (and prog_fpga() on top of it) against a model of the chain."""

    def client(self, drop_updates=0):
        recover = load_recover()
        c = object.__new__(recover.JtagClient)
        c.jtag = FakeEngine(drop_updates)
        c.xilinx_load_fpga = lambda name: None
        return recover, c

    def test_returns_the_register_value_from_before_the_write(self):
        _, c = self.client()
        self.assertEqual(c.user_set_outputs(0x80), 0x00)
        self.assertEqual(c.user_set_outputs(0x80), 0x80)  # the top bit must survive: a mask on the result fails here
        self.assertEqual(c.user_set_outputs(0x00), 0x80)
        self.assertEqual(c.jtag.register, 0x00)

    def test_prog_fpga_with_the_real_method_confirms_the_reset(self):
        recover, c = self.client(drop_updates=3)
        with mock.patch.object(recover.time, "sleep", lambda s: None):
            recover.prog_fpga(logging.getLogger("t"), c)
        self.assertEqual(c.jtag.register, 0x80)


if __name__ == "__main__":
    unittest.main()
