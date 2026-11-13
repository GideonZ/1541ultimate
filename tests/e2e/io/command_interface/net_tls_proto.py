#!/usr/bin/env python3
"""The TLS commands of the UCI network target, as bytes, and two ways to send them.

doc/uci_tls.md is the contract. This module knows the byte layouts of
$21-$25 and of the existing commands a TLS handle uses ($07, $09, $10, $11),
parses the replies, and carries every command over a `Link` with the client
deadlines:

  - B = 45 s from the PUSH for OPEN_TLS / START_TLS, 12 s for every other TLS
    command, and 45 s (advice) for OPEN_TCP;
  - at PUSH + B with no reply, write ABORT ($04 alone) and wait for $DF1C bit 2
    to read 0, up to the later of PUSH + B and ABORT + 12 s;
  - bit 2 still 1 then: the interface is wedged, and the caller resets the C64.

So no command here can hang a run: the longest any one of them can take is
PUSH + 57 s, after which it has either answered, been ABORTed, or been
reported as a wedge.

Two links reach the registers, as in net_target_test.py:
  RestLink    machine:readmem / writemem DMA cycles, one per register access.
              It can write ABORT while a command is still in PROCESSING, so it
              is the route for the ABORT scenarios (S19e family).
  NativeLink  the 6502 agent (uci_agent.asm). The real program's timing, but
              the agent cannot ABORT a command in flight.
net_tls_mock.py offers a third, a simulated device, for the offline self-test.
"""

import time
from dataclasses import dataclass, field

from report import Failure, detail
from uci import (CTRL_ABORT, CTRL_CLR_ERR, CTRL_DATA_ACC, CTRL_PUSH_CMD, MAX_REPLY_BLOCKS,
                 REG_COMMAND, ST_ABORT_P, ST_ERROR, ST_STATE_BUSY, ST_STATE_DATA_LAST, ST_STATE_DATA_MORE,
                 ST_STATE_IDLE, ST_STATE_MASK, TARGET_NETWORK, Uci, describe_status)

# -- command codes (network_target.h) ----------------------------------------
NET_CMD_IDENTIFY = 0x01
NET_CMD_OPEN_TCP = 0x07
NET_CMD_OPEN_UDP = 0x08
NET_CMD_CLOSE_SOCKET = 0x09
NET_CMD_READ_SOCKET = 0x10
NET_CMD_WRITE_SOCKET = 0x11
NET_CMD_OPEN_TLS = 0x21
NET_CMD_TLS_TRUST_CA = 0x22
NET_CMD_TLS_INFO = 0x23
NET_CMD_START_TLS = 0x24
NET_CMD_TLS_RELEASE = 0x25
TLS_COMMANDS = (NET_CMD_OPEN_TLS, NET_CMD_TLS_TRUST_CA, NET_CMD_TLS_INFO,
                NET_CMD_START_TLS, NET_CMD_TLS_RELEASE)
# The no-reply bit of byte 0 (command_target.h:9).
NO_REPLY = 0x80

TRUST_BUNDLE = 0
TRUST_CUSTOM = 1
TRUST_PIN = 2
FLAG_REQUIRE_TLS13 = 0x01

INFO_CAPABILITY = 0xFF
INFO_CAPABILITY_BYTES = 16
INFO_HANDLE_BYTES = 48
OPEN_REPLY_BYTES = 8

# -- the client deadlines (and the advice for OPEN_TCP) ----------------------
BOUND_OPEN = 45.0
BOUND_OTHER = 12.0
BOUND_OPEN_TCP = 45.0
POST_ABORT_MIN = 12.0
# A non-TLS command (IDENTIFY, CLOSE of a plain socket) is immediate.
BOUND_PLAIN = 12.0

# -- what the firmware answers, verbatim where the text is fixed ------------
STATUS_OK = b"00,OK"
STATUS_UNKNOWN_COMMAND = b"21,UNKNOWN COMMAND"
STATUS_INVALID_PARAMS = b"81,INVALID PARAMS"
STATUS_OUT_OF_RANGE = b"82,PARAMETER(S) OUT OF RANGE"
STATUS_CLOSE_UNOWNED = b"12,ERROR ON CLOSE: 9"
STATUS_NO_DATA_IDLE = b"02,NO DATA: 11"
STATUS_NO_DATA_GONE = b"02,NO DATA: 9"
STATUS_PEER_CLOSED = b"01,CONNECTION CLOSED BY HOST"
STATUS_NO_NOTIFY = b"05,TLS CLOSED WITHOUT NOTIFY"
STATUS_HS_TIMEOUT = b"15,TLS HANDSHAKE TIMEOUT"
STATUS_TLS_IN_USE = b"91,TLS SESSIONS IN USE"
STATUS_NO_CUSTOM_CA = b"97,NO CUSTOM CA"
STATUS_PIN_MISMATCH = b"96,PINNED KEY MISMATCH"
# S5: the full text, because the code alone cannot tell an installation-order
# bug (which prints 0xFFFFFFFF) from the right answer.
STATUS_UNKNOWN_ROOT = b"93,CERTIFICATE NOT TRUSTED: 0x00000008"

READ_MAX = 1472
WRITE_MAX = 892
CA_MAX = 1472
CA_CHUNK = 889
HANDLE_RANGE = range(16)   # this firmware's handles

# The IANA numbers the OPEN reply and INFO carry.
TLS13 = 0x0304
TLS12 = 0x0303
TLS13_SUITES = {0x1301, 0x1302, 0x1303}
TLS12_SUITES = {0xC02B, 0xC02C, 0xC02F, 0xC030, 0xCCA9, 0xCCA8}
GROUPS = {0x001D: "x25519", 0x0017: "secp256r1", 0x0018: "secp384r1", 0x0019: "secp521r1"}


def status_code(status: bytes) -> int | None:
    """The two-digit code a status line starts with, or None."""
    if len(status) >= 2 and status[:2].isdigit():
        return int(status[:2])
    return None


@dataclass
class Result:
    """Everything one command showed a client."""
    command: bytes
    data: bytes
    status: bytes
    elapsed: float
    # ABORTed at PUSH + B because no reply came in time. The reply, if
    # it came later, was dropped by the ABORT.
    aborted: bool = False

    @property
    def code(self) -> int | None:
        return status_code(self.status)

    @property
    def ok(self) -> bool:
        return self.status == STATUS_OK

    def describe(self) -> str:
        shown = self.data[:24].hex(" ") + (" ..." if len(self.data) > 24 else "")
        if self.aborted:
            return f"no reply within its bound; ABORTed after {self.elapsed:.1f}s"
        return f"[{len(self.data)} B: {shown}] {self.status.decode('latin-1')!r} in {self.elapsed:.2f}s"


class LinkLost(Failure):
    """The route can no longer carry commands: wedged, or the agent stopped.

    The caller resets the C64, which is the recovery (doc/uci_tls.md) for a
    wedge, and which also closes every TLS session the network target holds.
    """


@dataclass
class AbortedCommand:
    """What an ABORT written during a command's PROCESSING left behind."""
    command: bytes
    abort_after: float          # seconds from the PUSH to the ABORT
    busy_at_abort: bool         # the command was still in PROCESSING then
    bit2_cleared_after: float   # seconds from the PUSH until $DF1C bit 2 read 0
    final_status: int           # $DF1C once bit 2 was 0


def bound_for(command: bytes) -> float:
    """The client deadline B for this command."""
    code = command[1] if len(command) >= 2 else 0
    if code in (NET_CMD_OPEN_TLS, NET_CMD_START_TLS):
        return BOUND_OPEN
    if code in (NET_CMD_OPEN_TCP, NET_CMD_OPEN_UDP):
        return BOUND_OPEN_TCP
    if code in TLS_COMMANDS or code in (NET_CMD_READ_SOCKET, NET_CMD_WRITE_SOCKET,
                                        NET_CMD_CLOSE_SOCKET):
        return BOUND_OTHER
    return BOUND_PLAIN


class Link:
    """One way of handing commands to the network target."""

    name = "link"
    # Whether this link can write ABORT while a command is in PROCESSING.
    can_abort_in_flight = False

    def __init__(self) -> None:
        # Every command and its result, in order: the run's wire evidence.
        self.log: list[dict] = []

    def command(self, command: bytes, bound: float | None = None) -> Result:
        bound = bound_for(command) if bound is None else bound
        result = self._command(command, bound)
        self.log.append({"t": round(time.time(), 3), "link": self.name,
                         "command": command.hex(" "), "data": result.data.hex(" "),
                         "status": result.status.decode("latin-1"),
                         "elapsed": round(result.elapsed, 3), "aborted": result.aborted})
        detail(f"{command[:40].hex(' ')}{' ...' if len(command) > 40 else ''} -> {result.describe()}")
        return result

    def _command(self, command: bytes, bound: float) -> Result:
        raise NotImplementedError

    def abort_in_flight(self, command: bytes, abort_after: float,
                        bound: float | None = None) -> AbortedCommand:
        raise Failure(f"the {self.name} route cannot ABORT a command in flight")

    def startup_abort(self) -> float:
        """Startup: presence check, $0C, then wait for bit 2 up to 45 s. Seconds taken."""
        raise NotImplementedError


class RestLink(Link):
    """The registers over REST DMA, with the client deadlines and ABORT."""

    name = "rest"
    can_abort_in_flight = True
    poll = 0.05

    def __init__(self, uci: Uci) -> None:
        super().__init__()
        self.uci = uci

    def _wait_bit2(self, deadline: float) -> int:
        """Wait for $DF1C bit 2 (ABORT pending) to read 0; the status, or LinkLost."""
        while True:
            status = self.uci.status()
            if not status & ST_ABORT_P:
                return status
            if time.monotonic() > deadline:
                raise LinkLost(f"$DF1C bit 2 still set at the client deadline: "
                               f"{describe_status(status)}. The interface is wedged")
            time.sleep(self.poll)

    def _abort(self, pushed: float, bound: float) -> int:
        aborted = time.monotonic()
        self.uci.control(CTRL_ABORT)
        status = self._wait_bit2(max(pushed + bound, aborted + POST_ABORT_MIN))
        if status & ST_ERROR:
            self.uci.control(CTRL_CLR_ERR)
        return status

    def _push(self, command: bytes) -> float:
        if not self.uci.release():
            raise LinkLost(f"the interface did not return to Idle before {command.hex(' ')}: "
                           f"{describe_status(self.uci.status())}")
        self.uci.require_idle("before pushing a command")
        for byte in command:
            self.uci.poke(REG_COMMAND, byte)
        pushed = time.monotonic()
        self.uci.control(CTRL_PUSH_CMD)
        return pushed

    def _command(self, command: bytes, bound: float) -> Result:
        pushed = self._push(command)
        blocks: list[bytes] = []
        status_text = b""
        wait_from = pushed
        wait_bound = bound
        while True:
            # B bounds the first block; each later block gets 12 s from
            # the data-accept of the block before it.
            while True:
                status = self.uci.status()
                state = status & ST_STATE_MASK
                if state in (ST_STATE_DATA_LAST, ST_STATE_DATA_MORE):
                    break
                if time.monotonic() - wait_from > wait_bound:
                    self._abort(pushed, bound)
                    return Result(command, b"".join(blocks), b"",
                                  time.monotonic() - pushed, aborted=True)
                time.sleep(self.poll)
            data, text = self.uci.drain()
            blocks.append(data)
            status_text = text or status_text
            if state != ST_STATE_DATA_MORE:
                break
            if len(blocks) >= MAX_REPLY_BLOCKS:
                raise Failure(f"reply to {command.hex(' ')} still said Data More after "
                              f"{MAX_REPLY_BLOCKS} blocks")
            self.uci.control(CTRL_DATA_ACC)
            wait_from, wait_bound = time.monotonic(), BOUND_OTHER
        elapsed = time.monotonic() - pushed
        self.uci.control(CTRL_DATA_ACC)
        final = self.uci.status()
        if (final & ST_STATE_MASK) != ST_STATE_IDLE or final & ST_ERROR:
            raise LinkLost(f"the interface was {describe_status(final)} after the reply to "
                           f"{command.hex(' ')} was accepted, expected Idle")
        return Result(command, b"".join(blocks), status_text, elapsed)

    def abort_in_flight(self, command: bytes, abort_after: float,
                        bound: float | None = None) -> AbortedCommand:
        """PUSH, write ABORT ($04 alone) `abort_after` s later, wait out bit 2.

        This is the client of 1.1 "What a client does": after the ABORT it
        writes nothing to $DF1C/$DF1D until bit 2 reads 0, up to the later of
        PUSH + B and ABORT + 12 s. The reply, if any, is dropped by the ABORT.
        """
        bound = bound_for(command) if bound is None else bound
        pushed = self._push(command)
        time.sleep(abort_after)
        busy = (self.uci.status() & ST_STATE_MASK) == ST_STATE_BUSY
        self.uci.control(CTRL_ABORT)
        aborted = time.monotonic()
        final = self._wait_bit2(max(pushed + bound, aborted + POST_ABORT_MIN))
        cleared = time.monotonic() - pushed
        if final & ST_ERROR:
            self.uci.control(CTRL_CLR_ERR)
        if (final & ST_STATE_MASK) != ST_STATE_IDLE:
            # A reply that arrived after bit 2 cleared cannot be: the ABORT is
            # serviced right after the command returns. Accept anything left.
            self.uci.release()
        self.log.append({"t": round(time.time(), 3), "link": self.name,
                         "command": command.hex(" "), "abort_after": abort_after,
                         "busy_at_abort": busy, "bit2_cleared_after": round(cleared, 3),
                         "final": describe_status(final)})
        detail(f"{command[:40].hex(' ')} ABORTed {abort_after:.1f}s after its PUSH "
               f"({'in PROCESSING' if busy else 'already answered'}); bit 2 cleared "
               f"{cleared:.2f}s after the PUSH, then {describe_status(final)}")
        return AbortedCommand(command, abort_after, busy, cleared, final)

    def startup_abort(self) -> float:
        ident = self.uci.peek(REG_COMMAND)
        if ident not in (0xC9, 0x49):
            raise Failure(f"$DF1D reads ${ident:02X}, not $C9/$49: no command interface")
        started = time.monotonic()
        self.uci.control(CTRL_ABORT | CTRL_CLR_ERR)
        self._wait_bit2(started + BOUND_OPEN)
        self.uci.release()
        return time.monotonic() - started


class NativeLink(Link):
    """The registers from 6502 code on the C64 (uci_native.NativeUci)."""

    name = "native"
    # One wrap of the agent's 16-bit wait counter is about 1.7 s at 1 MHz (by
    # cycle count, not measured), so 40 wraps (about 67 s) outlast the host's
    # own deadline (57 s for an open), which therefore fires first.
    WAIT_WRAPS = 40

    def __init__(self, agent) -> None:
        super().__init__()
        self.agent = agent
        self.agent.wait_wraps = self.WAIT_WRAPS

    def _command(self, command: bytes, bound: float) -> Result:
        # The agent cannot ABORT a command in flight, so its wait is the
        # bound plus margin, and a command that outlives it loses the route.
        self.agent.busy_timeout = bound + POST_ABORT_MIN
        started = time.monotonic()
        try:
            transaction = self.agent.transact(command, single_part=False)
        except Failure as exc:
            raise LinkLost(f"the 6502 agent lost {command.hex(' ')}: {exc}") from exc
        return Result(command, transaction.data, transaction.status_text,
                      time.monotonic() - started)

    def startup_abort(self) -> float:
        # The agent's own transaction starts by handing back anything pending,
        # and the run_prg that started it reset the C64 a moment ago.
        return 0.0


# -- command builders --------------------------------------------------------

def open_tls_command(port: int, host: str | bytes, trust: int = TRUST_BUNDLE, flags: int = 0,
                     name: str | bytes | None = None, pin: bytes | None = None,
                     tail: bytes | None = None, no_reply: bool = False) -> bytes:
    """`03 21 port trust flags [pin] tail`, the tail being host [00 name]."""
    head = bytes([TARGET_NETWORK | (NO_REPLY if no_reply else 0), NET_CMD_OPEN_TLS,
                  port & 0xFF, (port >> 8) & 0xFF, trust & 0xFF, flags & 0xFF])
    if pin is not None:
        head += pin
    if tail is None:
        host_b = host.encode("ascii") if isinstance(host, str) else host
        tail = host_b
        if name is not None:
            tail += b"\x00" + (name.encode("ascii") if isinstance(name, str) else name)
    return head + tail


def open_tcp_command(host: str, port: int) -> bytes:
    return (bytes([TARGET_NETWORK, NET_CMD_OPEN_TCP, port & 0xFF, (port >> 8) & 0xFF])
            + host.encode("ascii") + b"\x00")


def read_command(handle: int, maxlen: int) -> bytes:
    return bytes([TARGET_NETWORK, NET_CMD_READ_SOCKET, handle, maxlen & 0xFF, (maxlen >> 8) & 0xFF])


def write_command(handle: int, data: bytes) -> bytes:
    return bytes([TARGET_NETWORK, NET_CMD_WRITE_SOCKET, handle]) + data


def close_command(handle: int) -> bytes:
    return bytes([TARGET_NETWORK, NET_CMD_CLOSE_SOCKET, handle])


def info_command(handle: int) -> bytes:
    return bytes([TARGET_NETWORK, NET_CMD_TLS_INFO, handle])


def release_command() -> bytes:
    return bytes([TARGET_NETWORK, NET_CMD_TLS_RELEASE])


def trust_ca_command(offset: int, total: int, chunk: bytes) -> bytes:
    return bytes([TARGET_NETWORK, NET_CMD_TLS_TRUST_CA, offset & 0xFF, (offset >> 8) & 0xFF,
                  total & 0xFF, (total >> 8) & 0xFF]) + chunk


def start_tls_command(handle: int, trust: int, flags: int = 0, name: str | None = None,
                      pin: bytes | None = None) -> bytes:
    command = bytes([TARGET_NETWORK, NET_CMD_START_TLS, handle, trust, flags])
    if pin is not None:
        command += pin
    if name is not None:
        command += name.encode("ascii")
    return command


# -- replies -----------------------------------------------------------------

@dataclass
class Capability:
    """TLS_INFO $FF: the capability record."""
    raw: bytes

    @property
    def version(self) -> int:
        return self.raw[0]

    @property
    def max_sessions(self) -> int:
        return self.raw[1]

    @property
    def entries(self) -> int:
        return self.raw[2]

    @property
    def trust_modes(self) -> int:
        return self.raw[3]

    @property
    def ready(self) -> int:
        return self.raw[4]

    @property
    def versions(self) -> int:
        return self.raw[5]

    @property
    def module_version(self) -> tuple[int, int]:
        return self.raw[6], int.from_bytes(self.raw[7:9], "little")

    @property
    def store_date(self) -> bytes:
        return self.raw[9:12]

    @property
    def block_max_ms(self) -> int:
        return int.from_bytes(self.raw[12:14], "little")

    @property
    def unclaimed(self) -> int:
        return self.raw[14]

    def as_dict(self) -> dict:
        major, minor = self.module_version
        return {"raw": self.raw.hex(" "), "interface": self.version,
                "max_sessions": self.max_sessions, "entries": self.entries,
                "trust_modes": self.trust_modes, "ready": self.ready,
                "ready_bits": {"module": bool(self.ready & 1), "entropy": bool(self.ready & 2),
                               "time": bool(self.ready & 4), "custom_ca": bool(self.ready & 8)},
                "versions": self.versions, "module_version": f"{major}.{minor}",
                "store_date": self.store_date.hex(" "), "block_max_ms": self.block_max_ms,
                "unclaimed": self.unclaimed}


@dataclass
class OpenReply:
    """The 8 bytes of a successful OPEN_TLS / START_TLS."""
    raw: bytes

    @property
    def handle(self) -> int:
        return self.raw[0]

    @property
    def version(self) -> int:
        return int.from_bytes(self.raw[1:3], "little")

    @property
    def suite(self) -> int:
        return int.from_bytes(self.raw[3:5], "little")

    @property
    def group(self) -> int:
        return int.from_bytes(self.raw[5:7], "little")

    @property
    def bits(self) -> int:
        return self.raw[7]

    def as_dict(self) -> dict:
        return {"handle": self.handle, "version": f"0x{self.version:04X}",
                "suite": f"0x{self.suite:04X}", "group": f"0x{self.group:04X}",
                "bits": self.bits}


@dataclass
class HandleInfo:
    """TLS_INFO for a handle: 48 bytes."""
    raw: bytes

    @property
    def version(self) -> int:
        return int.from_bytes(self.raw[0:2], "little")

    @property
    def suite(self) -> int:
        return int.from_bytes(self.raw[2:4], "little")

    @property
    def group(self) -> int:
        return int.from_bytes(self.raw[4:6], "little")

    @property
    def trust(self) -> int:
        return self.raw[6]

    @property
    def bits(self) -> int:
        return self.raw[7]

    @property
    def spki_sha256(self) -> bytes:
        return self.raw[8:40]

    @property
    def handshake_ms(self) -> int:
        return int.from_bytes(self.raw[40:44], "little")

    def as_dict(self) -> dict:
        return {"version": f"0x{self.version:04X}", "suite": f"0x{self.suite:04X}",
                "group": f"0x{self.group:04X}", "trust": self.trust, "bits": self.bits,
                "spki_sha256": self.spki_sha256.hex(), "handshake_ms": self.handshake_ms}


@dataclass
class ReadReply:
    """One READ_SOCKET: the 2-byte header, the payload, the status."""
    result: Result

    @property
    def header(self) -> int | None:
        data = self.result.data
        return int.from_bytes(data[:2], "little") if len(data) >= 2 else None

    @property
    def payload(self) -> bytes:
        return self.result.data[2:]

    @property
    def status(self) -> bytes:
        return self.result.status

    @property
    def gone(self) -> bool:
        """01/05: the handle is closed and untracked; never CLOSE it."""
        return self.header == 0 and status_code(self.status) in (1, 5)


@dataclass
class TlsSession:
    """What the suite knows about one handle it holds."""
    handle: int
    reply: OpenReply
    gone: bool = False
    notes: list[str] = field(default_factory=list)
