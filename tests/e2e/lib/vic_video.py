"""Multicast VIC video capture and frame assertions for hardware E2E tests."""

from collections import Counter
import os
import socket
import struct
import sys
from typing import List

from PIL import Image

sys.path.insert(0, os.path.join(
    os.path.dirname(os.path.abspath(__file__)), "..", "..", "lib"))
from report import Failure


MULTICAST_GROUP = "239.0.1.64"
VIDEO_PORT = 11000


class VicStreamCapture:
    """Capture one complete palette-indexed VIC frame from the multicast stream."""

    def __init__(self, port: int = VIDEO_PORT) -> None:
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.sock.settimeout(2.0)
        self.sock.bind(("", port))
        group = socket.inet_aton(MULTICAST_GROUP)
        membership = struct.pack("4sL", group, socket.INADDR_ANY)
        self.sock.setsockopt(socket.IPPROTO_IP, socket.IP_ADD_MEMBERSHIP, membership)

    def close(self) -> None:
        try:
            self.sock.close()
        except OSError:
            pass

    def _drain_partial_frame(self) -> None:
        while True:
            data, _ = self.sock.recvfrom(1024)
            _, _, line, _ = struct.unpack("<HHHH", data[0:8])
            if line & 0x8000:
                return

    def capture_image(self) -> Image.Image:
        for _ in range(8):
            self._drain_partial_frame()
            raw = bytearray()
            while True:
                data, _ = self.sock.recvfrom(1024)
                _, _, line, _ = struct.unpack("<HHHH", data[0:8])
                raw.extend(data[12:])
                if line & 0x8000:
                    break

            lines = len(raw) // 192
            if lines < 180:
                continue

            image = Image.new("P", (384, lines))
            i = 0
            for y in range(lines):
                for x in range(192):
                    value = raw[i]
                    image.putpixel((2 * x, y), value & 0x0F)
                    image.putpixel((2 * x + 1, y), value >> 4)
                    i += 1
            return image
        raise Failure("Did not receive a complete VIC frame.")


def frame_colors(image: Image.Image) -> Counter:
    """Palette-index histogram for a captured VIC frame."""
    return Counter(image.getdata())


def assert_not_black(image: Image.Image, label: str = "VIC frame") -> None:
    """Require at least one non-black pixel in a captured frame."""
    colors = frame_colors(image)
    if len(colors) == 1 and 0 in colors:
        raise Failure(f"{label} is completely black ({image.width}x{image.height})")


def assert_frames_differ(images: List[Image.Image], label: str = "VIC frames") -> None:
    """Require a sequence of captured frames to contain a visible change."""
    if len(images) < 2:
        raise ValueError("assert_frames_differ requires at least two frames")
    first = images[0].tobytes()
    if all(image.tobytes() == first for image in images[1:]):
        raise Failure(f"{label} did not change")
