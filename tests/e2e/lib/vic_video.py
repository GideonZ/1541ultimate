"""Multicast VIC video capture and frame assertions for hardware E2E tests.

The wire format, the socket options and the frame assembly are `streams.py`'s,
which the recorder shares. What stays here is what a suite does with a frame:
a palette-indexed image and the assertions that go with it.
"""

from collections import Counter
import os
import sys
from typing import List

from PIL import Image

sys.path.insert(0, os.path.join(
    os.path.dirname(os.path.abspath(__file__)), "..", "..", "lib"))
from report import Failure

import streams

# The public names a suite already imports, answered by the one place that
# knows them.
MULTICAST_GROUP = streams.VIDEO_GROUP
VIDEO_PORT = streams.VIDEO_PORT


class VicStreamCapture:
    """Capture one complete palette-indexed VIC frame from the multicast stream."""

    def __init__(self, port: int = VIDEO_PORT) -> None:
        # The socket sets SO_REUSEADDR and, where the platform has it,
        # SO_REUSEPORT. Multicast delivers to every subscriber that joined, so
        # that is what lets a suite and a recorder both receive this stream.
        self.sock = streams.stream_socket(MULTICAST_GROUP, port, timeout=2.0)

    def close(self) -> None:
        try:
            self.sock.close()
        except OSError:
            pass

    def capture_image(self) -> Image.Image:
        """One complete frame, as a palette-indexed image.

        Assembled by header offset rather than by concatenating payloads in
        arrival order, so a lost or reordered packet leaves an incomplete
        frame rather than shifting the rest of the picture upward.
        """
        assembler = streams.FrameAssembler()
        for _ in range(streams.VIDEO_PACKET_BYTES):
            try:
                data, _sender = self.sock.recvfrom(2048)
            except OSError as exc:
                raise Failure(f"the VIC stream stopped: {exc}") from exc
            frame = assembler.push(data)
            if frame is None:
                continue
            image = Image.frombytes("P", (frame.width, frame.height),
                                    streams.unpack(frame.packed))
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
