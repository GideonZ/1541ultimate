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
daemon may refuse these datagrams outright. One datagram is one line, empty
lines never arrive because the firmware skips them, and carriage returns never
arrive because `Syslog::charout` discards them.

**The log is incomplete by construction and the loss is not measurable from
here.** Four independent causes, none of which leaves a trace on this side:

- UDP has no retransmission, so a datagram lost in the network is gone.
- The forwarding buffer is 16 KB (`syslog_bufsize` in
  `software/application/ultimate/ultimate.cc`). On overflow `Syslog::charout`
  sets a flag and drops every subsequent character, and `forwardLogging` then
  rewinds and discards the whole buffer, so a burst loses an unbounded block.
- Output is throttled to about 200 lines a second by a 5ms delay per line.
- `Syslog::failed_sends` counts send errors and nothing ever reads it.

A line's receive time also lags the moment the firmware printed it by an
unbounded amount: the forwarding task polls every 100ms, the throttle means a
200-line burst takes at least a second to drain, and everything printed before
the network link comes up arrives in one burst afterwards. So a line is
attributed as "received during this check", never as "produced during it".

An assertion failure never arrives at all. `vAssertCalled` disables interrupts,
prints, and spins, so the syslog task never runs again and the text sits in the
buffer. What a reader sees is the log stopping, which is a signal in its own
right.

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
from typing import Callable, Dict, List, Optional, Sequence, Tuple

import targets as targets_lib

# A non-privileged port, and the devices are configured to it rather than to
# 514. `Syslog::init` defaults to 514 only when the configured value carries no
# `:<port>` suffix and accepts any port from 1 to 65535, and binding 514 needs
# root on Linux and on macOS. Running a whole gate as root to collect a log is
# a worse trade than a port number.
DEFAULT_PORT = 5514

# The configuration item the devices carry, in the store it lives in.
CONFIG_STORE = "Network Settings"
CONFIG_ITEM = "Log to Syslog Server"

# The first distinctive boot marker that reaches the syslog, printed by
# `ultimate_main` and immediately followed by the FreeRTOS task list. There is
# no uptime counter in the firmware and nothing on the REST surface answers
# "has this device rebooted", so this is the only signal there is.
BOOT_MARKER = "All linked modules have been initialized and are now running."

# One datagram is one line, and the firmware sends the line text alone. 2 KB is
# far above anything `small_printf` produces and bounds a stray sender.
MAX_DATAGRAM = 2048

# How long the receive loop blocks before looking at whether it should stop.
POLL_SECONDS = 0.25


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
        self.unmapped_path = os.path.join(self.directory, "syslog-unmapped.txt")
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
                        "attributed and land in syslog-unmapped.txt")
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

    def __enter__(self) -> "Collector":
        return self

    def __exit__(self, *_exc) -> None:
        self.stop()

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
        """Write one datagram's line. The receive path, exposed for a test.

        The receive time is the only time any log line carries: nothing in the
        payload has one, and the device's own clock is never used.
        """
        when = self.clock()
        text = data.decode("utf-8", "replace").rstrip("\r\n")
        route = self.routes.get(address)
        if route is None:
            # A device nobody expected to be talking is itself worth knowing
            # about, so its lines are kept rather than dropped, with the
            # address that sent them.
            self._append(self.unmapped_path, f"{when:.3f} {address} {text}\n")
            with self._lock:
                self.unmapped += 1
            return
        self._append(route.path, f"{when:.3f} {text}\n")
        with self._lock:
            self.lines += 1

    def _append(self, path: str, line: str) -> None:
        with self._lock:
            handle = self._handles.get(path)
            if handle is None:
                try:
                    os.makedirs(os.path.dirname(path), exist_ok=True)
                    handle = open(path, "a", encoding="utf-8")
                except OSError as exc:
                    self.problems.append(f"{path} could not be written: {exc}")
                    self._handles[path] = _Discard()
                    return
                self._handles[path] = handle
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
    its device's, so one rule decides what belongs to whom.
    """
    try:
        found = socket.getaddrinfo(machine, 0, socket.AF_INET, socket.SOCK_DGRAM)
    except OSError:
        return []
    return sorted({entry[4][0] for entry in found})


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


def between(path: str, start: float, end: float) -> List[str]:
    """The lines received in an interval, for a report or for a suite."""
    return [text for when, text in read(path) if start <= when <= end]


def restarts(path: str) -> List[float]:
    """When the device restarted, as far as its log can say."""
    return [when for when, text in read(path) if BOOT_MARKER in text]
