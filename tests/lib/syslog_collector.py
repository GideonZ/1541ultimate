"""The device's own log, collected off the network while a run happens.

The firmware's log reaches the network by exactly one route: remote syslog.
`software/network/syslog.cc` opens a UDP socket to the address in
`CFG_NETWORK_REMOTE_SYSLOG_SERVER`, which the configuration tree shows as
`Network Settings` / `Log to Syslog Server`, and it is built by six of the
seven ultimate makefiles, so a U2 logs as well as a U64. There is no HTTP
endpoint that returns a log.

A plain UDP sink, not an RFC syslog daemon. `Syslog::forwardLogging` sends the
bare line text with `send(sockfd, line, linelen, 0)`: no priority prefix, no
version, no timestamp, no hostname, no trailing newline, so a conformant
daemon may refuse these datagrams outright. One datagram is one line, except
from `Syslog::flush`, which sends a block of the buffer; empty lines never
arrive because the firmware skips them, and carriage returns never arrive
because `Syslog::charout` discards them.

**The log is incomplete by construction and the loss is not measurable from
here.** Four independent causes, none of which leaves a trace on this side:

- UDP has no retransmission, so a datagram lost in the network is gone.
- The forwarding buffer is 16 KB (`syslog_bufsize` in
  `software/application/ultimate/ultimate.cc`). On overflow `Syslog::charout`
  sets a flag and drops every subsequent character, and `forwardLogging` then
  rewinds and discards the whole buffer, so a burst loses an unbounded block.
- Output is throttled to about 200 lines a second by a 5ms delay per line.
- `Syslog::failed_sends` counts send errors. `GET /v1/info` carries it, and
  nothing on this side reads it, so a send failure still leaves no trace in a
  run's artefacts.

A line's receive time also lags the moment the firmware printed it by an
unbounded amount: the forwarding task polls every 100ms, the throttle means a
200-line burst takes at least a second to drain, and everything printed before
the network link comes up arrives in one burst afterwards. So a line is
attributed as "received during this check", never as "produced during it".

An assertion failure arrives only from firmware that flushes it. `vAssertCalled`
disables interrupts and spins, so the forwarding task never runs again; the
firmware in this repository calls `Syslog::flush` from the failing task first,
and firmware without that flush leaves the text in the buffer. Either way the
log stops there, which is a signal in its own right.

Exactly one process binds the port. The datagrams are unicast, and two sockets
bound to one UDP port do not both receive each one: the kernel picks one per
datagram, so a second reader would silently take about half the lines with
nothing in either looking wrong. Multicast behaves the opposite way, which is
why the video streams deliberately share a port and this does not. A suite that
needs device log lines reads the file this writes, through `read` below.
"""

from __future__ import annotations

import os
import socket
import threading
import time
from dataclasses import dataclass, field
from typing import Callable, Dict, List, Optional, Sequence, Set, Tuple

import targets as targets_lib

# A non-privileged port, and the devices are configured to it rather than to
# 514. `Syslog::init` defaults to 514 only when the configured value carries no
# `:<port>` suffix and accepts any port from 1 to 65535, and binding 514 needs
# root on Linux and on macOS. Running a whole gate as root to collect a log is
# a worse trade than a port number.
DEFAULT_PORT = 5514

# Where an operator declares an address a machine logs from that its name does
# not resolve to. See `declared`.
ADDRESS_ENV = "U64_LOG_ADDRESSES"

# Where lines whose sender is no machine of any target in the run are kept.
#
# Named for what a reader has to decide about them rather than for the lookup
# that failed: the question is who sent them, and the answer this file gives
# is the address and nothing else. Nothing here ever guesses a target for such
# a line, because the source address of a datagram is the only identification
# a datagram carries and an unrecognised one identifies nothing.
UNKNOWN_SENDER_NAME = "syslog-unknown-sender.txt"

# The configuration item the devices carry, in the store it lives in.
CONFIG_STORE = "Network Settings"
CONFIG_ITEM = "Log to Syslog Server"

# One datagram carries one line from the forwarding task, and several from a
# flush, and in both cases the line text is the whole payload. 2 KB is far
# above anything `small_printf` produces and bounds a stray sender.
MAX_DATAGRAM = 2048

# How long the receive loop blocks before looking at whether it should stop.
POLL_SECONDS = 0.25

# How long a device that has been logging may say nothing before the run
# records the silence.
#
# Measured over a three-target run of the machine-code monitor suite: a device
# being driven logs a line every 5ms at the median and every 0.12s at the 99th
# percentile, so an interval of seconds is already far outside what a busy
# device produces. What sets the value is not that but the other case: a device
# that has finished its own suites and is idle while another target holds the
# C64 legitimately says nothing at all, and in that run the longest such
# silence was 31s. Thirty seconds keeps the ordinary quiet of a driven device
# off the timeline without claiming to tell an idle device from a stopped one,
# which nothing in a log can do; the reader decides, from the suite the
# timeline puts it beside.
#
# OBS-7.15 is why it is worth recording at all: a device that has stopped goes
# on saying nothing, and the log ending is the signal, whether or not the
# firmware managed to flush a last message before it stopped.
SILENT_SECONDS = 30.0


@dataclass
class Route:
    """Where one address's lines go, and what to call the machine that sent them."""

    path: str
    machine: str
    target: str


@dataclass
class Collector:
    """A UDP sink for every device in a run.

    Runs in the one process that owns the whole run, because it binds one port
    and maps source addresses to targets, so it has to know every target and
    there has to be exactly one of it.
    """

    directory: str
    port: int = DEFAULT_PORT
    clock: Callable[[], float] = time.time
    routes: Dict[str, Route] = field(default_factory=dict)
    unmapped_path: str = ""
    started: float = 0.0
    lines: int = 0
    unmapped: int = 0
    problems: List[str] = field(default_factory=list)
    # When each machine last said anything, and the silences that followed.
    # See `gaps` for what counts as one.
    seen: Dict[str, float] = field(default_factory=dict)
    silences: List[dict] = field(default_factory=list)
    # Which addresses actually sent lines, per target token, and how many
    # each sent. The mapped addresses are what the run expected; these are
    # what it got, and a device that logs from a second interface is the
    # difference between the two.
    senders: Dict[str, Dict[str, int]] = field(default_factory=dict)
    # The same for the addresses no target claimed.
    unknown: Dict[str, int] = field(default_factory=dict)
    _socket: Optional[socket.socket] = None
    _thread: Optional[threading.Thread] = None
    _running: bool = False
    _handles: Dict[str, object] = field(default_factory=dict)
    _lock: threading.Lock = field(default_factory=threading.Lock)

    # -- lifecycle --

    def bind(self, wanted: Sequence[targets_lib.Target]) -> bool:
        """Resolve every target's machines and open the port. Never raises.

        Reports what went wrong once, here, rather than late in a run that has
        already cost 15 to 30 minutes. A collector that cannot start leaves the
        run exactly as it was.
        """
        self.unmapped_path = os.path.join(self.directory, UNKNOWN_SENDER_NAME)
        for target in wanted:
            for index, machine in enumerate(target.log_hosts):
                # The device under test's log is the log about the firmware
                # being tested, so it gets the plain name; a cartridge's
                # computer logs as well and gets its own file.
                name = "syslog.txt" if index == 0 else f"syslog-{machine}.txt"
                path = os.path.join(self.directory, target.slug, name)
                addresses = resolve(machine)
                if not addresses:
                    self.problems.append(
                        f"{machine} does not resolve, so its lines cannot be "
                        f"attributed and land in {UNKNOWN_SENDER_NAME}")
                for address in addresses:
                    taken = self.routes.get(address)
                    if taken is not None:
                        # Two machines at one address cannot be told apart by
                        # anything in a datagram, so the first claim stands and
                        # the ambiguity is reported rather than resolved.
                        self.problems.append(
                            f"{machine} and {taken.machine} are both {address}, "
                            f"so those lines are all attributed to "
                            f"{taken.target}")
                        continue
                    self.routes[address] = Route(path, machine, target.token)

        # A name in U64_LOG_ADDRESSES that no target has is almost always a
        # typo, and its symptom is exactly the one the variable exists to
        # remove: every line from that device in syslog-unknown-sender.txt with
        # nothing saying why.
        machines = {machine for target in wanted for machine in target.log_hosts}
        for entry in (os.environ.get(ADDRESS_ENV) or "").split(","):
            name = entry.partition("=")[0].strip()
            if name and name not in machines:
                self.problems.append(
                    f"{ADDRESS_ENV} names {name!r}, which is not a machine of "
                    f"any target in this run: {sorted(machines)}")

        # 0.0.0.0 rather than a chosen interface: the runner host may have more
        # than one, and a datagram's source address is what identifies a
        # device, so binding the wildcard costs nothing and removes an operator
        # decision.
        try:
            sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            sock.bind(("0.0.0.0", self.port))
            sock.settimeout(POLL_SECONDS)
        except OSError as exc:
            self.problems.append(f"port {self.port} could not be opened: {exc}")
            return False
        self._socket = sock
        self.port = sock.getsockname()[1]
        self.started = self.clock()
        # Opened now rather than on the first datagram. A file that cannot be
        # written is a startup problem the operator can act on; discovered
        # later it is a silently discarded log, reported nowhere, in a run that
        # has already cost 15 to 30 minutes.
        for path in sorted({route.path for route in self.routes.values()}
                           | {self.unmapped_path}):
            self._open(path)
        self._running = True
        self._thread = threading.Thread(target=self._receive, name="syslog",
                                        daemon=True)
        self._thread.start()
        return True

    def stop(self) -> None:
        self._running = False
        if self._thread is not None:
            self._thread.join(timeout=5)
        if self._socket is not None:
            self._socket.close()
            self._socket = None
        with self._lock:
            for handle in self._handles.values():
                handle.close()
            self._handles.clear()

    # -- receiving --

    def _receive(self) -> None:
        while self._running:
            try:
                data, (address, _port) = self._socket.recvfrom(MAX_DATAGRAM)
            except (socket.timeout, TimeoutError):
                continue
            except OSError:
                return
            self.deliver(address, data)

    def deliver(self, address: str, data: bytes) -> None:
        """Write one datagram's lines. The receive path, exposed for a test.

        The receive time is the only time any log line carries: nothing in the
        payload has one, and the device's own clock is never used.

        The forwarding task sends one line per datagram, but `Syslog::flush`
        sends a block of the buffer, so a datagram can hold several lines. Each
        one gets its own timestamped output line, because a written line whose
        first field is not a timestamp is dropped by `read` below.
        """
        when = self.clock()
        texts = data.decode("utf-8", "replace").rstrip("\r\n").split("\n")
        route = self.routes.get(address)
        if route is None:
            # A device nobody expected to be talking is itself worth knowing
            # about, so its lines are kept rather than dropped, with the
            # address that sent them.
            for text in texts:
                self._append(self.unmapped_path,
                             f"{when:.3f} {address} {text}\n")
            with self._lock:
                self.unmapped += len(texts)
                self.unknown[address] = self.unknown.get(address, 0) + len(texts)
            return
        for text in texts:
            self._append(route.path, f"{when:.3f} {text}\n")
        with self._lock:
            self.lines += len(texts)
            seen = self.senders.setdefault(route.target, {})
            seen[address] = seen.get(address, 0) + len(texts)
            previous = self.seen.get(route.machine)
            if previous is not None and when - previous >= SILENT_SECONDS:
                self.silences.append({"machine": route.machine,
                                      "target": route.target,
                                      "started": previous, "ended": when})
            self.seen[route.machine] = when

    def _open(self, path: str) -> None:
        """Open one output file, or record why it could not be opened.

        A file that cannot be opened is replaced by a sink that discards, so
        the collector keeps running and the run is unaffected, which is what
        an observability component owes the run it is watching.
        """
        with self._lock:
            if path in self._handles:
                return
            try:
                os.makedirs(os.path.dirname(path), exist_ok=True)
                self._handles[path] = open(path, "a", encoding="utf-8")
            except OSError as exc:
                self.problems.append(f"{path} could not be written: {exc}")
                self._handles[path] = _Discard()

    def _append(self, path: str, line: str) -> None:
        self._open(path)
        with self._lock:
            handle = self._handles[path]
            try:
                handle.write(line)
                handle.flush()
            except OSError:
                pass

    # -- what it collected --

    def files(self) -> List[Tuple[str, str]]:
        """Each target token and the file its lines went to."""
        found = {(route.target, route.path) for route in self.routes.values()}
        return sorted(found)

    def addresses_of(self, token: str) -> List[str]:
        """Every address this run expects `token`'s lines to arrive from."""
        return sorted(address for address, route in self.routes.items()
                      if route.target == token)

    def observed(self, token: str) -> Dict[str, int]:
        """Every address `token`'s lines actually arrived from, with a count."""
        with self._lock:
            return dict(self.senders.get(token, {}))

    def unknown_senders(self) -> Dict[str, int]:
        """Every address no target claimed, with how many lines it sent."""
        with self._lock:
            return dict(self.unknown)

    def gaps(self, until: Optional[float] = None) -> List[dict]:
        """Every interval a device that had been logging said nothing.

        A machine still silent when the run ends carries no `ended`, which is
        the shape OBS-15.11 asks for: a gap that is still open is recorded as
        open rather than closed at an invented time. A machine that never sent
        anything at all is not a gap here, because there is no interval to
        bound; the run already records that the collector started and the
        report says the file is empty.
        """
        when = self.clock() if until is None else until
        with self._lock:
            found = list(self.silences)
            for machine, last in sorted(self.seen.items()):
                if when - last >= SILENT_SECONDS:
                    target = next((route.target for route in self.routes.values()
                                   if route.machine == machine), "")
                    found.append({"machine": machine, "target": target,
                                  "started": last})
        return found


class _Discard:
    """Stands in for a file that could not be opened, so the run carries on."""

    def write(self, _line: str) -> None:
        return

    def flush(self) -> None:
        return

    def close(self) -> None:
        return


def resolve(machine: str) -> List[str]:
    """Every IPv4 address a machine answers to, or an empty list.

    The same call `av_stream.AvStreamCapture` uses to decide which packets are
    its device's, so one rule decides what belongs to whom, plus whatever
    `U64_LOG_ADDRESSES` adds for that machine.
    """
    try:
        found = socket.getaddrinfo(machine, 0, socket.AF_INET, socket.SOCK_DGRAM)
    except OSError:
        found = []
    addresses = {entry[4][0] for entry in found}
    return sorted(addresses | declared(machine))


def declared(machine: str) -> "Set[str]":
    """Addresses an operator has attached to a machine by hand.

    A device with two interfaces logs from whichever one the route picks, and
    that is not always the address its name resolves to: an Ultimate 64 on
    both Ethernet and WiFi answers REST on one and sends its log from the
    other. Nothing on the device's REST surface reports its interfaces, so the
    second address is something the person who set the machine up knows and
    the harness cannot discover.

        U64_LOG_ADDRESSES="u64=192.168.1.71,c64u=192.168.1.150"

    A datagram from an address nobody declared is still kept, in
    `syslog-unknown-sender.txt` with its sender, which is what makes the
    omission visible rather than silent.

    This is an escape hatch for a machine outside this repository, not the
    answer for an Ultimate: the firmware here now sends its log from the
    wired interface when there is one, so an Ultimate's log arrives from the
    address its name resolves to.
    """
    found = set()
    for entry in (os.environ.get(ADDRESS_ENV) or "").split(","):
        name, _, address = entry.partition("=")
        if name.strip() == machine and address.strip():
            found.add(address.strip())
    return found


# ---------------------------------------------------------------------------
# Reading what it wrote
# ---------------------------------------------------------------------------


def read(path: str) -> List[Tuple[float, str]]:
    """Every line in a collected log, with the time it was received.

    The interface for a suite that needs to assert on a device log line: it
    reads this rather than opening a socket, because a second socket on the
    port would silently take about half the datagrams.
    """
    found: List[Tuple[float, str]] = []
    try:
        with open(path, encoding="utf-8", errors="replace") as handle:
            for line in handle:
                stamp, _, text = line.rstrip("\n").partition(" ")
                try:
                    found.append((float(stamp), text))
                except ValueError:
                    continue
    except OSError:
        return []
    return found
