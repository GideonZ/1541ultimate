#!/usr/bin/env python3
# E2E: Verifies UCI network target socket reads and writes: length, delivery, framing.

"""End-to-end check of the UCI network target's socket read and write commands.

Regression guard for GideonZ/1541ultimate#802, reported as "a maxlen above 512
silently returns no data". Everything here was measured on real hardware
against the $DF1B-$DF1F registers; nothing is taken from the issue text or from
the manuals, which are used only as the statement of the intended contract.

The write side is GideonZ/1541ultimate#807, "a payload larger than one command
leaves as several datagrams". WRITE_SOCKET sends whatever one command carries,
and a command carries at most 892 payload bytes, so a 1000-byte message is two
commands and two datagrams: a peer that expects one message receives two, in
whatever order UDP hands them over and with either one free to go missing on
its own. WRITE_SOCKET_CHUNK carries the payload over as many chunks as it
takes, each naming the offset it starts at and the total it belongs to, and the
firmware sends once, when the accumulation is complete. Those cases watch the
wire rather than the command interface, because the reply to a write is a byte
count, and a count of 893 is the same whether the bytes left as one datagram
or as two.

This host process is the network peer. It binds a UDP socket and a TCP
listener, has the device open a socket back to it, learns the device's
ephemeral source port from the device's own WRITE_SOCKET, and then sends
payloads of a chosen size. That means the suite needs the device to be able to
reach this machine on two ports it binds; where it cannot, the scenarios that
need a peer skip rather than fail.

Payloads are pattern(): byte i is (i mod 251). 251 is prime and below 256, so
any run of received bytes identifies the offset it came from. That turns "64
bytes arrived" into "bytes 0-63 arrived", which is what separates truncation
at the head from anything else.

Every scenario runs twice, once down each way of reaching the registers. The
"rest" route uses machine:readmem / machine:writemem, which are DMA cycles the
Ultimate issues on the cartridge bus. The "native" route runs a small 6502 agent
on the C64 itself, which is how a program actually uses the interface: its
register accesses are microseconds apart rather than tens of milliseconds, and
no DMA is involved. Both were measured to agree in every field, which is what
says a finding is a property of the interface rather than of how it was
observed.

The native route runs only on a target that is one machine. On
cartridge@computer the host's own command interface answers its 6510 at
$DF1B-$DF1F, so a program there would measure the host instead of the cartridge
under test, and the suite skips the route rather than report on the wrong
device.

Reads are kept small on purpose. Over the REST route the response queue is
drained one DMA cycle per byte, so the size of the payload is the cost of the
suite, and every property under test is visible at 64 bytes as clearly as at
894. Writes cost the same way round, one register write per command byte, so
the chunked cases cross the 888-byte chunk boundary by as little as they can
rather than by as much as they could. The exception is
chunked-write-full-size-datagram, which carries a full 1472-byte payload to
completion: there the size is the property under test rather than the price of
measuring one.

Supported on any Ultimate whose FPGA provides the command interface. The suite
enables the "Command Interface" setting and restores it on exit.
"""

import argparse
import socket
import sys
import time
from pathlib import Path

# The one stanza that puts the shared library on sys.path; see tests/lib/bootstrap.py.
sys.path.insert(0, str(next(p for p in Path(__file__).resolve().parents
                            if (p / "tests" / "lib").is_dir()) / "tests" / "lib"))
import bootstrap  # noqa: E402,F401
import cli  # noqa: E402
import targets  # noqa: E402
from api import UltimateApi  # noqa: E402
from report import (  # noqa: E402
    FAIL, Failure, OK, SKIP, check, check_ok, check_skip, check_start, detail,
    format_exception, section, suite_fail, suite_ok, warn)
from uci import (  # noqa: E402
    CMD_QUEUE_BYTES, MAX_BLOCK_BYTES, REPLY_QUEUE_BYTES, TARGET_NETWORK, Uci,
    Wedged, interface_present)
from uci_native import NativeUci  # noqa: E402

CONFIG_CATEGORY = "C64 and Cartridge Settings"
CFG_CMD_IF = "Command Interface"
OWNED_SETTINGS = (CFG_CMD_IF,)

NET_CMD_IDENTIFY = 0x01
NET_CMD_GET_IPADDR = 0x05
NET_CMD_OPEN_TCP = 0x07
NET_CMD_OPEN_UDP = 0x08
NET_CMD_CLOSE_SOCKET = 0x09
NET_CMD_READ_SOCKET = 0x10
NET_CMD_WRITE_SOCKET = 0x11
NET_CMD_WRITE_SOCKET_CHUNK = 0x16

STATUS_OK = b"00,OK"
STATUS_INVALID_PARAMS = b"81,INVALID PARAMS"
STATUS_OUT_OF_RANGE = b"82,PARAMETER(S) OUT OF RANGE"
# What any target answers for a command number it does not know, from
# command_intf.cc. A firmware without WRITE_SOCKET_CHUNK answers this to every
# chunked case below, so those name it rather than failing on a symptom of it.
STATUS_UNKNOWN_COMMAND = b"21,UNKNOWN COMMAND"
# Not in the network target document, but what the firmware answers when
# lwip_recv returns -1. The number is the errno: 9 is EBADF for a handle that
# was never opened, 11 is EAGAIN for an open socket with nothing pending.
STATUS_NO_DATA_PREFIX = b"02,NO DATA"
# What a write answers for a handle this target does not own, from
# send_to_socket(): the number is the errno, 9 for EBADF. A chunked write that
# a reset should have discarded reaches the send with a handle that reset
# closed, so this is the refusal that says the payload outlived the reset.
STATUS_SEND_ERROR_PREFIX = b"12,SEND ERROR"
# The whole of that status for the one errno a client can reach deliberately: a
# handle no OPEN command returned is not a socket this target owns, so the send
# never happens and errno is EBADF.
STATUS_SEND_ERROR_EBADF = STATUS_SEND_ERROR_PREFIX + b": 9"

# A handle no OPEN command ever returned, so the socket can never supply data
# and the length in the command is the only variable.
NEVER_OPENED = 0x7F

# The largest UDP payload that fits one Ethernet frame: 1500 less 20 bytes of
# IPv4 header and 8 of UDP header. Anything above it is sent as fragments, and
# IP_REASSEMBLY is 0 in software/network/config/lwipopts.h, so nothing larger
# reaches a socket on the device at all.
UNFRAGMENTED_MAX = 1472

# The largest length READ_SOCKET accepts. A complete unfragmented IPv4 UDP
# datagram is the most the command can ever return, so the two are the same
# number. This is asserted rather than only discovered, because it is now a
# deliberate limit of the command rather than a side effect of how large a
# reply block the transport happens to be able to carry.
MAX_READ = UNFRAGMENTED_MAX
# The search for the boundary runs over this range, which spans every
# plausible answer, and is what turns "the limit is 1472" into a measurement.
LENGTH_SEARCH_MAX = 2048

# What one reply block can carry. A block of exactly REPLY_QUEUE_BYTES never
# ends, so a block holds at most MAX_BLOCK_BYTES, and the first block of a
# reply spends two of them on the length header.
FIRST_BLOCK_PAYLOAD = MAX_BLOCK_BYTES - 2

# A safe length for reads that only have to work, not to probe a boundary.
SAFE_READ = 512

# Small enough that a full drain is a few seconds, large enough that a
# truncated read and a complete read differ by an obvious amount.
SMALL_READ = 64
BIG_DATAGRAM = 200

# One payload that needs more than one reply block and is not a round number
# of blocks either way, so a payload placed at the wrong offset in the second
# block shows up as wrong data rather than as the right data shifted by zero.
# Used as a datagram over UDP and as a run of stream bytes over TCP.
SPANNING_DATAGRAM = 1420

# A truncated read whose returned part still needs more than one block: the
# datagram is larger than the request, and the request is larger than a block.
TRUNCATED_REQUEST = 1000
TRUNCATED_DATAGRAM = 1200

# A write payload whose cost is the command's framing rather than its size.
SMALL_WRITE = 64

# The largest command the interface delivers, and from it the payload one write
# can carry each way. command_protocol.vhd stops command_pointer on the last
# byte of the command buffer while command_length is measured from it, so the
# CMD_QUEUE_BYTES'th byte a client writes is never counted and a command carries
# at most MAX_COMMAND_BYTES. The response queue reaches the same number for a
# different reason, so it is not evidence for this one. A plain WRITE_SOCKET
# spends three bytes on the target, the command and the handle; a chunk spends
# seven, adding the offset it starts at and the total it belongs to.
#
# MAX_CHUNK_PAYLOAD is therefore not a limit the firmware has to enforce: a
# longer chunk is a longer command than the interface will carry, so no client
# can present one and nothing here can measure a refusal of it.
MAX_COMMAND_BYTES = CMD_QUEUE_BYTES - 1
PLAIN_WRITE_MAX = MAX_COMMAND_BYTES - 3
MAX_CHUNK_PAYLOAD = MAX_COMMAND_BYTES - 7

# The smallest payload that no single command of either kind could send: one
# byte more than a plain WRITE_SOCKET carries, which is also five bytes more
# than one chunk carries, so it needs this command and needs two chunks of it.
# Crossing both boundaries by as little as possible is deliberate. The property
# under test is that the firmware sends once rather than once per chunk, and
# that is as visible at 893 bytes as at the 1472 a datagram could hold, for a
# third of the register writes.
CHUNKED_WRITE = PLAIN_WRITE_MAX + 1

# The largest total a chunked write may announce. The payload leaves as one
# datagram, so the most it can ever be is one unfragmented datagram, and a
# larger announcement names a payload the device could never send.
MAX_ANNOUNCED_TOTAL = UNFRAGMENTED_MAX

# Where the drain of one reply block gives up. The response queue holds 896
# bytes, so anything past that is the queue failing to end rather than a
# longer block.
OVERRUN_LIMIT = REPLY_QUEUE_BYTES + 128

# The device's socket carries SO_RCVTIMEO of 40 ms, so a read finds a datagram
# that is already queued and does not wait for one in flight.
DELIVERY_SETTLE_SECONDS = 0.4
PEER_TIMEOUT_SECONDS = 10.0
# How often a lost probe datagram is resent before the socket is judged
# unreachable.
PROBE_ATTEMPTS = 3

# How many sockets the scenario leaks before the reset. Any number the device
# can spare will do: four is enough to be obvious in lwip's pool and leaves
# room for the sockets the firmware opens for itself. Opening the same number
# again after the reset says they were released rather than merely forgotten.
SOCKETS_LEFT_OPEN_AT_RESET = 4
RESET_CYCLES = 3

# The two ways of reaching the registers, described at the top of this file.
ROUTES = ["rest", "native"]

TESTS = [
    "read-length-limit",
    "reply-blocks-are-drainable",
    "datagram-spans-reply-blocks",
    "datagram-size-ceiling",
    "udp-truncation-is-detectable",
    "truncation-spans-reply-blocks",
    "tcp-read-is-lossless",
    "oversize-request-keeps-the-datagram",
    "multi-block-state-does-not-leak",
    "chunked-write-arrives-as-one-datagram",
    "chunked-write-full-size-datagram",
    "chunked-write-zero-total",
    "chunked-write-total-ceiling",
    "chunked-write-refuses-unowned-socket",
    "chunked-write-refuses-bad-chunks",
    "chunked-write-refuses-offset-ahead",
    "chunked-write-refuses-short-commands",
    "chunked-write-discarded-by-abort",
    "chunked-write-discarded-by-reset",
    "reset-closes-uci-sockets",
]


def pattern(size: int, seed: int = 0) -> bytes:
    """A payload whose every byte identifies its own offset.

    251 is prime and below 256, so a run of bytes fixes the offset it started
    at for any payload shorter than 251 * 251.
    """
    return bytes((i + seed) % 251 for i in range(size))


def run_offset(chunk: bytes, seed: int = 0) -> int | None:
    """Where this run started inside pattern(), or None if it is not a run of it."""
    if not chunk:
        return None
    start = (chunk[0] - seed) % 251
    for index, byte in enumerate(chunk):
        if byte != (start + seed + index) % 251:
            return None
    return start


class ReadResult:
    """What a client can see after one READ_SOCKET, and nothing more."""

    def __init__(self, transaction) -> None:
        block = transaction.first
        self.state = block.state
        self.state_name = block.state_name
        self.status_byte = block.status_byte
        # The logical reply, which is every block's data in order. A reply that
        # fits one block is that block; one that does not is announced as Data
        # More and continues in the next, and a client that concatenates them
        # holds exactly what a single block would have carried.
        self.reply = transaction.data
        self.status_text = transaction.status_text
        self.overrun = block.overrun
        self.block_lengths = [len(b.data) for b in transaction.blocks]
        self.blocks = len(transaction.blocks)
        # The reply's own framing: two header bytes, then the payload. The
        # header counts the whole payload, not the part in the first block.
        self.header = (int.from_bytes(self.reply[:2], "little", signed=True)
                       if len(self.reply) >= 2 else None)
        self.payload = self.reply[2:]

    @property
    def observable(self) -> tuple:
        """Everything a client could branch on, as one comparable value.

        Two reads that differ in what was actually delivered but agree here are
        indistinguishable to any client, however careful it is.
        """
        return (self.status_byte, self.header, len(self.payload),
                self.status_text, self.overrun)

    def describe(self) -> str:
        return (f"{self.state_name}, header {self.header}, payload {len(self.payload)} bytes"
                f"{f' from offset {run_offset(self.payload)}' if self.payload else ''}"
                f", blocks {self.block_lengths}"
                f", status {self.status_text!r}"
                + (f", overrun {self.overrun.hex(' ')}" if self.overrun else ""))


def describe_mismatch(actual: bytes, expected: bytes) -> str:
    """Where two payloads first differ, and what is there instead.

    A reply assembled from several blocks can go wrong by dropping bytes,
    repeating them, or starting the second block at the wrong offset. All three
    show up as the first differing position plus the offset the bytes there
    actually came from, because pattern() makes every byte name its own offset.
    """
    if len(actual) != len(expected):
        return (f"got {len(actual)} bytes, expected {len(expected)}"
                + (f"; the bytes it does have start at offset {run_offset(actual)}"
                   if run_offset(actual) is not None else ""))
    for index, (got, want) in enumerate(zip(actual, expected)):
        if got != want:
            tail = actual[index:index + 16]
            return (f"first difference at offset {index}: got ${got:02X}, expected "
                    f"${want:02X}; the run starting there came from offset "
                    f"{run_offset(tail)}")
    return "identical"


def describe_datagrams(arrived: list[bytes], expected: bytes) -> str:
    """What reached the peer, against the one datagram that should have.

    A payload that left as several datagrams is the defect a chunked write
    exists to remove, and it is worth separating from one that left as one and
    was assembled wrongly, so the concatenation is reported too.
    """
    if not arrived:
        return f"nothing arrived; {len(expected)} bytes were expected"
    if len(arrived) == 1:
        return describe_mismatch(arrived[0], expected)
    joined = b"".join(arrived)
    return (f"{len(arrived)} datagrams arrived, of lengths {[len(d) for d in arrived]}; "
            + ("concatenated they are the payload that was announced"
               if joined == expected
               else f"concatenated: {describe_mismatch(joined, expected)}"))


def send_count(transaction) -> int | None:
    """How many bytes a write reported sending, from its two-byte reply.

    None when the reply is not two bytes, which is a finding of its own: a
    write either answers with the count or, while a payload is still
    incomplete, with nothing at all.
    """
    return (int.from_bytes(transaction.data, "little", signed=True)
            if len(transaction.data) == 2 else None)


def require_implemented(transaction, what: str) -> None:
    """Stop with the reason when this firmware has no such command.

    A target that does not know a command number answers '21,UNKNOWN COMMAND'
    with an empty reply. Without this, every chunked case would report a
    missing datagram or a missing byte count, which is the symptom rather than
    the cause.
    """
    if transaction.status_text == STATUS_UNKNOWN_COMMAND:
        raise Failure(f"{what} answered {STATUS_UNKNOWN_COMMAND.decode()}: this firmware has "
                      f"no WRITE_SOCKET_CHUNK (${NET_CMD_WRITE_SOCKET_CHUNK:02X}) command, so "
                      f"a payload larger than one command still leaves as several datagrams")


def require_nothing_sent(after: list[bytes], what: str) -> None:
    """Nothing reached the wire, which is the other half of every refusal here.

    A status of 81 with a datagram behind it is worse than no status at all, so
    each refused chunk is confirmed on the command interface and on the wire.
    """
    if after:
        raise Failure(f"{len(after)} datagram(s) of {[len(d) for d in after]} bytes left "
                      f"the device on {what}. The refusal is only half of it: the bytes "
                      f"that reached the wire are a message the client never wrote")


def require_payload_open(started, premature: list[bytes], taken: int, total: int) -> None:
    """The accumulation a case needs is in progress and nothing has been sent.

    Every case that disturbs a payload part way through needs one part way
    through. Without it the disturbing chunk continues a payload that was never
    announced, which any firmware refuses, and the case passes whatever the
    firmware does.
    """
    require_implemented(started, "the opening chunk of a chunked write")
    if started.status_text != STATUS_OK:
        raise Failure(f"the opening chunk of {taken} of {total} bytes answered "
                      f"{started.status_text!r}, expected {STATUS_OK!r}; without it "
                      f"there is no payload in progress for this case to disturb")
    if premature:
        raise Failure(f"{len(premature)} datagram(s) of {[len(d) for d in premature]} "
                      f"bytes left the device on a chunk that carried {taken} of "
                      f"{total} announced bytes")


class Net:
    """The network target's commands, over the command interface."""

    def __init__(self, uci: Uci) -> None:
        self.uci = uci

    def identify(self):
        return self.uci.transact(bytes([TARGET_NETWORK, NET_CMD_IDENTIFY]))

    def ip_address(self) -> str | None:
        reply = self.uci.transact(bytes([TARGET_NETWORK, NET_CMD_GET_IPADDR, 0])).data
        return ".".join(str(b) for b in reply[:4]) if len(reply) >= 4 else None

    def open_command(self, command: int, ip: str, port: int) -> bytes:
        return (bytes([TARGET_NETWORK, command, port & 0xFF, (port >> 8) & 0xFF])
                + ip.encode("ascii") + b"\x00")

    def _open(self, command: int, ip: str, port: int, what: str) -> int:
        result = self.uci.transact(self.open_command(command, ip, port))
        if result.status_text != STATUS_OK or len(result.data) != 1:
            raise Failure(f"{what} {ip}:{port} answered {result.status_text!r} "
                          f"with reply {result.data!r}")
        return result.data[0]

    def open_udp(self, ip: str, port: int) -> int:
        return self._open(NET_CMD_OPEN_UDP, ip, port, "OPEN_UDP")

    def open_tcp(self, ip: str, port: int) -> int:
        return self._open(NET_CMD_OPEN_TCP, ip, port, "OPEN_TCP")

    def write(self, handle: int, payload: bytes):
        return self.uci.transact(bytes([TARGET_NETWORK, NET_CMD_WRITE_SOCKET, handle]) + payload)

    def write_chunk_command(self, handle: int, offset: int, total: int,
                            payload: bytes) -> bytes:
        """One chunk of a payload of `total` bytes, starting at `offset`.

        Every chunk repeats the handle and the total and names where it belongs,
        so a chunk is self-describing: a chunk at offset 0 opens a payload and
        announces how long it will be, and every later one has to agree with
        what is in progress in all three fields before its bytes are taken.
        """
        return (bytes([TARGET_NETWORK, NET_CMD_WRITE_SOCKET_CHUNK, handle,
                       offset & 0xFF, (offset >> 8) & 0xFF,
                       total & 0xFF, (total >> 8) & 0xFF]) + payload)

    def write_chunk(self, handle: int, offset: int, total: int, payload: bytes):
        return self.uci.transact(self.write_chunk_command(handle, offset, total, payload))

    def read_command(self, handle: int, maxlen: int) -> bytes:
        return bytes([TARGET_NETWORK, NET_CMD_READ_SOCKET, handle,
                      maxlen & 0xFF, (maxlen >> 8) & 0xFF])

    def read(self, handle: int, maxlen: int, overrun_reads: int = 0,
             single_part: bool = True) -> ReadResult:
        """One READ_SOCKET, followed through however many blocks it takes.

        `single_part` asserts that the reply arrives in one block, announced as
        Data Last, which is what every reply short enough to fit one has to do.
        Pass False for a read whose payload cannot fit one block.
        """
        return ReadResult(self.uci.transact(self.read_command(handle, maxlen),
                                            single_part=single_part,
                                            overrun_reads=overrun_reads))

    def probe_read(self, handle: int, maxlen: int) -> tuple[int, bool, bytes]:
        """One read, its first block drained to a cap rather than to DATA_AV."""
        return self.uci.probe_drain(self.read_command(handle, maxlen), OVERRUN_LIMIT)

    def abort_read(self, handle: int, maxlen: int) -> tuple[int, bool]:
        """One read whose reply is abandoned after its first block."""
        return self.uci.probe_abort(self.read_command(handle, maxlen))

    def close(self, handle: int):
        return self.uci.transact(bytes([TARGET_NETWORK, NET_CMD_CLOSE_SOCKET, handle]))

    def largest_accepted_length(self) -> int:
        """The largest length READ_SOCKET accepts, found by bisection.

        Probed against a handle that was never opened, so the socket refuses
        every one of them and the only thing that separates the answers is the
        length: an accepted length is answered by the socket
        ('02,NO DATA'), a refused one by the length check
        ('82,PARAMETER(S) OUT OF RANGE'). No reply carries a payload, so this
        costs a handful of register accesses per probe whatever the length.
        """
        def accepted(length: int) -> bool:
            return self.read(NEVER_OPENED, length).status_text != STATUS_OUT_OF_RANGE

        low, high = 1, LENGTH_SEARCH_MAX
        if not accepted(low):
            raise Failure(f"READ_SOCKET refused a length of {low}")
        if accepted(high):
            raise Failure(f"READ_SOCKET accepted a length of {high}, which is larger than "
                          f"any datagram that can reach the device")
        while high - low > 1:
            middle = (low + high) // 2
            if accepted(middle):
                low = middle
            else:
                high = middle
        return low

    def drain_socket(self, handle: int, maxlen: int = SAFE_READ, limit: int = 8) -> int:
        """Read until the socket has nothing left, so a case starts clean."""
        drained = 0
        for _ in range(limit):
            result = self.read(handle, maxlen)
            if not result.payload:
                return drained
            drained += len(result.payload)
        raise Failure(f"socket {handle} still had data after {limit} reads")


def local_address_towards(device_host: str) -> str:
    """This machine's address on the route to the device, without guessing.

    A connected UDP socket applies the routing table without sending anything,
    so getsockname reports the source address the device would see.
    """
    probe = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        probe.connect((device_host, 9))
        return probe.getsockname()[0]
    finally:
        probe.close()


class Peer:
    """The host-side endpoints the device opens sockets to."""

    def __init__(self, bind_ip: str) -> None:
        self.ip = bind_ip
        self.udp = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.udp.bind((bind_ip, 0))
        self.udp_port = self.udp.getsockname()[1]
        self.udp_peer: tuple[str, int] | None = None
        self.tcp = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.tcp.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.tcp.bind((bind_ip, 0))
        self.tcp.listen(2)
        self.tcp_port = self.tcp.getsockname()[1]
        self.tcp_conn: socket.socket | None = None

    def learn_udp_peer(self, net: Net, handle: int) -> tuple[str, int]:
        """The device's ephemeral source address, from a datagram it sends.

        The device connects its UDP socket, so it only accepts datagrams from
        the address it connected to, and a reply has to come from this bound
        port back to whatever source port the device chose.

        The probe is a datagram, so it can be lost; seen once in a dozen runs.
        A lost one is resent rather than counted against the firmware.

        Anything already queued here is discarded first. A datagram left over
        from an earlier scenario is read as this probe otherwise, and the
        address learned is then a socket that is already closed, so every
        send_udp that follows goes nowhere and the device reads report no data.
        """
        self.udp.setblocking(False)
        try:
            while True:
                self.udp.recvfrom(2048)
        except OSError:
            pass
        finally:
            self.udp.setblocking(True)
        for attempt in range(PROBE_ATTEMPTS):
            net.write(handle, b"PROBE")
            self.udp.settimeout(PEER_TIMEOUT_SECONDS)
            try:
                _, address = self.udp.recvfrom(2048)
            except TimeoutError:
                detail(f"probe datagram {attempt + 1} of {PROBE_ATTEMPTS} did not arrive "
                       f"within {PEER_TIMEOUT_SECONDS:.0f}s")
                continue
            self.udp_peer = address
            return address
        raise Failure(f"no probe datagram arrived from handle {handle} in "
                      f"{PROBE_ATTEMPTS} attempts")

    def send_udp(self, payload: bytes) -> None:
        if self.udp_peer is None:
            raise Failure("the device's UDP source address is not known yet")
        self.udp.sendto(payload, self.udp_peer)

    def collect_udp(self, settle: float = DELIVERY_SETTLE_SECONDS) -> list[bytes]:
        """Every datagram the device sent here, over one settle window.

        The only method in this class that watches the device emitting rather
        than receiving. Everything else goes host to device and reads the
        result back over the command interface, which cannot see datagram
        boundaries at all: the reply to a write is a byte count, and 893 is 893
        whether the bytes left as one datagram or as two.

        The window always runs to its end rather than stopping at the first
        datagram, because "exactly one arrived" is only established once a
        second one has had its chance. It is the same wait the rest of the
        suite spends in DELIVERY_SETTLE_SECONDS after a send, so watching the
        wire costs nothing over settling for it.
        """
        if self.udp_peer is None:
            raise Failure("the device's UDP source address is not known yet")
        datagrams: list[bytes] = []
        deadline = time.time() + settle
        while True:
            remaining = deadline - time.time()
            if remaining <= 0:
                return datagrams
            self.udp.settimeout(remaining)
            try:
                # Larger than any datagram that can reach either end, so a
                # datagram of an unexpected length is reported at its length
                # rather than silently cut to the expected one.
                datagrams.append(self.udp.recv(2048))
            except TimeoutError:
                return datagrams

    def accept_tcp(self) -> socket.socket:
        self.tcp.settimeout(PEER_TIMEOUT_SECONDS)
        conn, _ = self.tcp.accept()
        conn.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        self.tcp_conn = conn
        return conn

    def close(self) -> None:
        for sock in (self.tcp_conn, self.tcp, self.udp):
            if sock is not None:
                try:
                    sock.close()
                except OSError:
                    pass


def run_read_length_limit(net: Net) -> bool:
    """What the length in the command does, isolated from the socket.

    The handle here was never opened, so the socket can only ever refuse. Any
    difference between two rows is therefore the length handling and nothing
    else.

    The boundary is found on the device by bisection and then compared against
    MAX_READ. It is asserted rather than only recorded because it is now a
    deliberate limit of the command: a complete unfragmented IPv4 UDP datagram
    is the most READ_SOCKET can return, so 1472 is the most it accepts. A reply
    longer than one transport block is split across blocks rather than refused,
    so the limit no longer follows from the size of the response queue.
    """
    section("read-length-limit")
    accepted_max = net.largest_accepted_length()

    with check(f"READ_SOCKET accepts lengths up to {MAX_READ} and no further"):
        detail(f"bisection found the boundary at {accepted_max}")
        if accepted_max != MAX_READ:
            raise Failure(
                f"READ_SOCKET accepts up to {accepted_max}, expected {MAX_READ}: "
                f"1500 bytes of Ethernet MTU less 20 of IPv4 header and 8 of UDP "
                f"header, which is the largest datagram that can reach the device")

    with check(f"READ_SOCKET accepts a length of {accepted_max}"):
        result = net.read(NEVER_OPENED, accepted_max, overrun_reads=4)
        detail(result.describe())
        if not result.status_text.startswith(STATUS_NO_DATA_PREFIX):
            raise Failure(
                f"length {accepted_max} answered {result.status_text!r}; the socket was "
                f"never opened, so the answer has to come from the socket "
                f"({STATUS_NO_DATA_PREFIX.decode()}), not from the length")
        if result.header != -1:
            raise Failure(f"a refused socket read should report -1 in the reply header, "
                          f"got {result.header}")

    with check(f"READ_SOCKET refuses a length of {accepted_max + 1}"):
        result = net.read(NEVER_OPENED, accepted_max + 1, overrun_reads=4)
        detail(result.describe())
        if result.status_text != STATUS_OUT_OF_RANGE:
            raise Failure(f"length {accepted_max + 1} answered {result.status_text!r}, "
                          f"expected {STATUS_OUT_OF_RANGE!r}")
        if result.reply:
            raise Failure(f"a refused request put {len(result.reply)} bytes in the response "
                          f"queue; it must leave it empty")
        # The client-visible shape of that refusal, which is what #802 reported
        # as "silently returns no data": the response queue is empty, so
        # DATA_AV never sets, and a client that reads the two header bytes
        # regardless of the flag reads back zeros and computes a length of 0.
        if result.overrun and set(result.overrun) == {0}:
            detail("an empty response queue reads back $00, so a client that takes the "
                   "2-byte header without checking DATA_AV computes a length of 0 and "
                   "sees a refusal as an empty read")

    with check("the refusal boundary is sharp"):
        # Bisection only finds a boundary if the answer is monotonic in the
        # length. This confirms the two lengths either side of it directly.
        accepted = net.read(NEVER_OPENED, accepted_max)
        refused = net.read(NEVER_OPENED, accepted_max + 1)
        if not accepted.status_text.startswith(STATUS_NO_DATA_PREFIX):
            raise Failure(f"length {accepted_max} was refused: {accepted.status_text!r}")
        if refused.status_text != STATUS_OUT_OF_RANGE:
            raise Failure(f"length {accepted_max + 1} was accepted: {refused.status_text!r}")
        detail(f"accepted up to {accepted_max}, refused from {accepted_max + 1}")
    return True


def run_reply_blocks_are_drainable(net: Net, peer: Peer) -> bool:
    """Every reply block the firmware builds has to end.

    A reply block is delivered through the response queue, and the transport
    cannot deliver a block of exactly the queue's size. Measured on a U64
    Elite: a block of 895 bytes ends and DATA_AV clears, a block of 896 never
    ends and every further read returns the same last byte.

    The mechanism is in fpga/io/command_interface/vhdl_source/command_protocol.vhd.
    response_pointer stops incrementing once it reaches
    c_cmd_if_response_buffer_end, the last byte of the buffer, while
    response_valid stays asserted for as long as
    (response_pointer - buffer_addr) < response_length. At a length of exactly
    896 the pointer saturates at offset 895 and that comparison stays true, so
    the two never agree.

    A client written to the protocol reads the response register while DATA_AV
    is set, so this is not a slow read or a lost byte: it is an endless one.
    The reference client, ultimateii-dos-lib, does exactly that into a
    fixed-size buffer.

    This measures the first block of a reply directly, at three lengths: the
    largest payload that fits one block, one byte more than that, and the
    largest read the command accepts. The second is the case that used to build
    a 896-byte block. It is now the first block of a reply that continues, and
    it still has to end.
    """
    section("reply-blocks-are-drainable")
    handle = net.open_udp(peer.ip, peer.udp_port)
    try:
        peer.learn_udp_peer(net, handle)
        measured = {}
        for maxlen in (FIRST_BLOCK_PAYLOAD, FIRST_BLOCK_PAYLOAD + 1, MAX_READ):
            net.drain_socket(handle)
            peer.send_udp(pattern(MAX_READ))
            time.sleep(DELIVERY_SETTLE_SECONDS)
            measured[maxlen] = net.probe_read(handle, maxlen)
            block, cleared, tail = measured[maxlen]
            detail(f"length {maxlen}: first block {block} bytes, DATA_AV cleared {cleared}, "
                   f"last bytes {tail.hex(' ')}")

        with check(f"a read of {FIRST_BLOCK_PAYLOAD} bytes ends its reply in one block"):
            block, cleared, _ = measured[FIRST_BLOCK_PAYLOAD]
            if not cleared:
                raise Failure(f"DATA_AV was still set after {block} bytes")
            if block != FIRST_BLOCK_PAYLOAD + 2:
                raise Failure(f"expected {FIRST_BLOCK_PAYLOAD + 2} bytes (2 header + "
                              f"{FIRST_BLOCK_PAYLOAD} payload), got {block}")

        for maxlen in (FIRST_BLOCK_PAYLOAD + 1, MAX_READ):
            with check(f"the first block of a read of {maxlen} bytes ends"):
                block, cleared, tail = measured[maxlen]
                if not cleared:
                    raise Failure(
                        f"DATA_AV was still set after {block} bytes and the queue was still "
                        f"repeating {tail[-1:].hex()}. A block of exactly "
                        f"{REPLY_QUEUE_BYTES} bytes, the size of the response queue, never "
                        f"ends, and a client that reads while DATA_AV is set never stops")
                if block > MAX_BLOCK_BYTES:
                    raise Failure(
                        f"the first block carried {block} bytes; a block may carry at most "
                        f"{MAX_BLOCK_BYTES}, because one of {REPLY_QUEUE_BYTES} never ends")
                if block == 0:
                    raise Failure(
                        f"a read of {maxlen} bytes put nothing in the response queue, so it "
                        f"was refused rather than answered; a datagram of {MAX_READ} bytes "
                        f"was pending and the command accepts lengths up to {MAX_READ}")

        with check(f"no block of any of these replies reaches {REPLY_QUEUE_BYTES} bytes"):
            sizes = {maxlen: block for maxlen, (block, _, _) in measured.items()}
            detail(f"first block sizes: {sizes}")
            oversized = {maxlen: block for maxlen, block in sizes.items()
                         if block >= REPLY_QUEUE_BYTES}
            if oversized:
                raise Failure(f"reads of {sorted(oversized)} built a block of "
                              f"{sorted(oversized.values())} bytes")
    finally:
        net.close(handle)
    return True


def run_datagram_spans_reply_blocks(net: Net, peer: Peer) -> bool:
    """A datagram larger than one reply block still arrives complete.

    The reply is a 2-byte length header followed by the payload. When that does
    not fit one block, the firmware announces Data More and continues in the
    next, and a client that concatenates the blocks holds exactly the reply a
    large enough response queue would have delivered in one. The header stays
    the total number of payload bytes rather than the number in the first
    block, which is what a client uses to find the end of the payload.

    Four sizes: the largest payload that fits one block, one byte more than
    that, a size in the middle of the second block, and the largest datagram
    that can reach the device at all. The payload identifies its own offset, so
    a second block written from the wrong offset, or one that repeats or drops
    bytes, shows up as wrong data rather than as a plausible reply.
    """
    section("datagram-spans-reply-blocks")
    handle = net.open_udp(peer.ip, peer.udp_port)
    try:
        peer.learn_udp_peer(net, handle)
        # (datagram size, requested length, whether it can fit one block)
        cases = [
            (FIRST_BLOCK_PAYLOAD, FIRST_BLOCK_PAYLOAD, True),
            (FIRST_BLOCK_PAYLOAD + 1, MAX_READ, False),
            (SPANNING_DATAGRAM, MAX_READ, False),
            (MAX_READ, MAX_READ, False),
        ]
        measured = []
        for size, maxlen, one_block in cases:
            net.drain_socket(handle)
            peer.send_udp(pattern(size))
            time.sleep(DELIVERY_SETTLE_SECONDS)
            result = net.read(handle, maxlen, single_part=one_block)
            measured.append((size, maxlen, one_block, result))
            detail(f"{size}-byte datagram read at {maxlen}: {result.describe()}")
            if not one_block and len(result.payload) > FIRST_BLOCK_PAYLOAD:
                # What a wrong split looks like in the payload: the first byte
                # of the second block has to continue the run the first block
                # ended with. The equality check below is what fails on it;
                # this records the bytes so the failure reads directly.
                edge = FIRST_BLOCK_PAYLOAD
                around = result.payload[edge - 4:edge + 4]
                detail(f"  bytes {edge - 4} to {edge + 3}, spanning the block boundary: "
                       f"{around.hex(' ')}, a run from offset {run_offset(around)}")

        for size, maxlen, one_block, result in measured:
            with check(f"a {size}-byte datagram read with a length of {maxlen} arrives whole"):
                if result.status_text != STATUS_OK:
                    raise Failure(f"expected {STATUS_OK!r}, got {result.status_text!r}")
                if result.header != size:
                    raise Failure(
                        f"the header reports {result.header} while the datagram was {size} "
                        f"bytes; the header is the total number of payload bytes the reply "
                        f"carries, not the number in its first block")
                if result.payload != pattern(size):
                    raise Failure(f"the payload is not the datagram that was sent: "
                                  f"{describe_mismatch(result.payload, pattern(size))}")

            with check(f"the reply to a {size}-byte read is delivered in "
                       f"{'one block' if one_block else 'blocks that each end'}"):
                detail(f"block sizes {result.block_lengths}")
                if one_block and result.blocks != 1:
                    raise Failure(
                        f"a payload of {size} bytes fits one block, so it must arrive in "
                        f"one rather than in {result.blocks}; splitting it would break a "
                        f"client that does not follow Data More")
                if not one_block and result.blocks < 2:
                    raise Failure(
                        f"a payload of {size} bytes cannot fit one block, because a block "
                        f"carries at most {MAX_BLOCK_BYTES} bytes, yet the reply arrived "
                        f"in {result.blocks}")
                oversized = [b for b in result.block_lengths if b > MAX_BLOCK_BYTES]
                if oversized:
                    raise Failure(
                        f"blocks of {oversized} bytes were built; a block of "
                        f"{REPLY_QUEUE_BYTES} bytes never ends, so no block may exceed "
                        f"{MAX_BLOCK_BYTES}")
    finally:
        net.close(handle)
    return True


def run_datagram_size_ceiling(net: Net, peer: Peer) -> bool:
    """How large a datagram can reach the device at all, before any read length.

    This is a property of the network stack, not of the command interface, and
    it bounds anything the command interface could ever deliver. It is measured
    with a small read length so the reply drain stays short: whether a datagram
    arrived is visible from the first byte of it, and the payload identifies its
    own offset.
    """
    section("datagram-size-ceiling")
    handle = net.open_udp(peer.ip, peer.udp_port)
    try:
        peer.learn_udp_peer(net, handle)
        arrivals: dict[int, int] = {}
        for size in (SMALL_READ, BIG_DATAGRAM, UNFRAGMENTED_MAX, UNFRAGMENTED_MAX + 1, 1500):
            net.drain_socket(handle)
            peer.send_udp(pattern(size))
            time.sleep(DELIVERY_SETTLE_SECONDS)
            result = net.read(handle, SMALL_READ)
            arrivals[size] = len(result.payload)
            detail(f"{size:>4}-byte datagram: {result.describe()}")

        with check(f"a datagram of up to {UNFRAGMENTED_MAX} bytes reaches the device"):
            missing = [size for size in (SMALL_READ, BIG_DATAGRAM, UNFRAGMENTED_MAX)
                       if arrivals[size] == 0]
            if missing:
                raise Failure(f"no part of a datagram of {missing} bytes arrived, "
                              f"though each fits one Ethernet frame")

        with check(f"a datagram above {UNFRAGMENTED_MAX} bytes does not reach the device"):
            arrived = [size for size in (UNFRAGMENTED_MAX + 1, 1500) if arrivals[size] > 0]
            if arrived:
                raise Failure(f"a datagram of {arrived} bytes arrived; IP_REASSEMBLY is 0 in "
                              f"software/network/config/lwipopts.h, so a fragmented datagram "
                              f"should be dropped before any socket sees it")
            detail("IP reassembly is off for the whole device (lwipopts.h IP_REASSEMBLY 0), "
                   "so this bounds every network service, not just the command interface")
    finally:
        net.close(handle)
    return True


def run_udp_truncation(net: Net, peer: Peer) -> bool:
    """A datagram larger than the requested length must not vanish in silence.

    The property under test is detectability, not delivery: whatever the
    firmware does with the bytes that do not fit, a client has to be able to
    tell that they existed. Today a 200-byte datagram read with a length of 64
    and a genuine 64-byte datagram produce byte-for-byte identical replies, so
    a protocol with per-datagram integrity sees an authentication failure with
    nothing to attribute it to.
    """
    section("udp-truncation-is-detectable")
    ok = True
    handle = net.open_udp(peer.ip, peer.udp_port)
    detail(f"UDP socket handle {handle}")
    try:
        address = peer.learn_udp_peer(net, handle)
        detail(f"device source address {address[0]}:{address[1]}")

        net.drain_socket(handle)
        peer.send_udp(pattern(SMALL_READ))
        time.sleep(DELIVERY_SETTLE_SECONDS)
        complete = net.read(handle, SMALL_READ, overrun_reads=2)

        net.drain_socket(handle)
        peer.send_udp(pattern(BIG_DATAGRAM))
        time.sleep(DELIVERY_SETTLE_SECONDS)
        truncated = net.read(handle, SMALL_READ, overrun_reads=2)
        follow_up = net.read(handle, SAFE_READ)

        with check(f"a {SMALL_READ}-byte datagram read with a length of {SMALL_READ} arrives whole"):
            detail(complete.describe())
            if complete.payload != pattern(SMALL_READ):
                raise Failure(f"expected the whole {SMALL_READ}-byte payload, got "
                              f"{len(complete.payload)} bytes from offset "
                              f"{run_offset(complete.payload)}")
            if complete.status_text != STATUS_OK:
                raise Failure(f"expected {STATUS_OK!r}, got {complete.status_text!r}")

        # Delivery, as opposed to detectability, is recorded rather than gated:
        # whether the bytes that did not fit are queued for a further read or
        # discarded is a design decision, and a signal-only fix leaves them
        # discarded on purpose. Recorded before the check below, which ends the
        # scenario when it fails.
        recovered = len(truncated.payload) + len(follow_up.payload)
        if recovered < BIG_DATAGRAM:
            warn(f"{BIG_DATAGRAM - recovered} of {BIG_DATAGRAM} datagram bytes were not "
                 f"retrievable: the first read delivered {len(truncated.payload)} and a "
                 f"further read delivered {len(follow_up.payload)}, status "
                 f"{follow_up.status_text!r}")

        with check("the reply header still reports the bytes the reply carries"):
            # The network target document defines the header as "the actual
            # number of bytes read". Anything that reports a datagram's true
            # length there instead would tell a client how much was lost at the
            # cost of breaking every client that uses the header to find the
            # end of the payload, so the contract is asserted on both reads.
            for label, result in (("complete", complete), ("truncated", truncated)):
                if result.header != len(result.payload):
                    raise Failure(
                        f"the {label} read reported {result.header} in its header while "
                        f"carrying {len(result.payload)} payload bytes; the header is "
                        f"specified as the number of bytes read")

        with check(f"a {BIG_DATAGRAM}-byte datagram read with a length of {SMALL_READ} "
                   f"is distinguishable from a complete one"):
            detail(f"truncated read: {truncated.describe()}")
            detail(f"a further read returned {len(follow_up.payload)} bytes, "
                   f"status {follow_up.status_text!r}")
            if truncated.observable == complete.observable:
                raise Failure(
                    f"reading a {BIG_DATAGRAM}-byte datagram with a length of {SMALL_READ} "
                    f"is byte-for-byte identical to reading a genuine {SMALL_READ}-byte "
                    f"datagram: both report header {complete.header}, "
                    f"{len(complete.payload)} payload bytes and status "
                    f"{complete.status_text!r}. {BIG_DATAGRAM - len(truncated.payload)} bytes "
                    f"were dropped and no field a client can read says so")

    finally:
        net.close(handle)
    return ok


def run_truncation_spans_reply_blocks(net: Net, peer: Peer) -> bool:
    """Truncation and continuation are independent, and have to compose.

    A 1200-byte datagram read with a length of 1000: the request is smaller
    than the datagram, so the read is truncated, and it is larger than one
    reply block, so what is returned still has to be split. The firmware must
    return exactly the requested number of bytes, report the true datagram
    length on the status channel, and keep the header at the number of bytes
    the reply carries.
    """
    section("truncation-spans-reply-blocks")
    handle = net.open_udp(peer.ip, peer.udp_port)
    try:
        peer.learn_udp_peer(net, handle)
        net.drain_socket(handle)
        peer.send_udp(pattern(TRUNCATED_DATAGRAM))
        time.sleep(DELIVERY_SETTLE_SECONDS)
        result = net.read(handle, TRUNCATED_REQUEST, single_part=False)
        follow_up = net.read(handle, SAFE_READ)
        detail(f"{TRUNCATED_DATAGRAM}-byte datagram read at {TRUNCATED_REQUEST}: "
               f"{result.describe()}")

        with check(f"exactly {TRUNCATED_REQUEST} bytes of a {TRUNCATED_DATAGRAM}-byte "
                   f"datagram are returned"):
            if result.header != TRUNCATED_REQUEST:
                raise Failure(
                    f"the header reports {result.header}; it is the number of bytes the "
                    f"reply carries, which is the requested {TRUNCATED_REQUEST}, not the "
                    f"datagram's {TRUNCATED_DATAGRAM}")
            if result.payload != pattern(TRUNCATED_REQUEST):
                raise Failure(f"the payload is not the head of the datagram: "
                              f"{describe_mismatch(result.payload, pattern(TRUNCATED_REQUEST))}")
            if result.blocks < 2:
                raise Failure(f"the reply arrived in {result.blocks} block(s); "
                              f"{TRUNCATED_REQUEST} payload bytes cannot fit one block of at "
                              f"most {MAX_BLOCK_BYTES}")
            oversized = [b for b in result.block_lengths if b > MAX_BLOCK_BYTES]
            if oversized:
                raise Failure(f"blocks of {oversized} bytes were built; no block may exceed "
                              f"{MAX_BLOCK_BYTES}, because one of {REPLY_QUEUE_BYTES} never "
                              f"ends")

        with check("the true datagram length is reported on the status channel"):
            expected = f"04,DATAGRAM TRUNCATED: {TRUNCATED_DATAGRAM}".encode("ascii")
            if result.status_text != expected:
                raise Failure(f"expected {expected!r}, got {result.status_text!r}")

        with check("the rest of the truncated datagram is gone, not left in the socket"):
            # UDP semantics: what did not fit is discarded with the datagram.
            # Recorded as a check rather than a warning because a following
            # read that returned the remainder would mean the read had queued
            # part of a datagram, which no client can ask about.
            detail(f"a further read returned {len(follow_up.payload)} bytes, "
                   f"status {follow_up.status_text!r}")
            if follow_up.payload:
                raise Failure(f"a further read returned {len(follow_up.payload)} bytes from "
                              f"offset {run_offset(follow_up.payload)}; the remainder of a "
                              f"truncated datagram is discarded, so it must return nothing")
    finally:
        net.close(handle)
    return True


def run_multi_block_state_does_not_leak(net: Net, peer: Peer) -> bool:
    """A reply that spans blocks must not outlive its own command.

    The firmware holds the received payload between blocks, so there is state
    that a client could leave behind: by abandoning the reply part way through,
    or simply by finishing it. Neither may show up in the next command, and
    neither may leave the target unable to answer one.

    Abandoning is the documented way out of a reply a client no longer wants:
    the abort bit in the control register, which the Ultimate answers by
    resetting the exchange.
    """
    section("multi-block-state-does-not-leak")
    handle = net.open_udp(peer.ip, peer.udp_port)
    try:
        peer.learn_udp_peer(net, handle)

        net.drain_socket(handle)
        peer.send_udp(pattern(MAX_READ))
        time.sleep(DELIVERY_SETTLE_SECONDS)
        first_block, idle = net.abort_read(handle, MAX_READ)
        detail(f"abandoned after {first_block} bytes, interface idle {idle}")

        with check("a reply abandoned after its first block returns the interface to Idle"):
            # A payload of MAX_READ bytes cannot fit one block, so a first
            # block within the block limit says the reply had more to come and
            # the abort really did abandon one part way through.
            if not 0 < first_block <= MAX_BLOCK_BYTES:
                raise Failure(f"the first block carried {first_block} bytes; a block carries "
                              f"between 1 and {MAX_BLOCK_BYTES}, and a {MAX_READ}-byte "
                              f"payload needs more than one, so this reply was not one that "
                              f"could be abandoned part way through")
            if not idle:
                raise Failure("the interface did not return to Idle after the abort")

        with check("the next command after an abandoned reply is answered normally"):
            # NET_CMD_IDENTIFY has a fixed reply of its own, so anything left
            # over from the abandoned read would show up as extra bytes, as a
            # reply announced as Data More, or as no reply at all.
            identity = net.identify()
            detail(f"{identity.data!r}, {len(identity.blocks)} block(s), "
                   f"status {identity.status_text!r}")
            if identity.status_text != STATUS_OK:
                raise Failure(f"NET_CMD_IDENTIFY answered {identity.status_text!r}")
            if len(identity.blocks) != 1:
                raise Failure(f"NET_CMD_IDENTIFY replied in {len(identity.blocks)} blocks; "
                              f"its reply fits one, so the abandoned read left the target "
                              f"still handing out blocks")
            if not identity.data.startswith(b"ULTIMATE"):
                raise Failure(f"NET_CMD_IDENTIFY replied {identity.data!r}, which is not its "
                              f"identification string")

        with check("a read after an abandoned reply returns the next datagram, not the old one"):
            net.drain_socket(handle)
            peer.send_udp(pattern(SMALL_READ, seed=7))
            time.sleep(DELIVERY_SETTLE_SECONDS)
            result = net.read(handle, SAFE_READ)
            detail(result.describe())
            if result.payload != pattern(SMALL_READ, seed=7):
                raise Failure(f"expected the {SMALL_READ}-byte datagram sent after the "
                              f"abort, got {len(result.payload)} bytes: "
                              f"{describe_mismatch(result.payload, pattern(SMALL_READ, seed=7))}")

        with check("the command after a completed multi-block reply is answered normally"):
            net.drain_socket(handle)
            peer.send_udp(pattern(MAX_READ))
            time.sleep(DELIVERY_SETTLE_SECONDS)
            completed = net.read(handle, MAX_READ, single_part=False)
            detail(f"completed: {completed.describe()}")
            if completed.payload != pattern(MAX_READ):
                raise Failure(f"the multi-block read itself failed: "
                              f"{describe_mismatch(completed.payload, pattern(MAX_READ))}")
            identity = net.identify()
            detail(f"{identity.data!r}, {len(identity.blocks)} block(s), "
                   f"status {identity.status_text!r}")
            if len(identity.blocks) != 1 or not identity.data.startswith(b"ULTIMATE"):
                raise Failure(f"NET_CMD_IDENTIFY replied {identity.data!r} in "
                              f"{len(identity.blocks)} block(s); a finished reply must leave "
                              f"nothing behind")
    finally:
        net.close(handle)
    return True


def run_tcp_lossless(net: Net, peer: Peer) -> bool:
    """The same command over TCP loses nothing, which is the control for UDP.

    read_socket() never looks at the socket type, so this runs the identical
    firmware path with the identical length. Anything UDP loses here is lost by
    the socket layer under it, not by the command interface or by the length.

    The second half reads a stream with a length above what one reply block can
    carry. read_socket() splits a reply over blocks whatever the socket type,
    so a stream read can now return more than one block's worth in a single
    command. A stream has no datagram boundaries, so how many bytes any one
    read returns is up to the socket; what the test fixes is that repeated
    reads recover every byte in order, that nothing is reported as truncated,
    and that no block exceeds what the transport can deliver.
    """
    section("tcp-read-is-lossless")
    ok = True
    handle = net.open_tcp(peer.ip, peer.tcp_port)
    detail(f"TCP socket handle {handle}")
    conn = None
    try:
        conn = peer.accept_tcp()
        conn.sendall(pattern(BIG_DATAGRAM))
        time.sleep(DELIVERY_SETTLE_SECONDS)
        chunks: list[ReadResult] = []
        recovered = bytearray()
        for _ in range(8):
            result = net.read(handle, SMALL_READ)
            chunks.append(result)
            if not result.payload:
                break
            recovered += result.payload
            if len(recovered) >= BIG_DATAGRAM:
                break
        with check(f"{BIG_DATAGRAM} bytes sent over TCP are recovered by reads of "
                   f"{SMALL_READ} bytes"):
            detail(" ".join(f"{len(c.payload)}@{run_offset(c.payload)}" for c in chunks))
            if bytes(recovered) != pattern(BIG_DATAGRAM):
                raise Failure(f"recovered {len(recovered)} of {BIG_DATAGRAM} bytes; a stream "
                              f"read that asks for less than is pending must lose nothing")

        with check("a stream read that asks for less than is pending still answers 00,OK"):
            # Every read above asked for less than the socket held, which is
            # ordinary on a stream and loses nothing. Nothing that reports
            # truncation on datagram sockets may report it here.
            unexpected = [r.status_text for r in chunks if r.payload and r.status_text != STATUS_OK]
            if unexpected:
                raise Failure(f"a stream read answered {unexpected!r}; only {STATUS_OK!r} "
                              f"is correct when no data was lost")

        conn.sendall(pattern(SPANNING_DATAGRAM, seed=3))
        time.sleep(DELIVERY_SETTLE_SECONDS)
        wide: list[ReadResult] = []
        stream = bytearray()
        for _ in range(8):
            result = net.read(handle, MAX_READ, single_part=False)
            wide.append(result)
            if not result.payload:
                break
            stream += result.payload
            if len(stream) >= SPANNING_DATAGRAM:
                break

        with check(f"{SPANNING_DATAGRAM} bytes sent over TCP are recovered by reads of "
                   f"{MAX_READ} bytes"):
            detail(" ".join(f"{len(r.payload)}@{run_offset(r.payload, seed=3)}"
                            f"{r.block_lengths}" for r in wide))
            if bytes(stream) != pattern(SPANNING_DATAGRAM, seed=3):
                raise Failure(f"recovered {len(stream)} of {SPANNING_DATAGRAM} bytes: "
                              f"{describe_mismatch(bytes(stream), pattern(SPANNING_DATAGRAM, seed=3))}")

        with check("a stream read above one block reports its own length and stays 00,OK"):
            # Without this, the case passes whether or not any read was large
            # enough to need a second block, and a broken split over TCP would
            # go unnoticed. lwip_recv_tcp fills the requested length from every
            # segment already queued, so after the settle above one read takes
            # the whole run: measured as blocks [895, 527] on both a U64 Elite
            # and a U2+L.
            if not any(len(r.payload) > FIRST_BLOCK_PAYLOAD for r in wide):
                raise Failure(
                    f"no read returned more than {FIRST_BLOCK_PAYLOAD} bytes, the most one "
                    f"block carries, so the stream never crossed a block boundary and this "
                    f"case tested nothing: reads returned "
                    f"{[len(r.payload) for r in wide]}")
            for result in wide:
                if not result.payload:
                    continue
                if result.header != len(result.payload):
                    raise Failure(f"a read reported {result.header} in its header while "
                                  f"carrying {len(result.payload)} payload bytes")
                if result.status_text != STATUS_OK:
                    raise Failure(f"a stream read answered {result.status_text!r}; a stream "
                                  f"has no datagram to truncate, so only {STATUS_OK!r} is "
                                  f"correct")
                oversized = [b for b in result.block_lengths if b > MAX_BLOCK_BYTES]
                if oversized:
                    raise Failure(f"blocks of {oversized} bytes were built; no block may "
                                  f"exceed {MAX_BLOCK_BYTES}")
    finally:
        if conn is not None:
            conn.close()
        net.close(handle)
    return ok


def run_oversize_request_keeps_datagram(net: Net, peer: Peer) -> bool:
    """A refused request must not consume the datagram it refused to deliver.

    #802's client asked for 1024 bytes and got nothing. If the refusal also
    threw the pending datagram away, the data would be gone as well as
    undelivered, and a client that lowered its length afterwards would still
    see nothing.
    """
    section("oversize-request-keeps-the-datagram")
    ok = True
    over_long = net.largest_accepted_length() + 1
    handle = net.open_udp(peer.ip, peer.udp_port)
    try:
        peer.learn_udp_peer(net, handle)
        net.drain_socket(handle)
        peer.send_udp(pattern(SMALL_READ))
        time.sleep(DELIVERY_SETTLE_SECONDS)

        refused = net.read(handle, over_long, overrun_reads=2)
        after = net.read(handle, SMALL_READ)

        with check(f"a request of {over_long} bytes is refused without touching the socket"):
            detail(f"refused read: {refused.describe()}")
            detail(f"next read:    {after.describe()}")
            if refused.status_text != STATUS_OUT_OF_RANGE:
                raise Failure(f"expected {STATUS_OUT_OF_RANGE!r}, got {refused.status_text!r}")
            if after.payload != pattern(SMALL_READ):
                raise Failure(
                    f"the datagram was pending when the over-long request was refused, and a "
                    f"following read of {SMALL_READ} bytes recovered {len(after.payload)} "
                    f"bytes instead of {SMALL_READ}: the refusal consumed it")
    finally:
        net.close(handle)
    return ok


def run_chunked_write_one_datagram(net: Net, peer: Peer) -> bool:
    """A payload larger than one command still leaves as a single datagram.

    WRITE_SOCKET sends whatever one command carries, and a command carries at
    most PLAIN_WRITE_MAX payload bytes, so a client with more than that to send
    has to issue two of them. Two writes are two datagrams, and a peer that
    expects one message receives two, in whatever order UDP hands them over and
    with either one free to go missing on its own.

    WRITE_SOCKET_CHUNK carries the payload over as many chunks as it takes and
    the firmware holds the bytes, sending once when the accumulation reaches the
    announced total. What leaves the device is then one datagram whatever the
    command interface's own limit is.

    Both properties are measured on the wire, because the command interface
    cannot see the difference: one datagram of 893 bytes and two of 888 and 5
    both answer with a count of 893.

    The payload is pattern(), so a wrong assembly is wrong data rather than only
    a wrong length: a second chunk placed at offset 0, or appended where the
    first chunk started, gives the right number of bytes with the wrong ones in
    them.
    """
    section("chunked-write-arrives-as-one-datagram")
    handle = net.open_udp(peer.ip, peer.udp_port)
    try:
        peer.learn_udp_peer(net, handle)

        # One chunk that is a whole payload, which is the case a plain
        # WRITE_SOCKET already handles. It has to stay indistinguishable from
        # one, so that a client can use this command for every write it makes
        # rather than choosing between two commands by size.
        small = pattern(SMALL_WRITE)
        whole = net.write_chunk(handle, 0, SMALL_WRITE, small)
        arrived = peer.collect_udp()
        with check(f"a {SMALL_WRITE}-byte payload that fits one chunk is sent like a plain "
                   f"write"):
            detail(f"status {whole.status_text!r}, reply {whole.data!r}, datagrams "
                   f"{[len(d) for d in arrived]}")
            require_implemented(whole, f"a {SMALL_WRITE}-byte one-chunk write")
            if whole.status_text != STATUS_OK:
                raise Failure(f"expected {STATUS_OK!r}, got {whole.status_text!r}")
            if send_count(whole) != SMALL_WRITE:
                raise Failure(f"the reply reports {send_count(whole)}; a chunk that starts at "
                              f"offset 0 and reaches its announced total is complete on its "
                              f"own, so it sends and answers with the count, which is "
                              f"{SMALL_WRITE}")
            if arrived != [small]:
                raise Failure(f"expected one datagram of {SMALL_WRITE} bytes: "
                              f"{describe_datagrams(arrived, small)}")

        payload = pattern(CHUNKED_WRITE)
        head, tail = payload[:MAX_CHUNK_PAYLOAD], payload[MAX_CHUNK_PAYLOAD:]

        first = net.write_chunk(handle, 0, CHUNKED_WRITE, head)
        early = peer.collect_udp()
        with check(f"the first {len(head)} bytes of a {CHUNKED_WRITE}-byte payload are taken "
                   f"without anything leaving the device"):
            detail(f"status {first.status_text!r}, reply {first.data!r}, datagrams "
                   f"{[len(d) for d in early]}")
            require_implemented(first, "the opening chunk of a chunked write")
            if first.status_text != STATUS_OK:
                raise Failure(f"expected {STATUS_OK!r}, got {first.status_text!r}")
            if first.data:
                raise Failure(f"a chunk that does not complete its payload replied "
                              f"{first.data!r}; nothing has been sent yet, so there is no "
                              f"count to report and the reply is empty")
            if early:
                raise Failure(
                    f"{len(early)} datagram(s) of {[len(d) for d in early]} bytes left the "
                    f"device after {len(head)} of {CHUNKED_WRITE} announced bytes. Sending "
                    f"per chunk is what the two plain writes this command replaces already "
                    f"did, and it is the whole of what #807 reports")

        last = net.write_chunk(handle, MAX_CHUNK_PAYLOAD, CHUNKED_WRITE, tail)
        arrived = peer.collect_udp()
        with check(f"a {CHUNKED_WRITE}-byte payload sent as {len(head)} + {len(tail)} arrives "
                   f"as one datagram"):
            detail(f"status {last.status_text!r}, reply {last.data!r}, datagrams "
                   f"{[len(d) for d in arrived]}")
            require_implemented(last, "the completing chunk of a chunked write")
            if last.status_text != STATUS_OK:
                raise Failure(f"expected {STATUS_OK!r}, got {last.status_text!r}")
            if send_count(last) != CHUNKED_WRITE:
                raise Failure(f"the completing chunk reports {send_count(last)} bytes sent "
                              f"rather than the {CHUNKED_WRITE} it announced; the count is "
                              f"for the whole payload, not for the chunk that finished it")
            if arrived != [payload]:
                raise Failure(
                    f"the payload did not arrive as one datagram of {CHUNKED_WRITE} bytes: "
                    f"{describe_datagrams(arrived, payload)}. {CHUNKED_WRITE} bytes is one "
                    f"more than the {PLAIN_WRITE_MAX} a plain WRITE_SOCKET can carry, so "
                    f"there is no way to send this message as one datagram other than this "
                    f"command")
    finally:
        net.close(handle)
    return True


def run_chunked_write_full_size_datagram(net: Net, peer: Peer) -> bool:
    """The largest payload this command can carry, assembled and sent.

    chunked-write-total-ceiling establishes that MAX_ANNOUNCED_TOTAL is
    accepted as an announcement, and chunked-write-arrives-as-one-datagram
    establishes that an accumulation assembles and leaves as one datagram.
    Neither completes a payload larger than CHUNKED_WRITE, so the largest
    datagram this command exists to produce is one the suite has never seen it
    produce.

    A firmware whose buffer is smaller than the total it accepts passes both of
    those cases. The announcement is answered before a byte of it is copied
    anywhere, and the CHUNKED_WRITE bytes the other case assembles fit any
    buffer worth having; what such a firmware does on the chunk that runs past
    the end of its buffer is not a status but whatever the memory after the
    buffer belonged to. Catching that needs a payload carried to its announced
    total, not one announced at it.

    That costs MAX_ANNOUNCED_TOTAL register writes per route, which is why the
    suite spends it once here rather than in every chunked case.

    The oracle is the one chunked-write-arrives-as-one-datagram uses, for the
    same reason: `arrived != [payload]` compares how many datagrams left and
    every byte of the one that did in a single comparison, so a payload that
    left as two, or as one of the right length with the second chunk placed at
    the wrong offset in it, fails the same check.
    """
    section("chunked-write-full-size-datagram")
    handle = net.open_udp(peer.ip, peer.udp_port)
    # MAX_ANNOUNCED_TOTAL is 888 + 584: the fewest chunks the largest payload
    # can be carried in, so what this case pays for is payload and not framing.
    # Unseeded, as in chunked-write-arrives-as-one-datagram, so that
    # describe_mismatch can name the offset a misplaced chunk was taken from.
    payload = pattern(MAX_ANNOUNCED_TOTAL)
    head, tail = payload[:MAX_CHUNK_PAYLOAD], payload[MAX_CHUNK_PAYLOAD:]
    try:
        peer.learn_udp_peer(net, handle)

        first = net.write_chunk(handle, 0, MAX_ANNOUNCED_TOTAL, head)
        early = peer.collect_udp()
        with check(f"the first {len(head)} bytes of a {MAX_ANNOUNCED_TOTAL}-byte payload are "
                   f"taken without anything leaving the device"):
            detail(f"status {first.status_text!r}, reply {first.data!r}, datagrams "
                   f"{[len(d) for d in early]}")
            require_implemented(first, f"the opening chunk of a {MAX_ANNOUNCED_TOTAL}-byte "
                                       f"payload")
            if first.status_text != STATUS_OK:
                raise Failure(f"expected {STATUS_OK!r}, got {first.status_text!r}")
            if first.data:
                raise Failure(f"a chunk that does not complete its payload replied "
                              f"{first.data!r}; nothing has been sent yet, so there is no "
                              f"count to report and the reply is empty")
            if early:
                raise Failure(
                    f"{len(early)} datagram(s) of {[len(d) for d in early]} bytes left the "
                    f"device after {len(head)} of {MAX_ANNOUNCED_TOTAL} announced bytes. The "
                    f"largest payload is sent once, when it is complete, like any other")

        last = net.write_chunk(handle, MAX_CHUNK_PAYLOAD, MAX_ANNOUNCED_TOTAL, tail)
        arrived = peer.collect_udp()
        with check(f"a {MAX_ANNOUNCED_TOTAL}-byte payload sent as {len(head)} + {len(tail)} "
                   f"arrives as one datagram"):
            detail(f"status {last.status_text!r}, reply {last.data!r}, datagrams "
                   f"{[len(d) for d in arrived]}")
            require_implemented(last, f"the completing chunk of a {MAX_ANNOUNCED_TOTAL}-byte "
                                      f"payload")
            if last.status_text != STATUS_OK:
                raise Failure(
                    f"expected {STATUS_OK!r}, got {last.status_text!r}: "
                    f"{MAX_ANNOUNCED_TOTAL} bytes is the largest unfragmented datagram and "
                    f"the announcement of it is accepted, so the bytes it announced have to "
                    f"be taken as well. A firmware that accepts the announcement and then "
                    f"refuses the payload offers a size it cannot deliver")
            if send_count(last) != MAX_ANNOUNCED_TOTAL:
                raise Failure(f"the completing chunk reports {send_count(last)} bytes sent "
                              f"rather than the {MAX_ANNOUNCED_TOTAL} it announced; a count "
                              f"short of the announcement is a payload that lost bytes on "
                              f"its way to the socket, whatever arrived on the wire")
            if arrived != [payload]:
                raise Failure(
                    f"the payload did not arrive as one datagram of {MAX_ANNOUNCED_TOTAL} "
                    f"bytes: {describe_datagrams(arrived, payload)}. This is the largest "
                    f"datagram the command can produce and the only one the suite carries "
                    f"to completion, so it is the only case that would show a buffer "
                    f"smaller than the total the firmware accepts")
    finally:
        net.close(handle)
    return True


def run_chunked_write_zero_total(net: Net, peer: Peer) -> bool:
    """A payload announced as no bytes at all is a payload, and it is sent.

    Zero is not a case the command has to reject. A chunk at offset 0
    announcing 0 bytes has reached its announced total the moment it arrives,
    so it opens the payload and completes it in the same command, and what
    leaves is an empty datagram.

    That is deliberately the same thing a plain WRITE_SOCKET already does with
    no data bytes: '$03 $11 <handle>' is a command whose payload length is 0,
    and it sends an empty datagram. Keeping the two commands the same is the
    point of this case. An empty datagram is a message rather than the absence
    of one, and a client that uses WRITE_SOCKET_CHUNK for every write it makes,
    which is what chunked-write-arrives-as-one-datagram's one-chunk check is
    about, must not have to keep the other command around for the one message
    this one cannot express.

    So the two are measured against each other rather than against a written
    expectation. The plain write runs first and establishes what this device
    does with no data bytes; the chunked write then has to agree with it in the
    status, in the count and on the wire.
    """
    section("chunked-write-zero-total")
    handle = net.open_udp(peer.ip, peer.udp_port)
    try:
        peer.learn_udp_peer(net, handle)

        plain = net.write(handle, b"")
        after_plain = peer.collect_udp()
        with check("a plain WRITE_SOCKET with no data bytes sends one empty datagram"):
            detail(f"status {plain.status_text!r}, reply {plain.data!r}, datagrams "
                   f"{[len(d) for d in after_plain]}")
            if plain.status_text != STATUS_OK:
                raise Failure(f"expected {STATUS_OK!r}, got {plain.status_text!r}")
            if send_count(plain) != 0:
                raise Failure(f"the reply reports {send_count(plain)}; the command carried "
                              f"no data bytes, so the count of bytes sent is 0")
            if after_plain != [b""]:
                raise Failure(f"expected one datagram of 0 bytes: "
                              f"{describe_datagrams(after_plain, b'')}. Without it there is "
                              f"no behaviour here for the chunked write to be consistent "
                              f"with")

        chunked = net.write_chunk(handle, 0, 0, b"")
        after_chunk = peer.collect_udp()
        with check("a chunked write announced as 0 bytes completes at once and sends the "
                   "same empty datagram"):
            detail(f"status {chunked.status_text!r}, reply {chunked.data!r}, datagrams "
                   f"{[len(d) for d in after_chunk]}")
            require_implemented(chunked, "a chunked write announced as 0 bytes")
            if chunked.status_text != STATUS_OK:
                raise Failure(
                    f"expected {STATUS_OK!r}, got {chunked.status_text!r}: a payload of 0 "
                    f"bytes has reached its announced total as it arrives, so this chunk "
                    f"opens it and completes it at once. The plain write above answered "
                    f"{plain.status_text!r} for the same empty message")
            if send_count(chunked) != 0:
                raise Failure(f"the reply reports {send_count(chunked)}; the payload was "
                              f"announced as 0 bytes and none were carried, so the count is "
                              f"0, which is what the plain write reported "
                              f"({send_count(plain)})")
            if after_chunk != [b""]:
                raise Failure(
                    f"expected one datagram of 0 bytes: "
                    f"{describe_datagrams(after_chunk, b'')}. A firmware that holds a 0-byte "
                    f"payload instead of sending it leaves an accumulation behind that no "
                    f"chunk can ever complete, and the empty datagram the plain write above "
                    f"put on the wire becomes a message this command cannot send")
    finally:
        net.close(handle)
    return True


def run_chunked_write_total_ceiling(net: Net, peer: Peer) -> bool:
    """How large a payload a chunked write may announce.

    The payload leaves as one datagram, so the most it can ever be is one
    unfragmented datagram: MAX_ANNOUNCED_TOTAL, the same 1472 that bounds what
    can arrive in the other direction. An announcement above it names a payload
    the device could never send, and refusing it at the announcement is what
    saves the client from spending a chunk's worth of register writes on a
    payload that was doomed from its first byte.

    Both sides of the boundary are measured. A firmware that refuses the
    boundary itself passes any check that only tries to break it, and the
    difference between the two is one character in the firmware.

    Only the announcement is exercised here, not a payload of that size: the
    accepted case opens a payload of MAX_ANNOUNCED_TOTAL bytes and hands over
    the first few, which is what the boundary is about. Carrying all
    MAX_ANNOUNCED_TOTAL of them to completion would cost that many register
    writes per route to establish assembly, which
    chunked-write-arrives-as-one-datagram already establishes for a third of
    the price.
    """
    section("chunked-write-total-ceiling")
    handle = net.open_udp(peer.ip, peer.udp_port)
    opening = SMALL_WRITE // 2
    over = MAX_ANNOUNCED_TOTAL + 1
    try:
        peer.learn_udp_peer(net, handle)

        refused = net.write_chunk(handle, 0, over, pattern(opening))
        after_refusal = peer.collect_udp()
        with check(f"a payload announced as {over} bytes is refused, and sends nothing"):
            detail(f"status {refused.status_text!r}, reply {refused.data!r}, datagrams "
                   f"{[len(d) for d in after_refusal]}")
            require_implemented(refused, f"a chunked write announced as {over} bytes")
            if refused.status_text != STATUS_OUT_OF_RANGE:
                raise Failure(
                    f"expected {STATUS_OUT_OF_RANGE!r}, got {refused.status_text!r}: "
                    f"{MAX_ANNOUNCED_TOTAL} bytes is the largest unfragmented datagram, and "
                    f"the payload leaves as one datagram, so a larger announcement names "
                    f"something that could never be sent")
            if refused.data:
                raise Failure(f"a refused announcement replied {refused.data!r}; it sent "
                              f"nothing, so it has no count to report")
            if after_refusal:
                raise Failure(f"{len(after_refusal)} datagram(s) of "
                              f"{[len(d) for d in after_refusal]} bytes left the device on an "
                              f"announcement that was refused")

        accepted = net.write_chunk(handle, 0, MAX_ANNOUNCED_TOTAL, pattern(opening, seed=7))
        after_accept = peer.collect_udp()
        with check(f"a payload announced as {MAX_ANNOUNCED_TOTAL} bytes is accepted"):
            detail(f"status {accepted.status_text!r}, reply {accepted.data!r}, datagrams "
                   f"{[len(d) for d in after_accept]}")
            require_implemented(accepted, f"a chunked write announced as "
                                          f"{MAX_ANNOUNCED_TOTAL} bytes")
            if accepted.status_text != STATUS_OK:
                raise Failure(
                    f"expected {STATUS_OK!r}, got {accepted.status_text!r}: "
                    f"{MAX_ANNOUNCED_TOTAL} bytes is a datagram the device can send, so it "
                    f"is a payload a client may announce. A firmware that refuses it has "
                    f"the comparison one off, and the largest message this command can "
                    f"carry is then {MAX_ANNOUNCED_TOTAL - 1}")
            if accepted.data:
                raise Failure(f"a chunk carrying {opening} of {MAX_ANNOUNCED_TOTAL} announced "
                              f"bytes replied {accepted.data!r}; the payload is not complete, "
                              f"so nothing was sent and there is no count to report")
            if after_accept:
                raise Failure(f"{len(after_accept)} datagram(s) of "
                              f"{[len(d) for d in after_accept]} bytes left the device on a "
                              f"chunk that carried {opening} of {MAX_ANNOUNCED_TOTAL} bytes")

        # That accepted announcement is still in progress. A chunk at offset 0
        # would restart it anyway, but nothing after this belongs to it.
        net.identify()
    finally:
        net.close(handle)
    return True


def run_chunked_write_refuses_unowned_socket(net: Net, peer: Peer) -> bool:
    """A chunked write to a socket this client never opened puts nothing on the wire.

    The target owns the sockets it opened and refuses a write to any other
    handle, which is what stops a program from writing to a socket that belongs
    to the firmware or to a program that ran before it. Both write commands
    send from the same place, so a chunked write meets that check as a plain
    one does, but it meets it only at the end: the handle is carried by every
    chunk and is a handle to send to only once the accumulation is complete.

    Checking it there rather than at the announcement is the behaviour under
    test and not an accident of it. A payload announced for a socket that is
    open can have that socket closed while the accumulation is in progress, by
    a CLOSE_SOCKET or by a reset, so the check at the send is the one that has
    to be right; one at the announcement could only ever agree with it or be
    wrong.

    What must not happen is bytes on the wire. The accumulation here runs to
    completion holding a handle that names nothing this target owns, and a
    firmware that sends before it checks, or that checks the handle against
    lwip rather than against what this target owns, sends a datagram on a
    socket its client never opened.

    NEVER_OPENED is the handle no OPEN command ever returned, so it is not one
    this target owns. That premise is measured rather than assumed: the plain
    WRITE_SOCKET below is refused for it first, which also fixes on this device
    rather than from the document which refusal the chunked write has to give.
    """
    section("chunked-write-refuses-unowned-socket")
    handle = net.open_udp(peer.ip, peer.udp_port)
    half = SMALL_WRITE // 2
    payload = pattern(SMALL_WRITE, seed=29)
    try:
        # The peer's address is learned over a socket this target does own.
        # Nothing below writes to that socket, and it is where a datagram sent
        # for the unowned handle would arrive if one were sent at all.
        peer.learn_udp_peer(net, handle)

        plain = net.write(NEVER_OPENED, payload)
        after_plain = peer.collect_udp()
        with check(f"a plain WRITE_SOCKET to handle {NEVER_OPENED} is refused, and sends "
                   f"nothing"):
            detail(f"status {plain.status_text!r}, reply {plain.data!r}, datagrams "
                   f"{[len(d) for d in after_plain]}")
            if plain.status_text != STATUS_SEND_ERROR_EBADF:
                raise Failure(
                    f"expected {STATUS_SEND_ERROR_EBADF!r}, got {plain.status_text!r}: "
                    f"handle {NEVER_OPENED} was never returned by an OPEN command, so it is "
                    f"not a socket this target owns. Without that refusal there is no "
                    f"unowned handle for the chunked write below to name, and the case "
                    f"measures nothing")
            require_nothing_sent(after_plain, f"a plain write to handle {NEVER_OPENED}")

        opening = net.write_chunk(NEVER_OPENED, 0, SMALL_WRITE, payload[:half])
        premature = peer.collect_udp()
        with check(f"a payload announced for handle {NEVER_OPENED} is taken while it is "
                   f"still incomplete"):
            detail(f"status {opening.status_text!r}, reply {opening.data!r}, datagrams "
                   f"{[len(d) for d in premature]}")
            require_implemented(opening, f"a chunked write announced for handle "
                                         f"{NEVER_OPENED}")
            if opening.status_text != STATUS_OK:
                raise Failure(
                    f"expected {STATUS_OK!r}, got {opening.status_text!r}: the handle is a "
                    f"socket to send to only where the payload is sent, which this chunk "
                    f"does not reach. A firmware that refuses here has a second ownership "
                    f"check, and the answer it gives cannot survive the socket being closed "
                    f"while the accumulation is in progress")
            if opening.data:
                raise Failure(f"a chunk that does not complete its payload replied "
                              f"{opening.data!r}; nothing has been sent, so there is no "
                              f"count to report")
            require_nothing_sent(premature, f"a chunk that carried {half} of {SMALL_WRITE} "
                                            f"announced bytes for handle {NEVER_OPENED}")

        completing = net.write_chunk(NEVER_OPENED, half, SMALL_WRITE, payload[half:])
        after = peer.collect_udp()
        with check(f"the completing chunk of a payload for handle {NEVER_OPENED} is refused, "
                   f"and sends nothing"):
            detail(f"status {completing.status_text!r}, reply {completing.data!r}, "
                   f"datagrams {[len(d) for d in after]}")
            require_implemented(completing, f"the completing chunk of a chunked write for "
                                            f"handle {NEVER_OPENED}")
            if completing.status_text != STATUS_SEND_ERROR_EBADF:
                raise Failure(
                    f"expected {STATUS_SEND_ERROR_EBADF!r}, got {completing.status_text!r}: "
                    f"the payload is complete and the handle it names is not one this "
                    f"target owns, so it is refused where it would have been sent, exactly "
                    f"as the plain write of the same bytes was ({plain.status_text!r}). A "
                    f"firmware that answers {STATUS_OK.decode()} here reports a send that "
                    f"never happened, and one that answers "
                    f"{STATUS_INVALID_PARAMS.decode()} reports the chunk as malformed when "
                    f"every field in it agrees with the payload in progress")
            if send_count(completing) != send_count(plain):
                raise Failure(
                    f"the refused chunked write reports {send_count(completing)} and the "
                    f"refused plain write reports {send_count(plain)}; both are the same "
                    f"failed send to the same handle, so a client that reads the count "
                    f"cannot be told from it which command it used")
            require_nothing_sent(after, f"a payload completed for handle {NEVER_OPENED}")
    finally:
        net.close(handle)
    return True


def run_chunked_write_refuses_bad_chunks(net: Net, peer: Peer) -> bool:
    """A partial payload must never turn into a datagram by accident.

    The command holds bytes between commands, which is state a client can leave
    in any shape: it can contradict the offset it reached, contradict what it
    announced, name a different socket half way through, hand over more bytes
    than it announced, or simply walk away. None of those may put part of a
    payload on the wire, and none may leave bytes behind for a later write to
    carry.

    Each case is refused on the command interface and confirmed on the wire,
    because the two are separate failures: a status of 81 with a datagram
    behind it is worse than no status at all.

    Every bad chunk here is one that a firmware which does not check the field
    under test would complete, so the accumulation reaches its announced total
    and a datagram leaves. That is what makes these checks able to fail: a
    chunk that could not have completed either way would be refused by a
    firmware that checks nothing at all.

    The chunks are short on both sides of the split. Nothing requires a
    non-final chunk to be full, and a client streaming a payload as it produces
    it will not fill one, so an accumulation is opened with half of a short
    payload: the same state in progress for a fraction of the register writes a
    full chunk would cost.
    """
    section("chunked-write-refuses-bad-chunks")
    handle = net.open_udp(peer.ip, peer.udp_port)
    half = SMALL_WRITE // 2

    def open_partial(seed: int):
        """Half a payload announced, so the next chunk meets one in progress.

        A chunk at offset 0 opens a payload whatever was in progress before it,
        so every case starts from the same state without depending on how the
        case before it ended. Each case uses its own seed, so a datagram that
        arrives late names the case it came from.
        """
        started = net.write_chunk(handle, 0, SMALL_WRITE, pattern(SMALL_WRITE, seed)[:half])
        return started, peer.collect_udp()

    try:
        peer.learn_udp_peer(net, handle)

        # A chunk that starts where the accumulation is not. The payload it
        # carries is the right length for the payload in progress, so a
        # firmware that appends without looking at the offset completes it and
        # sends, which is the assembly this case exists to rule out.
        started, premature = open_partial(seed=1)
        stray = half // 2
        wrong_offset = net.write_chunk(handle, stray, SMALL_WRITE,
                                       pattern(SMALL_WRITE, 1)[half:])
        after_offset = peer.collect_udp()
        with check(f"a chunk claiming offset {stray} of a payload that has reached {half} is "
                   f"refused, and sends nothing"):
            detail(f"status {wrong_offset.status_text!r}, reply {wrong_offset.data!r}, "
                   f"datagrams {[len(d) for d in after_offset]}")
            require_payload_open(started, premature, half, SMALL_WRITE)
            if wrong_offset.status_text != STATUS_INVALID_PARAMS:
                raise Failure(
                    f"expected {STATUS_INVALID_PARAMS!r}, got {wrong_offset.status_text!r}: "
                    f"{half} bytes are held and the next chunk of this payload starts at "
                    f"{half}, so a chunk claiming {stray} either repeats bytes already held "
                    f"or is a chunk of some other payload. A firmware that appends it "
                    f"regardless reaches {SMALL_WRITE} bytes and sends a datagram whose "
                    f"middle is whichever copy landed last")
            require_nothing_sent(after_offset, "a chunk that named the wrong offset")

        # The refusal above also has to end the payload it refused. A client
        # that gets an 81 has lost track of what the device holds, and bytes
        # left behind become the head of whatever it announces next.
        resumed = net.write_chunk(handle, half, SMALL_WRITE, pattern(SMALL_WRITE, 1)[half:])
        after_resume = peer.collect_udp()
        with check("a refused chunk ends the payload it was refused from"):
            detail(f"status {resumed.status_text!r}, reply {resumed.data!r}, datagrams "
                   f"{[len(d) for d in after_resume]}")
            if resumed.status_text != STATUS_INVALID_PARAMS:
                raise Failure(
                    f"the chunk that would have completed the payload answered "
                    f"{resumed.status_text!r}, expected {STATUS_INVALID_PARAMS!r}: the chunk "
                    f"before it was refused, which ends the accumulation, so there is no "
                    f"longer a payload in progress at offset {half} for this one to continue")
            require_nothing_sent(after_resume, "a chunk continuing a payload that a refusal "
                                               "should have ended")

        # The total is announced once and every chunk repeats it. A chunk that
        # disagrees leaves the firmware with two lengths and no way to know
        # which of them the client meant.
        started, premature = open_partial(seed=2)
        wrong_total = net.write_chunk(handle, half, SMALL_WRITE + 1,
                                      pattern(SMALL_WRITE, 2)[half:])
        after_total = peer.collect_udp()
        with check(f"a chunk announcing {SMALL_WRITE + 1} bytes into a payload announced as "
                   f"{SMALL_WRITE} is refused, and sends nothing"):
            detail(f"status {wrong_total.status_text!r}, reply {wrong_total.data!r}, "
                   f"datagrams {[len(d) for d in after_total]}")
            require_payload_open(started, premature, half, SMALL_WRITE)
            if wrong_total.status_text != STATUS_INVALID_PARAMS:
                raise Failure(
                    f"expected {STATUS_INVALID_PARAMS!r}, got {wrong_total.status_text!r}: "
                    f"the payload was announced as {SMALL_WRITE} bytes and this chunk calls "
                    f"it {SMALL_WRITE + 1}. A firmware that takes the bytes anyway completes "
                    f"the payload it already had and sends {SMALL_WRITE} bytes of a message "
                    f"the client says is {SMALL_WRITE + 1} bytes long")
            require_nothing_sent(after_total, "a chunk that named the wrong total")

        # A payload belongs to the socket it was announced for. The other
        # handle is a socket that is open and usable, so what separates it from
        # the right one is only that it is not the one the payload was
        # announced for; a handle that was never opened would leave a firmware
        # free to refuse it for being closed rather than for being the wrong
        # one, and the two refusals are not the same property.
        other = net.open_udp(peer.ip, peer.udp_port)
        try:
            started, premature = open_partial(seed=3)
            wrong_handle = net.write_chunk(other, half, SMALL_WRITE,
                                           pattern(SMALL_WRITE, 3)[half:])
            after_handle = peer.collect_udp()
            with check("a chunk naming another open socket while a payload is in progress "
                       "is refused, and sends nothing"):
                detail(f"payload opened on socket {handle}, continued on socket {other}: "
                       f"status {wrong_handle.status_text!r}, reply {wrong_handle.data!r}, "
                       f"datagrams {[len(d) for d in after_handle]}")
                require_payload_open(started, premature, half, SMALL_WRITE)
                if wrong_handle.status_text != STATUS_INVALID_PARAMS:
                    raise Failure(
                        f"expected {STATUS_INVALID_PARAMS!r}, got "
                        f"{wrong_handle.status_text!r}: the payload was announced for socket "
                        f"{handle}, so a chunk for socket {other} belongs to a payload that "
                        f"was never announced. A firmware that takes it completes the "
                        f"payload and sends it, to one socket or the other, and either is a "
                        f"message on a connection its client never wrote to")
                require_nothing_sent(after_handle, "a chunk that named a different socket")
        finally:
            net.close(other)

        # More bytes than the announcement leaves room for.
        started, premature = open_partial(seed=4)
        overrun = net.write_chunk(handle, half, SMALL_WRITE, pattern(SMALL_WRITE, 4))
        after_overrun = peer.collect_udp()
        with check(f"a chunk of {SMALL_WRITE} bytes at offset {half} of a {SMALL_WRITE}-byte "
                   f"payload is refused, and sends nothing"):
            detail(f"status {overrun.status_text!r}, reply {overrun.data!r}, datagrams "
                   f"{[len(d) for d in after_overrun]}")
            detail(f"this chunk ends {half} bytes past the announcement, which says the "
                   f"guard is applied rather than that {half} bytes matter. The overrun a "
                   f"client can actually reach is larger: a payload announced as "
                   f"{MAX_ANNOUNCED_TOTAL} accepts an offset of {MAX_ANNOUNCED_TOTAL - 1}, "
                   f"and a full {MAX_CHUNK_PAYLOAD}-byte chunk there ends "
                   f"{MAX_ANNOUNCED_TOTAL - 1 + MAX_CHUNK_PAYLOAD - MAX_ANNOUNCED_TOTAL} "
                   f"bytes past the total the payload was sized by. Reaching that offset "
                   f"costs {MAX_ANNOUNCED_TOTAL - 1} register writes of setup per route, "
                   f"which this suite does not spend")
            require_payload_open(started, premature, half, SMALL_WRITE)
            if overrun.status_text != STATUS_INVALID_PARAMS:
                raise Failure(
                    f"expected {STATUS_INVALID_PARAMS!r}, got {overrun.status_text!r}: "
                    f"{half} bytes are held and this chunk carries {SMALL_WRITE} more, which "
                    f"would put {half + SMALL_WRITE} bytes into a payload the client "
                    f"announced as {SMALL_WRITE}. The announcement is the only size the "
                    f"firmware has, so the bytes past it have nowhere of their own to go")
            require_nothing_sent(after_overrun, "a chunk that ran past its announced total")

        # A client can also simply stop. Any other command means the payload in
        # progress has no one to finish it, so it ends there: the completing
        # chunk that follows continues a payload that no longer exists, and the
        # bytes it carries belong to no datagram.
        #
        # An abandoned payload that survives is the state that turns into a
        # message nobody wrote: the next chunked write on this socket finds
        # bytes already in place and sends them ahead of its own.
        started, premature = open_partial(seed=5)
        net.identify()
        abandoned = net.write_chunk(handle, half, SMALL_WRITE, pattern(SMALL_WRITE, 5)[half:])
        after_abandoned = peer.collect_udp()
        with check("a payload is abandoned by any command that is not its next chunk"):
            detail(f"{half} of {SMALL_WRITE} bytes taken, then NET_CMD_IDENTIFY, then the "
                   f"chunk that would have completed it: status {abandoned.status_text!r}, "
                   f"reply {abandoned.data!r}, datagrams "
                   f"{[len(d) for d in after_abandoned]}")
            require_payload_open(started, premature, half, SMALL_WRITE)
            if abandoned.status_text != STATUS_INVALID_PARAMS:
                raise Failure(
                    f"expected {STATUS_INVALID_PARAMS!r}, got {abandoned.status_text!r}: an "
                    f"IDENTIFY came between the two chunks, which ends the payload, so this "
                    f"chunk continues one that no longer exists. A firmware that answers "
                    f"{STATUS_OK.decode()} here held those {half} bytes across a command "
                    f"that had nothing to do with them, and holds them across any number of "
                    f"commands and any length of time")
            require_nothing_sent(after_abandoned, "a chunk continuing a payload that an "
                                                  "intervening command should have ended")
    finally:
        net.close(handle)
    return True


def run_chunked_write_refuses_offset_ahead(net: Net, peer: Peer) -> bool:
    """A chunk placed past the accumulation point would send memory nobody wrote.

    chunked-write-refuses-bad-chunks measures a chunk claiming an offset behind
    where the payload has reached, which is the direction a client reaches by
    resending. This is the other direction, and the two are not the same
    property: a firmware whose continuation test is 'offset >= write_offset'
    rather than 'offset == write_offset' refuses every chunk that case presents
    and takes this one. Only the equality refuses both.

    What it would take is a hole. The bytes between where the accumulation
    stopped and where this chunk claims to start are written by no chunk of
    this payload, and they are sent all the same once the total is reached.
    They are whatever the target's buffer held before, and that buffer is the
    one READ_SOCKET reads datagrams into, so the hole is as likely to be the
    last datagram some other peer sent this device as it is to be memory that
    was never written at all. Either way it leaves the device on the wire,
    inside a message whose client believes it wrote every byte.

    Both sides of the boundary are measured, as chunked-write-total-ceiling
    does it and the bad-chunk cases do not. An offset one past the accumulation
    point is refused, and the accumulation point itself is taken and completes.
    A case that only tries to break the comparison cannot tell a firmware that
    refuses the right thing from one that refuses everything, and a firmware
    that refused the equal offset as well would leave no way to continue a
    payload at all.

    The refused chunk is one that a firmware which does not check the offset
    would complete: it carries exactly the bytes that reach the announced total
    from the offset it claims, so the payload completes and the datagram with
    the hole in it leaves. That is what makes this check able to fail.
    """
    section("chunked-write-refuses-offset-ahead")
    handle = net.open_udp(peer.ip, peer.udp_port)
    half = SMALL_WRITE // 2
    # One byte past where the payload has reached: the smallest hole there is,
    # so what the check measures is the comparison rather than the size of the
    # gap. The chunk carries payload[ahead:], which is what a firmware that
    # took it would need to reach SMALL_WRITE and send.
    ahead = half + 1
    payload = pattern(SMALL_WRITE, seed=31)
    # The payload that has to assemble afterwards. A seed of its own, so a
    # datagram carrying the first one's bytes is reported as the wrong payload
    # rather than as the right one.
    fresh = pattern(SMALL_WRITE, seed=37)
    try:
        peer.learn_udp_peer(net, handle)

        started = net.write_chunk(handle, 0, SMALL_WRITE, payload[:half])
        premature = peer.collect_udp()
        skipped = net.write_chunk(handle, ahead, SMALL_WRITE, payload[ahead:])
        after_skip = peer.collect_udp()
        with check(f"a chunk claiming offset {ahead} of a payload that has reached {half} is "
                   f"refused, and sends nothing"):
            detail(f"status {skipped.status_text!r}, reply {skipped.data!r}, datagrams "
                   f"{[len(d) for d in after_skip]}")
            require_payload_open(started, premature, half, SMALL_WRITE)
            if skipped.status_text != STATUS_INVALID_PARAMS:
                raise Failure(
                    f"expected {STATUS_INVALID_PARAMS!r}, got {skipped.status_text!r}: "
                    f"{half} bytes are held and the next chunk of this payload starts at "
                    f"{half}, so a chunk claiming {ahead} leaves byte {half} written by no "
                    f"chunk at all. A firmware that takes it anyway reaches {SMALL_WRITE} "
                    f"bytes, completes, and sends a datagram carrying the target's own "
                    f"buffer in that hole. That buffer is where READ_SOCKET reads datagrams "
                    f"into, so what leaves is memory this client never wrote and may never "
                    f"have been entitled to")
            require_nothing_sent(after_skip, f"a chunk that started {ahead - half} byte(s) "
                                             f"past the accumulation")

        # The refusal has to end the payload as well, exactly as the refusals
        # in chunked-write-refuses-bad-chunks do. Bytes left behind are the
        # head of whatever the client announces next, and this client has just
        # been told its payload is in a state it cannot reason about.
        resumed = net.write_chunk(handle, half, SMALL_WRITE, payload[half:])
        after_resume = peer.collect_udp()
        with check("a chunk refused for starting ahead ends the payload it was refused from"):
            detail(f"status {resumed.status_text!r}, reply {resumed.data!r}, datagrams "
                   f"{[len(d) for d in after_resume]}")
            if resumed.status_text != STATUS_INVALID_PARAMS:
                raise Failure(
                    f"the chunk that would have completed the payload answered "
                    f"{resumed.status_text!r}, expected {STATUS_INVALID_PARAMS!r}: the chunk "
                    f"before it was refused, which ends the accumulation, so there is no "
                    f"payload in progress at offset {half} for this one to continue")
            require_nothing_sent(after_resume, "a chunk continuing a payload that a refusal "
                                               "should have ended")

        # The other side of the boundary, and the proof that the refusals above
        # left the target able to take a payload: a chunk at exactly the offset
        # the accumulation has reached is taken, completes, and arrives whole.
        opened = net.write_chunk(handle, 0, SMALL_WRITE, fresh[:half])
        opened_early = peer.collect_udp()
        completing = net.write_chunk(handle, half, SMALL_WRITE, fresh[half:])
        arrived = peer.collect_udp()
        with check("a chunk at exactly the offset the accumulation reached completes the "
                   "payload, and it arrives as one datagram"):
            detail(f"status {completing.status_text!r}, reply {completing.data!r}, datagrams "
                   f"{[len(d) for d in arrived]}")
            require_payload_open(opened, opened_early, half, SMALL_WRITE)
            if completing.status_text != STATUS_OK:
                raise Failure(
                    f"expected {STATUS_OK!r}, got {completing.status_text!r}: {half} bytes "
                    f"are held and this chunk starts at {half}, which is the one offset a "
                    f"continuation may claim. A firmware that refuses it has the comparison "
                    f"one off in the other direction, and no payload of more than one chunk "
                    f"can ever be sent")
            if send_count(completing) != SMALL_WRITE:
                raise Failure(f"the completing chunk reports {send_count(completing)} rather "
                              f"than {SMALL_WRITE}")
            if arrived != [fresh]:
                raise Failure(
                    f"expected one datagram of {SMALL_WRITE} bytes: "
                    f"{describe_datagrams(arrived, fresh)}. The two refusals above have to "
                    f"leave the target able to take the next payload, and every byte of "
                    f"this one has to come from a chunk: a hole where the refused chunk "
                    f"would have put its bytes shows up here as the wrong bytes rather than "
                    f"as a wrong length")
    finally:
        net.close(handle)
    return True


def run_chunked_write_refuses_short_commands(net: Net, peer: Peer) -> bool:
    """A chunk command too short to carry its own header is refused.

    Every field a chunk is read by lives in its first seven bytes: the target,
    the command, the handle, the offset it starts at and the total it belongs
    to. A command shorter than that names none of them, and the payload length
    the firmware works out from it is negative.

    That negative length is why this is measured rather than reviewed. The
    offset and the total are read from bytes the FPGA never wrote for this
    command: the command pointer goes back to the base of the command SRAM
    between commands, but the SRAM itself is not cleared, so those bytes are
    whatever the command before left there. Each short command here follows an
    opening chunk, so the stale bytes read back as an offset of 0 and a total
    the firmware accepts, and a firmware without the guard therefore opens a
    payload and reaches its copy with a length of -4 rather than refusing
    anything.

    What that looks like from here is worth stating, because it is not a wrong
    status. A copy of that length on a machine with no MMU does not return to
    answer anything, so this case fails by the interface never coming back
    rather than by reporting something else. Against firmware that has no
    chunk command at all it reports that instead, which is what it does on the
    unpatched build. Neither run demonstrates the guard rejecting a short
    command cleanly; what they establish is that the guard is present and that
    the header is there to read. That is the precondition the rest of the chunked cases rest on:
    chunked-write-refuses-bad-chunks measures what the offset, total and handle
    checks do, and all three are reached only once the header is known to be
    there to read.

    The refusal has to end the payload in progress as well. A short command is
    still a chunk command, so it is not one of the commands that end a payload
    on their way in, and the guard has to do it itself.

    Seven bytes is the other side of the boundary: a chunk that carries no
    payload, which is a client opening a payload and handing over none of it
    yet, and it has to be taken.
    """
    section("chunked-write-refuses-short-commands")
    handle = net.open_udp(peer.ip, peer.udp_port)
    half = SMALL_WRITE // 2
    payload = pattern(SMALL_WRITE, seed=9)
    # The seven bytes every chunk starts with, as a chunk that opens a payload
    # and carries none of it. The short commands below are its head, so each is
    # this same command with its header cut off at a different point rather than
    # a shape no client would ever produce.
    header = net.write_chunk_command(handle, 0, SMALL_WRITE, b"")
    truncated, almost = header[:3], header[:6]
    try:
        peer.learn_udp_peer(net, handle)

        started = net.write_chunk(handle, 0, SMALL_WRITE, payload[:half])
        premature = peer.collect_udp()
        cut = net.uci.transact(truncated)
        after_cut = peer.collect_udp()
        with check(f"a chunk command of {len(truncated)} bytes is refused, and sends nothing"):
            detail(f"{truncated.hex(' ')}: status {cut.status_text!r}, reply {cut.data!r}, "
                   f"datagrams {[len(d) for d in after_cut]}")
            require_payload_open(started, premature, half, SMALL_WRITE)
            require_implemented(cut, f"a chunk command of {len(truncated)} bytes")
            if cut.status_text != STATUS_INVALID_PARAMS:
                raise Failure(
                    f"expected {STATUS_INVALID_PARAMS!r}, got {cut.status_text!r}: this "
                    f"command carries the handle and nothing else, so the offset and the "
                    f"total it would be read by are four bytes the FPGA never wrote, and "
                    f"the payload length is {len(truncated)} - 7 = {len(truncated) - 7}. "
                    f"The chunk before it announced offset 0, so a firmware that does not "
                    f"measure the command reads that back as its own offset, opens a "
                    f"payload, and copies {len(truncated) - 7} bytes as a length no "
                    f"comparison against the announced total can reject")
            if cut.data:
                raise Failure(f"a refused command replied {cut.data!r}; it sent nothing, so "
                              f"it has no count to report")
            require_nothing_sent(after_cut, f"a chunk command of {len(truncated)} bytes")

        completing = net.write_chunk(handle, half, SMALL_WRITE, payload[half:])
        after_completing = peer.collect_udp()
        with check("a short chunk command ends the payload that was in progress"):
            detail(f"status {completing.status_text!r}, reply {completing.data!r}, "
                   f"datagrams {[len(d) for d in after_completing]}")
            if completing.status_text != STATUS_INVALID_PARAMS:
                raise Failure(
                    f"the chunk that would have completed the payload answered "
                    f"{completing.status_text!r}, expected {STATUS_INVALID_PARAMS!r}: the "
                    f"short command before it was refused, and a refusal ends the "
                    f"accumulation, so there is no payload in progress at offset {half} for "
                    f"this chunk to continue. A short command is a chunk command, so it is "
                    f"not one of the commands that end a payload on their way in and the "
                    f"guard has to end it itself")
            require_nothing_sent(after_completing, "a chunk continuing a payload that a "
                                                   "short command should have ended")

        whole = net.write_chunk(handle, 0, SMALL_WRITE, payload)
        arrived = peer.collect_udp()
        with check("a complete payload still assembles after a short command"):
            detail(f"status {whole.status_text!r}, reply {whole.data!r}, datagrams "
                   f"{[len(d) for d in arrived]}")
            if whole.status_text != STATUS_OK:
                raise Failure(f"expected {STATUS_OK!r}, got {whole.status_text!r}; the short "
                              f"command was refused, which must leave the target able to "
                              f"take the next payload")
            if send_count(whole) != SMALL_WRITE:
                raise Failure(f"the reply reports {send_count(whole)} rather than "
                              f"{SMALL_WRITE}")
            if arrived != [payload]:
                raise Failure(f"expected one datagram of {SMALL_WRITE} bytes: "
                              f"{describe_datagrams(arrived, payload)}")

        # The two sides of the boundary. Six bytes is one short of the header,
        # so the total it would be read by is half written by this command and
        # half left over from the one before; seven is the header exactly.
        clipped = net.uci.transact(almost)
        after_clipped = peer.collect_udp()
        with check(f"a chunk command of {len(almost)} bytes is refused, and sends nothing"):
            detail(f"{almost.hex(' ')}: status {clipped.status_text!r}, reply "
                   f"{clipped.data!r}, datagrams {[len(d) for d in after_clipped]}")
            if clipped.status_text != STATUS_INVALID_PARAMS:
                raise Failure(
                    f"expected {STATUS_INVALID_PARAMS!r}, got {clipped.status_text!r}: the "
                    f"header is seven bytes and this command is {len(almost)}, so the top "
                    f"byte of the total is one the FPGA never wrote and the payload length "
                    f"is {len(almost) - 7}")
            require_nothing_sent(after_clipped, f"a chunk command of {len(almost)} bytes")

        opening = net.uci.transact(header)
        after_opening = peer.collect_udp()
        with check(f"a chunk command of exactly {len(header)} bytes is taken"):
            detail(f"{header.hex(' ')}: status {opening.status_text!r}, reply "
                   f"{opening.data!r}, datagrams {[len(d) for d in after_opening]}")
            if opening.status_text != STATUS_OK:
                raise Failure(
                    f"expected {STATUS_OK!r}, got {opening.status_text!r}: {len(header)} "
                    f"bytes is the header and no payload, which is a client announcing a "
                    f"payload before it has produced any of it. Every field the chunk is "
                    f"read by is present, so there is nothing short about it, and a "
                    f"firmware that refuses it has the comparison one off")
            if opening.data:
                raise Failure(f"a chunk that carried none of its {SMALL_WRITE} announced "
                              f"bytes replied {opening.data!r}; nothing was sent, so there "
                              f"is no count to report")
            require_nothing_sent(after_opening, f"a chunk carrying 0 of {SMALL_WRITE} "
                                                f"announced bytes")
    finally:
        # Ends the payload that last chunk announced, along with the socket.
        net.close(handle)
    return True


def run_chunked_write_discarded_by_abort(net: Net, peer: Peer) -> bool:
    """A payload part way through does not survive an abort.

    The abort bit is how a client walks away from an exchange it no longer
    wants, and the target is told about it so that it can let go of whatever it
    was holding for that client. A reply spanning blocks is one such thing, and
    multi-block-state-does-not-leak covers it. A payload part way through is the
    other, and it is the one that turns into a message nobody wrote: the client
    that comes back announces its own payload and finds bytes already in place.

    The abort has to arrive with no command of its own in front of it, or it
    measures nothing. Every command that is not a chunk already ends a payload
    in progress on its way in, so an abort taken after a read would be that
    discard rather than this one. The command the abort abandons is therefore a
    chunk itself, and the one used is the opening chunk again: a chunk at offset
    0 re-announces the payload it announced the first time, so the accumulation
    is left exactly where the checked opening chunk left it. That first opening
    chunk is sent normally and checked, because an abort probe reports how many
    reply bytes it took and whether the interface came back, not what the target
    answered.

    A firmware that does not let go completes the payload on the next chunk and
    sends it, to a socket that is still open, so this is measured on the wire as
    well as on the status.
    """
    section("chunked-write-discarded-by-abort")
    handle = net.open_udp(peer.ip, peer.udp_port)
    half = SMALL_WRITE // 2
    payload = pattern(SMALL_WRITE, seed=13)
    opening = net.write_chunk_command(handle, 0, SMALL_WRITE, payload[:half])
    try:
        peer.learn_udp_peer(net, handle)

        started = net.uci.transact(opening)
        premature = peer.collect_udp()
        with check(f"a payload of {half} of {SMALL_WRITE} bytes is in progress"):
            detail(f"socket {handle}: status {started.status_text!r}, reply "
                   f"{started.data!r}, datagrams {[len(d) for d in premature]}")
            require_payload_open(started, premature, half, SMALL_WRITE)

        taken, idle = net.uci.probe_abort(opening)
        after_abort = peer.collect_udp()
        with check("the opening chunk is abandoned by the abort bit"):
            detail(f"the same opening chunk was sent again and abandoned after {taken} "
                   f"reply byte(s), interface idle {idle}, datagrams "
                   f"{[len(d) for d in after_abort]}")
            if not idle:
                raise Failure("the interface did not return to Idle after the abort, so the "
                              "abort this case needs did not happen and nothing below it "
                              "measures what it claims to")
            require_nothing_sent(after_abort, f"a chunk that carried {half} of "
                                              f"{SMALL_WRITE} announced bytes")

        abandoned = net.write_chunk(handle, half, SMALL_WRITE, payload[half:])
        after_abandoned = peer.collect_udp()
        with check("an abort discards a chunked write that was in progress"):
            detail(f"status {abandoned.status_text!r}, reply {abandoned.data!r}, datagrams "
                   f"{[len(d) for d in after_abandoned]}")
            if abandoned.status_text != STATUS_INVALID_PARAMS:
                raise Failure(
                    f"expected {STATUS_INVALID_PARAMS!r}, got {abandoned.status_text!r}: the "
                    f"client abandoned the exchange with {half} of {SMALL_WRITE} bytes "
                    f"accumulated, which ends the payload, so this chunk continues one that "
                    f"no longer exists. A firmware that answers {STATUS_OK.decode()} here "
                    f"held those {half} bytes across a client walking away, and socket "
                    f"{handle} is still open, so they leave as a datagram the moment "
                    f"anything completes them")
            require_nothing_sent(after_abandoned, "a chunk continuing a payload that an "
                                                  "abort should have discarded")
    finally:
        net.close(handle)
    return True


def run_chunked_write_discarded_by_reset(net: Net, peer: Peer, reset) -> bool:
    """A payload part way through does not survive a C64 reset.

    The program that was sending it is gone, so the chunks that would have
    completed it are never coming. What is left behind is a handle, a total and
    an offset that the next program can match by accident: the reset closes the
    sockets, lwip hands the low descriptors out again, and a stale handle then
    names a live socket belonging to somebody else.

    The completing chunk goes straight after the reset with no command between
    it and the reset, and that is the whole design of this scenario. Every
    command that is not a chunk ends a payload in progress on its way in, so a
    sequence that re-opened a socket first would have discarded the
    accumulation with the OPEN command and proved nothing about the reset.

    That leaves both firmwares refusing the same chunk, and what separates them
    is which refusal it is. One that discards on the reset holds no payload at
    the offset this chunk names, so the chunk is refused by the offset check
    before any socket is looked at: '81,INVALID PARAMS'. One that does not
    discard matches the chunk in all three fields, completes the payload and
    reaches the send, where the handle the reset closed is no longer one this
    target owns: '12,SEND ERROR: 9'. The status is therefore the whole of what
    tells "the accumulation is gone" from "only the socket is gone", and this
    check reads it that way rather than treating any refusal as a pass.

    A firmware that kept the sockets open across the reset as well is caught on
    the wire instead: the payload then completes and leaves.
    """
    section("chunked-write-discarded-by-reset")
    handle = net.open_udp(peer.ip, peer.udp_port)
    half = SMALL_WRITE // 2
    payload = pattern(SMALL_WRITE, seed=17)
    try:
        peer.learn_udp_peer(net, handle)

        started = net.write_chunk(handle, 0, SMALL_WRITE, payload[:half])
        premature = peer.collect_udp()
        with check(f"a payload of {half} of {SMALL_WRITE} bytes is in progress"):
            detail(f"socket {handle}: status {started.status_text!r}, reply "
                   f"{started.data!r}, datagrams {[len(d) for d in premature]}")
            require_payload_open(started, premature, half, SMALL_WRITE)

        with check(f"reset the C64 with {half} of {SMALL_WRITE} bytes accumulated"):
            net = reset()

        completing = net.write_chunk(handle, half, SMALL_WRITE, payload[half:])
        after = peer.collect_udp()
        with check("a C64 reset discards a chunked write that was in progress"):
            detail(f"status {completing.status_text!r}, reply {completing.data!r}, "
                   f"datagrams {[len(d) for d in after]}")
            require_implemented(completing, "the completing chunk of a chunked write")
            if completing.status_text.startswith(STATUS_SEND_ERROR_PREFIX):
                raise Failure(
                    f"the completing chunk answered {completing.status_text!r}: the payload "
                    f"survived the reset, matched this chunk in the handle, the offset and "
                    f"the total, completed, and reached the send. The only thing that "
                    f"stopped it there was socket {handle} having been closed by the same "
                    f"reset. Nothing stops the next program: it opens a socket, lwip hands "
                    f"back the descriptor this one had, and its first chunk at offset "
                    f"{half} of {SMALL_WRITE} completes and sends the payload a program "
                    f"that is no longer running abandoned")
            if completing.status_text != STATUS_INVALID_PARAMS:
                raise Failure(
                    f"expected {STATUS_INVALID_PARAMS!r}, got {completing.status_text!r}: "
                    f"the reset ends the payload that was in progress, so this chunk "
                    f"continues one that no longer exists and is refused at the offset")
            require_nothing_sent(after, "a chunk continuing a payload the reset should have "
                                        "discarded")
    finally:
        net.close(handle)
    return True


def close_quietly(net: Net, handles: list[int]) -> None:
    """Close every handle a scenario opened; an error means it was already gone."""
    for handle in handles:
        try:
            net.close(handle)
        except Failure:
            pass


def open_abandoned(net: Net, peer: Peer, count: int, handles: list[int],
                   ports: list[int]) -> None:
    """OPEN_UDP `count` times without a CLOSE_SOCKET, recording each socket.

    Each announces itself with a datagram, so this host learns the source port
    lwip gave it. Two live sockets never share a port, so it identifies one.
    """
    for n in range(count):
        with check(f"OPEN_UDP #{n + 1} without closing the previous ones"):
            handle = net.open_udp(peer.ip, peer.udp_port)
            handles.append(handle)
            _, port = peer.learn_udp_peer(net, handle)
            ports.append(port)
            detail(f"handle {handle}, source port {port}")


def run_reset_closes_uci_sockets(net: Net, peer: Peer, reset, device) -> bool:
    """A C64 reset releases every socket a client left open.

    The program that opened them is gone, so nothing else ever can. `reset`
    resets the C64 and returns a Net that reaches the target afterwards.
    """
    section("reset-closes-uci-sockets")
    handles: list[int] = []
    ports: list[int] = []
    heap_before = free_heap(device)
    try:
        for cycle in range(RESET_CYCLES):
            open_abandoned(net, peer, SOCKETS_LEFT_OPEN_AT_RESET, handles, ports)
            with check(f"cycle {cycle + 1}: reset the C64 with "
                       f"{SOCKETS_LEFT_OPEN_AT_RESET} sockets open"):
                net = reset()
            with check(f"cycle {cycle + 1}: the reset closed the sockets left open"):
                # A socket the reset closed answers CLOSE_SOCKET with an
                # error; one it left open closes now, and answers OK.
                still_open = [handle for handle in handles
                              if net.close(handle).status_text == STATUS_OK]
                handles.clear()
                if still_open:
                    raise Failure(f"handles {still_open} were still open after the reset; "
                                  f"the program that opened them is gone, so nothing "
                                  f"else can ever close them")
            # Opening as many again says the reset released the sockets rather
            # than merely forgetting them: had they still been held, lwip would
            # have that many fewer to give and this would answer 85.
            open_abandoned(net, peer, SOCKETS_LEFT_OPEN_AT_RESET, handles, ports)
            close_quietly(net, handles)
            handles.clear()
        heap_after = free_heap(device)
        if heap_before is not None and heap_after is not None:
            detail(f"free heap {heap_before} -> {heap_after} over {RESET_CYCLES} cycles")
    finally:
        close_quietly(net, handles)
    return True


def free_heap(device) -> int | None:
    """Free FreeRTOS heap, for the record; lwip's pools are static and do not show here."""
    try:
        heap = device.machine.heap()
    except Failure:
        return None
    return heap["free"] if heap else None


def restore_settings(device, original: dict[str, str], keep: bool) -> bool:
    if keep or not original:
        return True
    ok = True
    for item, value in original.items():
        with check(f"restore {item!r} to {value!r}"):
            device.configs.set(CONFIG_CATEGORY, item, value)
    return ok


def build_driver(route: str, computer, busy_timeout: float):
    """The driver for one route, ready to use."""
    if route == "rest":
        return Uci(computer.machine, busy_timeout)
    agent = NativeUci(computer.machine, computer.runners, busy_timeout)
    agent.start()
    return agent


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Verify how the UCI network target's READ_SOCKET handles the requested "
                    "length and datagrams larger than it (GideonZ/1541ultimate#802), and "
                    "how WRITE_SOCKET_CHUNK sends a payload larger than one command as a "
                    "single datagram (GideonZ/1541ultimate#807)."
    )
    cli.add_device_arguments(parser, password=None, timeout=30.0, colour=False)
    parser.add_argument("-b", "--busy-timeout", type=float, default=15.0,
                        help="How long one command may stay in Command Busy before it "
                             "counts as wedged.")
    parser.add_argument("--test", action="append", choices=["all", *TESTS])
    parser.add_argument("--route", action="append", choices=["all", *ROUTES],
                        help="How to reach $DF1C-$DF1F: 'rest' for DMA cycles the "
                             "Ultimate issues, 'native' for 6502 code running on the "
                             "C64. Both by default.")
    parser.add_argument("--keep-config", action="store_true",
                        help="Don't restore the original Command Interface setting on exit.")
    args = parser.parse_args()

    selected = TESTS if not args.test or "all" in args.test else [t for t in TESTS if t in args.test]
    routes = ROUTES if not args.route or "all" in args.route else [r for r in ROUTES if r in args.route]
    target = targets.parse(args.host)
    device = UltimateApi(target.device, args.password, args.timeout)
    # The command interface registers are decoded on the C64 expansion bus, so
    # the machine that reads them back has to be the one driving that bus. On a
    # single-machine target these are the same host; on cartridge@computer they
    # are not, and the 6502 agent runs on the computer while the network stack
    # under test is the cartridge's. See tests/lib/targets.py.
    computer = UltimateApi(target.computer, args.password, args.timeout)
    # The REST driver is also the suite's frame: it decides whether this machine
    # has a command interface at all, and it is what hands data back when a
    # scenario leaves a reply pending. Neither is something the 6502 agent can
    # do, since it needs a working interface in order to report anything and it
    # owns the CPU while it runs.
    rest_uci = Uci(computer.machine, args.busy_timeout)

    original: dict[str, str] = {}
    results: dict[str, bool | None] = {}
    peer: Peer | None = None
    interface_enabled = False
    native_started = False
    cleanup_ok = True
    setup_failed = False

    def run(route: str, name: str, fn, *fn_args) -> None:
        """Run one scenario on one route; a failed check ends that scenario only.

        report.check re-raises, so the first failing check leaves its scenario
        immediately. The others still have to run: this suite is measuring
        several independent properties of the same command, and stopping at the
        first one would hide the rest.
        """
        if name not in selected:
            return
        label = f"{name} [{route}]"
        results[label] = False  # so an aborted scenario reports FAIL, not "not reached"
        try:
            results[label] = fn(*fn_args)
        except Wedged:
            # The interface is stuck for every target; nothing after this can run.
            raise
        except Failure as exc:
            detail(f"{label} stopped: {format_exception(exc)}")

    try:
        with check(f"read {CONFIG_CATEGORY!r}"):
            config = device.configs.category(CONFIG_CATEGORY)
            if CFG_CMD_IF not in config:
                raise Failure(f"this device has no {CFG_CMD_IF!r} setting; it cannot run this suite")
            original = {CFG_CMD_IF: str(config[CFG_CMD_IF])}
            detail(f"current: {original}")

        with check("enable the Command Interface registers at $DF1B-$DF1F"):
            device.configs.set(CONFIG_CATEGORY, CFG_CMD_IF, "Enabled")
            interface_enabled = True
            rest_uci.release()

        if not interface_present(rest_uci):
            check_start("this machine answers at the Command Interface registers")
            check_skip("$DF1B-$DF1F all read $FF, so no command interface is present "
                       "at those addresses on this machine")
            suite_ok("net_target_test")
            return 0

        needs_peer = [name for name in TESTS
                      if name != "read-length-limit" and name in selected]
        if needs_peer:
            check_start("this host can act as the device's network peer")
            try:
                peer = Peer(local_address_towards(target.device))
                # Proves the route in the direction that matters: the device
                # opening a socket back to this process and its datagram
                # arriving here.
                probe = Net(rest_uci)
                handle = probe.open_udp(peer.ip, peer.udp_port)
                try:
                    peer.learn_udp_peer(probe, handle)
                finally:
                    probe.close(handle)
                check_ok(f"{peer.ip} UDP {peer.udp_port}, TCP {peer.tcp_port}")
            except (TimeoutError, Failure, OSError) as exc:
                check_skip(f"the device could not reach this host: {format_exception(exc)}")
                if peer is not None:
                    peer.close()
                peer = None

        for route in routes:
            section(f"route: {route}")
            if route == "native" and target.split:
                # Measured on a U2+L in a C64 Ultimate: the host's own command
                # interface claims $DF1B-$DF1F from its 6510, so a program there
                # reaches the host's target, not the cartridge's. Turning the
                # host's interface off does not hand the range over: the 6510
                # then reads $FF. DMA is the asymmetry, since a readmem issued
                # by the host does reach the cartridge. So on a split target
                # this route cannot address the device under test at all.
                check_start(f"the network target answers over the {route} route")
                check_skip(f"{target.device} is a cartridge in {target.computer}, and 6502 "
                           f"code on {target.computer} reaches its own command interface "
                           f"rather than the cartridge's")
                for name in selected:
                    results.setdefault(f"{name} [{route}]", None)
                continue
            # A route that cannot be reached fails the run rather than skipping
            # it: both are expected to work on any device with a command
            # interface, so one that does not is a finding, not a limitation.
            with check(f"the network target answers over the {route} route"):
                if route == "native":
                    # run_prg needs the machine at a BASIC prompt, and the agent
                    # keeps the CPU for itself until the machine is reset.
                    # force=True because MachineApi.reset skips one it believes
                    # cannot change anything, and this one has to happen.
                    computer.machine.reset(force=True)
                    native_started = True
                driver = build_driver(route, computer, args.busy_timeout)
                net = Net(driver)
                identity = net.identify()
                if identity.status_text != STATUS_OK or not identity.data:
                    raise Failure(f"NET_CMD_IDENTIFY answered {identity.status_text!r} "
                                  f"with reply {identity.data!r}")
                detail(identity.data.decode("latin-1"))

            run(route, "read-length-limit", run_read_length_limit, net)
            if peer is None:
                for name in needs_peer:
                    results.setdefault(f"{name} [{route}]", None)
                continue
            run(route, "reply-blocks-are-drainable", run_reply_blocks_are_drainable, net, peer)
            run(route, "datagram-spans-reply-blocks", run_datagram_spans_reply_blocks, net, peer)
            run(route, "datagram-size-ceiling", run_datagram_size_ceiling, net, peer)
            run(route, "udp-truncation-is-detectable", run_udp_truncation, net, peer)
            run(route, "truncation-spans-reply-blocks",
                run_truncation_spans_reply_blocks, net, peer)
            run(route, "tcp-read-is-lossless", run_tcp_lossless, net, peer)
            run(route, "oversize-request-keeps-the-datagram",
                run_oversize_request_keeps_datagram, net, peer)
            run(route, "multi-block-state-does-not-leak",
                run_multi_block_state_does_not_leak, net, peer)
            run(route, "chunked-write-arrives-as-one-datagram",
                run_chunked_write_one_datagram, net, peer)
            run(route, "chunked-write-full-size-datagram",
                run_chunked_write_full_size_datagram, net, peer)
            run(route, "chunked-write-zero-total",
                run_chunked_write_zero_total, net, peer)
            run(route, "chunked-write-total-ceiling",
                run_chunked_write_total_ceiling, net, peer)
            run(route, "chunked-write-refuses-unowned-socket",
                run_chunked_write_refuses_unowned_socket, net, peer)
            run(route, "chunked-write-refuses-bad-chunks",
                run_chunked_write_refuses_bad_chunks, net, peer)
            run(route, "chunked-write-refuses-offset-ahead",
                run_chunked_write_refuses_offset_ahead, net, peer)
            run(route, "chunked-write-refuses-short-commands",
                run_chunked_write_refuses_short_commands, net, peer)
            run(route, "chunked-write-discarded-by-abort",
                run_chunked_write_discarded_by_abort, net, peer)

            def reset_and_reopen(route=route):
                """Reset the C64 and hand back a driver that reaches the target."""
                nonlocal native_started
                computer.machine.reset(force=True)
                if route == "native":
                    native_started = True
                    return Net(build_driver(route, computer, args.busy_timeout))
                return Net(rest_uci)

            if "chunked-write-discarded-by-reset" in selected:
                run(route, "chunked-write-discarded-by-reset",
                    run_chunked_write_discarded_by_reset, net, peer, reset_and_reopen)
                # The reset inside it ends the 6502 agent `net` wraps, so the
                # native route needs a driver of its own for what follows. The
                # REST route has nothing to rebuild and would only pay for
                # another machine reset.
                if route == "native":
                    net = reset_and_reopen()
            # Last on purpose: on the native route the reset inside it ends
            # the 6502 agent that `net` wraps, so nothing can follow it here.
            run(route, "reset-closes-uci-sockets",
                run_reset_closes_uci_sockets, net, peer, reset_and_reopen, device)

    except Failure as exc:
        # Setup and route initialisation raise rather than recording a result,
        # so without this the summary below would find an empty results map, see
        # a clean cleanup, and report the run as passing.
        setup_failed = True
        suite_fail("net_target_test", format_exception(exc))
    finally:
        if peer is not None:
            peer.close()
        # A failed scenario can leave the interface holding a reply, so hand the
        # data back before the setting goes home.
        released = rest_uci.release() if interface_enabled else True
        if not released:
            warn("the command interface did not return to Idle")
        if native_started:
            # The agent runs with interrupts off and never returns, so the
            # machine has to be reset before anything else can use it.
            with check("reset the C64 so the 6502 agent releases the machine"):
                computer.machine.reset(force=True)
        restored = restore_settings(device, original, args.keep_config)
        cleanup_ok = released and restored

    section("summary")
    all_ok = cleanup_ok and not setup_failed
    for label, outcome in results.items():
        state = OK if outcome else (FAIL if outcome is False else SKIP)
        if outcome is False:
            all_ok = False
        detail(f"{label}: {state}")
    detail(f"cleanup: {OK if cleanup_ok else FAIL}")

    if all_ok:
        suite_ok("net_target_test")
        return 0
    suite_fail("net_target_test", "see the summary above")
    return 1


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Failure as exc:
        suite_fail("net_target_test", format_exception(exc))
        raise SystemExit(1)
