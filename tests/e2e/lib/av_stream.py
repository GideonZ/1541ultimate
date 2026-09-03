"""Short hardware captures of the C64U audio and VIC UDP streams.

The wire constants, the socket options, the source-address filter and the
arming discipline are `streams.py`'s, which the recorder shares. What stays
here is what a suite does with the packets: the assertions and the
measurements.
"""

from __future__ import annotations

import select
import struct
import time
from collections import Counter, defaultdict
from dataclasses import dataclass
from typing import Dict, Iterable, List, Optional, Tuple

from api import UltimateApi
from report import Failure

import streams

# The public names a suite already imports, answered by the one place that
# knows them.
VIDEO_GROUP = streams.VIDEO_GROUP
VIDEO_PORT = streams.VIDEO_PORT
AUDIO_GROUP = streams.AUDIO_GROUP
AUDIO_PORT = streams.AUDIO_PORT
VIDEO_PACKET_BYTES = streams.VIDEO_PACKET_BYTES
AUDIO_PACKET_BYTES = streams.AUDIO_PACKET_BYTES


@dataclass(frozen=True)
class Packet:
    received_at: float
    data: bytes


@dataclass(frozen=True)
class VideoFrame:
    received_at: float
    number: int
    width: int
    height: int
    pixels: bytes

    def colors(self) -> Counter:
        return Counter(self.pixels)


@dataclass(frozen=True)
class PacketSequence:
    packets: int
    missing: int
    reordered: int


class AvStreamCapture:
    """Receive short, simultaneous audio and video captures from one C64U."""

    def __init__(self, host, password: Optional[str] = None) -> None:
        """`host` is a target token or a resolved handle; see targets.py."""
        self.device = UltimateApi(host, password)
        self.source_addresses = streams.source_addresses(host)
        self.video_socket = streams.stream_socket(VIDEO_GROUP, VIDEO_PORT,
                                                  timeout=None)
        self.audio_socket = streams.stream_socket(AUDIO_GROUP, AUDIO_PORT,
                                                  timeout=None)
        # Ask before arming, stop only what this started, and write down every
        # arm and stop so a reader can attribute a gap in a recording to the
        # suite that took the stream.
        self.arming = streams.Arming(self.device, host)
        self.video_packets: List[Packet] = []
        self.audio_packets: List[Packet] = []
        # Packets on the group that were not this device's: an empty capture
        # with none of these is no stream at all, with some a wrong address.
        self.foreign_packets = 0
        self.started = False

    def start(self) -> None:
        """Arm both streams, or leave neither armed.

        Arming reports a refusal rather than raising, so this checks the
        answer. A capture that carried on with one stream armed would fail
        later as an empty capture, which says nothing about what went wrong.
        """
        self.arming.start("video")
        if not self.arming.start("audio") and "audio" not in self.arming.started:
            self.arming.stop("video")
            raise Failure("the audio stream could not be started: "
                          + self.arming.failures.get("audio", "no reason given"))
        self.started = True

    def capture(self, seconds: float) -> None:
        deadline = time.monotonic() + seconds
        sockets = (self.video_socket, self.audio_socket)
        while True:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                return
            ready, _, _ = select.select(sockets, (), (), remaining)
            now = time.monotonic()
            for sock in ready:
                data, sender = sock.recvfrom(2048)
                if sender[0] not in self.source_addresses:
                    self.foreign_packets += 1
                    continue
                packet = Packet(now, data)
                if sock is self.video_socket:
                    self.video_packets.append(packet)
                else:
                    self.audio_packets.append(packet)

    def clear(self) -> None:
        """Discard control-transition packets before measuring a stimulus."""
        self.video_packets.clear()
        self.audio_packets.clear()
        for sock in (self.video_socket, self.audio_socket):
            sock.setblocking(False)
            try:
                while True:
                    sock.recvfrom(2048)
            except BlockingIOError:
                pass
            finally:
                sock.setblocking(True)

    def close(self) -> None:
        if self.started:
            # Only what this started: a stream a suite found already running
            # belongs to whoever started it.
            self.arming.stop_all()
            self.started = False
        self.video_socket.close()
        self.audio_socket.close()

    def __enter__(self) -> "AvStreamCapture":
        self.start()
        return self

    def __exit__(self, *_: object) -> None:
        self.close()


def _sequence(packet: Packet, kind: str, size: int) -> int:
    if len(packet.data) != size:
        raise Failure(f"{kind} packet is {len(packet.data)} bytes, expected {size}")
    return struct.unpack_from("<H", packet.data)[0]


def packet_sequence(packets: Iterable[Packet], kind: str, size: int) -> PacketSequence:
    """Validate packet shape and report 16-bit UDP sequence gaps and reordering."""
    expected: Optional[int] = None
    count = 0
    missing = 0
    reordered = 0
    for packet in packets:
        sequence = _sequence(packet, kind, size)
        if expected is not None and sequence != expected:
            forward = (sequence - expected) & 0xFFFF
            if 0 < forward < 0x8000:
                missing += forward
            else:
                reordered += 1
        expected = (sequence + 1) & 0xFFFF
        count += 1
    if not count:
        raise Failure(f"no {kind} packets captured")
    return PacketSequence(count, missing, reordered)


def assert_no_packet_loss(packets: Iterable[Packet], kind: str, size: int) -> None:
    """Require a lossless packet sequence when a test needs that stronger invariant."""
    sequence = packet_sequence(packets, kind, size)
    if sequence.missing or sequence.reordered:
        raise Failure(f"{kind} packet sequence missing={sequence.missing} reordered={sequence.reordered}")


def audio_samples(packet: Packet) -> Tuple[int, ...]:
    _sequence(packet, "audio", AUDIO_PACKET_BYTES)
    return struct.unpack("<384h", packet.data[2:])


def audio_rms(packet: Packet) -> float:
    samples = audio_samples(packet)
    if not samples:
        return 0.0
    return (sum(value * value for value in samples) / len(samples)) ** 0.5 / 32768.0


def first_loud_packet(packets: Iterable[Packet], after: float,
                      threshold: float = 0.01) -> Packet:
    for packet in packets:
        if packet.received_at >= after and audio_rms(packet) >= threshold:
            return packet
    raise Failure(f"no audio packet reached RMS {threshold:.3f}")


def video_frames(packets: Iterable[Packet]) -> List[VideoFrame]:
    """Decode complete 4-bit VIC frames from ordered stream packets."""
    grouped: Dict[int, List[Packet]] = defaultdict(list)
    for packet in packets:
        _sequence(packet, "video", VIDEO_PACKET_BYTES)
        grouped[struct.unpack_from("<H", packet.data, 2)[0]].append(packet)

    frames: List[VideoFrame] = []
    for number, frame_packets in grouped.items():
        first = frame_packets[0].data
        width = struct.unpack_from("<H", first, 6)[0]
        lines_per_packet = first[8]
        bits_per_pixel = first[9]
        encoding = struct.unpack_from("<H", first, 10)[0]
        if width != 384 or lines_per_packet != 4 or bits_per_pixel != 4 or encoding != 0:
            raise Failure("unexpected VIC video packet header")
        rows: Dict[int, bytes] = {}
        terminal_line: Optional[int] = None
        received_at = 0.0
        for packet in frame_packets:
            _, packet_number, line_field = struct.unpack_from("<HHH", packet.data)
            if packet_number != number:
                raise Failure("VIC packet frame number changed while decoding")
            line = line_field & 0x7FFF
            rows[line] = packet.data[12:]
            if line_field & 0x8000:
                terminal_line = line
                received_at = packet.received_at
        if terminal_line is None:
            continue
        expected_lines = range(0, terminal_line + 1, lines_per_packet)
        if any(line not in rows for line in expected_lines):
            continue
        pixels = bytearray()
        for line in expected_lines:
            for value in rows[line]:
                pixels.append(value & 0x0F)
                pixels.append(value >> 4)
        frames.append(VideoFrame(received_at, number, width,
                                 terminal_line + lines_per_packet, bytes(pixels)))
    return sorted(frames, key=lambda frame: frame.received_at)


def assert_not_black(frame: VideoFrame, label: str = "VIC frame") -> None:
    if set(frame.pixels) == {0}:
        raise Failure(f"{label} is completely black ({frame.width}x{frame.height})")


def assert_frames_differ(frames: Iterable[VideoFrame], label: str = "VIC frames") -> None:
    """Require at least two captured frames to have different visible pixels."""
    images = [frame.pixels for frame in frames]
    if len(images) < 2:
        raise Failure(f"{label} contains fewer than two complete frames")
    if all(image == images[0] for image in images[1:]):
        raise Failure(f"{label} did not change")


def first_bright_frame(frames: Iterable[VideoFrame], after: float,
                       color: int = 1, minimum_fraction: float = 0.80) -> VideoFrame:
    for frame in frames:
        if frame.received_at < after:
            continue
        if frame.colors()[color] / len(frame.pixels) >= minimum_fraction:
            return frame
    raise Failure(f"no VIC frame reached {minimum_fraction:.0%} colour {color}")
