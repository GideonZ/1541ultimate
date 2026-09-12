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

# The host waits for a transaction without looking at it. Every REST memory read
# halts the C64, and one that lands in a serial transfer corrupts it: polling the
# mailbox every 20 ms through a 254 byte read spoiled one read in thirty on an
# Ultimate 64 Elite. Do not turn this wait into a poll loop.
#
# The wait is what the suites spend their time on, 84% of it, so the numbers below
# were calibrated rather than guessed. Every budget was scaled together and the whole
# suite run at each scale: it was clean down to three tenths of these values and
# began to overrun below that. These keep a little under twice that margin. The fixed
# part covers the KERNAL call and the REST round trip, and for everything but a write
# also the work the drive does for the command it was just given, which runs after
# the unlisten and delays the transfer that follows.
SECONDS_PER_BYTE = 0.0018
FIXED_SECONDS = {OPEN: 0.18, WRITE: 0.08, READ_TO_EOI: 0.22, CLOSE: 0.15, READ_COUNT: 0.18}

# What a read that ends at EOI is assumed to carry when the caller says nothing.
STATUS_BYTES = 64

# A device that is not the Software IEC drive is a drive emulation, and answers at the
# speed of the hardware it emulates. A 1541 seeks and reads its directory before it
# says anything, which is not a byte count. Calibrated the same way: 0.8 s left one
# transaction in a REL run short, 1.2 s left none.
EMULATED_DRIVE_SECONDS = 1.2

# What a transaction gets on top of its budget before the host gives up. Needing this
# is not a failure, but the look that found the transaction busy may have landed in
# the transfer, so each suite reports how often it happened.
GRACE_SECONDS = 2.0


def transfer_seconds(op, carried):
    """How long to leave the C64 alone for one mailbox operation."""
    return FIXED_SECONDS[op] + (carried * SECONDS_PER_BYTE)


class Agent:
    """Drives iec_agent.asm through its $c000 mailbox."""

    def __init__(self, api, softiec_device=11):
        self.api = api
        # Every other device number is served by a drive emulation. See
        # EMULATED_DRIVE_SECONDS.
        self.softiec_device = softiec_device
        # Transactions that needed more than the estimate. See GRACE_SECONDS.
        self.overruns = 0
        # The KERNAL status of the last transaction. Bit 6 is end of file.
        self.last_status = 0

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
        """One mailbox operation, and the bytes it read.

        `data` is what WRITE sends, `count` what READ_COUNT asks for, `secondary` the
        secondary address OPEN uses, and `expect` roughly how much READ_TO_EOI will
        bring back, which only sizes the wait.
        """
        requested = len(data) if count is None else count
        if requested > MAILBOX_CAPACITY:
            raise ValueError("IEC transaction exceeds mailbox capacity")
        if op == READ_COUNT and requested < 1:
            # The agent compares its counter after storing a byte, so a count of zero
            # would read until end of file instead of returning at once.
            raise ValueError("READ_COUNT needs at least one byte")
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
        self.last_status = status
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
        """Read a whole channel, in mailbox sized pieces, up to the end of the stream.

        A short piece ends it, and so does end of file on a full one, because asking
        for more after that is a read past the end and the KERNAL reports an error.
        """
        out = b""
        while True:
            block = self.call(READ_COUNT, channel, device=device, count=MAILBOX_CAPACITY)
            out += block
            if len(block) < MAILBOX_CAPACITY or (self.last_status & 64):
                return out

    def status(self, allowed=(0,)):
        response = self.call(READ_TO_EOI, 15, expect=STATUS_BYTES).decode("ascii").strip()
        if int(response.split(",", 1)[0]) not in allowed:
            raise Failure(f"DOS status: {response}")
        return response

    def command(self, command, allowed=(0,)):
        self.call(WRITE, 15, command if isinstance(command, bytes) else command.encode("ascii"))
        return self.status(allowed)

    def command_reply(self, command, size):
        """A command whose answer is data rather than a status line, such as G-P."""
        self.call(WRITE, 15, command)
        return self.call(READ_TO_EOI, 15, expect=size)
