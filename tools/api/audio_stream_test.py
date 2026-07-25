#!/usr/bin/env python3
"""End-to-end test for U64 data streaming over Ethernet and over WiFi.

Assumes the U64 is attached to both networks at once, and that this host can be
reached from the device on the capture port.

The "no WiFi available" rejection is not covered here: the WiFi interface cannot be
taken down over REST. The "WiFi Enabled" setting is only read at startup (the disable
branch of NetworkLWIP_WiFi::effectuate_settings() is deliberately commented out), and
the "Disable" menu entry is a CFG_TYPE_FUNC item, which the config API ignores.
"""
import argparse
import json
import os
import socket
import struct
import sys
import time
import urllib.error
import urllib.parse
import urllib.request
from contextlib import contextmanager
from typing import Any, Dict, List, Optional, Tuple

CHECK_COUNT = 0
INFO_PATH = "/v1/info"
# The payload shape and packet rate below are those of the audio stream, so this test is
# tied to it; the video and debug streams carry different payloads.
STREAM = "audio"
DEFAULT_PORT = 11001
# 48 kHz stereo, 192 frames per packet: a 2-byte little endian sequence number followed
# by 192 * 2 channels * 2 bytes of LPCM.
PAYLOAD_BYTES = 770
PACKET_RATE = 250.0
# The generator sends a two-byte probe to the destination before streaming starts, to open
# the port on the receiving side. It is not part of the stream and carries no sequence number.
PROBE_BYTES = 2
RATE_TOLERANCE = 0.10
MIN_CAPTURE_FRACTION = 0.90
STREAM_SETTLE_SECONDS = 1.0

TEST_CHOICES = ["ethernet", "wifi", "all"]
DEFAULT_TESTS = ["ethernet", "wifi"]


class Failure(RuntimeError):
    pass


@contextmanager
def check(label: str):
    global CHECK_COUNT
    CHECK_COUNT += 1
    print(f"[{CHECK_COUNT:02d}] {label} ... ", end="", flush=True)
    try:
        yield
    except Exception:
        print("FAIL", flush=True)
        raise
    print("OK", flush=True)


def format_exception(exc: BaseException) -> str:
    if isinstance(exc, urllib.error.URLError) and getattr(exc, "reason", None) is not None:
        return f"{exc} ({exc.reason})"
    return str(exc)


def device_unavailable(exc: BaseException) -> bool:
    return isinstance(exc, (ConnectionError, TimeoutError)) or (
        isinstance(exc, urllib.error.URLError) and isinstance(getattr(exc, "reason", None), OSError)
    )


class RestStreamSession:
    def __init__(self, host: str, password: Optional[str], timeout: float) -> None:
        self.host = host
        self.password = password
        self.timeout = timeout

    def url(self, path: str, params: Optional[Dict[str, Any]] = None) -> str:
        query = ""
        if params:
            query = "?" + urllib.parse.urlencode(params)
        return f"http://{self.host}{path}{query}"

    def request(self, method: str, path: str, params: Optional[Dict[str, Any]] = None) -> Tuple[int, bytes]:
        headers = {}
        if self.password:
            headers["X-Password"] = self.password
        request = urllib.request.Request(self.url(path, params), headers=headers, method=method)
        try:
            with urllib.request.urlopen(request, timeout=self.timeout) as response:
                return response.status, response.read()
        except urllib.error.HTTPError as exc:
            return exc.code, exc.read()
        except (OSError, TimeoutError, urllib.error.URLError) as exc:
            raise Failure(f"{method} {self.url(path, params)} failed: {format_exception(exc)}") from exc

    def json_request(self, method: str, path: str, params: Optional[Dict[str, Any]] = None) -> Dict[str, Any]:
        status, body = self.request(method, path, params)
        try:
            document = json.loads(body.decode("utf-8"))
        except ValueError as exc:
            raise Failure(f"{method} {path} returned non-JSON body: {body[:200]!r}") from exc
        document["_status"] = status
        return document

    def info(self) -> Dict[str, Any]:
        return self.json_request("GET", INFO_PATH)

    def start_stream(self, stream: str, listen_ip: str, port: int, wifi: Optional[bool]) -> Dict[str, Any]:
        params: Dict[str, Any] = {"ip": f"{listen_ip}:{port}"}
        if wifi is not None:
            params["wifi"] = "true" if wifi else "false"
        return self.json_request("PUT", f"/v1/streams/{stream}:start", params)

    def stop_stream(self, stream: str) -> Dict[str, Any]:
        return self.json_request("PUT", f"/v1/streams/{stream}:stop")


def errors_of(document: Dict[str, Any]) -> List[str]:
    return [e for e in document.get("errors", []) if e]


def expect_ok(document: Dict[str, Any], what: str) -> None:
    errors = errors_of(document)
    if errors:
        raise Failure(f"{what} reported errors: {errors}")


def local_ip_towards(host: str) -> str:
    probe = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        probe.connect((host, 9))
        return probe.getsockname()[0]
    except OSError as exc:
        raise Failure(f"Cannot determine the local IP address towards {host}: {exc}") from exc
    finally:
        probe.close()


class Capture:
    """Sequence-number analysis of one captured stream."""

    def __init__(self, packets: List[Tuple[float, str, bytes]]) -> None:
        self.probes = [p for p in packets if len(p[2]) == PROBE_BYTES]
        self.frames = [p for p in packets if len(p[2]) != PROBE_BYTES]
        self.sources = sorted({p[1] for p in self.frames})
        self.sizes = sorted({len(p[2]) for p in self.frames})

        self.count = len(self.frames)
        self.rate = 0.0
        if self.count >= 2:
            span = self.frames[-1][0] - self.frames[0][0]
            if span > 0:
                self.rate = (self.count - 1) / span

        # 16-bit sequence numbers wrap; unwrap them into a monotonic range so that a
        # reordered or duplicated packet is not mistaken for a gap.
        absolute: List[int] = []
        base = 0
        previous: Optional[int] = None
        for _, _, payload in self.frames:
            seq = struct.unpack("<H", payload[0:2])[0]
            if previous is not None and seq - previous < -32768:
                base += 65536
            if previous is not None and seq - previous > 32768:
                base -= 65536
            absolute.append(base + seq)
            previous = seq

        self.unique = len(set(absolute))
        self.duplicates = len(absolute) - self.unique
        self.reordered = sum(1 for i in range(1, len(absolute)) if absolute[i] < absolute[i - 1])
        if absolute:
            self.expected = max(absolute) - min(absolute) + 1
            self.missing = self.expected - self.unique
        else:
            self.expected = 0
            self.missing = 0

    def describe(self) -> str:
        return (
            f"{self.count} packets from {','.join(self.sources) or '-'} "
            f"({self.rate:.1f}/s, sizes={self.sizes}, expected={self.expected}, "
            f"missing={self.missing}, duplicates={self.duplicates}, reordered={self.reordered})"
        )


def capture_stream(port: int, duration: float) -> List[Tuple[float, str, bytes]]:
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    try:
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 4 * 1024 * 1024)
    except OSError:
        pass
    try:
        sock.bind(("", port))
    except OSError as exc:
        raise Failure(f"Cannot bind UDP port {port} to capture the stream: {exc}") from exc
    sock.settimeout(0.5)

    packets: List[Tuple[float, str, bytes]] = []
    end = time.time() + duration
    try:
        while time.time() < end:
            try:
                payload, addr = sock.recvfrom(2048)
            except socket.timeout:
                continue
            packets.append((time.time(), addr[0], payload))
    finally:
        sock.close()
    return packets


def assert_healthy(capture: Capture, duration: float, label: str) -> None:
    if capture.count == 0:
        raise Failure(f"{label}: no stream packets arrived on the capture port")
    minimum = int(duration * PACKET_RATE * MIN_CAPTURE_FRACTION)
    if capture.count < minimum:
        raise Failure(f"{label}: only {capture.count} packets in {duration:.0f}s, expected at least {minimum}")
    if capture.missing:
        raise Failure(
            f"{label}: {capture.missing} of {capture.expected} packets lost "
            f"({100.0 * capture.missing / capture.expected:.3f}%)"
        )
    if len(capture.sources) != 1:
        raise Failure(f"{label}: stream arrived from more than one address: {capture.sources}")
    if capture.sizes != [PAYLOAD_BYTES]:
        raise Failure(f"{label}: unexpected payload sizes {capture.sizes}, expected [{PAYLOAD_BYTES}]")
    low = PACKET_RATE * (1.0 - RATE_TOLERANCE)
    high = PACKET_RATE * (1.0 + RATE_TOLERANCE)
    if not (low <= capture.rate <= high):
        raise Failure(f"{label}: packet rate {capture.rate:.1f}/s outside {low:.0f}-{high:.0f}/s")


def run_transport(
    session: RestStreamSession,
    stream: str,
    listen_ip: str,
    port: int,
    duration: float,
    wifi: Optional[bool],
    label: str,
) -> Capture:
    with check(f"{label}: start {stream} stream to {listen_ip}:{port}"):
        expect_ok(session.start_stream(stream, listen_ip, port, wifi), f"{label} start")

    time.sleep(STREAM_SETTLE_SECONDS)
    packets = capture_stream(port, duration)
    capture = Capture(packets)

    with check(f"{label}: stop {stream} stream"):
        expect_ok(session.stop_stream(stream), f"{label} stop")

    with check(f"{label}: {capture.describe()}"):
        assert_healthy(capture, duration, label)

    return capture


def run_tests(
    session: RestStreamSession,
    stream: str,
    listen_ip: str,
    port: int,
    duration: float,
    selected: List[str],
) -> None:
    with check("REST interface is reachable"):
        info = session.info()
        expect_ok(info, "info")
        if not info.get("product"):
            raise Failure(f"Unexpected /v1/info response: {info}")

    with check(f"no {stream} stream is running"):
        session.stop_stream(stream)

    ethernet: Optional[Capture] = None
    wireless: Optional[Capture] = None

    if "ethernet" in selected:
        ethernet = run_transport(session, stream, listen_ip, port, duration, None, "ethernet")

    if "wifi" in selected:
        wireless = run_transport(session, stream, listen_ip, port, duration, True, "wifi")

    if ethernet and wireless:
        with check("wifi stream uses a different interface than the ethernet stream"):
            if ethernet.sources == wireless.sources:
                raise Failure(
                    f"Both transports streamed from {ethernet.sources[0]}; "
                    "the wifi stream did not leave over the wireless interface"
                )


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Validate U64 data streaming over Ethernet and over WiFi",
        epilog="Requires the U64 to be connected to both networks, and this host to be reachable from it.",
    )
    parser.add_argument("-H", "--host", default=os.environ.get("U64_AUDIO_HOST", "u64"))
    parser.add_argument("-r", "--rest-host", default=os.environ.get("U64_AUDIO_REST_HOST"))
    parser.add_argument(
        "-p",
        "--password",
        default=os.environ.get("U64_AUDIO_PASSWORD", os.environ.get("C64U_PASSWORD")),
    )
    parser.add_argument("-t", "--timeout", type=float, default=float(os.environ.get("U64_AUDIO_TIMEOUT", "30.0")))
    parser.add_argument(
        "--test",
        action="append",
        choices=TEST_CHOICES,
        help="run one transport; repeat for multiple selections (default: ethernet and wifi)",
    )
    parser.add_argument("--port", type=int, default=DEFAULT_PORT, help="capture port (default: %(default)s)")
    parser.add_argument(
        "-d",
        "--duration",
        type=float,
        default=float(os.environ.get("U64_AUDIO_DURATION", "10.0")),
        help="seconds to capture per transport (default: %(default)s)",
    )
    parser.add_argument(
        "--listen-ip",
        default=os.environ.get("U64_AUDIO_LISTEN_IP"),
        help="address the device should stream to (default: this host's address towards the device)",
    )
    args = parser.parse_args()

    rest_host = args.rest_host or args.host
    session = RestStreamSession(rest_host, args.password, args.timeout)

    selected = args.test or DEFAULT_TESTS
    if "all" in selected:
        selected = DEFAULT_TESTS + [t for t in selected if t not in DEFAULT_TESTS and t != "all"]

    try:
        listen_ip = args.listen_ip or local_ip_towards(rest_host)
        print(f"audio_stream_test: device {rest_host}, streaming to {listen_ip}:{args.port}")
        run_tests(session, STREAM, listen_ip, args.port, args.duration, selected)
    except Failure as exc:
        print(exc, file=sys.stderr)
        return 1
    except (OSError, TimeoutError, urllib.error.URLError) as exc:
        if device_unavailable(exc):
            print(f"Connection failure: {format_exception(exc)}", file=sys.stderr)
        else:
            print(f"REST failure: {format_exception(exc)}", file=sys.stderr)
        return 1

    print(f"audio_stream_test: OK ({CHECK_COUNT} checks)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
