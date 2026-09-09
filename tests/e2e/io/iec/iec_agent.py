"""The C64 side of the IEC suites: KERNAL transactions over the real serial bus.

The agent is a small 6502 program that performs OPEN, CHKOUT/CHROUT, CHKIN/CHRIN
and CLOSE through the KERNAL, so the bytes on the bus are the bytes a Commodore
puts there. REST only fills the agent's mailbox and reads its results back.

Every suite under this directory shares this module and iec_agent.asm.
"""
import sys
import time
from pathlib import Path

sys.path.insert(0, str(next(p for p in Path(__file__).resolve().parents
                          if (p / "tests" / "lib").is_dir()) / "tests" / "lib"))
import bootstrap  # noqa: E402,F401
from assembler import assemble  # noqa: E402
from report import Failure  # noqa: E402

OPEN, WRITE, READ_TO_EOI, CLOSE, READ_COUNT = 1, 2, 3, 4, 5

# The mailbox holds at most 254 bytes, and the agent counts them in one byte.
MAILBOX_CAPACITY = 254


class Agent:
    """Drives iec_agent.asm through its $c000 mailbox."""

    def __init__(self, api):
        self.api = api

    def start(self):
        self.api.machine.close_menu_from_anywhere()
        self.api.machine.writemem(0xc000, bytes(7))
        self.api.runners.upload("run_prg", assemble(Path(__file__).with_name("iec_agent.asm")))
        self.wait(0xc001, 0xa5)

    def wait(self, address, value):
        deadline = time.monotonic() + 15
        while time.monotonic() < deadline:
            if self.api.machine.readmem(address, 1) == bytes([value]):
                return
            time.sleep(.05)
        raise Failure(f"IEC agent timed out at ${address:04x}")

    def call(self, op, channel=5, data=b"", device=11, count=None, secondary=None):
        requested = len(data) if count is None else count
        if requested > MAILBOX_CAPACITY:
            raise ValueError("IEC transaction exceeds mailbox capacity")
        if data:
            self.api.machine.writemem(0xc100, data)
        # The KERNAL refuses logical file number zero, so a directory, which needs
        # secondary address zero, is opened on a different logical file number.
        self.api.machine.writemem(0xc002, bytes(
            [device, channel, requested, 0, 0, channel if secondary is None else secondary]))
        self.api.machine.writemem(0xc000, bytes([op]))
        # REST memory reads halt the C64; let the IEC transaction finish first.
        time.sleep(2)
        if self.api.machine.readmem(0xc000, 1) != b"\0":
            raise Failure("IEC transaction exceeded the two-second observation window")
        returned, status, error = self.api.machine.readmem(0xc004, 3)
        if error or status & ~64:
            raise Failure(f"KERNAL op={op} device={device} channel={channel}: ST={status}, error={error}")
        if op == READ_TO_EOI:
            if status != 64:
                raise Failure(f"IEC read ended without EOI: ST={status}")
            return self.api.machine.readmem(0xc100, returned)
        if op == READ_COUNT:
            # Fewer bytes than asked for is the end of the stream, and only that.
            if (returned < requested) and (status == 0):
                raise Failure(f"IEC read stopped after {returned} of {requested} bytes: ST={status}")
            return self.api.machine.readmem(0xc100, returned) if returned else b""
        return b""

    def read_exact(self, count, channel=5, device=11):
        """Read exactly count bytes, for a channel that signals EOI only at the end."""
        data = self.call(READ_COUNT, channel, device=device, count=count)
        if len(data) != count:
            raise Failure(f"IEC read returned {len(data)} bytes, expected {count}")
        return data

    def read_stream(self, channel=5, device=11):
        """Read a whole channel, in mailbox sized pieces, up to the end of the stream."""
        out = b""
        while True:
            block = self.call(READ_COUNT, channel, device=device, count=MAILBOX_CAPACITY)
            out += block
            if len(block) < MAILBOX_CAPACITY:
                return out

    def status(self, allowed=(0,)):
        response = self.call(READ_TO_EOI, 15).decode("ascii").strip()
        if int(response.split(",", 1)[0]) not in allowed:
            raise Failure(f"DOS status: {response}")
        return response

    def command(self, command, allowed=(0,)):
        self.call(WRITE, 15, command if isinstance(command, bytes) else command.encode("ascii"))
        self.status(allowed)
