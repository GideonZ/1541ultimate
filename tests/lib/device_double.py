"""One fake Ultimate, on loopback, for the observability tests.

The code under test reaches it exactly as it reaches a real device: through
`targets.Target` pointed at loopback with this double's ports, so
`api.UltimateApi`, `rest.py` and `health.py` run unmodified and the real
transport, the real retry policy, the real X-Password header and the real 404
branches are all exercised. Nothing here is injected into the code under test
and nothing here replaces a library with a fake object.

The seam is the address because the defects this code actually has are in the
transport and the protocol rather than at the call sites: a retry that repeats
a request the device already acted on, a 404 read as a transport failure, a
connection that opens and never answers, a body of the wrong length. A mock
object would pass through every one of them.

It is scripted rather than interactive. A test says what the device does, in
order, including what it does wrong, which is the only way to reach the faults
below: no real device can be asked to answer 404 on demand or to stop
answering halfway through a run.

    with DeviceDouble() as double:
        api = UltimateApi(double.target())
        api.version()
        double.faults.offline = True

What it serves:

    REST      version, info, machine:menu_screen, machine:readmem,
              machine:heap, machine:input, machine:reset and the other machine
              actions, the drives listing, a few configuration items, and
              streams:start and streams:stop
    FTP       the 220 banner the health sweep reads, and nothing else
    Telnet    an accepted connection, which is all the health sweep asks for
    DMA       the IDENTIFY exchange on the control port

What it does not serve, deliberately: the C64, the UI object stack, the file
system, a real FTP session or a real Telnet session. A suite that drives the
menu still needs a device.
"""

from __future__ import annotations

import http.server
import json
import os
import socket
import socketserver
import struct
import threading
import urllib.parse
from dataclasses import dataclass, field
from typing import Dict, List, Tuple

import targets

LOOPBACK = "127.0.0.1"

# The menu screen is two 40x25 planes, characters then colour, per the
# firmware's ACTIVE_SCREEN_MATRIX constants.
SCREEN_COLS = 40
SCREEN_ROWS = 25
SCREEN_CELLS = SCREEN_COLS * SCREEN_ROWS
SCREEN_BYTES = SCREEN_CELLS * 2

# The DMA control port's IDENTIFY command, framed as
# tests/lib/health.py sends it: a little-endian command word and length,
# answered by a length-prefixed title.
DMA_CMD_IDENTIFY = 0xFF0E
IDENTIFY_TITLE = b"ULTIMATE-DOUBLE"

DEFAULT_PRODUCT = "Ultimate 64"
DEFAULT_FIRMWARE = "3.15"
DEFAULT_FPGA = "1.4E"


@dataclass
class Faults:
    """Everything the double can do wrong, one switch each.

    Each switch exists because some component has to survive it and the
    surviving is what the tests assert. Flipping one mid-test is how a fault
    that arrives partway through a run is reproduced.
    """

    # machine:menu_screen answers 404, which on this endpoint means no menu is
    # open rather than old firmware. The ordinary case, not the rare one.
    menu_screen_404: bool = False
    # machine:heap answers 404, which is firmware predating the endpoint.
    heap_404: bool = False
    # machine:heap answers 200 with a field a decoder cannot turn into a
    # number, which is what a firmware change or a truncated body looks like
    # to everything that reads it.
    heap_malformed: bool = False
    # Close the connection without answering, which is what a device that has
    # gone off the network looks like to a client that can still route to it.
    offline: bool = False


@dataclass
class Request:
    """One request the double answered, for a test asserting on device traffic."""

    method: str
    path: str
    params: Dict[str, str] = field(default_factory=dict)

    @property
    def key(self) -> Tuple[str, str]:
        return (self.method, self.path)


class _Handler(http.server.BaseHTTPRequestHandler):
    """The REST face. One method per verb, one route table."""

    # BaseHTTPRequestHandler logs every request to stderr, which would bury a
    # test run's own output.
    def log_message(self, template: str, *args) -> None:
        return

    @property
    def double(self) -> "DeviceDouble":
        return self.server.double  # type: ignore[attr-defined]

    def do_GET(self) -> None:
        self._serve("GET")

    def do_PUT(self) -> None:
        self._serve("PUT")

    def do_POST(self) -> None:
        self._serve("POST")

    def _serve(self, method: str) -> None:
        parsed = urllib.parse.urlparse(self.path)
        params = {k: v[0] for k, v in urllib.parse.parse_qs(parsed.query).items()}
        length = int(self.headers.get("Content-Length") or 0)
        if length:
            self.rfile.read(length)
        double = self.double
        double.record(Request(method, parsed.path, params))

        faults = double.faults
        if faults.offline or (double.offline_flag
                              and os.path.exists(double.offline_flag)):
            # No status line at all: the client sees the connection close,
            # which is what a device that has stopped answering produces.
            self.close_connection = True
            return
        if double.password and \
                self.headers.get("X-Password") != double.password:
            self._send(403, b'{"errors":["wrong password"]}', "application/json")
            return

        handler = double.route(method, parsed.path)
        if handler is None:
            self._send(404, b'{"errors":["no such route"]}', "application/json")
            return
        status, body, content_type = handler(params)
        self._send(status, body, content_type)

    def _send(self, status: int, body: bytes, content_type: str) -> None:
        self.send_response(status)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        if body:
            self.wfile.write(body)


class _Server(socketserver.ThreadingMixIn, http.server.HTTPServer):
    daemon_threads = True
    allow_reuse_address = True


class DeviceDouble:
    """A fake device on loopback. Start it, point a handle at it, stop it."""

    def __init__(self, password: str = "") -> None:
        self.password = password
        self.faults = Faults()
        self.requests: List[Request] = []
        self.product = DEFAULT_PRODUCT
        self.firmware_version = DEFAULT_FIRMWARE
        self.fpga_version = DEFAULT_FPGA
        # What machine:menu_screen answers with while a menu is open. 25 rows
        # of text, rendered into the character plane on demand.
        self.menu_rows: List[str] = ["Ultimate 64 menu".ljust(SCREEN_COLS)] + \
            [f"row {n:02d}".ljust(SCREEN_COLS) for n in range(1, SCREEN_ROWS)]
        # When set, a menu counts as open for exactly as long as this path
        # exists. A file rather than a flag for the same reason
        # withhold_ftp_banner_while takes one: the processes that have to agree about it are a scripted
        # suite and this object, and they share no memory.
        self.menu_open_flag = ""
        self.heap_free = 1_500_000
        self.heap_min_ever_free = 1_200_000
        self.heap_total = 2_000_000
        self.mounted_image = ""
        # What the double was asked to stream, and where to. A caller that has
        # to leave the streams as it found them is tested against these.
        self.streams_started: List[str] = []
        self.streams_stopped: List[str] = []
        self.stream_address: Dict[str, str] = {}
        # When set, a keystroke moves which row carries the reverse-video bit
        # rather than changing any text, which is what moving a cursor through
        # a listing looks like on the wire.
        self.move_selection_on_input = False
        self.selected_row = 0
        # The settings the observability code and the UI backend read. Not the
        # whole tree: a fake of several hundred items would be a second
        # implementation of the device's configuration rather than a stand-in
        # for the few things anything here asks about.
        self.configs: Dict[str, Dict[str, str]] = {
            # The value tests/e2e/lib/ui_backend.py asks for under --mode
            # overlay, so a session starts without switching the setting.
            "User Interface Settings": {"Interface Type": "Overlay on HDMI"},
            "Network Settings": {"Log to Syslog Server": ""},
        }
        # When set, the device counts as gone for as long as this path exists.
        # A file for the same reason the other two switches take one: a
        # scripted suite and this object share no memory.
        self.offline_flag = ""
        # Every readmem answer counts up, so the health sweep's raster and
        # jiffy checks see a value that moves without the test waiting for a
        # real machine.
        self._tick = 0
        self._keys = 0
        self._lock = threading.Lock()

        self._http = _Server((LOOPBACK, 0), _Handler)
        self._http.double = self  # type: ignore[attr-defined]
        self._http_thread = threading.Thread(target=self._http.serve_forever,
                                             name="double-http", daemon=True)
        self._http_thread.start()
        self._ftp = _BannerListener(b"220 Ultimate FTP\r\n")
        self._telnet = _BannerListener(b"")
        self._dma = _DmaListener()

    # -- addressing --

    @property
    def rest_port(self) -> int:
        return self._http.server_address[1]

    def target(self, token: str = LOOPBACK) -> targets.Target:
        """A handle addressing this double, for the code under test.

        `token` is whatever the run calls the device, so a test can use a
        cartridge token and still reach one double.
        """
        parsed = targets.parse(token)
        return targets.Target(
            token=parsed.token, device=parsed.device, computer=parsed.computer,
            rest_port=self.rest_port, ftp_port=self._ftp.port,
            telnet_port=self._telnet.port, dma_port=self._dma.port)

    def withhold_ftp_banner(self, withhold: bool = True) -> None:
        """Accept on the FTP port and greet nothing.

        The health sweep reads a missing 220 as a failed listener while every
        other check still passes, which is the one-degraded-check shape.
        """
        self._ftp.refuse = withhold

    def withhold_ftp_banner_while(self, path: str) -> None:
        """Greet nothing on the FTP port for as long as `path` exists.

        A file rather than a flag, because the processes that have to agree
        about it are a scripted suite and a recovery command, and neither of
        them shares memory with this object. It is what makes a run that fails,
        recovers and passes on the second attempt reproducible with no device.
        """
        self._ftp.refuse_flag = path

    def environment(self) -> Dict[str, str]:
        """The variables that point a child process's targets at this double."""
        return {targets.REST_PORT_ENV: str(self.rest_port),
                targets.FTP_PORT_ENV: str(self._ftp.port),
                targets.TELNET_PORT_ENV: str(self._telnet.port),
                targets.DMA_PORT_ENV: str(self._dma.port)}

    # -- bookkeeping --

    def record(self, request: Request) -> None:
        with self._lock:
            self.requests.append(request)

    def calls(self, method: str = "", path: str = "") -> List[Request]:
        """Every recorded request, optionally narrowed to one method or path."""
        with self._lock:
            found = list(self.requests)
        return [r for r in found
                if (not method or r.method == method) and (not path or r.path == path)]

    def clear(self) -> None:
        with self._lock:
            self.requests.clear()

    # -- routes --

    def route(self, method: str, path: str):
        table = {
            ("GET", "/v1/version"): self._version,
            ("GET", "/v1/info"): self._info,
            ("GET", "/v1/machine:menu_screen"): self._menu_screen,
            ("GET", "/v1/machine:readmem"): self._readmem,
            ("GET", "/v1/machine:heap"): self._heap,
            ("GET", "/v1/drives"): self._drives,
            ("GET", "/v1/machine:input"): self._ok_json,
            ("POST", "/v1/machine:input"): self._input,
            ("PUT", "/v1/machine:writemem"): self._ok_json,
            ("POST", "/v1/machine:writemem"): self._ok_json,
        }
        found = table.get((method, path))
        if found is not None:
            return found
        if method == "PUT" and path.startswith("/v1/machine:"):
            return self._ok_json
        if method == "GET" and path.startswith("/v1/configs"):
            return lambda params, path=path: self._config(path)
        if method == "PUT" and path.startswith("/v1/streams/"):
            return lambda params, path=path: self._stream(path, params)
        if method == "PUT" and path.startswith("/v1/drives/"):
            return lambda params, path=path: self._drive_action(path, params)
        if method == "PUT" and path.startswith("/v1/configs"):
            return lambda params, path=path: self._set_config(path, params)
        return None

    def _version(self, params):
        return 200, self._json({"version": "0.1"}), "application/json"

    def _info(self, params):
        return 200, self._json({
            "product": self.product,
            "firmware_version": self.firmware_version,
            "fpga_version": self.fpga_version,
            "hostname": "double"}), "application/json"

    def _menu_screen(self, params):
        if self.menu_open_flag:
            open_now = os.path.exists(self.menu_open_flag)
        else:
            open_now = not self.faults.menu_screen_404
        if not open_now:
            return 404, b"Menu screen unavailable.", "text/plain"
        return 200, self.screen_bytes(), "application/octet-stream"

    def screen_bytes(self) -> bytes:
        """The two-plane payload for the rows this double is showing.

        Characters are literal ASCII, which is what the firmware writes into
        this plane, and the colour plane is a fixed pair of nibbles. Bit 7 is
        set on the selected row's characters, which is how a selection is
        marked on a machine whose colour plane carries no background.
        """
        chars = bytearray(SCREEN_CELLS)
        for row in range(SCREEN_ROWS):
            text = (self.menu_rows[row] if row < len(self.menu_rows) else "")
            text = text[:SCREEN_COLS].ljust(SCREEN_COLS)
            for col, character in enumerate(text):
                value = ord(character) & 0x7F
                reverse = 0x80 if row == self.selected_row else 0
                chars[row * SCREEN_COLS + col] = value | reverse
        return bytes(chars) + bytes([0x0E] * SCREEN_CELLS)

    # "READY." in C64 screen codes, at the top of screen RAM. api.MachineApi
    # polls for it after a reset to decide the machine reached the BASIC
    # prompt, so a double that never showed it would make every reset wait out
    # its whole budget.
    SCREEN_RAM = 0x0400
    READY_SCREEN_CODES = bytes((0x12, 0x05, 0x01, 0x04, 0x19, 0x2E))

    def _readmem(self, params):
        address = int(params.get("address", "0000"), 16)
        length = int(params.get("length", "256"))
        with self._lock:
            self._tick += 1
            tick = self._tick
        if address == self.SCREEN_RAM:
            # Space-filled after the prompt, which is what a C64's screen
            # matrix holds: screen code 0 is `@`, not a blank.
            body = (self.READY_SCREEN_CODES
                    + bytes([0x20]) * length)[:length]
        else:
            # A value that moves on every read, which is what the health
            # sweep's raster and jiffy checks are looking for: they read until
            # the byte changes, so a constant one costs them their whole
            # budget before reporting the machine dead.
            body = bytes((tick + i) & 0xFF for i in range(length))
        return 200, body, "application/octet-stream"

    def _input(self, params):
        """A keystroke, which redraws one row of the menu.

        A real menu changes when a key reaches it, and the settle loops in
        tests/e2e/lib/ui_backend.py wait for exactly that: they poll until the
        screen changes and then until it stops. A double whose screen never
        moved would make every one of those waits run to its timeout.
        """
        with self._lock:
            self._keys += 1
            keys = self._keys
        if self.move_selection_on_input:
            self.selected_row = (self.selected_row + 1) % SCREEN_ROWS
        else:
            self.menu_rows[1] = f"key {keys}".ljust(SCREEN_COLS)
        return 200, self._json({}), "application/json"

    def _stream(self, path, params):
        """streams:start and streams:stop, which are PUT rather than POST.

        Where the device sends is what `start` sets, so the last writer wins
        and a `stop` stops it for everyone. That is why a caller has to ask
        before arming and stop only what it started.
        """
        name, _, action = path[len("/v1/streams/"):].partition(":")
        name = urllib.parse.unquote(name)
        with self._lock:
            if action == "start":
                self.streams_started.append(name)
                self.stream_address[name] = str(params.get("ip", ""))
            elif action == "stop":
                self.streams_stopped.append(name)
        return 200, self._json({}), "application/json"

    def _drive_action(self, path, params):
        slot, _, action = path[len("/v1/drives/"):].partition(":")
        with self._lock:
            if action == "mount":
                self.mounted_image = str(params.get("image", ""))
            elif action in ("remove", "unlink"):
                self.mounted_image = ""
        return 200, self._json({}), "application/json"

    def _config(self, path):
        # /v1/configs[/<category>[/<item>]], so the category is the third part
        # and the item the fourth.
        parts = [urllib.parse.unquote(part) for part in path.split("/") if part]
        if len(parts) <= 2:
            return 200, self._json({"categories": sorted(self.configs)}), \
                "application/json"
        category = parts[2]
        values = self.configs.get(category)
        if values is None:
            return 404, self._json({"errors": ["no such category"]}), \
                "application/json"
        if len(parts) == 3:
            # The category listing maps each item straight to its value, which
            # is the shape ConfigsApi.get reads.
            return 200, self._json({category: dict(values)}), "application/json"
        item = parts[3]
        return 200, self._json({category: {item: {
            "current": values.get(item, ""), "values": [], "default": ""}}}), \
            "application/json"

    def _set_config(self, path, params):
        parts = [urllib.parse.unquote(part) for part in path.split("/") if part]
        if len(parts) >= 4:
            with self._lock:
                self.configs.setdefault(parts[2], {})[parts[3]] = \
                    params.get("value", "")
        return 200, self._json({}), "application/json"

    def _heap(self, params):
        if self.faults.heap_404:
            return 404, b"Not found", "text/plain"
        if self.faults.heap_malformed:
            return 200, self._json({"free": None, "min_ever_free": "lots"}), \
                "application/json"
        return 200, self._json({"free": self.heap_free,
                                "min_ever_free": self.heap_min_ever_free,
                                "total": self.heap_total}), "application/json"

    def _drives(self, params):
        return 200, self._json({"drives": [
            {"a": {"enabled": True, "bus_id": 8, "type": "1541", "rom": "1541",
                   "image_file": self.mounted_image, "image_path": ""}},
            {"b": {"enabled": False, "bus_id": 9, "type": "1541", "rom": "1541",
                   "image_file": "", "image_path": ""}},
        ]}), "application/json"

    def _ok_json(self, params):
        return 200, self._json({}), "application/json"

    @staticmethod
    def _json(payload: Dict[str, object]) -> bytes:
        payload = dict(payload)
        payload.setdefault("errors", [])
        return json.dumps(payload).encode("utf-8")

    # -- lifecycle --

    def close(self) -> None:
        self._http.shutdown()
        self._http.server_close()
        self._ftp.close()
        self._telnet.close()
        self._dma.close()

    def __enter__(self) -> "DeviceDouble":
        return self

    def __exit__(self, *_exc) -> None:
        self.close()


class _BannerListener:
    """A TCP listener that greets and closes, for the FTP and Telnet checks."""

    def __init__(self, banner: bytes) -> None:
        self.banner = banner
        self.refuse = False
        self.refuse_flag = ""
        self.socket = socket.socket()
        self.socket.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.socket.bind((LOOPBACK, 0))
        self.socket.listen(4)
        # A timeout rather than a blocking accept: closing a listening socket
        # does not reliably wake a thread already blocked in accept on Linux,
        # and a double per test would then leave three threads behind each.
        self.socket.settimeout(0.2)
        self.port = self.socket.getsockname()[1]
        self.running = True
        self.thread = threading.Thread(target=self._serve, daemon=True)
        self.thread.start()

    def _serve(self) -> None:
        while self.running:
            try:
                connection, _ = self.socket.accept()
            except socket.timeout:
                continue
            except OSError:
                return
            refusing = self.refuse or (self.refuse_flag
                                       and os.path.exists(self.refuse_flag))
            try:
                if self.banner and not refusing:
                    connection.sendall(self.banner)
            except OSError:
                pass
            finally:
                connection.close()

    def close(self) -> None:
        self.running = False
        try:
            self.socket.close()
        except OSError:
            pass


class _DmaListener(_BannerListener):
    """The control port: read a command word, answer a length-prefixed title."""

    def __init__(self) -> None:
        super().__init__(b"")

    def _serve(self) -> None:
        while self.running:
            try:
                connection, _ = self.socket.accept()
            except socket.timeout:
                continue
            except OSError:
                return
            try:
                header = connection.recv(4)
                if len(header) == 4:
                    command, _ = struct.unpack("<HH", header)
                    if command == DMA_CMD_IDENTIFY:
                        connection.sendall(bytes([len(IDENTIFY_TITLE)]) + IDENTIFY_TITLE)
            except OSError:
                pass
            finally:
                connection.close()


# ---------------------------------------------------------------------------
# The UDP faces: what a device sends, including what it sends wrongly
# ---------------------------------------------------------------------------
#
# Scripted rather than interactive, because that is the only way to reach the
# conditions a receiver has to survive: no real device can be asked to reorder
# a packet, duplicate one, wrap a 16-bit counter or change its video mode on
# demand, and every one of those is a defect a recorder can have that a real
# run would find weeks later.

VIDEO_PACKET_BYTES = 780
VIDEO_LINE_BYTES = 192
VIDEO_LINES_PER_PACKET = 4
VIDEO_WIDTH = 384
PAL_LINES = 272
NTSC_LINES = 240

AUDIO_PACKET_BYTES = 770
AUDIO_SAMPLE_BYTES = 768


def video_packets(frame: int, first_sequence: int = 0, height: int = PAL_LINES,
                  pattern: int = 0) -> List[bytes]:
    """One frame's datagrams, in order.

    `pattern` fills every nibble, so a test can tell one frame's pixels from
    another's without building an image.
    """
    made = []
    fill = bytes([(pattern & 0x0F) | ((pattern & 0x0F) << 4)]) * (
        VIDEO_LINE_BYTES * VIDEO_LINES_PER_PACKET)
    for index, line in enumerate(range(0, height, VIDEO_LINES_PER_PACKET)):
        last = line + VIDEO_LINES_PER_PACKET >= height
        header = struct.pack(
            "<HHHHBBH", (first_sequence + index) & 0xFFFF, frame & 0xFFFF,
            line | (0x8000 if last else 0), VIDEO_WIDTH,
            VIDEO_LINES_PER_PACKET, 4, 0)
        made.append(header + fill)
    return made


def audio_packets(first_sequence: int = 0, count: int = 1,
                  sample: int = 1000) -> List[bytes]:
    """`count` audio datagrams carrying one repeated sample value."""
    body = struct.pack("<h", sample) * (AUDIO_SAMPLE_BYTES // 2)
    return [struct.pack("<H", (first_sequence + n) & 0xFFFF) + body
            for n in range(count)]


class UdpSender:
    """Sends what a device sends, from an address a test chooses.

    The source address is bound rather than left to the kernel, because it is
    what a receiver filters on: a second machine streaming into the same group
    is the fault that looks like nothing at all in the receive path.
    """

    def __init__(self, host: str, port: int, source: str = LOOPBACK) -> None:
        self.host = host
        self.port = port
        self.socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.socket.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.socket.bind((source, 0))

    def send(self, packets: List[bytes]) -> int:
        for packet in packets:
            self.socket.sendto(packet, (self.host, self.port))
        return len(packets)

    def close(self) -> None:
        try:
            self.socket.close()
        except OSError:
            pass

    def __enter__(self) -> "UdpSender":
        return self

    def __exit__(self, *_exc) -> None:
        self.close()


def syslog_lines(sender: UdpSender, lines: List[str]) -> int:
    """One datagram per line, which is what Syslog::forwardLogging sends.

    No priority prefix, no version, no timestamp, no hostname and no trailing
    newline: the firmware sends the bare line text, which is why the collector
    is a plain UDP sink rather than an RFC syslog daemon.
    """
    return sender.send([line.encode("utf-8") for line in lines])
