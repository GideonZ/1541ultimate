#!/usr/bin/env python3
"""The Ultimate Command Interface transport, driven over REST DMA cycles.

The registers live at $DF1B-$DF1F on the cartridge bus and are reached through
machine:readmem / machine:writemem, so no 6502 code is involved.

  $DF1C  write: control (bit 0 PUSH_CMD, bit 1 DATA_ACC, bit 2 ABORT, bit 3 CLR_ERR)
         read:  status  (bit 0 CMD_BUSY, bit 1 DATA_ACC, bit 2 ABORT_P, bit 3 ERROR,
                         bits 4-5 state, bit 6 STAT_AV, bit 7 DATA_AV)
  $DF1D  write: command byte queue;  read: identification ($C9, or $49 on IRQ)
  $DF1E  read:  response data queue
  $DF1F  read:  status data queue

Command bytes go into $DF1D one request at a time on purpose, and only $DF1C is
polled. machine:readmem and machine:writemem walk an ascending span of
addresses, so a multi-byte access starting at $DF1D or $DF1E would spill onto
the next register. Measured on a U64 Elite: a length-8 read at $DF1E returned
one response byte, then one status byte from $DF1F, then the Ultimate audio
registers at $DF20 upwards.
"""

import time
from typing import Dict, List, Optional, Tuple

from report import Failure, detail

REG_IDENT = 0xDF1B
REG_CONTROL = 0xDF1C
REG_COMMAND = 0xDF1D
REG_RESPONSE = 0xDF1E
REG_STATUS = 0xDF1F

CTRL_PUSH_CMD = 0x01
CTRL_DATA_ACC = 0x02
CTRL_ABORT = 0x04
CTRL_CLR_ERR = 0x08

ST_CMD_BUSY = 0x01
ST_DATA_ACC = 0x02
ST_ABORT_P = 0x04
ST_ERROR = 0x08
ST_STATE_MASK = 0x30
ST_STATE_IDLE = 0x00
ST_STATE_BUSY = 0x10
ST_STATE_DATA_LAST = 0x20
ST_STATE_DATA_MORE = 0x30
ST_STAT_AV = 0x40
ST_DATA_AV = 0x80

STATE_NAMES = {ST_STATE_IDLE: "Idle", ST_STATE_BUSY: "Command Busy",
               ST_STATE_DATA_LAST: "Data Last", ST_STATE_DATA_MORE: "Data More"}
STATUS_FLAGS = ((ST_CMD_BUSY, "CMD_BUSY"), (ST_DATA_ACC, "DATA_ACC"),
                (ST_ABORT_P, "ABORT_P"), (ST_ERROR, "ERROR"),
                (ST_STAT_AV, "STAT_AV"), (ST_DATA_AV, "DATA_AV"))

# The dispatcher takes the target from the low nibble of the first command byte.
TARGET_NETWORK = 0x03

# The three FPGA queues, from "Ultimate Command Interface - Register API",
# section "Queues". They bound one command and one reply block.
CMD_QUEUE_BYTES = 896
REPLY_QUEUE_BYTES = 896
STATUS_QUEUE_BYTES = 256

BUSY_TIMEOUT_SECONDS = 15.0
BUSY_POLL_SECONDS = 0.05
ABORT_TIMEOUT_SECONDS = 5.0
RELEASE_TIMEOUT_SECONDS = 5.0
# A drain stops here rather than looping forever if DATA_AV never clears. Set
# above the queue size so an over-long reply is reported as a measurement
# instead of being silently cut to the expected length.
DRAIN_LIMIT_BYTES = 4096
# How many blocks of a Data More reply are collected before giving up.
MAX_REPLY_BLOCKS = 8


class Wedged(Failure):
    """The command interface never left Command Busy: issue #740's failure mode."""


def describe_status(status: int) -> str:
    state = STATE_NAMES[status & ST_STATE_MASK]
    flags = [name for bit, name in STATUS_FLAGS if status & bit]
    return f"${status:02X} {state}" + (f" [{', '.join(flags)}]" if flags else "")


class Reply:
    """One block of a reply: what a client can observe after it arrives."""

    def __init__(self, status_byte: int, data: bytes, status_text: bytes,
                 overrun: bytes = b"") -> None:
        self.status_byte = status_byte
        self.state = status_byte & ST_STATE_MASK
        self.data = data
        self.status_text = status_text
        # Bytes read from the response queue after DATA_AV went away, which is
        # what a client sees if it reads a fixed count instead of following the
        # flag.
        self.overrun = overrun

    @property
    def state_name(self) -> str:
        return STATE_NAMES[self.state]

    def __repr__(self) -> str:
        return (f"Reply({describe_status(self.status_byte)}, {len(self.data)} data bytes, "
                f"status {self.status_text!r})")


class Transaction:
    """Every observable of one command: its reply blocks and the final state."""

    def __init__(self, command: bytes, blocks: List[Reply], final_status: int,
                 elapsed: float) -> None:
        self.command = command
        self.blocks = blocks
        self.final_status = final_status
        self.elapsed = elapsed

    @property
    def first(self) -> Reply:
        return self.blocks[0]

    @property
    def data(self) -> bytes:
        """Every data byte of the reply, blocks concatenated in order."""
        return b"".join(block.data for block in self.blocks)

    @property
    def status_text(self) -> bytes:
        """The status text of the last block, which is the command's result."""
        return self.blocks[-1].status_text


class Uci:
    """The $DF1C-$DF1F transport, one REST DMA cycle per register access."""

    def __init__(self, machine, busy_timeout: float = BUSY_TIMEOUT_SECONDS) -> None:
        # `machine` is a tests/lib/api.py MachineApi: readmem / writemem.
        self.machine = machine
        self.busy_timeout = busy_timeout

    # -- register access ----------------------------------------------------

    def peek(self, address: int) -> int:
        return self.machine.readmem(address, 1)[0]

    def poke(self, address: int, value: int) -> None:
        self.machine.writemem(address, bytes([value]))

    def status(self) -> int:
        # Reading $DF1C has no side effect, unlike the response and status queues.
        return self.peek(REG_CONTROL)

    def control(self, bits: int) -> None:
        self.poke(REG_CONTROL, bits)

    def state(self) -> int:
        return self.status() & ST_STATE_MASK

    # -- state machine ------------------------------------------------------

    def release(self, timeout: float = RELEASE_TIMEOUT_SECONDS) -> bool:
        """Accept any pending data and clear a stale error, until the state is Idle.

        Accepting data from "Data More" returns to Command Busy while the target
        prepares the next block, so this has to keep trying rather than assume one
        write is enough.
        """
        deadline = time.time() + timeout
        while True:
            self.control(CTRL_DATA_ACC)
            self.control(CTRL_CLR_ERR)
            if self.state() == ST_STATE_IDLE:
                return True
            if time.time() > deadline:
                return False
            time.sleep(BUSY_POLL_SECONDS)

    def abort_to_idle(self) -> bool:
        """Ask the Ultimate to abandon the exchange, the documented way back to Idle."""
        self.control(CTRL_ABORT)
        if not self.wait_for_state(ST_STATE_IDLE, ABORT_TIMEOUT_SECONDS):
            return False
        self.control(CTRL_CLR_ERR)
        return True

    def wait_for_state(self, wanted: int, timeout: float) -> bool:
        deadline = time.time() + timeout
        while True:
            if self.state() == wanted:
                return True
            if time.time() > deadline:
                return False
            time.sleep(BUSY_POLL_SECONDS)

    def require_idle(self, when: str) -> None:
        status = self.status()
        if (status & ST_STATE_MASK) != ST_STATE_IDLE:
            raise Failure(f"command interface is not idle {when}: {describe_status(status)}")
        if status & ST_ERROR:
            raise Failure(f"command interface reports a command error {when}: {describe_status(status)}")

    def push(self, command: bytes) -> None:
        """Queue the command bytes and hand them to the Ultimate."""
        for byte in command:
            self.poke(REG_COMMAND, byte)
        self.control(CTRL_PUSH_CMD)

    def wait_for_reply(self, command: bytes) -> int:
        started = time.time()
        while True:
            status = self.status()
            if (status & ST_STATE_MASK) in (ST_STATE_DATA_LAST, ST_STATE_DATA_MORE):
                return status
            if time.time() - started > self.busy_timeout:
                raise Wedged(
                    f"command {command.hex(' ') or '<empty>'} never left Command Busy after "
                    f"{self.busy_timeout:.0f}s: {describe_status(status)}. The command "
                    f"interface is now wedged for every target (issue #740) and only a "
                    f"firmware restart or power cycle releases it."
                )
            time.sleep(BUSY_POLL_SECONDS)

    # -- queues -------------------------------------------------------------

    def _drain(self, available_bit: int, register: int, what: str) -> bytes:
        out = bytearray()
        while self.status() & available_bit:
            out.append(self.peek(register))
            if len(out) >= DRAIN_LIMIT_BYTES:
                raise Failure(f"{what} queue did not drain within {DRAIN_LIMIT_BYTES} bytes: "
                              f"{bytes(out)[:80]!r}")
        return bytes(out)

    def drain(self) -> Tuple[bytes, bytes]:
        return (self._drain(ST_DATA_AV, REG_RESPONSE, "response"),
                self._drain(ST_STAT_AV, REG_STATUS, "status"))

    # -- one command --------------------------------------------------------

    def transact(self, command: bytes, single_part: bool = True,
                 overrun_reads: int = 0) -> Transaction:
        """Push one command and collect everything the reply exposes.

        `single_part` asserts what most replies are: one block, announced as
        Data Last. A single-part reply announced as Data More makes a client
        that follows the protocol accept the data and then wait for a block
        that never comes. Pass False for a command whose reply may span blocks;
        this then follows the protocol through them.

        `overrun_reads` pulls that many further bytes from the response queue
        after DATA_AV has gone away, recording what a client that reads a fixed
        count regardless of the flag would see.
        """
        self.release()
        self.require_idle("before pushing a command")
        started = time.time()
        self.push(command)
        status = self.wait_for_reply(command)
        elapsed = time.time() - started

        if single_part and (status & ST_STATE_MASK) != ST_STATE_DATA_LAST:
            raise Failure(
                f"command {command.hex(' ')} replied in state {describe_status(status)}; "
                f"this reply is sent in one part, so the state has to be Data Last"
            )

        blocks: List[Reply] = []
        while True:
            data = self._drain(ST_DATA_AV, REG_RESPONSE, "response")
            overrun = bytes(self.peek(REG_RESPONSE) for _ in range(overrun_reads)) if not blocks else b""
            text = self._drain(ST_STAT_AV, REG_STATUS, "status")
            blocks.append(Reply(status, data, text, overrun))
            if (status & ST_STATE_MASK) != ST_STATE_DATA_MORE:
                break
            if len(blocks) >= MAX_REPLY_BLOCKS:
                raise Failure(f"reply to {command.hex(' ')} still said Data More after "
                              f"{MAX_REPLY_BLOCKS} blocks")
            self.control(CTRL_DATA_ACC)
            status = self.wait_for_reply(command)

        self.control(CTRL_DATA_ACC)
        final = self.status()
        self.require_idle("after accepting the reply")
        detail(f"{command.hex(' ') or '<empty>'} -> {elapsed:.2f}s, "
               f"{len(blocks)} block(s), data {blocks[0].data[:32]!r}"
               f"{'...' if len(blocks[0].data) > 32 else ''}, "
               f"status {blocks[-1].status_text!r}")
        return Transaction(command, blocks, final, elapsed)


def interface_present(uci: Uci) -> bool:
    """False when nothing answers at $DF1B-$DF1F.

    Measured on a C64 Ultimate 1.2.0: the "Command Interface" setting reads
    Enabled while the whole register window reads $FF, which is the bus
    floating because nothing answers there. An Ultimate 64 answers
    $02 $FF $02 $02 $02 at the same five addresses even with the setting off.
    """
    return not all(uci.peek(REG_IDENT + offset) == 0xFF for offset in range(5))
