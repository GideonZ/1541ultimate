#!/usr/bin/env python3
# E2E: Verifies UCI network target socket reads: length handling and datagram delivery.

"""End-to-end check of the UCI network target's READ_SOCKET command.

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

Reads are kept small on purpose. The response queue is drained one REST DMA
cycle per byte, so the size of the payload is the cost of the suite, and every
property under test is visible at 64 bytes as clearly as at 894.

Supported on any Ultimate whose FPGA provides the command interface. The suite
enables the "Command Interface" setting and restores it on exit.
"""

import argparse
import os
import socket
import sys
import time
from typing import Dict, List, Optional, Tuple

# tests/lib holds the reporting rules and the transport every suite shares.
sys.path.insert(0, os.path.join(
    os.path.dirname(os.path.abspath(__file__)), "..", "..", "..", "lib"))
sys.path.insert(0, os.path.join(
    os.path.dirname(os.path.abspath(__file__)), "..", "..", "lib"))
import targets  # noqa: E402
from api import UltimateApi  # noqa: E402
from report import (  # noqa: E402
    FAIL, Failure, OK, SKIP, check, check_ok, check_skip, check_start, detail,
    format_exception, section, suite_fail, suite_ok, warn)
from uci import (  # noqa: E402
    REPLY_QUEUE_BYTES, TARGET_NETWORK, Uci, Wedged, interface_present)

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

STATUS_OK = b"00,OK"
STATUS_CLOSED = b"01,CONNECTION CLOSED BY HOST"
STATUS_OUT_OF_RANGE = b"82,PARAMETER(S) OUT OF RANGE"
# Not in the network target document, but what the firmware answers when
# lwip_recv returns -1. The number is the errno: 9 is EBADF for a handle that
# was never opened, 11 is EAGAIN for an open socket with nothing pending.
STATUS_NO_DATA_PREFIX = b"02,NO DATA"

# A handle no OPEN command ever returned, so the socket can never supply data
# and the length in the command is the only variable.
NEVER_OPENED = 0x7F

# The largest length READ_SOCKET accepts, measured below: the 896-byte response
# queue less the two header bytes the reply puts in front of the payload.
ACCEPTED_MAX = REPLY_QUEUE_BYTES - 2

# Small enough that a full drain is a few seconds, large enough that a
# truncated read and a complete read differ by an obvious amount.
SMALL_READ = 64
BIG_DATAGRAM = 200

# A datagram long enough to fill any read length this suite asks for.
FULL_DATAGRAM = 1000
# Where the drain of the largest accepted read gives up. The response queue
# holds 896 bytes, so anything past that is the queue failing to end rather
# than a longer reply.
OVERRUN_LIMIT = REPLY_QUEUE_BYTES + 128

# The device's socket carries SO_RCVTIMEO of 40 ms, so a read finds a datagram
# that is already queued and does not wait for one in flight.
DELIVERY_SETTLE_SECONDS = 0.4
PEER_TIMEOUT_SECONDS = 10.0
RESET_SETTLE_SECONDS = 3.0

TESTS = [
    "read-length-limit",
    "largest-accepted-read-drains",
    "datagram-size-ceiling",
    "udp-truncation-is-detectable",
    "tcp-read-is-lossless",
    "oversize-request-keeps-the-datagram",
]

# The largest UDP payload that fits one Ethernet frame: 1500 less 20 bytes of
# IP header and 8 of UDP header. Anything above it is fragmented on the wire.
UNFRAGMENTED_MAX = 1472


def pattern(size: int, seed: int = 0) -> bytes:
    """A payload whose every byte identifies its own offset.

    251 is prime and below 256, so a run of bytes fixes the offset it started
    at for any payload shorter than 251 * 251.
    """
    return bytes((i + seed) % 251 for i in range(size))


def run_offset(chunk: bytes, seed: int = 0) -> Optional[int]:
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
        self.reply = block.data
        self.status_text = transaction.status_text
        self.overrun = block.overrun
        self.blocks = len(transaction.blocks)
        # The reply's own framing: two header bytes, then the payload.
        self.header = (int.from_bytes(self.reply[:2], "little", signed=True)
                       if len(self.reply) >= 2 else None)
        self.payload = self.reply[2:]

    @property
    def observable(self) -> Tuple:
        """Everything a client could branch on, as one comparable value.

        Two reads that differ in what was actually delivered but agree here are
        indistinguishable to any client, however careful it is.
        """
        return (self.status_byte, self.header, len(self.payload),
                self.status_text, self.overrun)

    def describe(self) -> str:
        return (f"{self.state_name}, header {self.header}, payload {len(self.payload)} bytes"
                f"{f' from offset {run_offset(self.payload)}' if self.payload else ''}"
                f", status {self.status_text!r}"
                + (f", overrun {self.overrun.hex(' ')}" if self.overrun else ""))


class Net:
    """The network target's commands, over the command interface."""

    def __init__(self, uci: Uci) -> None:
        self.uci = uci

    def identify(self):
        return self.uci.transact(bytes([TARGET_NETWORK, NET_CMD_IDENTIFY]))

    def ip_address(self) -> Optional[str]:
        reply = self.uci.transact(bytes([TARGET_NETWORK, NET_CMD_GET_IPADDR, 0])).data
        return ".".join(str(b) for b in reply[:4]) if len(reply) >= 4 else None

    def _open(self, command: int, ip: str, port: int, what: str) -> int:
        message = (bytes([TARGET_NETWORK, command, port & 0xFF, (port >> 8) & 0xFF])
                   + ip.encode("ascii") + b"\x00")
        result = self.uci.transact(message)
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

    def read(self, handle: int, maxlen: int, overrun_reads: int = 0) -> ReadResult:
        command = bytes([TARGET_NETWORK, NET_CMD_READ_SOCKET, handle,
                         maxlen & 0xFF, (maxlen >> 8) & 0xFF])
        return ReadResult(self.uci.transact(command, overrun_reads=overrun_reads))

    def close(self, handle: int):
        return self.uci.transact(bytes([TARGET_NETWORK, NET_CMD_CLOSE_SOCKET, handle]))

    def drain_socket(self, handle: int, maxlen: int = ACCEPTED_MAX, limit: int = 8) -> int:
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
        self.udp_peer: Optional[Tuple[str, int]] = None
        self.tcp = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.tcp.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.tcp.bind((bind_ip, 0))
        self.tcp.listen(2)
        self.tcp_port = self.tcp.getsockname()[1]
        self.tcp_conn: Optional[socket.socket] = None

    def learn_udp_peer(self, net: Net, handle: int) -> Tuple[str, int]:
        """The device's ephemeral source address, from a datagram it sends.

        The device connects its UDP socket, so it only accepts datagrams from
        the address it connected to, and a reply has to come from this bound
        port back to whatever source port the device chose.
        """
        net.write(handle, b"PROBE")
        self.udp.settimeout(PEER_TIMEOUT_SECONDS)
        _, address = self.udp.recvfrom(2048)
        self.udp_peer = address
        return address

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
    """
    section("read-length-limit")
    ok = True

    with check(f"READ_SOCKET accepts a length of {ACCEPTED_MAX}"):
        result = net.read(NEVER_OPENED, ACCEPTED_MAX, overrun_reads=4)
        detail(result.describe())
        if not result.status_text.startswith(STATUS_NO_DATA_PREFIX):
            raise Failure(
                f"length {ACCEPTED_MAX} answered {result.status_text!r}; the socket was "
                f"never opened, so the answer has to come from the socket "
                f"({STATUS_NO_DATA_PREFIX.decode()}), not from the length")
        if result.header != -1:
            raise Failure(f"a refused socket read should report -1 in the reply header, "
                          f"got {result.header}")

    with check(f"READ_SOCKET refuses a length of {ACCEPTED_MAX + 1}"):
        result = net.read(NEVER_OPENED, ACCEPTED_MAX + 1, overrun_reads=4)
        detail(result.describe())
        if result.status_text != STATUS_OUT_OF_RANGE:
            raise Failure(f"length {ACCEPTED_MAX + 1} answered {result.status_text!r}, "
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

    with check("the refusal boundary is where the response queue ends"):
        accepted = net.read(NEVER_OPENED, ACCEPTED_MAX)
        refused = net.read(NEVER_OPENED, ACCEPTED_MAX + 1)
        if not accepted.status_text.startswith(STATUS_NO_DATA_PREFIX):
            raise Failure(f"length {ACCEPTED_MAX} was refused: {accepted.status_text!r}")
        if refused.status_text != STATUS_OUT_OF_RANGE:
            raise Failure(f"length {ACCEPTED_MAX + 1} was accepted: {refused.status_text!r}")
        detail(f"accepted up to {ACCEPTED_MAX}, refused from {ACCEPTED_MAX + 1}: the "
               f"{REPLY_QUEUE_BYTES}-byte response queue less the 2-byte reply header")
    return ok


def run_largest_accepted_read_drains(net: Net, peer: Peer) -> bool:
    """The largest length READ_SOCKET accepts has to produce a reply a client can read.

    A reply block is a 2-byte header plus the payload, so a read of 894 bytes
    builds a block of exactly 896, the size of the response queue. Measured on
    a U64 Elite: at 893 the queue delivers 895 bytes and DATA_AV clears; at 894
    DATA_AV never clears and every further read returns the same last byte.

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
    """
    section("largest-accepted-read-drains")
    handle = net.open_udp(peer.ip, peer.udp_port)
    try:
        peer.learn_udp_peer(net, handle)
        measured = {}
        for maxlen in (ACCEPTED_MAX - 1, ACCEPTED_MAX):
            net.drain_socket(handle)
            peer.send_udp(pattern(FULL_DATAGRAM))
            time.sleep(DELIVERY_SETTLE_SECONDS)
            measured[maxlen] = drain_count(net.uci, handle, maxlen)
            block, cleared, tail = measured[maxlen]
            detail(f"length {maxlen}: reply block {block} bytes, DATA_AV cleared {cleared}, "
                   f"last bytes {tail.hex(' ')}")

        with check(f"a read of {ACCEPTED_MAX - 1} bytes ends its reply"):
            block, cleared, _ = measured[ACCEPTED_MAX - 1]
            if not cleared:
                raise Failure(f"DATA_AV was still set after {block} bytes")
            if block != ACCEPTED_MAX + 1:
                raise Failure(f"expected {ACCEPTED_MAX + 1} bytes (2 header + "
                              f"{ACCEPTED_MAX - 1} payload), got {block}")

        with check(f"a read of {ACCEPTED_MAX} bytes, the largest accepted, ends its reply"):
            block, cleared, tail = measured[ACCEPTED_MAX]
            if not cleared:
                raise Failure(
                    f"DATA_AV was still set after {block} bytes and the queue was still "
                    f"repeating {tail[-1:].hex()}. A read of {ACCEPTED_MAX} bytes builds a "
                    f"reply block of {ACCEPTED_MAX + 2} bytes, the exact size of the "
                    f"{REPLY_QUEUE_BYTES}-byte response queue, and that block never ends. "
                    f"A client that reads while DATA_AV is set never stops")
    finally:
        net.close(handle)
    return True


def drain_count(uci: Uci, handle: int, maxlen: int) -> Tuple[int, bool, bytes]:
    """Push one READ_SOCKET and pull bytes until DATA_AV clears or the cap is hit.

    Returns the number of bytes the queue handed out, whether DATA_AV cleared,
    and the last few bytes, which say whether the queue was repeating itself.
    """
    from uci import (CTRL_DATA_ACC, REG_RESPONSE, REG_STATUS, ST_DATA_AV, ST_STAT_AV)
    uci.release()
    uci.require_idle("before the largest accepted read")
    command = bytes([TARGET_NETWORK, NET_CMD_READ_SOCKET, handle,
                     maxlen & 0xFF, (maxlen >> 8) & 0xFF])
    uci.push(command)
    uci.wait_for_reply(command)
    out = bytearray()
    while uci.status() & ST_DATA_AV:
        out.append(uci.peek(REG_RESPONSE))
        if len(out) >= OVERRUN_LIMIT:
            break
    cleared = not (uci.status() & ST_DATA_AV)
    uci._drain(ST_STAT_AV, REG_STATUS, "status")
    uci.control(CTRL_DATA_ACC)
    # Writing DATA_ACC leaves the data state whether or not the queue emptied,
    # so the interface is usable again even after a block that never ended.
    uci.release()
    return len(out), cleared, bytes(out[-6:])


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
        arrivals: Dict[int, int] = {}
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
        follow_up = net.read(handle, ACCEPTED_MAX)

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


def run_tcp_lossless(net: Net, peer: Peer) -> bool:
    """The same command over TCP loses nothing, which is the control for UDP.

    read_socket() never looks at the socket type, so this runs the identical
    firmware path with the identical length. Anything UDP loses here is lost by
    the socket layer under it, not by the command interface or by the length.
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
        chunks: List[ReadResult] = []
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
    handle = net.open_udp(peer.ip, peer.udp_port)
    try:
        peer.learn_udp_peer(net, handle)
        net.drain_socket(handle)
        peer.send_udp(pattern(SMALL_READ))
        time.sleep(DELIVERY_SETTLE_SECONDS)

        refused = net.read(handle, ACCEPTED_MAX + 1, overrun_reads=2)
        after = net.read(handle, SMALL_READ)

        with check(f"a request of {ACCEPTED_MAX + 1} bytes is refused without touching the socket"):
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


def restore_settings(device, original: Dict[str, str], keep: bool) -> bool:
    if keep or not original:
        return True
    ok = True
    for item, value in original.items():
        with check(f"restore {item!r} to {value!r}"):
            device.configs.set(CONFIG_CATEGORY, item, value)
    return ok


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Verify how the UCI network target's READ_SOCKET handles the requested "
                    "length and datagrams larger than it (GideonZ/1541ultimate#802)."
    )
    parser.add_argument("-H", "--host", default=os.environ.get("U64_HOST", "u64"))
    parser.add_argument("-p", "--password", default=os.environ.get("U64_PASS"))
    parser.add_argument("-t", "--timeout", type=float,
                        default=float(os.environ.get("U64_TIMEOUT", "30.0")))
    parser.add_argument("-b", "--busy-timeout", type=float, default=15.0,
                        help="How long one command may stay in Command Busy before it "
                             "counts as wedged.")
    parser.add_argument("--test", action="append", choices=["all"] + TESTS)
    parser.add_argument("--keep-config", action="store_true",
                        help="Don't restore the original Command Interface setting on exit.")
    args = parser.parse_args()

    selected = TESTS if not args.test or "all" in args.test else [t for t in TESTS if t in args.test]
    target = targets.parse(args.host)
    device = UltimateApi(target.device, args.password, args.timeout)
    # The command interface registers are decoded on the C64 expansion bus, so
    # the machine that reads them back has to be the one driving that bus. On a
    # single-machine target these are the same host; on cartridge@computer they
    # are not. See tests/lib/targets.py.
    computer = UltimateApi(target.computer, args.password, args.timeout)
    uci = Uci(computer.machine, args.busy_timeout)
    net = Net(uci)

    original: Dict[str, str] = {}
    results: Dict[str, bool] = {}
    peer: Optional[Peer] = None
    interface_enabled = False
    cleanup_ok = True

    def run(name: str, fn, *fn_args) -> None:
        """Run one scenario; a failed check ends that scenario, not the run.

        report.check re-raises, so the first failing check leaves its scenario
        immediately. The others still have to run: this suite is measuring
        several independent properties of the same command, and stopping at the
        first one would hide the rest.
        """
        if name not in selected:
            return
        results[name] = False  # so an aborted scenario reports FAIL, not "not reached"
        try:
            results[name] = fn(*fn_args)
        except Wedged:
            # The interface is stuck for every target; nothing after this can run.
            raise
        except Failure as exc:
            detail(f"{name} stopped: {format_exception(exc)}")

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
            uci.release()

        if not interface_present(uci):
            check_start("this machine answers at the Command Interface registers")
            check_skip("$DF1B-$DF1F all read $FF, so no command interface is present "
                       "at those addresses on this machine")
            suite_ok("net_target_test")
            return 0

        with check("the network target identifies itself"):
            result = net.identify()
            if result.status_text != STATUS_OK or not result.data:
                raise Failure(f"NET_CMD_IDENTIFY answered {result.status_text!r} "
                              f"with reply {result.data!r}")
            detail(result.data.decode("latin-1"))

        run("read-length-limit", run_read_length_limit, net)

        needs_peer = [name for name in
                      ("largest-accepted-read-drains", "datagram-size-ceiling",
                       "udp-truncation-is-detectable",
                       "tcp-read-is-lossless", "oversize-request-keeps-the-datagram")
                      if name in selected]
        if needs_peer:
            check_start("this host can act as the device's network peer")
            try:
                peer = Peer(local_address_towards(target.device))
                # Proves the route in the direction that matters: the device
                # opening a socket back to this process and its datagram
                # arriving here.
                handle = net.open_udp(peer.ip, peer.udp_port)
                try:
                    peer.learn_udp_peer(net, handle)
                finally:
                    net.close(handle)
                check_ok(f"{peer.ip} UDP {peer.udp_port}, TCP {peer.tcp_port}")
            except (Failure, OSError, socket.timeout) as exc:
                check_skip(f"the device could not reach this host: {format_exception(exc)}")
                if peer is not None:
                    peer.close()
                peer = None

        if peer is not None:
            run("largest-accepted-read-drains", run_largest_accepted_read_drains, net, peer)
            run("datagram-size-ceiling", run_datagram_size_ceiling, net, peer)
            run("udp-truncation-is-detectable", run_udp_truncation, net, peer)
            run("tcp-read-is-lossless", run_tcp_lossless, net, peer)
            run("oversize-request-keeps-the-datagram",
                run_oversize_request_keeps_datagram, net, peer)
        else:
            for name in needs_peer:
                results.setdefault(name, None)

    except Failure as exc:
        suite_fail("net_target_test", format_exception(exc))
    finally:
        if peer is not None:
            peer.close()
        # A failed scenario can leave the interface holding a reply, so hand the
        # data back before the setting goes home.
        released = uci.release() if interface_enabled else True
        if not released:
            warn("the command interface did not return to Idle")
        restored = restore_settings(device, original, args.keep_config)
        cleanup_ok = released and restored

    section("summary")
    all_ok = cleanup_ok
    for name in selected:
        outcome = results.get(name)
        state = OK if outcome else (FAIL if outcome is False else SKIP)
        if outcome is False:
            all_ok = False
        detail(f"{name}: {state}")
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
