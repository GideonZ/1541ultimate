#!/usr/bin/env python3
"""Drive the Ultimate Command Interface from 6502 code running on the C64.

`uci.Uci` reaches $DF1C-$DF1F through machine:readmem / machine:writemem, which
are DMA cycles the Ultimate issues on the cartridge bus. This module reaches the
same registers the way a real program does: ordinary loads and stores executed
by the 6510, at bus speed, with no DMA involved.

The 6502 side is `tests/e2e/io/command_interface/uci_agent.asm`, assembled here
with the repository's own 64tass and started over runners:run_prg. The host
writes a command block into C64 memory, sets one byte to start it, polls one
byte until it finishes, and reads the whole result back in one bulk readmem.
That is a handful of REST calls per command however large the reply, where the
DMA route costs one call per byte of it.

`NativeUci` exposes the same `transact` and `probe_drain` as `uci.Uci`, so a
test can run the same scenario down either route.
"""

import os
import time

from assembler import assemble
from report import Failure, detail
from uci import (BUSY_TIMEOUT_SECONDS, CMD_QUEUE_BYTES, Reply, ST_ERROR,
                 ST_STATE_DATA_LAST, ST_STATE_IDLE, ST_STATE_MASK, Transaction,
                 describe_status)

# These mirror uci_agent.asm. They are the interface between the two halves and
# have to be changed together.
GO = 0xC000
SEQ = 0xC001
CMDLEN = 0xC002
OPT_OVR = 0xC004
OPT_MAXB = 0xC005
OPT_CAP = 0xC006
READY = 0xC008
OPT_ABRT = 0xC009
CMDBUF = 0xC010

RESULT_BASE = 0xC400
RESULT_BYTES = 0x200            # covers the result block and the status text
R_FIRST = 0xC400
R_BLOCKS = 0xC401
R_DLEN = 0xC402
R_SLEN = 0xC404
R_FINAL = 0xC405
R_FLAGS = 0xC406
R_OVRN = 0xC410
R_BSTAT = 0xC420
R_BLEN = 0xC430
STATBUF = 0xC500
DATABUF = 0x2000

FLAG_WAIT_TIMEOUT = 0x01
FLAG_DRAIN_CAP = 0x02
FLAG_STATUS_CAP = 0x04

AGENT_READY = 0xA5
# What the agent is allowed to pull from the response queue when the caller has
# no opinion. Above the 896-byte queue, so an over-long reply shows up as a
# measurement rather than being cut to the expected size.
DEFAULT_DRAIN_CAP = 4096
# How many reply blocks and overrun bytes the result block has room for.
MAX_BLOCKS = 8
MAX_OVERRUN_READS = 16

AGENT_SOURCE = os.path.join(
    os.path.dirname(os.path.abspath(__file__)),
    "..", "io", "command_interface", "uci_agent.asm")

START_TIMEOUT_SECONDS = 20.0
POLL_SECONDS = 0.02


class NativeUci:
    """The command interface as the C64 sees it, driven by the resident agent."""

    def __init__(self, machine, runners,
                 busy_timeout: float = BUSY_TIMEOUT_SECONDS) -> None:
        # `machine` and `runners` are tests/lib/api.py MachineApi and RunnersApi
        # for the machine whose bus the registers are on.
        self.machine = machine
        self.runners = runners
        self.busy_timeout = busy_timeout
        self._sequence = 0

    # -- lifecycle ----------------------------------------------------------

    def start(self) -> None:
        """Assemble the agent, run it, and wait for it to say it is alive."""
        prg = assemble(AGENT_SOURCE)
        detail(f"assembled uci_agent.asm: {len(prg)} bytes, load address "
               f"${int.from_bytes(prg[:2], 'little'):04X}")
        status, _, body = self.runners.upload("run_prg", prg)
        if status != 200:
            raise Failure(f"runners:run_prg returned HTTP {status}: {body[:160]!r}")
        deadline = time.monotonic() + START_TIMEOUT_SECONDS
        while True:
            if self.machine.readmem(READY, 1)[0] == AGENT_READY:
                break
            if time.monotonic() > deadline:
                raise Failure(
                    f"the 6502 agent did not report ready within "
                    f"{START_TIMEOUT_SECONDS:.0f}s; ${READY:04X} never became "
                    f"${AGENT_READY:02X}")
            time.sleep(POLL_SECONDS)
        self._sequence = self.machine.readmem(SEQ, 1)[0]
        detail("the 6502 agent is resident and idle")

    # -- one command --------------------------------------------------------

    def _run(self, command: bytes, overrun_reads: int, cap: int,
             max_blocks: int = MAX_BLOCKS, abort_first: bool = False) -> tuple[bytes, bytes]:
        """Hand one command to the agent and return (result block, payload)."""
        if len(command) > CMD_QUEUE_BYTES:
            raise Failure(f"a command of {len(command)} bytes does not fit the command queue")
        self.machine.writemem(CMDBUF, command)
        self.machine.writemem(CMDLEN, bytes([
            len(command) & 0xFF, (len(command) >> 8) & 0xFF,
            min(overrun_reads, MAX_OVERRUN_READS), max_blocks,
            cap & 0xFF, (cap >> 8) & 0xFF]))
        # Outside the option block above, so it is written every time rather
        # than left over from the previous transaction.
        self.machine.writemem(OPT_ABRT, bytes([0x01 if abort_first else 0x00]))
        self.machine.writemem(GO, bytes([0x01]))

        deadline = time.monotonic() + self.busy_timeout
        while True:
            sequence = self.machine.readmem(SEQ, 1)[0]
            if sequence != self._sequence:
                self._sequence = sequence
                break
            if time.monotonic() > deadline:
                raise Failure(
                    f"the 6502 agent did not finish {command.hex(' ')} within "
                    f"{self.busy_timeout:.0f}s. It is either wedged in the command "
                    f"interface or no longer running; ${READY:04X} reads "
                    f"${self.machine.readmem(READY, 1)[0]:02X}")
            time.sleep(POLL_SECONDS)

        block = self.machine.readmem(RESULT_BASE, RESULT_BYTES)
        length = int.from_bytes(self._at(block, R_DLEN, 2), "little")
        payload = self.machine.readmem(DATABUF, length) if length else b""
        return block, payload

    @staticmethod
    def _at(block: bytes, address: int, count: int = 1) -> bytes:
        """The `count` bytes the agent wrote at `address`."""
        start = address - RESULT_BASE
        return block[start:start + count]

    def _byte(self, block: bytes, address: int) -> int:
        return self._at(block, address)[0]

    def transact(self, command: bytes, single_part: bool = True,
                 overrun_reads: int = 0) -> Transaction:
        """Push one command and collect everything the reply exposes.

        The same contract as uci.Uci.transact. The agent already followed a
        Data More reply through its blocks, so this only unpacks what it
        recorded.
        """
        started = time.monotonic()
        block, payload = self._run(command, overrun_reads, DEFAULT_DRAIN_CAP)
        elapsed = time.monotonic() - started
        flags = self._byte(block, R_FLAGS)
        if flags & FLAG_WAIT_TIMEOUT:
            raise Failure(f"command {command.hex(' ') or '<empty>'} never left Command Busy: "
                          f"the agent gave up waiting, CTRL was "
                          f"{describe_status(self._byte(block, R_FIRST))}")
        if flags & FLAG_DRAIN_CAP:
            raise Failure(
                f"the response queue did not stop after {len(payload)} bytes; DATA_AV was "
                f"still set when the agent reached its cap. Use probe_drain to measure that")
        if flags & FLAG_STATUS_CAP:
            raise Failure(f"the status text of {command.hex(' ')} was longer than the "
                          f"agent's buffer, so what it reported is truncated")

        # The agent counts every block it saw but only has room to describe
        # MAX_BLOCKS of them, so reading further would be reading the reserved
        # area rather than a block.
        count = min(self._byte(block, R_BLOCKS), MAX_BLOCKS)
        if count == 0:
            raise Failure(f"the agent recorded no reply block for {command.hex(' ')}")
        first_status = self._byte(block, R_FIRST)
        if single_part and (first_status & ST_STATE_MASK) != ST_STATE_DATA_LAST:
            raise Failure(
                f"command {command.hex(' ')} replied in state {describe_status(first_status)}; "
                f"this reply is sent in one part, so the state has to be Data Last")

        text = self._at(block, STATBUF, self._byte(block, R_SLEN))
        overrun = self._at(block, R_OVRN, min(overrun_reads, MAX_OVERRUN_READS))

        blocks: list[Reply] = []
        previous = 0
        for index in range(count):
            cumulative = int.from_bytes(self._at(block, R_BLEN + index * 2, 2), "little")
            status_byte = self._byte(block, R_BSTAT + index)
            # The agent appends every block's status text to one buffer, so the
            # whole text belongs to the transaction rather than to a block. It
            # is reported on the last one, which is where uci.Transaction reads
            # a command's result from.
            last = index == count - 1
            blocks.append(Reply(status_byte, payload[previous:cumulative],
                                text if last else b"",
                                overrun if index == 0 else b""))
            previous = cumulative

        # The same assertion uci.Uci.transact makes with require_idle: a reply
        # that has been accepted must leave the interface idle and unflagged.
        final = self._byte(block, R_FINAL)
        if (final & ST_STATE_MASK) != ST_STATE_IDLE or (final & ST_ERROR):
            raise Failure(f"the interface was {describe_status(final)} after the reply to "
                          f"{command.hex(' ')} was accepted, expected Idle")
        detail(f"{command.hex(' ') or '<empty>'} -> {elapsed:.2f}s, {count} block(s), "
               f"data {blocks[0].data[:32]!r}{'...' if len(blocks[0].data) > 32 else ''}, "
               f"status {blocks[-1].status_text!r}")
        return Transaction(command, blocks, final, elapsed)

    # -- raw drain ----------------------------------------------------------

    def probe_drain(self, command: bytes, cap: int) -> tuple[int, bool, bytes]:
        """Push one command and pull bytes until DATA_AV clears or `cap` is hit.

        Returns how many bytes the queue handed out, whether DATA_AV cleared,
        and the last few of them, which say whether the queue was repeating
        itself. This is the same measurement uci.Uci.probe_drain makes, taken
        by the 6510 instead of by DMA, and like that one it measures the first
        reply block: the agent is told to follow no further one, so a reply
        announced as Data More is counted here as the block it started with.
        """
        block, payload = self._run(command, 0, cap, max_blocks=1)
        flags = self._byte(block, R_FLAGS)
        if flags & FLAG_WAIT_TIMEOUT:
            raise Failure(f"command {command.hex(' ')} never left Command Busy")
        cleared = not (flags & FLAG_DRAIN_CAP)
        return len(payload), cleared, payload[-6:]

    # -- abort --------------------------------------------------------------

    def probe_abort(self, command: bytes) -> tuple[int, bool]:
        """Push one command, take its first reply block, then abandon it.

        The same measurement uci.Uci.probe_abort makes: how many bytes that
        first block carried, and whether the interface came back to Idle.
        """
        block, payload = self._run(command, 0, DEFAULT_DRAIN_CAP,
                                   max_blocks=1, abort_first=True)
        flags = self._byte(block, R_FLAGS)
        if flags & FLAG_WAIT_TIMEOUT:
            raise Failure(f"command {command.hex(' ')} never left Command Busy, or the "
                          f"interface never returned to Idle after the abort; CTRL was "
                          f"{describe_status(self._byte(block, R_FINAL))}")
        final = self._byte(block, R_FINAL)
        idle = (final & ST_STATE_MASK) == ST_STATE_IDLE and not (final & ST_ERROR)
        return len(payload), idle
