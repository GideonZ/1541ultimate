#!/usr/bin/env python3
# E2E: Verifies UCI network target socket reads: length handling and datagram delivery.

"""End-to-end check of the UCI network target's READ_SOCKET and GET_RANDOM commands.

Regression guard for GideonZ/1541ultimate#802, reported as "a maxlen above 512
silently returns no data". Everything here was measured on real hardware
against the $DF1B-$DF1F registers; nothing is taken from the issue text or from
the manuals, which are used only as the statement of the intended contract.

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
894.

GET_RANDOM hands a C64 program random bytes drawn on the ESP32. Its scenarios
need no network peer except the one that interleaves it with a socket. What
they can check about the bytes is that each reply has the length asked for and
that replies are not stuck, zeroed or patterned; they cannot check that a real
entropy source is behind them, since the output of a post-processed RNG looks
the same either way. Whether bytes are owed at all is taken from the device's
own report, not from GET_RANDOM's answer: an Ultimate 64 or Ultimate 64
Elite owes them, once "WiFi Enabled" is set and it reports a wifi_mac in
/v1/info. Any other product has to refuse.

Supported on any Ultimate whose FPGA provides the command interface. The suite
enables the "Command Interface" setting and restores it on exit. On an Ultimate
64 or Ultimate 64 Elite it also sets "WiFi Enabled" and restores the stored
setting on exit; the module it started keeps running until the next reboot.
"""

import argparse
import functools
import math
import socket
import sys
import time
from pathlib import Path
from typing import NamedTuple

# The one stanza that puts the shared library on sys.path; see tests/lib/bootstrap.py.
sys.path.insert(0, str(next(p for p in Path(__file__).resolve().parents
                            if (p / "tests" / "lib").is_dir()) / "tests" / "lib"))
import bootstrap  # noqa: E402,F401
import cli  # noqa: E402
import targets  # noqa: E402
from api import UltimateApi  # noqa: E402
from report import (  # noqa: E402
    FAIL, Failure, OK, SKIP, check, check_fail, check_ok, check_skip, check_start, detail,
    format_exception, section, suite_fail, suite_ok, warn)
from uci import (  # noqa: E402
    MAX_BLOCK_BYTES, REPLY_QUEUE_BYTES, TARGET_NETWORK, Uci, Wedged,
    interface_present)
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
NET_CMD_GET_RANDOM = 0x20

STATUS_OK = b"00,OK"
STATUS_INVALID_PARAMS = b"81,INVALID PARAMS"
STATUS_OUT_OF_RANGE = b"82,PARAMETER(S) OUT OF RANGE"
# GET_RANDOM's answer when the ESP32 has nothing to draw bytes from: absent,
# not running, timed out, or not yet started its radio. It is only ever
# answered with no data, whatever the reason.
STATUS_NO_ENTROPY = b"87,NO ENTROPY SOURCE"
STATUS_NO_ENTROPY_PREFIX = b"87,"
# GET_RANDOM's answer when the ESP32 runs but was busy and did not answer in
# time. The client should try again; like 87 it never carries data.
STATUS_ENTROPY_BUSY = b"88,ENTROPY SOURCE BUSY"
STATUS_ENTROPY_BUSY_PREFIX = b"88,"
# What the network target answers for an opcode it does not implement, which
# is what GET_RANDOM is on firmware that predates it.
STATUS_UNKNOWN_COMMAND = b"21,UNKNOWN COMMAND"
# Not in the network target document, but what the firmware answers when
# lwip_recv returns -1. The number is the errno: 9 is EBADF for a handle that
# was never opened, 11 is EAGAIN for an open socket with nothing pending.
STATUS_NO_DATA_PREFIX = b"02,NO DATA"

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

# The /v1/info products that draw GET_RANDOM bytes on the ESP32 (U64 == 1 in
# product.cc), matched exactly since "Ultimate 64-II" starts with "Ultimate 64".
# Every other product answers 87, even with a wifi_mac: its ESP32 runs other code.
U64_MK1_PRODUCTS = ("Ultimate 64", "Ultimate 64 Elite")
# The setting that starts the module there, by the store's exact name (the answer
# is keyed by it). Set for the run; writing Disabled back restores the setting but
# does not stop the module (network_esp32.cc, effectuate_settings).
CFG_WIFI_CATEGORY = "WiFi settings"
CFG_WIFI_ENABLED = "WiFi Enabled"
# How long a device with Wi-Fi enabled gets to report its wifi_mac, which it
# does once the module has started; this covers a device that has just booted.
WIFI_MAC_SECONDS = 30.0

# The most GET_RANDOM hands out in one command. 512 bytes fits one reply block,
# so every GET_RANDOM reply arrives in one.
RANDOM_MAX_BYTES = 512
# The size a program asking for a key or a nonce would use.
RANDOM_TYPICAL = 32
# A length whose high byte is neither 0 nor that of RANDOM_MAX_BYTES, so a
# handler that reads only the low byte answers with 44 bytes instead of 300.
RANDOM_SPLIT_LENGTH = 300
# How long a run of pattern() bytes inside a random reply has to be before it
# counts as the socket read buffer handed back. A chance match of eight bytes
# is 1 in 2^56 per position.
STALE_RUN_BYTES = 8
# Every window of this many bytes, at every offset of every reply in a route,
# has to be new. A route holds at most about 2,500, so honest bytes repeat one by
# chance with a probability of about 2,500^2 / 2 / 2^64, 1 in 10^12.
RANDOM_WINDOW_BYTES = 8
# Lengths that are not a multiple of 4 or of 16, so a handler that fills whole
# words or blocks leaves a tail it did not draw in every one of them.
RANDOM_ODD_LENGTHS = (3, 17, 31, 257, 511)
# Same-offset matches over a pair of length m are Binomial(m, 1/256). A pair fails
# at the smallest count less likely than this, capped at RANDOM_PAIR_MATCHES_MAX
# (4 in 10^10 for 512 bytes); pairs too short to get there (m = 1, 3) are not judged.
RANDOM_PAIR_FALSE_ALARM = 1e-12
RANDOM_PAIR_MATCHES_MAX = 16
# A run of replies of one length, so a byte a handler never draws stays the
# same through all of them. Honest bytes hold one offset through 16 replies
# with a probability of 256^-15 per offset.
RANDOM_SERIES_LENGTH = 17
RANDOM_SERIES_COUNT = 16
# How often, and how far apart, a GET_RANDOM that answered 88 is asked again:
# 15 s covers the up to 10 s the ESP32 can block queueing a scan or connect.
# A source still busy after that fails where bytes are owed.
RANDOM_BUSY_RETRIES = 15
RANDOM_BUSY_DELAY_SECONDS = 1.0
# How many GET_RANDOM calls in one route may need a retry where bytes are
# owed. On an idle device an 88 is rare, so more than this says the source
# is busy as a matter of course rather than now and then.
RANDOM_BUSY_CALLS_MAX = 2

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
    "get-random-lengths",
    "get-random-bytes-vary",
    "get-random-refusals",
    "get-random-leaves-sockets-alone",
    "reset-closes-uci-sockets",
]
# The scenarios that need no network peer, because they open no socket.
PEERLESS_TESTS = ("read-length-limit", "get-random-lengths", "get-random-bytes-vary",
                  "get-random-refusals")


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
    size = (f"got {len(actual)} bytes, expected {len(expected)}; "
            if len(actual) != len(expected) else "")
    for index, (got, want) in enumerate(zip(actual, expected)):
        if got != want:
            tail = actual[index:index + 16]
            return (f"{size}first difference at offset {index}: got ${got:02X}, expected "
                    f"${want:02X}; the run starting there came from offset "
                    f"{run_offset(tail)}; around it: {actual[max(0, index - 8):index + 8].hex(' ')}")
    return f"{size}the shorter one is a prefix of the other" if size else "identical"


class Net:
    """The network target's commands, over the command interface."""

    def __init__(self, uci: Uci, duty: "RandomDuty | None" = None) -> None:
        self.uci = uci
        # What GET_RANDOM owes on this device, which decides what an 88 is.
        # None for a driver that never sends it.
        self.duty = duty
        # Every GET_RANDOM reply on this driver, and how many calls needed a
        # retry after an 88.
        self.random_replies = 0
        self.busy_calls = 0
        # Every RANDOM_WINDOW_BYTES window of every GET_RANDOM reply so far,
        # with the reply and offset it came from, and every reply itself.
        self.random_windows: dict[bytes, tuple[int, int]] = {}
        self.random_data: list[tuple[int, bytes]] = []

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

    def random_command(self, length: int) -> bytes:
        return bytes([TARGET_NETWORK, NET_CMD_GET_RANDOM, length & 0xFF, (length >> 8) & 0xFF])

    def get_random(self, length: int | None = None, command: bytes | None = None):
        """One GET_RANDOM, of `length` bytes or as the raw `command` given.

        Every reply passes the fail-closed rule here, so it holds for each one
        the suite provokes rather than only where a scenario thinks to look: a
        reply that is not 00,OK carries no data, and an 87 above all. Bytes
        handed out when the ESP32 had no entropy source would look exactly as
        random as real ones, so the status is the only place a client can learn
        not to use them, and a reply that sends both has already lost.

        An 88 fails at once on a product that owes no bytes, which never asks
        the ESP32, so its only refusal is an immediate 87. Elsewhere it is asked
        again, up to RANDOM_BUSY_RETRIES times, each retried reply held to the
        same rule, and more than RANDOM_BUSY_CALLS_MAX calls needing a retry
        fails.
        """
        if command is None:
            command = self.random_command(length)
        result = self._random_reply(command)
        busy = result.status_text.startswith(STATUS_ENTROPY_BUSY_PREFIX)
        if busy and self.duty is not None and not self.duty.owed:
            raise Failure(f"GET_RANDOM answered {result.status_text!r} on this "
                          f"{self.duty.product}, which never asks the ESP32 for bytes; "
                          f"the only correct refusal there is an immediate "
                          f"{STATUS_NO_ENTROPY!r}")
        if busy:
            self.busy_calls += 1
            if self.duty is not None and self.duty.owed and \
                    self.busy_calls > RANDOM_BUSY_CALLS_MAX:
                raise Failure(f"{self.busy_calls} GET_RANDOM calls on this route answered "
                              f"{result.status_text!r} and needed a retry; where bytes are "
                              f"owed an idle device does that at most "
                              f"{RANDOM_BUSY_CALLS_MAX} times")
        for attempt in range(1, RANDOM_BUSY_RETRIES + 1):
            if not result.status_text.startswith(STATUS_ENTROPY_BUSY_PREFIX):
                break
            detail(f"GET_RANDOM answered {result.status_text!r}; retry {attempt} of "
                   f"{RANDOM_BUSY_RETRIES} in {RANDOM_BUSY_DELAY_SECONDS:.1f}s")
            time.sleep(RANDOM_BUSY_DELAY_SECONDS)
            result = self._random_reply(command)
        return result

    def _random_reply(self, command: bytes):
        """One GET_RANDOM exchange, counted and held to the fail-closed rule."""
        result = self.uci.transact(command)
        self.random_replies += 1
        for prefix, what in ((STATUS_NO_ENTROPY_PREFIX, "with no entropy source"),
                             (STATUS_ENTROPY_BUSY_PREFIX, "with the entropy source busy")):
            if result.status_text.startswith(prefix) and result.data:
                raise Failure(f"GET_RANDOM answered {result.status_text!r} and still handed "
                              f"out {len(result.data)} bytes; {what} it must hand out none")
        if result.status_text != STATUS_OK and result.data:
            raise Failure(f"GET_RANDOM answered {result.status_text!r} with {len(result.data)} "
                          f"data bytes; a refusal carries no data")
        if result.status_text == STATUS_OK:
            self._remember_random(result.data)
        return result

    def _remember_random(self, data: bytes) -> None:
        """Fail if any window of `data` was handed out before on this route.

        A sanity check against bytes handed out twice, not an entropy proof.
        A handler that fills only part of its buffer still answers with the
        full length, and the rest is whatever an earlier reply left there:
        random, so no check on one reply sees it, but the same bytes as
        before. Every offset is checked rather than aligned blocks, so bytes
        served again at a different position are caught as well, and so is
        a reply that repeats itself.
        """
        reply = self.random_replies
        for offset in range(len(data) - RANDOM_WINDOW_BYTES + 1):
            window = data[offset:offset + RANDOM_WINDOW_BYTES]
            seen = self.random_windows.get(window)
            if seen is not None:
                raise Failure(
                    f"bytes {offset}-{offset + RANDOM_WINDOW_BYTES - 1} of GET_RANDOM reply "
                    f"{reply} ({len(data)} bytes) were handed out before, at offset "
                    f"{seen[1]} of reply {seen[0]}: {window.hex(' ')}. Random bytes do "
                    f"not repeat {RANDOM_WINDOW_BYTES} at a time, so part of this reply "
                    f"was left over rather than drawn")
            self.random_windows[window] = (reply, offset)
        # Bytes left over at the same offset rather than in a run: a handler
        # that draws every other byte, or whole words and not the tail.
        for earlier, before in self.random_data:
            common = min(len(data), len(before))
            limit = same_offset_limit(common)
            if limit is None:
                continue
            matches = sum(1 for a, b in zip(data, before, strict=False) if a == b)
            if matches >= limit:
                raise Failure(
                    f"GET_RANDOM reply {reply} ({len(data)} bytes) has the same byte as reply "
                    f"{earlier} at {matches} of their {common} common offsets; honest bytes "
                    f"match at about {common / 256:.1f}, and {limit} or more has a chance "
                    f"below {RANDOM_PAIR_FALSE_ALARM:g}")
        self.random_data.append((reply, data))

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


@functools.cache
def same_offset_limit(common: int) -> int | None:
    """How many same-offset matches fail a pair with `common` offsets in common.

    The smallest k with P(Binomial(common, 1/256) >= k) below
    RANDOM_PAIR_FALSE_ALARM, capped at RANDOM_PAIR_MATCHES_MAX, or None where
    no k up to `common` gets there.
    """
    for k in range(1, min(common, RANDOM_PAIR_MATCHES_MAX) + 1):
        # P(X >= k), summed directly rather than as 1 - P(X < k) for precision.
        tail = sum(math.comb(common, j) * (1 / 256) ** j * (255 / 256) ** (common - j)
                   for j in range(k, common + 1))
        if tail < RANDOM_PAIR_FALSE_ALARM:
            return k
    return RANDOM_PAIR_MATCHES_MAX if common >= RANDOM_PAIR_MATCHES_MAX else None


def require_random(result, length: int) -> bytes:
    """The bytes of a GET_RANDOM reply that has to have succeeded, or Failure."""
    if result.status_text == STATUS_UNKNOWN_COMMAND:
        raise Failure(f"GET_RANDOM answered {result.status_text!r}: this firmware does not "
                      f"implement NET_CMD_GET_RANDOM (${NET_CMD_GET_RANDOM:02X})")
    if result.status_text.startswith(STATUS_ENTROPY_BUSY_PREFIX):
        raise Failure(f"GET_RANDOM of {length} bytes answered {result.status_text!r}: the "
                      f"entropy source was still busy when the suite stopped asking, which "
                      f"is after {RANDOM_BUSY_RETRIES} retries "
                      f"{RANDOM_BUSY_DELAY_SECONDS:.1f}s apart")
    if result.status_text.startswith(STATUS_NO_ENTROPY_PREFIX):
        raise Failure(f"GET_RANDOM of {length} bytes answered {result.status_text!r}, expected "
                      f"{STATUS_OK!r}; if WiFi was disabled from the menu, enable it there or "
                      f"reboot, then run the suite again")
    if result.status_text != STATUS_OK:
        raise Failure(f"GET_RANDOM of {length} bytes answered {result.status_text!r}, "
                      f"expected {STATUS_OK!r}")
    if len(result.data) != length:
        raise Failure(f"GET_RANDOM of {length} bytes handed out {len(result.data)}")
    if len(result.blocks) != 1:
        raise Failure(f"the reply arrived in {len(result.blocks)} blocks; "
                      f"{RANDOM_MAX_BYTES} bytes fit one, so every reply must")
    return result.data


def read_wifi_setting(device, info) -> object:
    """CFG_WIFI_ENABLED as the device reports it, or None where it has none.

    Asked only of the products that can owe bytes: the others either have no
    such store or, on an Elite II, one whose Wi-Fi items are actions.
    """
    if info.product not in U64_MK1_PRODUCTS:
        return None
    if CFG_WIFI_CATEGORY not in device.configs.category_names():
        return None
    return device.configs.get(CFG_WIFI_CATEGORY, CFG_WIFI_ENABLED)


def reports_wifi_mac(info) -> bool:
    wifi_mac = info.extra.get("wifi_mac")
    return isinstance(wifi_mac, str) and bool(wifi_mac.strip())


def wait_for_wifi_mac(device, info):
    """The device's report once it names a wifi_mac, or after WIFI_MAC_SECONDS."""
    deadline = time.monotonic() + WIFI_MAC_SECONDS
    while not reports_wifi_mac(info) and time.monotonic() < deadline:
        time.sleep(1.0)
        info = device.info()
    return info


def read_duty(device, wifi_original: dict[str, str]) -> "RandomDuty":
    """What GET_RANDOM owes on this device, from its report.

    On an Ultimate 64 or Ultimate 64 Elite "WiFi Enabled" is set first, and its
    stored value kept in `wifi_original` for the restore. A device that then cannot
    owe bytes fails this check, and the Failure is passed on for the
    GET_RANDOM scenarios to fail with.
    """
    with check("read whether this device owes GET_RANDOM bytes"):
        info = device.info()
        wifi_setting = read_wifi_setting(device, info)
        if info.product in U64_MK1_PRODUCTS and wifi_setting is None:
            raise Failure(f"this {info.product} has no {CFG_WIFI_ENABLED!r} in "
                          f"{CFG_WIFI_CATEGORY!r}, so the suite cannot start its WiFi module")
        if wifi_setting is not None and wifi_setting != "Enabled":
            wifi_original[CFG_WIFI_ENABLED] = str(wifi_setting)
            device.configs.set(CFG_WIFI_CATEGORY, CFG_WIFI_ENABLED, "Enabled")
            wifi_setting = "Enabled"
        if wifi_setting == "Enabled":
            info = wait_for_wifi_mac(device, info)
        duty = random_duty(info)
        expected = ("has to hand out bytes" if duty.owed else "has to refuse with 87")
        mac = "reported" if reports_wifi_mac(info) else "not reported"
        detail(f"product {info.product!r}, wifi_mac {mac}, "
               f"{CFG_WIFI_ENABLED!r} {wifi_setting!r}"
               f"{' (set for the run)' if wifi_original else ''}: GET_RANDOM {expected}")
    return duty


class RandomDuty(NamedTuple):
    """What GET_RANDOM has to answer on one device, decided from its report."""

    owed: bool  # bytes are owed: 87, or 88 past the retries, fails; else only 87 passes
    product: str


def random_duty(info) -> RandomDuty:
    """What a device with "WiFi Enabled" set has to answer to GET_RANDOM.

    Only an Ultimate 64 or Ultimate 64 Elite draws bytes at all, so on any
    other product a reply with bytes in it came from somewhere else, which
    is the fallback the command must never have. On those two the bytes are
    owed, and one whose module never reported a wifi_mac fails rather than
    skips; otherwise a firmware that always refuses would pass.
    """
    owed = info.product in U64_MK1_PRODUCTS
    if owed and not reports_wifi_mac(info):
        raise Failure(f"this {info.product} has {CFG_WIFI_ENABLED!r} set but reported no "
                      f"wifi_mac in /v1/info within {WIFI_MAC_SECONDS:g}s, so its WiFi module "
                      f"did not start. Check that the WiFi module is fitted and starts, "
                      f"then run the suite again")
    return RandomDuty(owed=owed, product=info.product)


def random_expected(net: Net, duty: RandomDuty) -> bool:
    """Whether this device should hand out random bytes at all.

    `duty` is decided in main from the device's own report, never from this
    answer. Where bytes are owed, 87 is a failure, and so is an 88 that
    outlasts its retries. Elsewhere 87 with no data is the one correct answer:
    that is checked here, and the checks that need bytes skip; 00,OK fails
    here. Anything else, the unknown-command answer of firmware without
    GET_RANDOM included, goes on to fail the checks that need bytes.
    """
    if duty.owed:
        return True
    probe = net.get_random(RANDOM_TYPICAL)
    if probe.status_text == STATUS_OK:
        with check(f"GET_RANDOM on this {duty.product} refuses rather than hand out bytes"):
            raise Failure(f"GET_RANDOM answered {STATUS_OK!r} with {len(probe.data)} bytes on "
                          f"this {duty.product}, which has no source to draw them from: only an "
                          f"{' or '.join(U64_MK1_PRODUCTS)} does. The only correct answer "
                          f"here is {STATUS_NO_ENTROPY!r}, with no data")
    if not probe.status_text.startswith(STATUS_NO_ENTROPY_PREFIX):
        return True
    with check("where no bytes are owed, GET_RANDOM refuses with 87 and no data"):
        if probe.status_text != STATUS_NO_ENTROPY:
            raise Failure(f"expected {STATUS_NO_ENTROPY!r}, got {probe.status_text!r}")
    check_start("GET_RANDOM hands out random bytes")
    check_skip(f"GET_RANDOM answered {probe.status_text[:2].decode()} on a device that owes "
               f"no bytes: only an {' or '.join(U64_MK1_PRODUCTS)} serves them")
    return False


def run_get_random_lengths(net: Net, duty: RandomDuty, native: bool) -> bool | None:
    """GET_RANDOM hands out exactly the number of bytes asked for.

    At the typical size and the lower bound on both routes. On the native route
    also at the upper bound, at a length whose high byte is neither 0 nor 2, so
    a handler that drops the high byte is caught on the success path as well as
    on the refusals, and at lengths that are not a multiple of 4 or 16, whose
    tails Net checks against earlier replies. The REST route reads one byte per
    DMA cycle, and the firmware path is the same.
    """
    section("get-random-lengths")
    if not random_expected(net, duty):
        return None
    for length in (RANDOM_TYPICAL, 1,
                   *((RANDOM_MAX_BYTES, RANDOM_SPLIT_LENGTH, *RANDOM_ODD_LENGTHS) if native else ())):
        with check(f"GET_RANDOM of {length} bytes answers {STATUS_OK.decode()} with exactly "
                   f"{length} bytes"):
            require_random(net.get_random(length), length)
    return True


def run_get_random_bytes_vary(net: Net, duty: RandomDuty) -> bool | None:
    """No byte of a reply stays the same from one reply to the next.

    A sanity check, NOT a proof of entropy: the bytes are the output of a
    post-processed RNG, which looks random whether or not a physical source is
    feeding it. Replies handed out again, repeated or patterned are caught on
    every reply by Net's window and same-offset checks; what those cannot see
    is a tail of one to three bytes that a handler never draws. Native route
    only, where it is cheap.
    """
    section("get-random-bytes-vary")
    if not random_expected(net, duty):
        return None
    with check(f"no byte stays the same through {RANDOM_SERIES_COUNT} GET_RANDOM of "
               f"{RANDOM_SERIES_LENGTH} bytes"):
        series = [require_random(net.get_random(RANDOM_SERIES_LENGTH), RANDOM_SERIES_LENGTH)
                  for _ in range(RANDOM_SERIES_COUNT)]
        stuck = [offset for offset in range(RANDOM_SERIES_LENGTH)
                 if len({reply[offset] for reply in series}) == 1]
        if stuck:
            raise Failure(f"offsets {stuck} held the same byte in all {RANDOM_SERIES_COUNT} "
                          f"replies: ${series[0][stuck[0]]:02X} at {stuck[0]}")
    return True


def run_get_random_refusals(net: Net, duty: RandomDuty) -> bool:
    """Malformed and out-of-range requests are refused, with no data.

    Refusals do not need an entropy source, so these run on every device.
    The lengths above the limit include ones with the top bit set, which a
    handler that holds the length in a signed 16-bit value reads as negative.
    """
    section("get-random-refusals")
    malformed = [
        ("no length bytes", bytes([TARGET_NETWORK, NET_CMD_GET_RANDOM])),
        ("only the low length byte",
         bytes([TARGET_NETWORK, NET_CMD_GET_RANDOM, RANDOM_TYPICAL])),
        # A valid length with a byte after it: the command is exactly four
        # bytes, and a check for "at least four" would take this one.
        ("a byte after a valid length",
         bytes([TARGET_NETWORK, NET_CMD_GET_RANDOM, RANDOM_TYPICAL, 0, 0])),
    ]
    for what, command in malformed:
        with check(f"GET_RANDOM with {what} answers {STATUS_INVALID_PARAMS.decode()} "
                   f"and no data"):
            result = net.get_random(command=command)
            if result.status_text != STATUS_INVALID_PARAMS:
                raise Failure(f"expected {STATUS_INVALID_PARAMS!r}, got {result.status_text!r}")

    for length in (0, RANDOM_MAX_BYTES + 1, 0x8000, 0xFFFF):
        with check(f"GET_RANDOM of {length} bytes answers {STATUS_OUT_OF_RANGE.decode()} "
                   f"and no data"):
            result = net.get_random(length)
            if result.status_text != STATUS_OUT_OF_RANGE:
                raise Failure(f"expected {STATUS_OUT_OF_RANGE!r}, got {result.status_text!r}")

    # Refusals must not leave the command unusable. Where there is no entropy
    # source the answer is 87 either way, and random_expected says so.
    if random_expected(net, duty):
        with check("GET_RANDOM still hands out bytes after the refusals"):
            require_random(net.get_random(RANDOM_TYPICAL), RANDOM_TYPICAL)
    return True


def run_get_random_leaves_sockets_alone(net: Net, peer: Peer, duty: RandomDuty,
                                        native: bool) -> bool | None:
    """GET_RANDOM does not disturb the network target's socket state.

    It is answered by the same target that holds the socket table and the
    buffer READ_SOCKET replies from, so it runs here between an open, a
    pending datagram, the read of it, and the close, all of which have to
    behave as they do without it.
    """
    section("get-random-leaves-sockets-alone")
    if not random_expected(net, duty):
        return None
    baseline = net.identify()
    handle = net.open_udp(peer.ip, peer.udp_port)
    try:
        peer.learn_udp_peer(net, handle)
        net.drain_socket(handle)
        peer.send_udp(pattern(SAFE_READ))
        time.sleep(DELIVERY_SETTLE_SECONDS)

        with check("GET_RANDOM answers while a socket holds a pending datagram"):
            require_random(net.get_random(RANDOM_TYPICAL), RANDOM_TYPICAL)
            refused = net.get_random(0)
            if refused.status_text != STATUS_OUT_OF_RANGE:
                raise Failure(f"a length of 0 answered {refused.status_text!r}, expected "
                              f"{STATUS_OUT_OF_RANGE!r}")

        with check("the pending datagram is read back whole after GET_RANDOM"):
            result = net.read(handle, SAFE_READ)
            detail(result.describe())
            if result.status_text != STATUS_OK or result.header != SAFE_READ:
                raise Failure(f"expected {STATUS_OK!r} and a header of {SAFE_READ}, got "
                              f"{result.status_text!r} and {result.header}")
            if result.payload != pattern(SAFE_READ):
                raise Failure(f"the datagram changed: "
                              f"{describe_mismatch(result.payload, pattern(SAFE_READ))}")

        if native:  # 512 bytes; the REST route reads them one DMA cycle at a time
            with check("GET_RANDOM right after a socket read does not hand back the read's bytes"):
                # The read just filled the target's buffer with pattern() bytes. A
                # handler that answers from that buffer without filling all of it
                # hands some of them out again, and they read as a run of pattern().
                data = require_random(net.get_random(RANDOM_MAX_BYTES), RANDOM_MAX_BYTES)
                stale = [offset for offset in range(len(data) - STALE_RUN_BYTES + 1)
                         if run_offset(data[offset:offset + STALE_RUN_BYTES]) is not None]
                if stale:
                    raise Failure(f"bytes from offset {stale[0]} are a run of the datagram just "
                                  f"read: {data[stale[0]:stale[0] + 16].hex(' ')}")

        with check("the socket still sends after GET_RANDOM"):
            # learn_udp_peer sends from the device and waits for it here, with
            # the retry a lost datagram needs.
            peer.learn_udp_peer(net, handle)

        with check("the socket closes with 00,OK"):
            closing, handle = handle, None
            result = net.close(closing)
            if result.status_text != STATUS_OK:
                raise Failure(f"CLOSE_SOCKET answered {result.status_text!r}")

        with check("IDENTIFY after GET_RANDOM answers exactly as before it"):
            identity = net.identify()
            if (identity.data, identity.status_text, len(identity.blocks)) != \
                    (baseline.data, baseline.status_text, len(baseline.blocks)):
                raise Failure(f"IDENTIFY answered {identity.data!r}, {identity.status_text!r} "
                              f"in {len(identity.blocks)} block(s); before GET_RANDOM it was "
                              f"{baseline.data!r}, {baseline.status_text!r} in "
                              f"{len(baseline.blocks)}")
    finally:
        if handle is not None:
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
                    "what GET_RANDOM hands out."
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
                        help="Don't restore the original Command Interface and WiFi Enabled "
                             "settings on exit.")
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
    wifi_original: dict[str, str] = {}
    results: dict[str, bool | None] = {}
    peer: Peer | None = None
    interface_enabled = False
    native_started = False
    cleanup_ok = True
    setup_failed = False

    # Why no GET_RANDOM scenario can be run on this device, when the device
    # cannot owe bytes; each of them then fails with it, and the rest run.
    duty_failure: str | None = None

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
        if duty_failure is not None and name.startswith("get-random-"):
            section(name)
            check_start(f"{name} can run on this device")
            check_fail(duty_failure)
            return
        try:
            results[label] = fn(*fn_args)
        except Wedged:
            # The interface is stuck for every target; nothing after this can run.
            raise
        except Failure as exc:
            detail(f"{label} stopped: {format_exception(exc)}")

    try:
        # Whether bytes are owed comes from the device's report, not the answer,
        # so a firmware that always answers 87 fails. If it cannot be read, only
        # the GET_RANDOM scenarios fail.
        duty = RandomDuty(owed=False, product="")
        if any(name.startswith("get-random-") for name in selected):
            try:
                duty = read_duty(device, wifi_original)
            except Failure as exc:
                duty_failure = format_exception(exc)

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
                      if name not in PEERLESS_TESTS and name in selected]
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
                net = Net(driver, duty)
                identity = net.identify()
                if identity.status_text != STATUS_OK or not identity.data:
                    raise Failure(f"NET_CMD_IDENTIFY answered {identity.status_text!r} "
                                  f"with reply {identity.data!r}")
                detail(identity.data.decode("latin-1"))

            run(route, "read-length-limit", run_read_length_limit, net)
            run(route, "get-random-lengths", run_get_random_lengths, net, duty, route == "native")
            if route == "native":
                run(route, "get-random-bytes-vary", run_get_random_bytes_vary, net, duty)
            run(route, "get-random-refusals", run_get_random_refusals, net, duty)
            if peer is None:
                for name in needs_peer:
                    if duty_failure is not None and name.startswith("get-random-"):
                        run(route, name, None)  # records the FAIL without calling anything
                    else:
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
            run(route, "get-random-leaves-sockets-alone",
                run_get_random_leaves_sockets_alone, net, peer, duty, route == "native")

            def reset_and_reopen(route=route):
                """Reset the C64 and hand back a driver that reaches the target."""
                nonlocal native_started
                computer.machine.reset(force=True)
                if route == "native":
                    native_started = True
                    return Net(build_driver(route, computer, args.busy_timeout))
                return Net(rest_uci)
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
        wifi_restored = True
        if wifi_original and not args.keep_config:
            # First, and caught, so that it runs whatever the restores below raise.
            try:
                with check(f"restore {CFG_WIFI_ENABLED!r} to {wifi_original[CFG_WIFI_ENABLED]!r}"):
                    device.configs.set(CFG_WIFI_CATEGORY, CFG_WIFI_ENABLED,
                                       wifi_original[CFG_WIFI_ENABLED])
            except Exception:  # check() has reported it
                wifi_restored = False
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
        cleanup_ok = released and restored and wifi_restored

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
