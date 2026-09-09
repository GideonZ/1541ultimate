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

# The device halts the C64 for every REST memory read, and a read that lands in the
# middle of a serial transfer corrupts it. Polling the mailbox every 20 ms during a
# 254 byte read spoiled one read in thirty on an Ultimate 64 Elite, so the host
# waits without looking and only then reads the result.
#
# The serial bus carries about 600 bytes a second, measured on the same machine: a
# 254 byte read took 0.42 s and a two byte command took under 0.03 s. The rate below
# allows about a third more time than that. The fixed part covers the KERNAL call and
# the REST round trip, and for everything but a write it also covers the work the
# drive does for the command it was just given, because a command runs after the
# unlisten that ends the write and delays the transfer that follows.
SECONDS_PER_BYTE = 0.0035
FIXED_SECONDS = {OPEN: 0.35, WRITE: 0.15, READ_TO_EOI: 0.45, CLOSE: 0.30, READ_COUNT: 0.35}

# A read that ends at EOI does not say in advance how much will arrive, so it is
# budgeted for a full mailbox unless the caller says what it expects. A status line
# is the common short case.
STATUS_BYTES = 64

# Anything that is not the Software IEC drive is a drive emulation, which answers at
# the speed of the hardware it emulates: a 1541 seeks and reads its directory before
# it says anything. That is not a byte count, so it is a flat allowance.
EMULATED_DRIVE_SECONDS = 1.8

# What a transaction gets on top of that before the host gives up on it. Reaching
# this is not a failure, but it means the look that found the transaction busy may
# have landed in the transfer, so a suite reports how often it happened.
GRACE_SECONDS = 2.0


def transfer_seconds(op, requested):
    """How long to leave the C64 alone for one mailbox operation."""
    return FIXED_SECONDS.get(op, 0.30) + (requested * SECONDS_PER_BYTE)


class Agent:
    """Drives iec_agent.asm through its $c000 mailbox."""

    def __init__(self, api, softiec_device=11):
        self.api = api
        # Every other device number is served by a drive emulation. See
        # EMULATED_DRIVE_SECONDS.
        self.softiec_device = softiec_device
        # Transactions that needed more than the estimate. See GRACE_SECONDS.
        self.overruns = 0

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

    def call(self, op, channel=5, data=b"", device=11, count=None, secondary=None, expect=None):
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
        carried = requested
        if op == READ_TO_EOI:
            carried = MAILBOX_CAPACITY if expect is None else expect
        budget = transfer_seconds(op, carried)
        if device != self.softiec_device:
            budget += EMULATED_DRIVE_SECONDS
        time.sleep(budget)
        # One read for the whole mailbox: the byte the agent clears when it is done,
        # and, behind it, the count, the KERNAL status and the error it reported.
        state = self.api.machine.readmem(0xc000, 7)
        if state[0] != 0:
            # The estimate was short. Waiting again costs nothing in the normal case
            # and keeps a slow transfer from being reported as a hang. The look that
            # found it busy may have spoiled it, which the check that asked for the
            # data will report as a mismatch.
            self.overruns += 1
            time.sleep(GRACE_SECONDS)
            state = self.api.machine.readmem(0xc000, 7)
            if state[0] != 0:
                raise Failure(f"IEC operation {op} on channel {channel} did not finish "
                              f"within {budget + GRACE_SECONDS:.2f}s")
        returned, status, error = state[4], state[5], state[6]
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
        response = self.call(READ_TO_EOI, 15, expect=STATUS_BYTES).decode("ascii").strip()
        if int(response.split(",", 1)[0]) not in allowed:
            raise Failure(f"DOS status: {response}")
        return response

    def command(self, command, allowed=(0,)):
        self.call(WRITE, 15, command if isinstance(command, bytes) else command.encode("ascii"))
        self.status(allowed)
